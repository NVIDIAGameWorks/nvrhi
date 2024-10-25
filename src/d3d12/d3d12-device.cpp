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

#include <nvrhi/common/misc.h>

#if NVRHI_D3D12_WITH_NVAPI
#include <nvShaderExtnEnums.h>
#endif

#include <sstream>
#include <iomanip>

namespace nvrhi::d3d12
{
    void Context::error(const std::string& message) const
    {
        messageCallback->message(MessageSeverity::Error, message.c_str());
    }

    void WaitForFence(ID3D12Fence* fence, uint64_t value, HANDLE event)
    {
        // Test if the fence has been reached
        if (fence->GetCompletedValue() < value)
        {
            // If it's not, wait for it to finish using an event
            ResetEvent(event);
            fence->SetEventOnCompletion(value, event);
            WaitForSingleObject(event, INFINITE);
        }
    }


    DeviceHandle createDevice(const DeviceDesc& desc)
    {
        Device* device = new Device(desc);
        return DeviceHandle::Create(device);
    }

    DeviceResources::DeviceResources(const Context& context, const DeviceDesc& desc)
        : renderTargetViewHeap(context)
        , depthStencilViewHeap(context)
        , shaderResourceViewHeap(context)
        , samplerHeap(context)
        , timerQueries(desc.maxTimerQueries, true)
        , m_Context(context)
    {
    }

    Queue::Queue(const Context& context, ID3D12CommandQueue* queue)
        : queue(queue)
        , m_Context(context)
    {
        assert(queue);
        m_Context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    }

    uint64_t Queue::updateLastCompletedInstance()
    {
        if (lastCompletedInstance < lastSubmittedInstance)
        {
            lastCompletedInstance = fence->GetCompletedValue();
        }
        return lastCompletedInstance;
    }

    Device::Device(const DeviceDesc& desc)
        : m_Resources(m_Context, desc)
    {
        m_Context.device = desc.pDevice;
        m_Context.messageCallback = desc.errorCB;

        if (desc.pGraphicsCommandQueue)
            m_Queues[int(CommandQueue::Graphics)] = std::make_unique<Queue>(m_Context, desc.pGraphicsCommandQueue);
        if (desc.pComputeCommandQueue)
            m_Queues[int(CommandQueue::Compute)] = std::make_unique<Queue>(m_Context, desc.pComputeCommandQueue);
        if (desc.pCopyCommandQueue)
            m_Queues[int(CommandQueue::Copy)] = std::make_unique<Queue>(m_Context, desc.pCopyCommandQueue);

        m_Resources.depthStencilViewHeap.allocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, desc.depthStencilViewHeapSize, false);
        m_Resources.renderTargetViewHeap.allocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.renderTargetViewHeapSize, false);
        m_Resources.shaderResourceViewHeap.allocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, desc.shaderResourceViewHeapSize, true);
        m_Resources.samplerHeap.allocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, desc.samplerHeapSize, true);

        m_Context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &m_Options, sizeof(m_Options));
        bool hasOptions5 = SUCCEEDED(m_Context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &m_Options5, sizeof(m_Options5)));
        bool hasOptions6 = SUCCEEDED(m_Context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &m_Options6, sizeof(m_Options6)));
        bool hasOptions7 = SUCCEEDED(m_Context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &m_Options7, sizeof(m_Options7)));

        if (SUCCEEDED(m_Context.device->QueryInterface(&m_Context.device5)) && hasOptions5)
        {
            m_RayTracingSupported = m_Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
            m_TraceRayInlineSupported = m_Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;

#ifdef NVRHI_WITH_RTXMU
            if (m_RayTracingSupported)
            {
                m_Context.rtxMemUtil = std::make_unique<rtxmu::DxAccelStructManager>(m_Context.device5);

                // Initialize suballocator blocks to 8 MB
                m_Context.rtxMemUtil->Initialize(8388608);
            }
#endif
        }

        if (SUCCEEDED(m_Context.device->QueryInterface(&m_Context.device2)) && hasOptions7)
        {
            m_MeshletsSupported = m_Options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
        }

        if (SUCCEEDED(m_Context.device->QueryInterface(&m_Context.device8)) && hasOptions7)
        {
            m_SamplerFeedbackSupported = m_Options7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9;
        }

        if (hasOptions6)
        {
            m_VariableRateShadingSupported = m_Options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
        }
        
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            D3D12_COMMAND_SIGNATURE_DESC csDesc = {};
            csDesc.NumArgumentDescs = 1;
            csDesc.pArgumentDescs = &argDesc;

            csDesc.ByteStride = 16;
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            m_Context.device->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_Context.drawIndirectSignature));

            csDesc.ByteStride = 20;
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            m_Context.device->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_Context.drawIndexedIndirectSignature));

            csDesc.ByteStride = 12;
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            m_Context.device->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_Context.dispatchIndirectSignature));
        }
        
        m_FenceEvent = CreateEvent(nullptr, false, false, nullptr);

        m_CommandListsToExecute.reserve(64);

