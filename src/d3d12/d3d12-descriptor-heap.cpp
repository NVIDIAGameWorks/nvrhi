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

namespace nvrhi::d3d12
{
    
    StaticDescriptorHeap::StaticDescriptorHeap(const Context& context)
        : m_Context(context)
    {
    }
    
    HRESULT StaticDescriptorHeap::allocateResources(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool shaderVisible)
    {
        m_Heap = nullptr;
        m_ShaderVisibleHeap = nullptr;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = heapType;
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT hr = m_Context.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap));

        if (FAILED(hr))
            return hr;

        if (shaderVisible)
        {
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            hr = m_Context.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_ShaderVisibleHeap));

            if (FAILED(hr))
                return hr;

            m_StartCpuHandleShaderVisible = m_ShaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
            m_StartGpuHandleShaderVisible = m_ShaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
        }

        m_NumDescriptors = heapDesc.NumDescriptors;
        m_HeapType = heapDesc.Type;
        m_StartCpuHandle = m_Heap->GetCPUDescriptorHandleForHeapStart();
        m_Stride = m_Context.device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        m_AllocatedDescriptors.resize(m_NumDescriptors);

        return S_OK;
    }

    static uint32_t nextPowerOf2(uint32_t v)
    {
        // https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2

        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;

        return v;
    }

    HRESULT StaticDescriptorHeap::Grow(uint32_t minRequiredSize)
    {
        uint32_t oldSize = m_NumDescriptors;
        uint32_t newSize = nextPowerOf2(minRequiredSize);

        RefCountPtr<ID3D12DescriptorHeap> oldHeap = m_Heap; 

        HRESULT hr = allocateResources(m_HeapType, newSize, m_ShaderVisibleHeap != nullptr);
        
        if (FAILED(hr))
            return hr;

        m_Context.device->CopyDescriptorsSimple(oldSize, m_StartCpuHandle, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);

        if (m_ShaderVisibleHeap != nullptr)
        {
            m_Context.device->CopyDescriptorsSimple(oldSize, m_StartCpuHandleShaderVisible, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);
        }

        return S_OK;
    }

    DescriptorIndex StaticDescriptorHeap::allocateDescriptors(uint32_t count)
    {
        std::lock_guard lockGuard(m_Mutex);

        DescriptorIndex foundIndex = 0;
        uint32_t freeCount = 0;
        bool found = false;

        // Find a contiguous range of 'count' indices for which m_AllocatedDescriptors[index] is false

        for (DescriptorIndex index = m_SearchStart; index < m_NumDescriptors; index++)
        {
            if (m_AllocatedDescriptors[index])
                freeCount = 0;
            else
                freeCount += 1;

            if (freeCount >= count)
            {
                foundIndex = index - count + 1;
                found = true;
                break;
            }
        }

        if (!found)
        {
            foundIndex = m_NumDescriptors;

            if (FAILED(Grow(m_NumDescriptors + count)))
            {
                m_Context.error("Failed to grow a descriptor heap!");
                return c_InvalidDescriptorIndex;
            }
        }

        for (DescriptorIndex index = foundIndex; index < foundIndex + count; index++)
        {
            m_AllocatedDescriptors[index] = true;
        }

        m_NumAllocatedDescriptors += count;

        m_SearchStart = foundIndex + count;
        return foundIndex;
    }

    DescriptorIndex StaticDescriptorHeap::allocateDescriptor()
    {
        return allocateDescriptors(1);
    }

    void StaticDescriptorHeap::releaseDescriptors(DescriptorIndex baseIndex, uint32_t count)
    {
        std::lock_guard lockGuard(m_Mutex);

        if (count == 0)
            return;

        for (DescriptorIndex index = baseIndex; index < baseIndex + count; index++)
        {
#ifdef _DEBUG
            if (!m_AllocatedDescriptors[index])
            {
                m_Context.error("Attempted to release an un-allocated descriptor");
            }
#endif

            m_AllocatedDescriptors[index] = false;
        }

        m_NumAllocatedDescriptors -= count;

        if (m_SearchStart > baseIndex)
            m_SearchStart = baseIndex;
    }

    void StaticDescriptorHeap::releaseDescriptor(DescriptorIndex index)
    {
        releaseDescriptors(index, 1);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::getCpuHandle(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandle;
        handle.ptr += index * m_Stride;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::getCpuHandleShaderVisible(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandleShaderVisible;
        handle.ptr += index * m_Stride;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::getGpuHandle(DescriptorIndex index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_StartGpuHandleShaderVisible;
        handle.ptr += index * m_Stride;
        return handle;
    }

    ID3D12DescriptorHeap* StaticDescriptorHeap::getHeap() const
    {
        return m_Heap;
    }

    ID3D12DescriptorHeap* StaticDescriptorHeap::getShaderVisibleHeap() const
    {
        return m_ShaderVisibleHeap;
    }

    void StaticDescriptorHeap::copyToShaderVisibleHeap(DescriptorIndex index, uint32_t count)
    {
        m_Context.device->CopyDescriptorsSimple(count, getCpuHandleShaderVisible(index), getCpuHandle(index), m_HeapType);
    }

} // namespace nvrhi::d3d12
