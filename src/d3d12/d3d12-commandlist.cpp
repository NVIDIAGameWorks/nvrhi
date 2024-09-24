/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "d3d12-backend.h"
#include <pix.h>
#include <sstream>

#include <nvrhi/common/misc.h>

namespace nvrhi::d3d12
{
    CommandList::CommandList(Device* device, const Context& context, DeviceResources& resources, const CommandListParameters& params)
        : m_Context(context)
        , m_Resources(resources)
        , m_Device(device)
        , m_Queue(device->getQueue(params.queueType))
        , m_UploadManager(context, m_Queue, params.uploadChunkSize, 0, false)
        , m_DxrScratchManager(context, m_Queue, params.scratchChunkSize, params.scratchMaxMemory, true)
        , m_StateTracker(context.messageCallback)
        , m_Desc(params)
    {
#if NVRHI_WITH_AFTERMATH
        if (m_Device->isAftermathEnabled())
            m_Device->getAftermathCrashDumpHelper().registerAftermathMarkerTracker(&m_AftermathTracker);
#endif
    }

    CommandList::~CommandList()
    {
#if NVRHI_WITH_AFTERMATH
        if (m_Device->isAftermathEnabled())
            m_Device->getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(&m_AftermathTracker);
#endif
    }
    
    Object CommandList::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_GraphicsCommandList:
            if (m_ActiveCommandList)
                return Object(m_ActiveCommandList->commandList.Get());
            else
                return nullptr;

        case ObjectTypes::D3D12_CommandAllocator:
            if (m_ActiveCommandList)
                return Object(m_ActiveCommandList->allocator.Get());
            else
                return nullptr;

        case ObjectTypes::Nvrhi_D3D12_CommandList:
            return this;