#if NVRHI_D3D12_WITH_NVAPI
        //We need to use NVAPI to set resource hints for SLI
        m_NvapiIsInitialized = NvAPI_Initialize() == NVAPI_OK;

        if (m_NvapiIsInitialized)
        {
            NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams{};
            stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;

            if (NvAPI_D3D12_QuerySinglePassStereoSupport(m_Context.device, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
            {
                m_SinglePassStereoSupported = true;
            }

            // There is no query for FastGS, so query support for FP16 atomics as a proxy.
            // Both features were introduced in the same architecture (Maxwell).
            bool supported = false;
            if (NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(m_Context.device, NV_EXTN_OP_FP16_ATOMIC, &supported) == NVAPI_OK && supported)
            {
                m_FastGeometryShaderSupported = true;
            }

            NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAPS ser = NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_NONE;
            if (NvAPI_D3D12_GetRaytracingCaps(m_Context.device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_THREAD_REORDERING, &ser, sizeof(ser)) == NVAPI_OK)
            {
                m_ShaderExecutionReorderingSupported = (ser & NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD) == NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD;
            }
        }

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
#ifdef NVRHI_WITH_RTXMU
        m_OpacityMicromapSupported = false; // RTXMU does not support OMMs
#else
        if (m_NvapiIsInitialized)
        {
            NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS caps = NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE;
            NvAPI_D3D12_GetRaytracingCaps(m_Context.device5, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &caps, sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS));
            m_OpacityMicromapSupported = caps == NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_STANDARD;
        }

        if (m_OpacityMicromapSupported)
        {
            NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
            params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
            params.flags = NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT;
            [[maybe_unused]] NvAPI_Status res = NvAPI_D3D12_SetCreatePipelineStateOptions(m_Context.device5, &params);
            assert(res == NVAPI_OK);
        }
#endif
#endif // #if NVRHI_WITH_NVAPI_OPACITY_MICROMAPS

#endif // #if NVRHI_D3D12_WITH_NVAPI

#if NVRHI_WITH_AFTERMATH
        if (desc.aftermathEnabled)
        {
            const uint32_t aftermathFlags = GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking | GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo | GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;
            GFSDK_Aftermath_Result aftermathResult = GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, m_Context.device);
            if (!GFSDK_Aftermath_SUCCEED(aftermathResult))
            {
                std::stringstream ss;
                ss << "Aftermath initialize call failed, result = 0x" << std::hex << std::setw(8) << aftermathResult;
                m_Context.error(ss.str());
            }
            else
            {
                m_AftermathEnabled = true;
            }
        }
