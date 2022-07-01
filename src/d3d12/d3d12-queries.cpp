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

namespace nvrhi::d3d12
{
    TimerQuery::~TimerQuery()
    {
        m_Resources.timerQueries.release(static_cast<int>(beginQueryIndex) / 2);
    }

    EventQueryHandle Device::createEventQuery(void)
    {
        EventQuery *ret = new EventQuery();
        return EventQueryHandle::Create(ret);
    }

    void Device::setEventQuery(IEventQuery* _query, CommandQueue queue)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);
        Queue* pQueue = getQueue(queue);
        
        query->started = true;
        query->fence = pQueue->fence;
        query->fenceCounter = pQueue->lastSubmittedInstance;
        query->resolved = false;
    }

    bool Device::pollEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        if (!query->started)
            return false;

        if (query->resolved)
            return true;

        assert(query->fence);
        
        if (query->fence->GetCompletedValue() >= query->fenceCounter)
        {
            query->resolved = true;
            query->fence = nullptr;
        }

        return query->resolved;
    }

    void Device::waitEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        if (!query->started || query->resolved)
            return;

        assert(query->fence);

        WaitForFence(query->fence, query->fenceCounter, m_FenceEvent);
    }

    void Device::resetEventQuery(IEventQuery* _query)
    {
        EventQuery* query = checked_cast<EventQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->fence = nullptr;
    }
    
    TimerQueryHandle Device::createTimerQuery(void)
    {
        if (!m_Context.timerQueryHeap)
        {
            std::lock_guard lockGuard(m_Mutex);

            if (!m_Context.timerQueryHeap)
            {
                D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
                queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
                queryHeapDesc.Count = uint32_t(m_Resources.timerQueries.getCapacity()) * 2; // Use 2 D3D12 queries per 1 TimerQuery
                m_Context.device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_Context.timerQueryHeap));

                BufferDesc qbDesc;
                qbDesc.byteSize = queryHeapDesc.Count * 8;
                qbDesc.cpuAccess = CpuAccessMode::Read;

                BufferHandle timerQueryBuffer = createBuffer(qbDesc);
                m_Context.timerQueryResolveBuffer = checked_cast<Buffer*>(timerQueryBuffer.Get());
            }
        }

        int queryIndex = m_Resources.timerQueries.allocate();

        if (queryIndex < 0)
            return nullptr;
        
        TimerQuery* query = new TimerQuery(m_Resources);
        query->beginQueryIndex = uint32_t(queryIndex) * 2;
        query->endQueryIndex = query->beginQueryIndex + 1;
        query->resolved = false;
        query->time = 0.f;

        return TimerQueryHandle::Create(query);
    }

    bool Device::pollTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        if (!query->started)
            return false;

        if (!query->fence)
            return true;

        if (query->fence->GetCompletedValue() >= query->fenceCounter)
        {
            query->fence = nullptr;
            return true;
        }

        return false;
    }

    float Device::getTimerQueryTime(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        if (!query->resolved)
        {
            if (query->fence)
            {
                WaitForFence(query->fence, query->fenceCounter, m_FenceEvent);
                query->fence = nullptr;
            }

            uint64_t frequency;
            getQueue(CommandQueue::Graphics)->queue->GetTimestampFrequency(&frequency);

            D3D12_RANGE bufferReadRange = {
                query->beginQueryIndex * sizeof(uint64_t),
                (query->beginQueryIndex + 2) * sizeof(uint64_t) };
            uint64_t *data;
            const HRESULT res = m_Context.timerQueryResolveBuffer->resource->Map(0, &bufferReadRange, (void**)&data);

            if (FAILED(res))
            {
                m_Context.error("getTimerQueryTime: Map() failed");
                return 0.f;
            }

            query->resolved = true;
            query->time = float(double(data[query->endQueryIndex] - data[query->beginQueryIndex]) / double(frequency));

            m_Context.timerQueryResolveBuffer->resource->Unmap(0, nullptr);
        }

        return query->time;
    }

    void Device::resetTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->time = 0.f;
        query->fence = nullptr;
    }

    void CommandList::beginTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        m_Instance->referencedTimerQueries.push_back(query);

        m_ActiveCommandList->commandList->EndQuery(m_Context.timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, query->beginQueryIndex);

        // two timestamps within the same command list are always reliably comparable, so we avoid kicking off here
        // (note: we don't call SetStablePowerState anymore)
    }

    void CommandList::endTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = checked_cast<TimerQuery*>(_query);

        m_Instance->referencedTimerQueries.push_back(query);

        m_ActiveCommandList->commandList->EndQuery(m_Context.timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, query->endQueryIndex);

        m_ActiveCommandList->commandList->ResolveQueryData(m_Context.timerQueryHeap,
            D3D12_QUERY_TYPE_TIMESTAMP,
            query->beginQueryIndex,
            2,
            m_Context.timerQueryResolveBuffer->resource,
            query->beginQueryIndex * 8);
    }


} // namespace nvrhi::d3d12