        default:
            return nullptr;
        }
    }

    std::shared_ptr<InternalCommandList> CommandList::createInternalCommandList() const
    {
        auto commandList = std::make_shared<InternalCommandList>();

        D3D12_COMMAND_LIST_TYPE d3dCommandListType;
        switch (m_Desc.queueType)
        {
        case CommandQueue::Graphics:
            d3dCommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case CommandQueue::Compute:
            d3dCommandListType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case CommandQueue::Copy:
            d3dCommandListType = D3D12_COMMAND_LIST_TYPE_COPY;
            break;

        case CommandQueue::Count:
        default:
            utils::InvalidEnum();
            return nullptr;
        }

        m_Context.device->CreateCommandAllocator(d3dCommandListType, IID_PPV_ARGS(&commandList->allocator));
        m_Context.device->CreateCommandList(0, d3dCommandListType, commandList->allocator, nullptr, IID_PPV_ARGS(&commandList->commandList));

        commandList->commandList->QueryInterface(IID_PPV_ARGS(&commandList->commandList4));
        commandList->commandList->QueryInterface(IID_PPV_ARGS(&commandList->commandList6));

#if NVRHI_WITH_AFTERMATH
        if (m_Device->isAftermathEnabled())
            GFSDK_Aftermath_DX12_CreateContextHandle(commandList->commandList, &commandList->aftermathContext);
#endif

        return commandList;
    }

    bool CommandList::commitDescriptorHeaps()
    {
        ID3D12DescriptorHeap* heapSRVetc = m_Resources.shaderResourceViewHeap.getShaderVisibleHeap();
        ID3D12DescriptorHeap* heapSamplers = m_Resources.samplerHeap.getShaderVisibleHeap();

        if (heapSRVetc != m_CurrentHeapSRVetc || heapSamplers != m_CurrentHeapSamplers)
        {
            ID3D12DescriptorHeap* heaps[2] = { heapSRVetc, heapSamplers };
            m_ActiveCommandList->commandList->SetDescriptorHeaps(2, heaps);

            m_CurrentHeapSRVetc = heapSRVetc;
            m_CurrentHeapSamplers = heapSamplers;

            m_Instance->referencedNativeResources.push_back(heapSRVetc);
            m_Instance->referencedNativeResources.push_back(heapSamplers);

            return true;
        }

        return false;
    }

    bool CommandList::allocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress)
    {
        return m_UploadManager.suballocateBuffer(size, nullptr, nullptr, nullptr, pCpuAddress, pGpuAddress,
            m_RecordingVersion, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    bool CommandList::allocateDxrScratchBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress)
    {
        return m_DxrScratchManager.suballocateBuffer(size, m_ActiveCommandList->commandList, nullptr, nullptr, pCpuAddress, pGpuAddress,
            m_RecordingVersion, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    }

    D3D12_GPU_VIRTUAL_ADDRESS CommandList::getBufferGpuVA(IBuffer* _buffer)
    {
        if (!_buffer)
            return 0;

        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        if (buffer->desc.isVolatile)
        {
            return m_VolatileConstantBufferAddresses[buffer];
        }

        return buffer->gpuVA;
    }

    nvrhi::IDevice* CommandList::getDevice()
    {
        return m_Device;
    }

    void CommandList::beginMarker(const char* name)
    {
        PIXBeginEvent(m_ActiveCommandList->commandList, 0, name);
#if NVRHI_WITH_AFTERMATH
        if (m_Device->isAftermathEnabled())
        {
            const size_t aftermathMarker = m_AftermathTracker.pushEvent(name);
            GFSDK_Aftermath_SetEventMarker(m_ActiveCommandList->aftermathContext, (const void*)aftermathMarker, 0);
        }
#endif
    }

    void CommandList::endMarker()
    {
        PIXEndEvent(m_ActiveCommandList->commandList);
#if NVRHI_WITH_AFTERMATH
        if (m_Device->isAftermathEnabled())
            m_AftermathTracker.popEvent();
#endif
    }

    void CommandList::setPushConstants(const void* data, size_t byteSize)
    {
        const RootSignature* rootsig = nullptr;
        bool isGraphics = false;

        if (m_CurrentGraphicsStateValid && m_CurrentGraphicsState.pipeline)
        {
            GraphicsPipeline* pso = checked_cast<GraphicsPipeline*>(m_CurrentGraphicsState.pipeline);
            rootsig = pso->rootSignature;
            isGraphics = true;
        }
        else if (m_CurrentComputeStateValid && m_CurrentComputeState.pipeline)
        {
            ComputePipeline* pso = checked_cast<ComputePipeline*>(m_CurrentComputeState.pipeline);
            rootsig = pso->rootSignature;
            isGraphics = false;
        }
        else if (m_CurrentRayTracingStateValid && m_CurrentRayTracingState.shaderTable)
        {
            RayTracingPipeline* pso = checked_cast<RayTracingPipeline*>(m_CurrentRayTracingState.shaderTable->getPipeline());
            rootsig = pso->globalRootSignature;
            isGraphics = false;
        }
        else if (m_CurrentMeshletStateValid && m_CurrentMeshletState.pipeline)
        {
            MeshletPipeline* pso = checked_cast<MeshletPipeline*>(m_CurrentMeshletState.pipeline);
            rootsig = pso->rootSignature;
            isGraphics = true;
        }

        if (!rootsig || !rootsig->pushConstantByteSize)
            return;

        assert(byteSize == rootsig->pushConstantByteSize); // the validation error handles the error message
        
        if (isGraphics)
            m_ActiveCommandList->commandList->SetGraphicsRoot32BitConstants(rootsig->rootParameterPushConstants, UINT(byteSize / 4), data, 0);
        else
            m_ActiveCommandList->commandList->SetComputeRoot32BitConstants(rootsig->rootParameterPushConstants, UINT(byteSize / 4), data, 0);
    }

    void CommandList::open()
    {
        uint64_t completedInstance = m_Queue->updateLastCompletedInstance();

        std::shared_ptr<InternalCommandList> chunk;

        if (!m_CommandListPool.empty())
        {
            chunk = m_CommandListPool.front();

            if (chunk->lastSubmittedInstance <= completedInstance)
            {
                chunk->allocator->Reset();
                chunk->commandList->Reset(chunk->allocator, nullptr);
                m_CommandListPool.pop_front();
            }
            else
            {
                chunk = nullptr;
            }
        }

        if (chunk == nullptr)
        {
            chunk = createInternalCommandList();
        }

        m_ActiveCommandList = chunk;

        m_Instance = std::make_shared<CommandListInstance>();
        m_Instance->commandAllocator = m_ActiveCommandList->allocator;
        m_Instance->commandList = m_ActiveCommandList->commandList;
        m_Instance->commandQueue = m_Desc.queueType;

        m_RecordingVersion = MakeVersion(m_Queue->recordingInstance++, m_Desc.queueType, false);
    }

    void CommandList::clearStateCache()
    {
        m_AnyVolatileBufferWrites = false;
        m_CurrentGraphicsStateValid = false;
        m_CurrentComputeStateValid = false;
        m_CurrentMeshletStateValid = false;
        m_CurrentRayTracingStateValid = false;
        m_CurrentHeapSRVetc = nullptr;
        m_CurrentHeapSamplers = nullptr;
        m_CurrentGraphicsVolatileCBs.resize(0);
        m_CurrentComputeVolatileCBs.resize(0);
        m_CurrentSinglePassStereoState = SinglePassStereoState();
    }

    void CommandList::clearState()
    {
        m_ActiveCommandList->commandList->ClearState(nullptr);

#if NVRHI_D3D12_WITH_NVAPI
        if (m_CurrentGraphicsStateValid && m_CurrentSinglePassStereoState.enabled)
        {
            NvAPI_Status Status = NvAPI_D3D12_SetSinglePassStereoMode(m_ActiveCommandList->commandList, 
                1, 0, false);

            if (Status != NVAPI_OK)
            {
                m_Context.error("NvAPI_D3D12_SetSinglePassStereoMode call failed");
            }
        }
#endif

        clearStateCache();

        commitDescriptorHeaps();
    }

    void CommandList::close()
    {
        m_StateTracker.keepBufferInitialStates();
        m_StateTracker.keepTextureInitialStates();
        commitBarriers();

#ifdef NVRHI_WITH_RTXMU
        if (!m_Instance->rtxmuBuildIds.empty())
        {
            m_Context.rtxMemUtil->PopulateCompactionSizeCopiesCommandList(m_ActiveCommandList->commandList4, m_Instance->rtxmuBuildIds);
        }
#endif

        m_ActiveCommandList->commandList->Close();

        clearStateCache();

        m_CurrentUploadBuffer = nullptr;
        m_VolatileConstantBufferAddresses.clear();
        m_ShaderTableStates.clear();
    }

    std::shared_ptr<CommandListInstance> CommandList::executed(Queue* pQueue)
    {
        std::shared_ptr<CommandListInstance> instance = m_Instance;
        instance->fence = pQueue->fence;
        instance->submittedInstance = pQueue->lastSubmittedInstance;
        m_Instance.reset();

        m_ActiveCommandList->lastSubmittedInstance = pQueue->lastSubmittedInstance;
        m_CommandListPool.push_back(m_ActiveCommandList);
        m_ActiveCommandList.reset();

        for (const auto& it : instance->referencedStagingTextures)
        {
            it->lastUseFence = pQueue->fence;
            it->lastUseFenceValue = instance->submittedInstance;
        }

        for (const auto& it : instance->referencedStagingBuffers)
        {
            it->lastUseFence = pQueue->fence;
            it->lastUseFenceValue = instance->submittedInstance;
        }

        for (const auto& it : instance->referencedTimerQueries)
        {
            it->started = true;
            it->resolved = false;
            it->fence = pQueue->fence;
            it->fenceCounter = instance->submittedInstance;
        }

        m_StateTracker.commandListSubmitted();

        uint64_t submittedVersion = MakeVersion(instance->submittedInstance, m_Desc.queueType, true);
        m_UploadManager.submitChunks(m_RecordingVersion, submittedVersion);
        m_DxrScratchManager.submitChunks(m_RecordingVersion, submittedVersion);
        m_RecordingVersion = 0;
        
        return instance;
    }

} // namespace nvrhi::d3d12