#endif
    }

    Device::~Device()
    {
        waitForIdle();

        if (m_FenceEvent)
        {
            CloseHandle(m_FenceEvent);
            m_FenceEvent = nullptr;
        }
    }

    bool Device::waitForIdle()
    {
        // Wait for every queue to reach its last submitted instance
        for (const auto& pQueue : m_Queues)
        {
            if (!pQueue)
                continue;

            if (pQueue->updateLastCompletedInstance() < pQueue->lastSubmittedInstance)
            {
                WaitForFence(pQueue->fence, pQueue->lastSubmittedInstance, m_FenceEvent);
            }
        }
        return true;
    }
    
    Object RootSignature::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_RootSignature:
            return Object(handle.Get());
        default:
            return nullptr;
        }
    }
    
    Sampler::Sampler(const Context& context, const SamplerDesc& desc)
        : m_Context(context)
        , m_Desc(desc)
        , m_d3d12desc{}
    {
        UINT reductionType = convertSamplerReductionType(desc.reductionType);

        if (m_Desc.maxAnisotropy > 1.0f)
        {
            m_d3d12desc.Filter = D3D12_ENCODE_ANISOTROPIC_FILTER(reductionType);
        }
        else
        {
            m_d3d12desc.Filter = D3D12_ENCODE_BASIC_FILTER(
                m_Desc.minFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                m_Desc.magFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                m_Desc.mipFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                reductionType);
        }

        m_d3d12desc.AddressU = convertSamplerAddressMode(m_Desc.addressU);
        m_d3d12desc.AddressV = convertSamplerAddressMode(m_Desc.addressV);
        m_d3d12desc.AddressW = convertSamplerAddressMode(m_Desc.addressW);
        
        m_d3d12desc.MipLODBias = m_Desc.mipBias;
        m_d3d12desc.MaxAnisotropy = std::max((UINT)m_Desc.maxAnisotropy, 1U);
        m_d3d12desc.ComparisonFunc = desc.reductionType == SamplerReductionType::Comparison
            ? D3D12_COMPARISON_FUNC_LESS
            : D3D12_COMPARISON_FUNC_NEVER;
        m_d3d12desc.BorderColor[0] = m_Desc.borderColor.r;
        m_d3d12desc.BorderColor[1] = m_Desc.borderColor.g;
        m_d3d12desc.BorderColor[2] = m_Desc.borderColor.b;
        m_d3d12desc.BorderColor[3] = m_Desc.borderColor.a;
        m_d3d12desc.MinLOD = 0;
        m_d3d12desc.MaxLOD = D3D12_FLOAT32_MAX;
    }
    
    void Sampler::createDescriptor(size_t descriptor) const
    {
        m_Context.device->CreateSampler(&m_d3d12desc, { descriptor });
    }
    
    SamplerHandle Device::createSampler(const SamplerDesc& d)
    {
        Sampler* sampler = new Sampler(m_Context, d);
        return SamplerHandle::Create(sampler);
    }
    
    GraphicsAPI Device::getGraphicsAPI()
    {
        return GraphicsAPI::D3D12;
    }


    Object Device::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Device:
            return Object(m_Context.device);
        case ObjectTypes::Nvrhi_D3D12_Device:
            return Object(this);
        case ObjectTypes::D3D12_CommandQueue:
            return Object(getQueue(CommandQueue::Graphics)->queue.Get());
        default:
            return nullptr;
        }
    }

    nvrhi::CommandListHandle Device::createCommandList(const CommandListParameters& params)
    {
        if (!getQueue(params.queueType))
            return nullptr;

        return CommandListHandle::Create(new CommandList(this, m_Context, m_Resources, params));
    }
    
    uint64_t Device::executeCommandLists(nvrhi::ICommandList* const* pCommandLists, size_t numCommandLists, CommandQueue executionQueue)
    {
        m_CommandListsToExecute.resize(numCommandLists);
        for (size_t i = 0; i < numCommandLists; i++)
        {
            m_CommandListsToExecute[i] = checked_cast<CommandList*>(pCommandLists[i])->getD3D12CommandList();
        }

        Queue* pQueue = getQueue(executionQueue);

        pQueue->queue->ExecuteCommandLists(uint32_t(m_CommandListsToExecute.size()), m_CommandListsToExecute.data());
        pQueue->lastSubmittedInstance++;
        pQueue->queue->Signal(pQueue->fence, pQueue->lastSubmittedInstance);

        for (size_t i = 0; i < numCommandLists; i++)
        {
            auto instance = checked_cast<CommandList*>(pCommandLists[i])->executed(pQueue);
            pQueue->commandListsInFlight.push_front(instance);
        }

        HRESULT hr = m_Context.device->GetDeviceRemovedReason();
        if (FAILED(hr))
        {
            m_Context.messageCallback->message(MessageSeverity::Error, "Device Removed!");
        }

        return pQueue->lastSubmittedInstance;
    }

    void Device::queueWaitForCommandList(CommandQueue waitQueue, CommandQueue executionQueue, uint64_t instanceID)
    {
        Queue* pWaitQueue = getQueue(waitQueue);
        Queue* pExecutionQueue = getQueue(executionQueue);
        assert(instanceID <= pExecutionQueue->lastSubmittedInstance);

        pWaitQueue->queue->Wait(pExecutionQueue->fence, instanceID);
    }

    void Device::getTextureTiling(ITexture* texture, uint32_t* numTiles, PackedMipDesc* desc, TileShape* tileShape, uint32_t* subresourceTilingsNum, SubresourceTiling* _subresourceTilings)
    {
        ID3D12Resource* resource = checked_cast<Texture*>(texture)->resource;
        D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

        D3D12_PACKED_MIP_INFO packedMipDesc = {};
        D3D12_TILE_SHAPE standardTileShapeForNonPackedMips = {};
        D3D12_SUBRESOURCE_TILING subresourceTilings[16];

        m_Context.device->GetResourceTiling(resource, numTiles, desc ? &packedMipDesc : nullptr, tileShape ? &standardTileShapeForNonPackedMips : nullptr, subresourceTilingsNum, 0, subresourceTilings);

        if (desc)
        {
            desc->numStandardMips = packedMipDesc.NumStandardMips;
            desc->numPackedMips = packedMipDesc.NumPackedMips;
            desc->startTileIndexInOverallResource = packedMipDesc.StartTileIndexInOverallResource;
            desc->numTilesForPackedMips = packedMipDesc.NumTilesForPackedMips;
        }

        if (tileShape)
        {
            tileShape->widthInTexels = standardTileShapeForNonPackedMips.WidthInTexels;
            tileShape->heightInTexels = standardTileShapeForNonPackedMips.HeightInTexels;
            tileShape->depthInTexels = standardTileShapeForNonPackedMips.DepthInTexels;
        }

        for (uint32_t i = 0; i < *subresourceTilingsNum; ++i)
        {
            _subresourceTilings[i].widthInTiles = subresourceTilings[i].WidthInTiles;
            _subresourceTilings[i].heightInTiles = subresourceTilings[i].HeightInTiles;
            _subresourceTilings[i].depthInTiles = subresourceTilings[i].DepthInTiles;
            _subresourceTilings[i].startTileIndexInOverallResource = subresourceTilings[i].StartTileIndexInOverallResource;
        }
    }

    void Device::updateTextureTilesMappings(ITexture* _texture, const TextureTilesMapping* tileMappings, uint32_t numTileMappings, CommandQueue executionQueue)
    {
        Queue* queue = getQueue(executionQueue);
        Texture* texture = checked_cast<Texture*>(_texture);

        D3D12_TILE_SHAPE tileShape;
        D3D12_SUBRESOURCE_TILING subresourceTiling;
        m_Context.device->GetResourceTiling(texture->resource, nullptr, nullptr, &tileShape, nullptr, 0, &subresourceTiling);

        for (size_t i = 0; i < numTileMappings; i++)
        {
            ID3D12Heap* heap = tileMappings[i].heap ? checked_cast<Heap*>(tileMappings[i].heap)->heap : nullptr;

            uint32_t numRegions = tileMappings[i].numTextureRegions;
            std::vector<D3D12_TILED_RESOURCE_COORDINATE> resourceCoordinates(numRegions);
            std::vector<D3D12_TILE_REGION_SIZE> regionSizes(numRegions);
            std::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags(numRegions, heap ? D3D12_TILE_RANGE_FLAG_NONE : D3D12_TILE_RANGE_FLAG_NULL);
            std::vector<UINT> heapStartOffsets(numRegions);
            std::vector<UINT> rangeTileCounts(numRegions);

            for (uint32_t j = 0; j < numRegions; ++j)
            {
                const TiledTextureCoordinate& tiledTextureCoordinate = tileMappings[i].tiledTextureCoordinates[j];
                const TiledTextureRegion& tiledTextureRegion = tileMappings[i].tiledTextureRegions[j];

                resourceCoordinates[j].Subresource = tiledTextureCoordinate.mipLevel * texture->desc.arraySize + tiledTextureCoordinate.arrayLevel;
                resourceCoordinates[j].X = tiledTextureCoordinate.x;
                resourceCoordinates[j].Y = tiledTextureCoordinate.y;
                resourceCoordinates[j].Z = tiledTextureCoordinate.z;

                if (tiledTextureRegion.tilesNum)
                {
                    regionSizes[j].NumTiles = tiledTextureRegion.tilesNum;
                    regionSizes[j].UseBox = false;
                }
                else
                {
                    uint32_t tilesX = (tiledTextureRegion.width + (tileShape.WidthInTexels - 1)) / tileShape.WidthInTexels;
                    uint32_t tilesY = (tiledTextureRegion.height + (tileShape.HeightInTexels - 1)) / tileShape.HeightInTexels;
                    uint32_t tilesZ = (tiledTextureRegion.depth + (tileShape.DepthInTexels - 1)) / tileShape.DepthInTexels;

                    regionSizes[j].Width = tilesX;
                    regionSizes[j].Height = (uint16_t)tilesY;
                    regionSizes[j].Depth = (uint16_t)tilesZ;

                    regionSizes[j].NumTiles = tilesX * tilesY * tilesZ;
                    regionSizes[j].UseBox = true;
                }

                // Offset in tiles
                if (heap)
                    heapStartOffsets[j] = (uint32_t)(tileMappings[i].byteOffsets[j] / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);

                rangeTileCounts[j] = regionSizes[j].NumTiles;
            }

            queue->queue->UpdateTileMappings(texture->resource, tileMappings[i].numTextureRegions, resourceCoordinates.data(), regionSizes.data(), heap, numRegions, rangeFlags.data(), heap ? heapStartOffsets.data() : nullptr, rangeTileCounts.data(), D3D12_TILE_MAPPING_FLAG_NONE);
        }
    }

    void Device::runGarbageCollection()
    {
        for (const auto& pQueue : m_Queues)
        {
            if (!pQueue)
                continue;

            pQueue->updateLastCompletedInstance();

            // Starting from the back of the queue, i.e. oldest submitted command lists,
            // see if those command lists have finished executing.
            while (!pQueue->commandListsInFlight.empty())
            {
                std::shared_ptr<CommandListInstance> instance = pQueue->commandListsInFlight.back();
                
                if (pQueue->lastCompletedInstance >= instance->submittedInstance)
                {
#ifdef NVRHI_WITH_RTXMU
                    if (!instance->rtxmuBuildIds.empty())
                    {
                        std::lock_guard lockGuard(m_Resources.asListMutex);

                        m_Resources.asBuildsCompleted.insert(m_Resources.asBuildsCompleted.end(),
                            instance->rtxmuBuildIds.begin(), instance->rtxmuBuildIds.end());

                        instance->rtxmuBuildIds.clear();
                    }
                    if (!instance->rtxmuCompactionIds.empty())
                    {
                        m_Context.rtxMemUtil->GarbageCollection(instance->rtxmuCompactionIds);
                        instance->rtxmuCompactionIds.clear();
                    }
#endif
                    pQueue->commandListsInFlight.pop_back();
                }
                else
                {
                    break;
                }
            }
        }
    }

    bool Device::queryFeatureSupport(Feature feature, void* pInfo, size_t infoSize)
    {
        switch (feature)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case Feature::DeferredCommandLists:
            return true;
        case Feature::SinglePassStereo:
            return m_SinglePassStereoSupported;
        case Feature::RayTracingAccelStruct:
            return m_RayTracingSupported;
        case Feature::RayTracingPipeline:
            return m_RayTracingSupported;
        case Feature::RayTracingOpacityMicromap:
            return m_OpacityMicromapSupported;
        case Feature::RayQuery:
            return m_TraceRayInlineSupported;
        case Feature::FastGeometryShader:
            return m_FastGeometryShaderSupported;
        case Feature::ShaderExecutionReordering:
            return m_ShaderExecutionReorderingSupported;
        case Feature::Meshlets:
            return m_MeshletsSupported;
        case Feature::VariableRateShading:
            if (pInfo)
            {
                if (infoSize == sizeof(VariableRateShadingFeatureInfo))
                {
                    auto* pVrsInfo = reinterpret_cast<VariableRateShadingFeatureInfo*>(pInfo);
                    pVrsInfo->shadingRateImageTileSize = m_Options6.ShadingRateImageTileSize;
                }
                else
                    utils::NotSupported();
            }
            return m_VariableRateShadingSupported;
        case Feature::VirtualResources:
            return true;
        case Feature::ComputeQueue:
            return (getQueue(CommandQueue::Compute) != nullptr);
        case Feature::CopyQueue:
            return (getQueue(CommandQueue::Copy) != nullptr);
        case Feature::ConservativeRasterization:
            return true;
        case Feature::ConstantBufferRanges:
            return true;
        default:
            return false;
        }
    }

    FormatSupport Device::queryFormatSupport(Format format)
    {
        const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(format);

        FormatSupport result = FormatSupport::None;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT featureData = {};
        featureData.Format = formatMapping.rtvFormat;

        m_Context.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &featureData, sizeof(featureData));

        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER)
            result = result | FormatSupport::Buffer;
        if (featureData.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE))
            result = result | FormatSupport::Texture;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)
            result = result | FormatSupport::DepthStencil;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)
            result = result | FormatSupport::RenderTarget;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)
            result = result | FormatSupport::Blendable;

        if (formatMapping.srvFormat != featureData.Format)
        {
            featureData.Format = formatMapping.srvFormat;
            featureData.Support1 = (D3D12_FORMAT_SUPPORT1)0;
            featureData.Support2 = (D3D12_FORMAT_SUPPORT2)0;
            m_Context.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &featureData, sizeof(featureData));
        }

        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER)
            result = result | FormatSupport::IndexBuffer;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER)
            result = result | FormatSupport::VertexBuffer;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
            result = result | FormatSupport::ShaderLoad;
        if (featureData.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)
            result = result | FormatSupport::ShaderSample;
        if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD)
            result = result | FormatSupport::ShaderAtomic;
        if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
            result = result | FormatSupport::ShaderUavLoad;
        if (featureData.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)
            result = result | FormatSupport::ShaderUavStore;

        return result;
    }

    Object Device::getNativeQueue(ObjectType objectType, CommandQueue queue)
    {
        if (objectType != ObjectTypes::D3D12_CommandQueue)
            return nullptr;

        if (queue >= CommandQueue::Count)
            return nullptr;

        Queue* pQueue = getQueue(queue);

        if (!pQueue)
            return nullptr;

        return Object(pQueue->queue.Get());
    }

    IDescriptorHeap* Device::getDescriptorHeap(DescriptorHeapType heapType)
    {
        switch(heapType)
        {
        case DescriptorHeapType::RenderTargetView:
            return &m_Resources.renderTargetViewHeap;
        case DescriptorHeapType::DepthStencilView:
            return &m_Resources.depthStencilViewHeap;
        case DescriptorHeapType::ShaderResourceView:
            return &m_Resources.shaderResourceViewHeap;
        case DescriptorHeapType::Sampler:
            return &m_Resources.samplerHeap;
        }

        return nullptr;
    }

    HeapHandle Device::createHeap(const HeapDesc& d)
    {
        D3D12_HEAP_DESC heapDesc;
        heapDesc.SizeInBytes = d.capacity;
        heapDesc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
        heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapDesc.Properties.CreationNodeMask = 1; // no mGPU support in nvrhi so far
        heapDesc.Properties.VisibleNodeMask = 1;

        if (m_Options.ResourceHeapTier == D3D12_RESOURCE_HEAP_TIER_1)
            heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
        else
            heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

        switch (d.type)
        {
        case HeapType::DeviceLocal:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            break;
        case HeapType::Upload:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
            break;
        case HeapType::Readback:
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
            break;
        default:
            utils::InvalidEnum();
            return nullptr;
        }

        RefCountPtr<ID3D12Heap> d3dHeap;
        const HRESULT res = m_Context.device->CreateHeap(&heapDesc, IID_PPV_ARGS(&d3dHeap));

        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateHeap call failed for heap " << utils::DebugNameToString(d.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());

            return nullptr;
        }

        if (!d.debugName.empty())
        {
            std::wstring wname(d.debugName.begin(), d.debugName.end());
            d3dHeap->SetName(wname.c_str());
        }

        Heap* heap = new Heap();
        heap->heap = d3dHeap;
        heap->desc = d;
        return HeapHandle::Create(heap);
    }

} // namespace nvrhi::d3d12
