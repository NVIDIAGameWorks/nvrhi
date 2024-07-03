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
#include <sstream>

namespace nvrhi::d3d12
{

    BufferChunk::~BufferChunk()
    {
        if (buffer && cpuVA)
        {
            buffer->Unmap(0, nullptr);
            cpuVA = nullptr;
        }
    }
    
    UploadManager::UploadManager(const Context& context, class Queue* pQueue, size_t defaultChunkSize, uint64_t memoryLimit, bool isScratchBuffer)
        : m_Context(context)
        , m_Queue(pQueue)
        , m_DefaultChunkSize(defaultChunkSize)
        , m_MemoryLimit(memoryLimit)
        , m_IsScratchBuffer(isScratchBuffer)
    {
        assert(pQueue);
    }

    std::shared_ptr<BufferChunk> UploadManager::createChunk(size_t size) const
    {
        auto chunk = std::make_shared<BufferChunk>();

        size = align(size, BufferChunk::c_sizeAlignment);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = m_IsScratchBuffer ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = size;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (m_IsScratchBuffer) bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = m_Context.device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            m_IsScratchBuffer ? D3D12_RESOURCE_STATE_COMMON: D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&chunk->buffer));

        if (FAILED(hr))
            return nullptr;

        if (!m_IsScratchBuffer)
        {
            hr = chunk->buffer->Map(0, nullptr, &chunk->cpuVA);

            if (FAILED(hr))
                return nullptr;
        }

        chunk->bufferSize = size;
        chunk->gpuVA = chunk->buffer->GetGPUVirtualAddress();
        chunk->identifier = uint32_t(m_ChunkPool.size());

        std::wstringstream wss;
        if (m_IsScratchBuffer)
            wss << L"DXR Scratch Buffer " << chunk->identifier;
        else
            wss << L"Upload Buffer " << chunk->identifier;
        chunk->buffer->SetName(wss.str().c_str());

        return chunk;
    }
        
    bool UploadManager::suballocateBuffer(uint64_t size, ID3D12GraphicsCommandList* pCommandList, ID3D12Resource** pBuffer, size_t* pOffset,
        void** pCpuVA, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, uint64_t currentVersion, uint32_t alignment)
    {
        // Scratch allocations need a command list, upload ones don't
        assert(!m_IsScratchBuffer || pCommandList);

        std::shared_ptr<BufferChunk> chunkToRetire;

        // Try to allocate from the current chunk first
        if (m_CurrentChunk != nullptr)
        {
            uint64_t alignedOffset = align(m_CurrentChunk->writePointer, (uint64_t)alignment);
            uint64_t endOfDataInChunk = alignedOffset + size;

            if (endOfDataInChunk <= m_CurrentChunk->bufferSize)
            {
                // The buffer can fit into the current chunk - great, we're done
                m_CurrentChunk->writePointer = endOfDataInChunk;

                if (pBuffer) *pBuffer = m_CurrentChunk->buffer;
                if (pOffset) *pOffset = alignedOffset;
                if (pCpuVA && m_CurrentChunk->cpuVA)
                    *pCpuVA = (char*)m_CurrentChunk->cpuVA + alignedOffset;
                if (pGpuVA && m_CurrentChunk->gpuVA)
                    *pGpuVA = m_CurrentChunk->gpuVA + alignedOffset;

                return true;
            }

            chunkToRetire = m_CurrentChunk;
            m_CurrentChunk.reset();
        }

        uint64_t completedInstance = m_Queue->lastCompletedInstance;

        // Try to find a chunk in the pool that's no longer used and is large enough to allocate our buffer
        for (auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); ++it)
        {
            std::shared_ptr<BufferChunk> chunk = *it;

            if (VersionGetSubmitted(chunk->version)
                && VersionGetInstance(chunk->version) <= completedInstance)
            {
                chunk->version = 0;
            }

            if (chunk->version == 0 && chunk->bufferSize >= size)
            {
                m_ChunkPool.erase(it);
                m_CurrentChunk = chunk;
                break;
            }
        }

        if (chunkToRetire)
        {
            m_ChunkPool.push_back(chunkToRetire);
        }

        if (!m_CurrentChunk)
        {
            uint64_t sizeToAllocate = align(std::max(size, m_DefaultChunkSize), BufferChunk::c_sizeAlignment);

            // See if we're allowed to allocate more memory
            if ((m_MemoryLimit > 0) && (m_AllocatedMemory + sizeToAllocate > m_MemoryLimit))
            {
                if (m_IsScratchBuffer)
                {
                    // Nope, need to reuse something.
                    // Find the largest least recently used chunk that can fit our buffer.

                    std::shared_ptr<BufferChunk> bestChunk;
                    for (const auto& candidateChunk : m_ChunkPool)
                    {
                        if (candidateChunk->bufferSize >= sizeToAllocate)
                        {
                            // Pick the first fitting chunk if we have nothing so far
                            if (!bestChunk)
                            {
                                bestChunk = candidateChunk;
                                continue;
                            }

                            bool candidateSubmitted = VersionGetSubmitted(candidateChunk->version);
                            bool bestSubmitted = VersionGetSubmitted(bestChunk->version);
                            uint64_t candidateInstance = VersionGetInstance(candidateChunk->version);
                            uint64_t bestInstance = VersionGetInstance(bestChunk->version);

                            // Compare chunks: submitted is better than current, old is better than new, large is better than small
                            if ((candidateSubmitted && !bestSubmitted) ||
                                (candidateSubmitted == bestSubmitted && candidateInstance < bestInstance) ||
                                (candidateSubmitted == bestSubmitted && candidateInstance == bestInstance
                                    && candidateChunk->bufferSize > bestChunk->bufferSize))
                            {
                                bestChunk = candidateChunk;
                            }
                        }
                    }

                    if (!bestChunk)
                    {
                        // No chunk found that can be reused. And we can't allocate. :(
                        return false;
                    }

                    // Move the found chunk from the pool to the current chunk
                    m_ChunkPool.erase(std::find(m_ChunkPool.begin(), m_ChunkPool.end(), bestChunk));
                    m_CurrentChunk = bestChunk;

                    // Place a UAV barrier on the chunk.
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.UAV.pResource = bestChunk->buffer;
                    pCommandList->ResourceBarrier(1, &barrier);
                }
                else // !m_IsScratchBuffer
                {
                    // Can't reuse in-flight buffers for uploads.
                    // But uploads have no memory limit, so this should never execute.
                    return false;
                }
            }
            else
            {
                m_CurrentChunk = createChunk(sizeToAllocate);
            }
        }

        m_CurrentChunk->version = currentVersion;
        m_CurrentChunk->writePointer = size;

        if (pBuffer) *pBuffer = m_CurrentChunk->buffer;
        if (pOffset) *pOffset = 0;
        if (pCpuVA) *pCpuVA = m_CurrentChunk->cpuVA;
        if (pGpuVA) *pGpuVA = m_CurrentChunk->gpuVA;

        return true;
    }

    void UploadManager::submitChunks(uint64_t currentVersion, uint64_t submittedVersion)
    {
        if (m_CurrentChunk)
        {
            m_ChunkPool.push_back(m_CurrentChunk);
            m_CurrentChunk.reset();
        }

        for (const auto& chunk : m_ChunkPool)
        {
            if (chunk->version == currentVersion)
                chunk->version = submittedVersion;
        }
    }
} // namespace nvrhi::d3d12
