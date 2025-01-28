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
#include "nvrhi/common/misc.h"

namespace nvrhi::vulkan
{

    TrackedCommandBuffer::~TrackedCommandBuffer()
    {
        m_Context.device.destroyCommandPool(cmdPool, m_Context.allocationCallbacks);
    }

    Queue::Queue(const VulkanContext& context, CommandQueue queueID, vk::Queue queue, uint32_t queueFamilyIndex)
        : m_Context(context)
        , m_Queue(queue)
        , m_QueueID(queueID)
        , m_QueueFamilyIndex(queueFamilyIndex)
    {
        auto semaphoreTypeInfo = vk::SemaphoreTypeCreateInfo()
            .setSemaphoreType(vk::SemaphoreType::eTimeline);

        auto semaphoreInfo = vk::SemaphoreCreateInfo()
            .setPNext(&semaphoreTypeInfo);

        trackingSemaphore = context.device.createSemaphore(semaphoreInfo, context.allocationCallbacks);
    }

    Queue::~Queue()
    {
        m_Context.device.destroySemaphore(trackingSemaphore, m_Context.allocationCallbacks);
        trackingSemaphore = vk::Semaphore();
    }

    TrackedCommandBufferPtr Queue::createCommandBuffer()
    {
        vk::Result res;

        TrackedCommandBufferPtr ret = std::make_shared<TrackedCommandBuffer>(m_Context);

        auto cmdPoolInfo = vk::CommandPoolCreateInfo()
                            .setQueueFamilyIndex(m_QueueFamilyIndex)
                            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                                        vk::CommandPoolCreateFlagBits::eTransient);

        res = m_Context.device.createCommandPool(&cmdPoolInfo, m_Context.allocationCallbacks, &ret->cmdPool);
        CHECK_VK_FAIL(res)
        
        // allocate command buffer
        auto allocInfo = vk::CommandBufferAllocateInfo()
                            .setLevel(vk::CommandBufferLevel::ePrimary)
                            .setCommandPool(ret->cmdPool)
                            .setCommandBufferCount(1);

        res = m_Context.device.allocateCommandBuffers(&allocInfo, &ret->cmdBuf);
        CHECK_VK_FAIL(res)

