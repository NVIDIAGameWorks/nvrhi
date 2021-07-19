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

#include "d3d11-backend.h"

#include <nvrhi/common/containers.h>
#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <sstream>
#include <iomanip>


namespace nvrhi::d3d11
{

static bool checkedCreateQuery(const D3D11_QUERY_DESC& queryDesc, const char* name, const Context& context, ID3D11Query** pQuery)
{
    const HRESULT res = context.device->CreateQuery(&queryDesc, pQuery);

    if (FAILED(res))
    {
        std::stringstream ss;
        ss << "CreateQuery call failed for " << name
           << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
        context.error(ss.str());

        return false;
    }

    return true;
}

EventQueryHandle Device::createEventQuery()
{
    EventQuery *ret = new EventQuery();

    D3D11_QUERY_DESC queryDesc;
    queryDesc.Query = D3D11_QUERY_EVENT;
    queryDesc.MiscFlags = 0;

    if (!checkedCreateQuery(queryDesc, "EventQuery", m_Context, &ret->query))
    {
        delete ret;
        return nullptr;
    }

    return EventQueryHandle::Create(ret);
}

void Device::setEventQuery(IEventQuery* _query, CommandQueue queue)
{
    (void)queue;

    EventQuery* query = checked_cast<EventQuery*>(_query);

    m_Context.immediateContext->End(query->query.Get());
}

bool Device::pollEventQuery(IEventQuery* _query)
{
    EventQuery* query = checked_cast<EventQuery*>(_query);

    if (query->resolved)
    {
        return true;
    }

    const HRESULT hr = m_Context.immediateContext->GetData(query->query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);

    if (SUCCEEDED(hr))
    {
        query->resolved = true;
        return true;
    } else {
        return false;
    }
}

void Device::waitEventQuery(IEventQuery* _query)
{
    EventQuery* query = checked_cast<EventQuery*>(_query);

    if (query->resolved)
    {
        return;
    }

    HRESULT hr;

    do {
        hr = m_Context.immediateContext->GetData(query->query.Get(), nullptr, 0, 0);
    } while (hr == S_FALSE);

    assert(SUCCEEDED(hr));
}

void Device::resetEventQuery(IEventQuery* _query)
{
    EventQuery* query = checked_cast<EventQuery*>(_query);

    query->resolved = false;
}

TimerQueryHandle Device::createTimerQuery(void)
{
    TimerQuery *ret = new TimerQuery();

    D3D11_QUERY_DESC queryDesc;

    queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    queryDesc.MiscFlags = 0;

    if (!checkedCreateQuery(queryDesc, "TimerQuery Disjoint", m_Context, &ret->disjoint))
    {
        delete ret;
        return nullptr;
    }

    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    queryDesc.MiscFlags = 0;

    if (!checkedCreateQuery(queryDesc, "TimerQuery Start", m_Context, &ret->start))
    {
        delete ret;
        return nullptr;
    }

    if (!checkedCreateQuery(queryDesc, "TimerQuery End", m_Context, &ret->end))
    {
        delete ret;
        return nullptr;
    }

    return TimerQueryHandle::Create(ret);
}

void CommandList::beginTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = checked_cast<TimerQuery*>(_query);

    assert(!query->resolved);
    m_Context.immediateContext->Begin(query->disjoint.Get());
    m_Context.immediateContext->End(query->start.Get());
}

void CommandList::endTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = checked_cast<TimerQuery*>(_query);

    assert(!query->resolved);
    m_Context.immediateContext->End(query->end.Get());
    m_Context.immediateContext->End(query->disjoint.Get());
}

bool Device::pollTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = checked_cast<TimerQuery*>(_query);

    if (query->resolved)
    {
        return true;
    }

    const HRESULT hr = m_Context.immediateContext->GetData(query->disjoint.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);

    if (SUCCEEDED(hr))
    {
        // note: we don't mark this as resolved since we need to read data back and compute timing info
        // this is done in getTimerQueryTimeMS
        return true;
    } else {
        return false;
    }
}

float Device::getTimerQueryTime(ITimerQuery* _query)
{
    TimerQuery* query = checked_cast<TimerQuery*>(_query);

    if (!query->resolved)
    {
        HRESULT hr;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;

        do {
            hr = m_Context.immediateContext->GetData(query->disjoint.Get(), &disjointData, sizeof(disjointData), 0);
        } while (hr == S_FALSE);
        assert(SUCCEEDED(hr));

        query->resolved = true;

        if (disjointData.Disjoint == TRUE)
        {
            // query resolved but captured invalid timing data
            query->time = 0.f;
        } else {
            UINT64 startTime = 0, endTime = 0;
            do {
                hr = m_Context.immediateContext->GetData(query->start.Get(), &startTime, sizeof(startTime), 0);
            } while (hr == S_FALSE);
            assert(SUCCEEDED(hr));

            do {
                hr = m_Context.immediateContext->GetData(query->end.Get(), &endTime, sizeof(endTime), 0);
            } while (hr == S_FALSE);
            assert(SUCCEEDED(hr));

            double delta = double(endTime - startTime);
            double frequency = double(disjointData.Frequency);
            query->time = float(delta / frequency);
        }
    }

    return query->time;
}

void Device::resetTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = checked_cast<TimerQuery*>(_query);

    query->resolved = false;
    query->time = 0.f;
}

} // namespace nvrhi::d3d11