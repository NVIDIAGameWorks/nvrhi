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

        for (size_t i = 0; i < numCmd; i++)
        {
            CommandList* commandList = checked_cast<CommandList*>(ppCmd[i]);
            TrackedCommandBufferPtr commandBuffer = commandList->getCurrentCmdBuf();

            commandBuffers[i] = commandBuffer->cmdBuf;
            m_CommandBuffersInFlight.push_back(commandBuffer);
        }

        m_LastSubmittedID++;

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

        m_Queue.submit(submitInfo);

        m_WaitSemaphores.clear();
        m_WaitSemaphoreValues.clear();
        m_SignalSemaphores.clear();
        m_SignalSemaphoreValues.clear();
        
        return m_LastSubmittedID;
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

    vk::Semaphore Device::getQueueSemaphore(CommandQueue queueID)
    {
        Queue& queue = *m_Queues[uint32_t(queueID)];

        return queue.trackingSemaphore;
    }

    void Device::queueWaitForSemaphore(CommandQueue waitQueueID, vk::Semaphore semaphore, uint64_t value)
    {
        Queue& waitQueue = *m_Queues[uint32_t(waitQueueID)];

        waitQueue.addWaitSemaphore(semaphore, value);
    }

    void Device::queueSignalSemaphore(CommandQueue executionQueueID, vk::Semaphore semaphore, uint64_t value)
    {
        Queue& executionQueue = *m_Queues[uint32_t(executionQueueID)];

        executionQueue.addSignalSemaphore(semaphore, value);
    }

    void Device::queueWaitForCommandList(CommandQueue waitQueueID, CommandQueue executionQueueID, uint64_t instance)
    {
        queueWaitForSemaphore(waitQueueID, getQueueSemaphore(executionQueueID), instance);
    }

    uint64_t Device::queueGetCompletedInstance(CommandQueue queue)
    {
        return m_Context.device.getSemaphoreCounterValue(getQueueSemaphore(queue));
    }

} // namespace nvrhi::vulkan