        return ret;
    }

    TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer()
    {
        std::lock_guard lockGuard(m_Mutex); // this is called from CommandList::open, so free-threaded

        uint64_t recordingID = ++m_LastRecordingID;

        TrackedCommandBufferPtr cmdBuf;
        if (m_CommandBuffersPool.empty())
        {
            cmdBuf = createCommandBuffer();
        }
        else
        {
            cmdBuf = m_CommandBuffersPool.front();
            m_CommandBuffersPool.pop_front();
        }

        cmdBuf->recordingID = recordingID;
        return cmdBuf;
    }

    void Queue::addWaitSemaphore(vk::Semaphore semaphore, uint64_t value)
    {
        if (!semaphore)
            return;

        m_WaitSemaphores.push_back(semaphore);
        m_WaitSemaphoreValues.push_back(value);
    }

    void Queue::addSignalSemaphore(vk::Semaphore semaphore, uint64_t value)
    {
        if (!semaphore)
            return;

        m_SignalSemaphores.push_back(semaphore);
        m_SignalSemaphoreValues.push_back(value);
    }

    uint64_t Queue::submit(ICommandList* const* ppCmd, size_t numCmd)
    {
        std::vector<vk::PipelineStageFlags> waitStageArray(m_WaitSemaphores.size());
        std::vector<vk::CommandBuffer> commandBuffers(numCmd);

        for (size_t i = 0; i < m_WaitSemaphores.size(); i++)
        {
            waitStageArray[i] = vk::PipelineStageFlagBits::eTopOfPipe;
        }

        m_LastSubmittedID++;

        for (size_t i = 0; i < numCmd; i++)
        {
            CommandList* commandList = checked_cast<CommandList*>(ppCmd[i]);
            TrackedCommandBufferPtr commandBuffer = commandList->getCurrentCmdBuf();

            commandBuffers[i] = commandBuffer->cmdBuf;
            m_CommandBuffersInFlight.push_back(commandBuffer);

            for (const auto& buffer : commandBuffer->referencedStagingBuffers)
            {
                buffer->lastUseQueue = m_QueueID;
                buffer->lastUseCommandListID = m_LastSubmittedID;
            }
        }
        
        m_SignalSemaphores.push_back(trackingSemaphore);
        m_SignalSemaphoreValues.push_back(m_LastSubmittedID);

        auto timelineSemaphoreInfo = vk::TimelineSemaphoreSubmitInfo()
            .setSignalSemaphoreValueCount(uint32_t(m_SignalSemaphoreValues.size()))
            .setPSignalSemaphoreValues(m_SignalSemaphoreValues.data());

        if (!m_WaitSemaphoreValues.empty()) 
        {
            timelineSemaphoreInfo.setWaitSemaphoreValueCount(uint32_t(m_WaitSemaphoreValues.size()));
            timelineSemaphoreInfo.setPWaitSemaphoreValues(m_WaitSemaphoreValues.data());
        }

        auto submitInfo = vk::SubmitInfo()
            .setPNext(&timelineSemaphoreInfo)
            .setCommandBufferCount(uint32_t(numCmd))
            .setPCommandBuffers(commandBuffers.data())
            .setWaitSemaphoreCount(uint32_t(m_WaitSemaphores.size()))
            .setPWaitSemaphores(m_WaitSemaphores.data())
            .setPWaitDstStageMask(waitStageArray.data())
            .setSignalSemaphoreCount(uint32_t(m_SignalSemaphores.size()))
            .setPSignalSemaphores(m_SignalSemaphores.data());

        try {
            m_Queue.submit(submitInfo);
        }
        catch (vk::DeviceLostError&)
        {
            m_Context.messageCallback->message(MessageSeverity::Error, "Device Removed!");
        }

        m_WaitSemaphores.clear();
        m_WaitSemaphoreValues.clear();
        m_SignalSemaphores.clear();
        m_SignalSemaphoreValues.clear();
        
        return m_LastSubmittedID;
    }

    void Queue::updateTextureTileMappings(ITexture* _texture, const TextureTilesMapping* tileMappings, uint32_t numTileMappings)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        std::vector<vk::SparseImageMemoryBind> sparseImageMemoryBinds;
        std::vector<vk::SparseMemoryBind> sparseMemoryBinds;

        for (size_t i = 0; i < numTileMappings; i++)
        {
            uint32_t numRegions = tileMappings[i].numTextureRegions;
            Heap* heap = tileMappings[i].heap ? checked_cast<Heap*>(tileMappings[i].heap) : nullptr;
            vk::DeviceMemory deviceMemory = heap ? heap->memory : VK_NULL_HANDLE;

            for (uint32_t j = 0; j < numRegions; ++j)
            {
                const TiledTextureCoordinate& tiledTextureCoordinate = tileMappings[i].tiledTextureCoordinates[j];
                const TiledTextureRegion& tiledTextureRegion = tileMappings[i].tiledTextureRegions[j];

                if (tiledTextureRegion.tilesNum)
                {
                    sparseMemoryBinds.push_back(vk::SparseMemoryBind()
                        .setResourceOffset(0)
                        .setSize(tiledTextureRegion.tilesNum * texture->tileByteSize)
                        .setMemory(deviceMemory)
                        .setMemoryOffset(deviceMemory ? tileMappings[i].byteOffsets[j] : 0));
                }
                else
                {
                    vk::ImageSubresource subresource = {};
                    subresource.arrayLayer = tiledTextureCoordinate.arrayLevel;
                    subresource.mipLevel = tiledTextureCoordinate.mipLevel;

                    vk::Offset3D offset3D;
                    offset3D.x = tiledTextureCoordinate.x;
                    offset3D.y = tiledTextureCoordinate.y;
                    offset3D.z = tiledTextureCoordinate.z;

                    vk::Extent3D extent3D;
                    extent3D.width = tiledTextureRegion.width;
                    extent3D.height = tiledTextureRegion.height;
                    extent3D.depth = tiledTextureRegion.depth;

                    sparseImageMemoryBinds.push_back(vk::SparseImageMemoryBind()
                        .setSubresource(subresource)
                        .setOffset(offset3D)
                        .setExtent(extent3D)
                        .setMemory(deviceMemory)
                        .setMemoryOffset(deviceMemory ? tileMappings[i].byteOffsets[j] : 0));
                }
            }
        }

        vk::BindSparseInfo bindSparseInfo = {};

        vk::SparseImageMemoryBindInfo sparseImageMemoryBindInfo;
        if (!sparseImageMemoryBinds.empty())
        {
            sparseImageMemoryBindInfo.setImage(texture->image);
            sparseImageMemoryBindInfo.setBinds(sparseImageMemoryBinds);
            bindSparseInfo.setImageBinds(sparseImageMemoryBindInfo);
        }

        vk::SparseImageOpaqueMemoryBindInfo sparseImageOpaqueMemoryBindInfo;
        if (!sparseMemoryBinds.empty())
        {
            sparseImageOpaqueMemoryBindInfo.setImage(texture->image);
            sparseImageOpaqueMemoryBindInfo.setBinds(sparseMemoryBinds);
            bindSparseInfo.setImageOpaqueBinds(sparseImageOpaqueMemoryBindInfo);
        }

        m_Queue.bindSparse(bindSparseInfo, vk::Fence());
    }

    uint64_t Queue::updateLastFinishedID()
    {
        m_LastFinishedID = m_Context.device.getSemaphoreCounterValue(trackingSemaphore);

        return m_LastFinishedID;
    }

    void Queue::retireCommandBuffers()
    {
        std::list<TrackedCommandBufferPtr> submissions = std::move(m_CommandBuffersInFlight);

        uint64_t lastFinishedID = updateLastFinishedID();
        
        for (const TrackedCommandBufferPtr& cmd : submissions)
        {
            if (cmd->submissionID <= lastFinishedID)
            {
                cmd->referencedResources.clear();
                cmd->referencedStagingBuffers.clear();
                cmd->submissionID = 0;
                m_CommandBuffersPool.push_back(cmd);

#ifdef NVRHI_WITH_RTXMU
                if (!cmd->rtxmuBuildIds.empty())
                {
                    std::lock_guard lockGuard(m_Context.rtxMuResources->asListMutex);
                    
                    m_Context.rtxMuResources->asBuildsCompleted.insert(m_Context.rtxMuResources->asBuildsCompleted.end(),
                        cmd->rtxmuBuildIds.begin(), cmd->rtxmuBuildIds.end());

                    cmd->rtxmuBuildIds.clear();
                }
                if (!cmd->rtxmuCompactionIds.empty())
                {
                    m_Context.rtxMemUtil->GarbageCollection(cmd->rtxmuCompactionIds);
                    cmd->rtxmuCompactionIds.clear();
                }
#endif
            }
            else
            {
                m_CommandBuffersInFlight.push_back(cmd);
            }
        }
    }

    TrackedCommandBufferPtr Queue::getCommandBufferInFlight(uint64_t submissionID)
    {
        for (const TrackedCommandBufferPtr& cmd : m_CommandBuffersInFlight)
        {
            if (cmd->submissionID == submissionID)
                return cmd;
        }

        return nullptr;
    }

    VkSemaphore Device::getQueueSemaphore(CommandQueue queueID)
    {
        Queue& queue = *m_Queues[uint32_t(queueID)];

        return queue.trackingSemaphore;
    }

    void Device::queueWaitForSemaphore(CommandQueue waitQueueID, VkSemaphore semaphore, uint64_t value)
    {
        Queue& waitQueue = *m_Queues[uint32_t(waitQueueID)];

        waitQueue.addWaitSemaphore(semaphore, value);
    }

    void Device::queueSignalSemaphore(CommandQueue executionQueueID, VkSemaphore semaphore, uint64_t value)
    {
        Queue& executionQueue = *m_Queues[uint32_t(executionQueueID)];

        executionQueue.addSignalSemaphore(semaphore, value);
    }

    void Device::queueWaitForCommandList(CommandQueue waitQueueID, CommandQueue executionQueueID, uint64_t instance)
    {
        queueWaitForSemaphore(waitQueueID, getQueueSemaphore(executionQueueID), instance);
    }

    void Device::updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, uint32_t numTileMappings, CommandQueue executionQueue)
    {
        Queue& queue = *m_Queues[uint32_t(executionQueue)];

        queue.updateTextureTileMappings(texture, tileMappings, numTileMappings);
    }

    uint64_t Device::queueGetCompletedInstance(CommandQueue queue)
    {
        return m_Context.device.getSemaphoreCounterValue(getQueueSemaphore(queue));
    }

    bool Queue::pollCommandList(uint64_t commandListID)
    {
        if (commandListID > m_LastSubmittedID || commandListID == 0)
            return false;
        
        bool completed = getLastFinishedID() >= commandListID;
        if (completed)
            return true;

        completed = updateLastFinishedID() >= commandListID;
        return completed;
    }

    bool Queue::waitCommandList(uint64_t commandListID, uint64_t timeout)
    {
        if (commandListID > m_LastSubmittedID || commandListID == 0)
            return false;

        if (pollCommandList(commandListID))
            return true;

        std::array<const vk::Semaphore, 1> semaphores = { trackingSemaphore };
        std::array<uint64_t, 1> waitValues = { commandListID };

        auto waitInfo = vk::SemaphoreWaitInfo()
            .setSemaphores(semaphores)
            .setValues(waitValues);

        vk::Result result = m_Context.device.waitSemaphores(waitInfo, timeout);

        return (result == vk::Result::eSuccess);
    }
} // namespace nvrhi::vulkan
