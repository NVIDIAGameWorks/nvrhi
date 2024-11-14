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
#include <sstream>

#include <nvrhi/common/misc.h>

namespace nvrhi::vulkan
{

    BufferHandle Device::createBuffer(const BufferDesc& desc)
    {
        // Check some basic constraints first - the validation layer is expected to handle them too

        if (desc.isVolatile && desc.maxVersions == 0)
            return nullptr;

        if (desc.isVolatile && !desc.isConstantBuffer)
            return nullptr;

        if (desc.byteSize == 0)
            return nullptr;


        Buffer *buffer = new Buffer(m_Context, m_Allocator);
        buffer->desc = desc;

        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferSrc |
                                          vk::BufferUsageFlagBits::eTransferDst;

        if (desc.isVertexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
        
        if (desc.isIndexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
        
        if (desc.isDrawIndirectArgs)
            usageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
        
        if (desc.isConstantBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;

        if (desc.structStride != 0 || desc.canHaveUAVs || desc.canHaveRawViews)
            usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
        
        if (desc.canHaveTypedViews)
            usageFlags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;

        if (desc.canHaveTypedViews && desc.canHaveUAVs)
            usageFlags |= vk::BufferUsageFlagBits::eStorageTexelBuffer;

        if (desc.isAccelStructBuildInput)
            usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;

        if (desc.isAccelStructStorage)
            usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;

        if (desc.isShaderBindingTable)
            usageFlags |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;

        if (m_Context.extensions.buffer_device_address)
            usageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;

        uint64_t size = desc.byteSize;

        if (desc.isVolatile)
        {
            assert(!desc.isVirtual);

            uint64_t alignment = m_Context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;

            uint64_t atomSize = m_Context.physicalDeviceProperties.limits.nonCoherentAtomSize;
            alignment = std::max(alignment, atomSize);

            assert((alignment & (alignment - 1)) == 0); // check if it's a power of 2
            
            size = (size + alignment - 1) & ~(alignment - 1);
            buffer->desc.byteSize = size;

            size *= desc.maxVersions;

            buffer->versionTracking.resize(desc.maxVersions);
            std::fill(buffer->versionTracking.begin(), buffer->versionTracking.end(), 0);

            buffer->desc.cpuAccess = CpuAccessMode::Write; // to get the right memory type allocated
        }
        else if (desc.byteSize < 65536)
        {
            // vulkan allows for <= 64kb buffer updates to be done inline via vkCmdUpdateBuffer,
            // but the data size must always be a multiple of 4
            // enlarge the buffer slightly to allow for this
            size = (size + 3) & ~3ull;
        }

        auto bufferInfo = vk::BufferCreateInfo()
            .setSize(size)
            .setUsage(usageFlags)
            .setSharingMode(vk::SharingMode::eExclusive);

#if _WIN32
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        const auto handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
        vk::ExternalMemoryBufferCreateInfo externalBuffer{ handleType };
        if (desc.sharedResourceFlags == SharedResourceFlags::Shared)
            bufferInfo.setPNext(&externalBuffer);

        vk::Result res = m_Context.device.createBuffer(&bufferInfo, m_Context.allocationCallbacks, &buffer->buffer);
        CHECK_VK_FAIL(res);

        m_Context.nameVKObject(VkBuffer(buffer->buffer), vk::ObjectType::eBuffer, vk::DebugReportObjectTypeEXT::eBuffer, desc.debugName.c_str());

        if (!desc.isVirtual)
        {
            res = m_Allocator.allocateBufferMemory(buffer, (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags(0));
            CHECK_VK_FAIL(res)

            m_Context.nameVKObject(buffer->memory, vk::ObjectType::eDeviceMemory, vk::DebugReportObjectTypeEXT::eDeviceMemory, desc.debugName.c_str());

            if (desc.isVolatile)
            {
                buffer->mappedMemory = m_Context.device.mapMemory(buffer->memory, 0, size);
                assert(buffer->mappedMemory);
            }

            if (m_Context.extensions.buffer_device_address)
            {
                auto addressInfo = vk::BufferDeviceAddressInfo().setBuffer(buffer->buffer);

                buffer->deviceAddress = m_Context.device.getBufferAddress(addressInfo);
            }

            if (desc.sharedResourceFlags == SharedResourceFlags::Shared)
            {
#ifdef _WIN32
                buffer->sharedHandle = m_Context.device.getMemoryWin32HandleKHR({ buffer->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 });
#else
                buffer->sharedHandle = (void*)(size_t)m_Context.device.getMemoryFdKHR({ buffer->memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd });
#endif
            }
        }

        return BufferHandle::Create(buffer);
    }

    BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc)
    {
        if (!_buffer.pointer)
            return nullptr;

        if (objectType != ObjectTypes::VK_Buffer)
            return nullptr;
        
        Buffer* buffer = new Buffer(m_Context, m_Allocator);
        buffer->buffer = VkBuffer(_buffer.integer);
        buffer->desc = desc;
        buffer->managed = false;

        return BufferHandle::Create(buffer);
    }

    void CommandList::copyBuffer(IBuffer* _dest, uint64_t destOffsetBytes,
                                             IBuffer* _src, uint64_t srcOffsetBytes,
                                             uint64_t dataSizeBytes)
    {
        Buffer* dest = checked_cast<Buffer*>(_dest);
        Buffer* src = checked_cast<Buffer*>(_src);

        assert(destOffsetBytes + dataSizeBytes <= dest->desc.byteSize);
        assert(srcOffsetBytes + dataSizeBytes <= src->desc.byteSize);

        assert(m_CurrentCmdBuf);

        if (dest->desc.cpuAccess != CpuAccessMode::None)
            m_CurrentCmdBuf->referencedStagingBuffers.push_back(dest);
        else
            m_CurrentCmdBuf->referencedResources.push_back(dest);

        if (src->desc.cpuAccess != CpuAccessMode::None)
            m_CurrentCmdBuf->referencedStagingBuffers.push_back(src);
        else
            m_CurrentCmdBuf->referencedResources.push_back(src);

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(src, ResourceStates::CopySource);
            requireBufferState(dest, ResourceStates::CopyDest);
        }
        commitBarriers();

        auto copyRegion = vk::BufferCopy()
            .setSize(dataSizeBytes)
            .setSrcOffset(srcOffsetBytes)
            .setDstOffset(destOffsetBytes);

        m_CurrentCmdBuf->cmdBuf.copyBuffer(src->buffer, dest->buffer, { copyRegion });
    }

    static uint64_t getQueueLastFinishedID(Device* device, CommandQueue queueIndex)
    {
        Queue* queue = device->getQueue(queueIndex);
        if (queue)
            return queue->getLastFinishedID();
        return 0;
    }

    void CommandList::writeVolatileBuffer(Buffer* buffer, const void* data, size_t dataSize)
    {
        VolatileBufferState& state = m_VolatileBufferStates[buffer];

        if (!state.initialized)
        {
            state.minVersion = int(buffer->desc.maxVersions);
            state.maxVersion = -1;
            state.initialized = true;
        }
        
        std::array<uint64_t, uint32_t(CommandQueue::Count)> queueCompletionValues = {
            getQueueLastFinishedID(m_Device, CommandQueue::Graphics),
            getQueueLastFinishedID(m_Device, CommandQueue::Compute),
            getQueueLastFinishedID(m_Device, CommandQueue::Copy)
        };

        uint32_t searchStart = buffer->versionSearchStart;
        uint32_t maxVersions = buffer->desc.maxVersions;
        uint32_t version = 0;

        uint64_t originalVersionInfo = 0;

        // Since versionTracking[] can be accessed by multiple threads concurrently,
        // perform the search in a loop ending with compare_exchange until the exchange is successful.
        while (true)
        {
            bool found = false;

            // Search through the versions of this buffer, looking for either unused (0)
            // or submitted and already finished versions

            for (uint32_t searchIndex = 0; searchIndex < maxVersions; searchIndex++)
            {
                version = searchIndex + searchStart;
                version = (version >= maxVersions) ? (version - maxVersions) : version;

                originalVersionInfo = buffer->versionTracking[version];

                if (originalVersionInfo == 0)
                {
                    // Previously unused version - definitely available
                    found = true;
                    break;
                }

                // Decode the bitfield
                bool isSubmitted = (originalVersionInfo & c_VersionSubmittedFlag) != 0;
                uint32_t queueIndex = uint32_t(originalVersionInfo >> c_VersionQueueShift) & c_VersionQueueMask;
                uint64_t id = originalVersionInfo & c_VersionIDMask;

                // If the version is in a recorded but not submitted command list,
                // we can't use it. So, only compare the version ID for submitted CLs.
                if (isSubmitted)
                {
                    // Versions can potentially be used in CLs submitted to different queues.
                    // So we store the queue index and use look at the last finished CL in that queue.

                    if (queueIndex >= uint32_t(CommandQueue::Count))
                    {
                        // If the version points at an invalid queue, assume it's available. Signal the error too.
                        utils::InvalidEnum();
                        found = true;
                        break;
                    }

                    if (id <= queueCompletionValues[queueIndex])
                    {
                        // If the version was used in a completed CL, it's available.
                        found = true;
                        break;
                    }
                }
            }

            if (!found)
            {
                // Not enough versions - need to relay this information to the developer.
                // This has to be a real message and not assert, because asserts only happen in the
                // debug mode, and buffer versioning will behave differently in debug vs. release,
                // or validation on vs. off, because it is timing related.

                std::stringstream ss;
                ss << "Volatile constant buffer " << utils::DebugNameToString(buffer->desc.debugName) <<
                    " has maxVersions = " << buffer->desc.maxVersions << ", which is insufficient.";

                m_Context.error(ss.str());
                return;
            }

            // Encode the current CL ID for this version of the buffer, in a "pending" state
            uint64_t newVersionInfo = (uint64_t(m_CommandListParameters.queueType) << c_VersionQueueShift) | (m_CurrentCmdBuf->recordingID);

            // Try to store the new version info, end the loop if we actually won this version, i.e. no other thread has claimed it
            if (buffer->versionTracking[version].compare_exchange_weak(originalVersionInfo, newVersionInfo))
                break;
        }

        buffer->versionSearchStart = (version + 1 < maxVersions) ? (version + 1) : 0;

        // Store the current version and expand the version range in this CL
        state.latestVersion = int(version);
        state.minVersion = std::min(int(version), state.minVersion);
        state.maxVersion = std::max(int(version), state.maxVersion);

        // Finally, write the actual data
        void* hostData = (char*)buffer->mappedMemory + version * buffer->desc.byteSize;
        memcpy(hostData, data, dataSize);

        m_AnyVolatileBufferWrites = true;
    }

    void CommandList::flushVolatileBufferWrites()
    {
        // The volatile CBs are permanently mapped with the eHostVisible flag, but not eHostCoherent,
        // so before using the data on the GPU, we need to make sure it's available there.
        // Go over all the volatile CBs that were used in this CL and flush their written versions.

        std::vector<vk::MappedMemoryRange> ranges;

        for (auto& iter : m_VolatileBufferStates)
        {
            Buffer* buffer = iter.first;
            VolatileBufferState& state = iter.second;

            if (state.maxVersion < state.minVersion || !state.initialized)
                continue;

            // Flush all the versions between min and max - that might be too conservative,
            // but that should be fine - better than using potentially hundreds of ranges.
            int numVersions = state.maxVersion - state.minVersion + 1;

            auto range = vk::MappedMemoryRange()
                .setMemory(buffer->memory)
                .setOffset(state.minVersion * buffer->desc.byteSize)
                .setSize(numVersions * buffer->desc.byteSize);

            ranges.push_back(range);
        }

        if (!ranges.empty())
        {
            m_Context.device.flushMappedMemoryRanges(ranges);
        }
    }

    void CommandList::submitVolatileBuffers(uint64_t recordingID, uint64_t submittedID)
    {
        // For each volatile CB that was written in this command list, and for every version thereof,
        // we need to replace the tracking information from "pending" to "submitted".
        // This is potentially slow as there might be hundreds of versions of a buffer,
        // but at least the find-and-replace operation is constrained to the min/max version range.

        uint64_t stateToFind = (uint64_t(m_CommandListParameters.queueType) << c_VersionQueueShift) | (recordingID & c_VersionIDMask);
        uint64_t stateToReplace = (uint64_t(m_CommandListParameters.queueType) << c_VersionQueueShift) | (submittedID & c_VersionIDMask) | c_VersionSubmittedFlag;

        for (auto& iter : m_VolatileBufferStates)
        {
            Buffer* buffer = iter.first;
            VolatileBufferState& state = iter.second;

            if (!state.initialized)
                continue;

            for (int version = state.minVersion; version <= state.maxVersion; version++)
            {
                // Use compare_exchange to conditionally replace the entries equal to stateToFind with stateToReplace.
                uint64_t expected = stateToFind;
                buffer->versionTracking[version].compare_exchange_strong(expected, stateToReplace);
            }
        }
    }

    void CommandList::writeBuffer(IBuffer* _buffer, const void *data, size_t dataSize, uint64_t destOffsetBytes)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        assert(dataSize <= buffer->desc.byteSize);

        assert(m_CurrentCmdBuf);

        endRenderPass();

        m_CurrentCmdBuf->referencedResources.push_back(buffer);

        if (buffer->desc.isVolatile)
        {
            assert(destOffsetBytes == 0);

            writeVolatileBuffer(buffer, data, dataSize);
            
            return;
        }

        const size_t vkCmdUpdateBufferLimit = 65536;

        // Per Vulkan spec, vkCmdUpdateBuffer requires that the data size is smaller than or equal to 64 kB,
        // and that the offset and data size are a multiple of 4. We can't change the offset, but data size
        // is rounded up later.
        if (dataSize <= vkCmdUpdateBufferLimit && (destOffsetBytes & 3) == 0)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireBufferState(buffer, ResourceStates::CopyDest);
            }
            commitBarriers();

            // Round up the write size to a multiple of 4
            const size_t sizeToWrite = (dataSize + 3) & ~3ull;

            m_CurrentCmdBuf->cmdBuf.updateBuffer(buffer->buffer, destOffsetBytes, sizeToWrite, data);
        }
        else
        {
            if (buffer->desc.cpuAccess != CpuAccessMode::Write)
            {
                // use the upload manager
                Buffer* uploadBuffer;
                uint64_t uploadOffset;
                void* uploadCpuVA;
                m_UploadManager->suballocateBuffer(dataSize, &uploadBuffer, &uploadOffset, &uploadCpuVA, MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false));

                memcpy(uploadCpuVA, data, dataSize);

                copyBuffer(buffer, destOffsetBytes, uploadBuffer, uploadOffset, dataSize);
            }
            else
            {
                m_Context.error("Using writeBuffer on mappable buffers is invalid");
            }
        }
    }

    void CommandList::clearBufferUInt(IBuffer* b, uint32_t clearValue)
    {
        Buffer* vkbuf = checked_cast<Buffer*>(b);

        assert(m_CurrentCmdBuf);

        endRenderPass();

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(vkbuf, ResourceStates::CopyDest);
        }
        commitBarriers();

        m_CurrentCmdBuf->cmdBuf.fillBuffer(vkbuf->buffer, 0, vkbuf->desc.byteSize, clearValue);
        m_CurrentCmdBuf->referencedResources.push_back(b);
    }

    Buffer::~Buffer()
    {
        if (mappedMemory)
        {
            m_Context.device.unmapMemory(memory);
            mappedMemory = nullptr;
        }

        for (auto&& iter : viewCache)
        {
            m_Context.device.destroyBufferView(iter.second, m_Context.allocationCallbacks);
        }

        viewCache.clear();

        if (managed)
        {
            assert(buffer != vk::Buffer());

            m_Context.device.destroyBuffer(buffer, m_Context.allocationCallbacks);
            buffer = vk::Buffer();

            if (memory)
            {
                m_Allocator.freeBufferMemory(this);
                memory = vk::DeviceMemory();
            }
        }
    }

    Object Buffer::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Buffer:
            return Object(buffer);
        case ObjectTypes::VK_DeviceMemory:
            return Object(memory);
        case ObjectTypes::SharedHandle:
            return Object(sharedHandle);
        default:
            return nullptr;
        }
    }

    void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags, uint64_t offset, size_t size) const
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        assert(flags != CpuAccessMode::None);

        // If the buffer has been used in a command list before, wait for that CL to complete
        if (buffer->lastUseCommandListID != 0)
        {
            auto& queue = m_Queues[uint32_t(buffer->lastUseQueue)];
            queue->waitCommandList(buffer->lastUseCommandListID, ~0ull);
        }

        vk::AccessFlags accessFlags;

        switch(flags)
        {
            case CpuAccessMode::Read:
                accessFlags = vk::AccessFlagBits::eHostRead;
                break;

            case CpuAccessMode::Write:
                accessFlags = vk::AccessFlagBits::eHostWrite;
                break;
                
            case CpuAccessMode::None:
            default:
                utils::InvalidEnum();
                break;
        }

        // TODO: there should be a barrier... But there can't be a command list here
        // buffer->barrier(cmd, vk::PipelineStageFlagBits::eHost, accessFlags);

        void* ptr = nullptr;
        [[maybe_unused]] const vk::Result res = m_Context.device.mapMemory(buffer->memory, offset, size, vk::MemoryMapFlags(), &ptr);
        assert(res == vk::Result::eSuccess);

        return ptr;
    }

    void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        return mapBuffer(buffer, flags, 0, buffer->desc.byteSize);
    }

    void Device::unmapBuffer(IBuffer* _buffer)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_Context.device.unmapMemory(buffer->memory);

        // TODO: there should be a barrier
        // buffer->barrier(cmd, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
    }

    MemoryRequirements Device::getBufferMemoryRequirements(IBuffer* _buffer)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        vk::MemoryRequirements vulkanMemReq;
        m_Context.device.getBufferMemoryRequirements(buffer->buffer, &vulkanMemReq);

        MemoryRequirements memReq;
        memReq.alignment = vulkanMemReq.alignment;
        memReq.size = vulkanMemReq.size;
        return memReq;
    }

    bool Device::bindBufferMemory(IBuffer* _buffer, IHeap* _heap, uint64_t offset)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);
        Heap* heap = checked_cast<Heap*>(_heap);

        if (buffer->heap)
            return false;

        if (!buffer->desc.isVirtual)
            return false;
        
        m_Context.device.bindBufferMemory(buffer->buffer, heap->memory, offset);

        buffer->heap = heap;

        if (m_Context.extensions.buffer_device_address)
        {
            auto addressInfo = vk::BufferDeviceAddressInfo().setBuffer(buffer->buffer);

            buffer->deviceAddress = m_Context.device.getBufferAddress(addressInfo);
        }

        return true;
    }

} // namespace nvrhi::vulkan
