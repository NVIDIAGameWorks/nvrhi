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

#include "vulkan-backend.h"

namespace nvrhi::vulkan
{

    CommandList::CommandList(Device* device, const VulkanContext& context, const CommandListParameters& parameters)
        : m_Device(device)
        , m_Context(context)
        , m_CommandListParameters(parameters)
        , m_StateTracker(context.messageCallback)
        , m_UploadManager(std::make_unique<UploadManager>(device, parameters.uploadChunkSize, 0, false))
        , m_ScratchManager(std::make_unique<UploadManager>(device, parameters.scratchChunkSize, parameters.scratchMaxMemory, true))
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

    nvrhi::Object CommandList::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_CommandBuffer:
            return Object(m_CurrentCmdBuf->cmdBuf);
        default:
            return nullptr;
        }
    }

    void CommandList::open()
    {
        m_CurrentCmdBuf = m_Device->getQueue(m_CommandListParameters.queueType)->getOrCreateCommandBuffer();

        auto beginInfo = vk::CommandBufferBeginInfo()
            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        (void)m_CurrentCmdBuf->cmdBuf.begin(&beginInfo);
        m_CurrentCmdBuf->referencedResources.push_back(this); // prevent deletion of e.g. UploadManager

        clearState();
    }

    void CommandList::close()
    {
        endRenderPass();

        m_StateTracker.keepBufferInitialStates();
        m_StateTracker.keepTextureInitialStates();
        commitBarriers();

#ifdef NVRHI_WITH_RTXMU
        if (!m_CurrentCmdBuf->rtxmuBuildIds.empty())
        {
            m_Context.rtxMemUtil->PopulateCompactionSizeCopiesCommandList(m_CurrentCmdBuf->cmdBuf, m_CurrentCmdBuf->rtxmuBuildIds);
        }
#endif

        m_CurrentCmdBuf->cmdBuf.end();

        clearState();

        flushVolatileBufferWrites();
    }

    void CommandList::clearState()
    {
        endRenderPass();

        m_CurrentPipelineLayout = vk::PipelineLayout();
        m_CurrentPushConstantsVisibility = vk::ShaderStageFlagBits();

        m_CurrentGraphicsState = GraphicsState();
        m_CurrentComputeState = ComputeState();
        m_CurrentMeshletState = MeshletState();
        m_CurrentRayTracingState = rt::State();
        m_CurrentShaderTablePointers = ShaderTableState();

        m_AnyVolatileBufferWrites = false;

        // TODO: add real context clearing code here 
    }

    void CommandList::setPushConstants(const void* data, size_t byteSize)
    {
        assert(m_CurrentCmdBuf);

        m_CurrentCmdBuf->cmdBuf.pushConstants(m_CurrentPipelineLayout, m_CurrentPushConstantsVisibility, 0, uint32_t(byteSize), data);
    }

    void CommandList::executed(Queue& queue, const uint64_t submissionID)
    {
        assert(m_CurrentCmdBuf);

        m_CurrentCmdBuf->submissionID = submissionID;

        const CommandQueue queueID = queue.getQueueID();
        const uint64_t recordingID = m_CurrentCmdBuf->recordingID;

        m_CurrentCmdBuf = nullptr;

        submitVolatileBuffers(recordingID, submissionID);

        m_StateTracker.commandListSubmitted();

        m_UploadManager->submitChunks(
            MakeVersion(recordingID, queueID, false),
            MakeVersion(submissionID, queueID, true));

        m_ScratchManager->submitChunks(
            MakeVersion(recordingID, queueID, false),
            MakeVersion(submissionID, queueID, true));

        m_VolatileBufferStates.clear();
    }
    
}