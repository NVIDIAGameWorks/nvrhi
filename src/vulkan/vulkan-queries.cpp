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
#include <nvrhi/common/misc.h>

namespace nvrhi::vulkan
{

    EventQueryHandle Device::createEventQuery(void)
    {
        EventQuery *query = new EventQuery();
        return EventQueryHandle::Create(query);
    }

    void Device::setEventQuery(IEventQuery* _query, CommandQueue queue)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        assert(query->commandListID == 0);

        query->queue = queue;
        query->commandListID = m_Queues[uint32_t(queue)]->getLastSubmittedID();
    }

    bool Device::pollEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        if (query->commandListID == 0)
            return false;

        auto& queue = *m_Queues[uint32_t(query->queue)];
        bool completed = queue.getLastFinishedID() >= query->commandListID;
        if (completed)
            return true;
        
        completed = queue.updateLastFinishedID() >= query->commandListID;
        return completed;
    }

    void Device::waitEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        if (query->commandListID == 0)
            return;

        if (pollEventQuery(_query))
            return;

        auto& queue = *m_Queues[uint32_t(query->queue)];
        std::array<const vk::Semaphore, 1> semaphores = { queue.trackingSemaphore };
        std::array<uint64_t, 1> waitValues = { query->commandListID };

        auto waitInfo = vk::SemaphoreWaitInfo()
            .setSemaphores(semaphores)
            .setValues(waitValues);

       vk::Result result = m_Context.device.waitSemaphores(waitInfo, ~0ull);
       assert(result == vk::Result::eSuccess); // TODO: add support for limited-time waits
       (void)result;
    }

    void Device::resetEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        query->commandListID = 0;
    }


    TimerQueryHandle Device::createTimerQuery(void)
    {
        if (!m_TimerQueryPool)
        {
            // set up the timer query pool on first use
            auto poolInfo = vk::QueryPoolCreateInfo()
                                .setQueryType(vk::QueryType::eTimestamp)
                                .setQueryCount(c_NumTimerQueries * 2);

            const vk::Result res = m_Context.device.createQueryPool(&poolInfo, m_Context.allocationCallbacks, &m_TimerQueryPool);
            CHECK_VK_FAIL(res)
        }

        int queryIndex = m_TimerQueryAllocator.allocate();

        if (queryIndex < 0)
        {
            m_Context.error("Insufficient query pool space, increase Device::numTimerQueries");
            return nullptr;
        }

        TimerQuery* query = new TimerQuery(m_TimerQueryAllocator);
        query->beginQueryIndex = queryIndex * 2;
        query->endQueryIndex = queryIndex * 2 + 1;

        return TimerQueryHandle::Create(query);
    }

    TimerQuery::~TimerQuery()
    {
        m_QueryAllocator.release(beginQueryIndex / 2);
        beginQueryIndex = -1;
        endQueryIndex = -1;
    }

    void CommandList::beginTimerQuery(ITimerQuery* _query)
    {
        endRenderPass();

        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        assert(query->beginQueryIndex >= 0);
        assert(!query->started);
        assert(m_CurrentCmdBuf);

        query->resolved = false;

        m_CurrentCmdBuf->cmdBuf.resetQueryPool(m_Device->getTimerQueryPool(), query->beginQueryIndex, 2);
        m_CurrentCmdBuf->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_Device->getTimerQueryPool(), query->beginQueryIndex);
    }

    void CommandList::endTimerQuery(ITimerQuery* _query)
    {
        endRenderPass();

        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        assert(query->endQueryIndex >= 0);
        assert(!query->started);
        assert(!query->resolved);

        assert(m_CurrentCmdBuf);

        m_CurrentCmdBuf->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_Device->getTimerQueryPool(), query->endQueryIndex);
        query->started = true;
    }

    bool Device::pollTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        assert(query->started);

        if (query->resolved)
        {
            return true;
        }

        uint32_t timestamps[2] = { 0, 0 };

        vk::Result res;
        res = m_Context.device.getQueryPoolResults(m_TimerQueryPool,
                                                 query->beginQueryIndex, 2,
                                                 sizeof(timestamps), timestamps,
                                                 sizeof(timestamps[0]), vk::QueryResultFlags());
        assert(res == vk::Result::eSuccess || res == vk::Result::eNotReady);

        if (res == vk::Result::eNotReady)
        {
            return false;
        }

        const auto timestampPeriod = m_Context.physicalDeviceProperties.limits.timestampPeriod; // in nanoseconds
        const float scale = 1e-9f * timestampPeriod;

        query->time = float(timestamps[1] - timestamps[0]) * scale;
        query->resolved = true;
        return true;
    }

    float Device::getTimerQueryTime(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        if (!query->started)
            return 0.f;

        if (!query->resolved)
        {
            while(!pollTimerQuery(query))
                ;
        }

        query->started = false;

        assert(query->resolved);
        return query->time;
    }

    void Device::resetTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->time = 0.f;
    }


    void CommandList::beginMarker(const char* name)
    {
        if (m_Context.extensions.EXT_debug_marker)
        {
            assert(m_CurrentCmdBuf);

            auto markerInfo = vk::DebugMarkerMarkerInfoEXT()
                                .setPMarkerName(name);
            m_CurrentCmdBuf->cmdBuf.debugMarkerBeginEXT(&markerInfo);
        }
    }

    void CommandList::endMarker()
    {
        if (m_Context.extensions.EXT_debug_marker)
        {
            assert(m_CurrentCmdBuf);

            m_CurrentCmdBuf->cmdBuf.debugMarkerEndEXT();
        }
    }

} // namespace nvrhi::vulkan