/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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
#include <sstream>

namespace nvrhi::vulkan
{
    static vk::DeviceOrHostAddressConstKHR getBufferAddress(IBuffer* _buffer, uint64_t offset)
    {
        if (!_buffer)
            return vk::DeviceOrHostAddressConstKHR();

        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        return vk::DeviceOrHostAddressConstKHR().setDeviceAddress(buffer->deviceAddress + size_t(offset));
    }

    static vk::DeviceOrHostAddressKHR getMutableBufferAddress(IBuffer* _buffer, uint64_t offset)
    {
        if (!_buffer)
            return vk::DeviceOrHostAddressKHR();

        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        return vk::DeviceOrHostAddressKHR().setDeviceAddress(buffer->deviceAddress + size_t(offset));
    }

    static vk::BuildMicromapFlagBitsEXT GetAsVkBuildMicromapFlagBitsEXT(rt::OpacityMicromapBuildFlags flags)
    {
        assert((flags & (rt::OpacityMicromapBuildFlags::FastBuild | rt::OpacityMicromapBuildFlags::FastTrace)) == flags);
        static_assert((uint32_t)vk::BuildMicromapFlagBitsEXT::ePreferFastTrace == (uint32_t)rt::OpacityMicromapBuildFlags::FastTrace);
        static_assert((uint32_t)vk::BuildMicromapFlagBitsEXT::ePreferFastBuild == (uint32_t)rt::OpacityMicromapBuildFlags::FastBuild);
        return (vk::BuildMicromapFlagBitsEXT)flags;
    }

    static const vk::MicromapUsageEXT* GetAsVkOpacityMicromapUsageCounts(const rt::OpacityMicromapUsageCount* counts) 
    {
        static_assert(sizeof(rt::OpacityMicromapUsageCount) == sizeof(vk::MicromapUsageEXT));
        static_assert(offsetof(rt::OpacityMicromapUsageCount, count) == offsetof(vk::MicromapUsageEXT, count));
        static_assert(sizeof(rt::OpacityMicromapUsageCount::count) == sizeof(vk::MicromapUsageEXT::count));
        static_assert(offsetof(rt::OpacityMicromapUsageCount, subdivisionLevel) == offsetof(vk::MicromapUsageEXT, subdivisionLevel));
        static_assert(sizeof(rt::OpacityMicromapUsageCount::subdivisionLevel) == sizeof(vk::MicromapUsageEXT::subdivisionLevel));
        static_assert(offsetof(rt::OpacityMicromapUsageCount, format) == offsetof(vk::MicromapUsageEXT, format));
        static_assert(sizeof(rt::OpacityMicromapUsageCount::format) == sizeof(vk::MicromapUsageEXT::format));
        return (vk::MicromapUsageEXT*)counts;
    }

    static void convertBottomLevelGeometry(const rt::GeometryDesc& src, vk::AccelerationStructureGeometryKHR& dst, vk::AccelerationStructureTrianglesOpacityMicromapEXT& dstOmm,
        uint32_t& maxPrimitiveCount, vk::AccelerationStructureBuildRangeInfoKHR* pRange, const VulkanContext& context)
    {
        switch (src.geometryType)
        {
        case rt::GeometryType::Triangles: {
            const rt::GeometryTriangles& srct = src.geometryData.triangles;
            vk::AccelerationStructureGeometryTrianglesDataKHR dstt;

            switch (srct.indexFormat)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case Format::R8_UINT:
                dstt.setIndexType(vk::IndexType::eUint8EXT);
                break;

            case Format::R16_UINT:
                dstt.setIndexType(vk::IndexType::eUint16);
                break;

            case Format::R32_UINT:
                dstt.setIndexType(vk::IndexType::eUint32);
                break;

            case Format::UNKNOWN:
                dstt.setIndexType(vk::IndexType::eNoneKHR);
                break;

            default:
                context.error("Unsupported ray tracing geometry index type");
                dstt.setIndexType(vk::IndexType::eNoneKHR);
                break;
            }

            dstt.setVertexFormat(vk::Format(convertFormat(srct.vertexFormat)));
            dstt.setVertexData(getBufferAddress(srct.vertexBuffer, srct.vertexOffset));
            dstt.setVertexStride(srct.vertexStride);
            dstt.setMaxVertex(std::max(srct.vertexCount, 1u) - 1u);
            dstt.setIndexData(getBufferAddress(srct.indexBuffer, srct.indexOffset));

            if (src.useTransform)
            {
                dstt.setTransformData(vk::DeviceOrHostAddressConstKHR().setHostAddress(&src.transform));
            }

            if (srct.opacityMicromap)
            {
                OpacityMicromap* om = checked_cast<OpacityMicromap*>(srct.opacityMicromap);

                dstOmm
                    .setIndexType(srct.ommIndexFormat == Format::R16_UINT ? vk::IndexType::eUint16 : vk::IndexType::eUint32)
                    .setIndexBuffer(getMutableBufferAddress(srct.ommIndexBuffer, srct.ommIndexBufferOffset).deviceAddress)
                    .setIndexStride(srct.ommIndexFormat == Format::R16_UINT ? 2 : 4)
                    .setBaseTriangle(0)
                    .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(srct.pOmmUsageCounts))
                    .setUsageCountsCount(srct.numOmmUsageCounts)
                    .setMicromap(om->opacityMicromap.get());

                dstt.setPNext(&dstOmm);
            }

            maxPrimitiveCount = (srct.indexFormat == Format::UNKNOWN)
                ? (srct.vertexCount / 3)
                : (srct.indexCount / 3);

            dst.setGeometryType(vk::GeometryTypeKHR::eTriangles);
            dst.geometry.setTriangles(dstt);

            break;
        }
        case rt::GeometryType::AABBs: {
            const rt::GeometryAABBs& srca = src.geometryData.aabbs;
            vk::AccelerationStructureGeometryAabbsDataKHR dsta;

            dsta.setData(getBufferAddress(srca.buffer, srca.offset));
            dsta.setStride(srca.stride);

            maxPrimitiveCount = srca.count;

            dst.setGeometryType(vk::GeometryTypeKHR::eAabbs);
            dst.geometry.setAabbs(dsta);

            break;
        }
        }

        if (pRange)
        {
            pRange->setPrimitiveCount(maxPrimitiveCount);
        }

        vk::GeometryFlagsKHR geometryFlags = vk::GeometryFlagBitsKHR(0);
        if ((src.flags & rt::GeometryFlags::Opaque) != 0)
            geometryFlags |= vk::GeometryFlagBitsKHR::eOpaque;
        if ((src.flags & rt::GeometryFlags::NoDuplicateAnyHitInvocation) != 0)
            geometryFlags |= vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
        dst.setFlags(geometryFlags);
    }

    rt::OpacityMicromapHandle Device::createOpacityMicromap(const rt::OpacityMicromapDesc& desc)
    {
        auto buildSize = vk::MicromapBuildSizesInfoEXT();

        auto buildInfo = vk::MicromapBuildInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setFlags(GetAsVkBuildMicromapFlagBitsEXT(desc.flags))
            .setMode(vk::BuildMicromapModeEXT::eBuild)
            .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(desc.counts.data()))
            .setUsageCountsCount((uint32_t)desc.counts.size())
            ;

        m_Context.device.getMicromapBuildSizesEXT(vk::AccelerationStructureBuildTypeKHR::eDevice, &buildInfo, &buildSize);

        OpacityMicromap* om = new OpacityMicromap();
        om->desc = desc;
        om->compacted = false;
        
        BufferDesc bufferDesc;
        bufferDesc.canHaveUAVs = true;
        bufferDesc.byteSize = buildSize.micromapSize;
        bufferDesc.initialState = ResourceStates::AccelStructBuildBlas;
        bufferDesc.keepInitialState = true;
        bufferDesc.isAccelStructStorage = true;
        bufferDesc.debugName = desc.debugName;
        bufferDesc.isVirtual = false;
        om->dataBuffer = createBuffer(bufferDesc);

        Buffer* buffer = checked_cast<Buffer*>(om->dataBuffer.Get());

        auto create = vk::MicromapCreateInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setBuffer(buffer->buffer)
            .setSize(buildSize.micromapSize)
            .setDeviceAddress(getMutableBufferAddress(buffer, 0).deviceAddress);

        om->opacityMicromap = m_Context.device.createMicromapEXTUnique(create, m_Context.allocationCallbacks);
        return rt::OpacityMicromapHandle::Create(om);
    }

    rt::AccelStructHandle Device::createAccelStruct(const rt::AccelStructDesc& desc)
    {
        AccelStruct* as = new AccelStruct(m_Context);
        as->desc = desc;
        as->allowUpdate = (desc.buildFlags & rt::AccelStructBuildFlags::AllowUpdate) != 0;

#ifdef NVRHI_WITH_RTXMU
        bool isManaged = desc.isTopLevel;
#else
        bool isManaged = true;
#endif

        if (isManaged)
        {
            std::vector<vk::AccelerationStructureGeometryKHR> geometries;
            std::vector<vk::AccelerationStructureTrianglesOpacityMicromapEXT> omms;
            std::vector<uint32_t> maxPrimitiveCounts;

            auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR();

            if (desc.isTopLevel)
            {
                geometries.push_back(vk::AccelerationStructureGeometryKHR()
                    .setGeometryType(vk::GeometryTypeKHR::eInstances));

                geometries[0].geometry.setInstances(vk::AccelerationStructureGeometryInstancesDataKHR());

                maxPrimitiveCounts.push_back(uint32_t(desc.topLevelMaxInstances));

                buildInfo.setType(vk::AccelerationStructureTypeKHR::eTopLevel);
            }
            else
            {
                geometries.resize(desc.bottomLevelGeometries.size());
                omms.resize(desc.bottomLevelGeometries.size());
                maxPrimitiveCounts.resize(desc.bottomLevelGeometries.size());

                for (size_t i = 0; i < desc.bottomLevelGeometries.size(); i++)
                {
                    convertBottomLevelGeometry(desc.bottomLevelGeometries[i],  geometries[i], omms[i], maxPrimitiveCounts[i], nullptr, m_Context);
                }

                buildInfo.setType(vk::AccelerationStructureTypeKHR::eBottomLevel);
            }

            buildInfo.setMode(vk::BuildAccelerationStructureModeKHR::eBuild)
                .setGeometries(geometries)
                .setFlags(convertAccelStructBuildFlags(desc.buildFlags));

            auto buildSizes = m_Context.device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

            BufferDesc bufferDesc;
            bufferDesc.byteSize = buildSizes.accelerationStructureSize;
            bufferDesc.debugName = desc.debugName;
            bufferDesc.initialState = desc.isTopLevel ? ResourceStates::AccelStructRead : ResourceStates::AccelStructBuildBlas;
            bufferDesc.keepInitialState = true;
            bufferDesc.isAccelStructStorage = true;
            bufferDesc.isVirtual = desc.isVirtual;
            as->dataBuffer = createBuffer(bufferDesc);

            Buffer* dataBuffer = checked_cast<Buffer*>(as->dataBuffer.Get());

            auto createInfo = vk::AccelerationStructureCreateInfoKHR()
                .setType(desc.isTopLevel ? vk::AccelerationStructureTypeKHR::eTopLevel : vk::AccelerationStructureTypeKHR::eBottomLevel)
                .setBuffer(dataBuffer->buffer)
                .setSize(buildSizes.accelerationStructureSize);

            as->accelStruct = m_Context.device.createAccelerationStructureKHR(createInfo, m_Context.allocationCallbacks);

            if (!desc.isVirtual)
            {
                auto addressInfo = vk::AccelerationStructureDeviceAddressInfoKHR()
                    .setAccelerationStructure(as->accelStruct);

                as->accelStructDeviceAddress = m_Context.device.getAccelerationStructureAddressKHR(addressInfo);
            }
        }

        // Sanitize the geometry data to avoid dangling pointers, we don't need these buffers in the Desc
        for (auto& geometry : as->desc.bottomLevelGeometries)
        {
            static_assert(offsetof(rt::GeometryTriangles, indexBuffer)
                == offsetof(rt::GeometryAABBs, buffer));
            static_assert(offsetof(rt::GeometryTriangles, vertexBuffer)
                == offsetof(rt::GeometryAABBs, unused));

            static_assert(offsetof(rt::GeometryTriangles, indexBuffer)
                == offsetof(rt::GeometrySpheres, indexBuffer));
            static_assert(offsetof(rt::GeometryTriangles, vertexBuffer)
                == offsetof(rt::GeometrySpheres, vertexBuffer));

            static_assert(offsetof(rt::GeometryTriangles, indexBuffer)
                == offsetof(rt::GeometryLss, indexBuffer));
            static_assert(offsetof(rt::GeometryTriangles, vertexBuffer)
                == offsetof(rt::GeometryLss, vertexBuffer));

            // Clear only the triangles' data, because the other types' data is aliased to triangles (verified above)
            geometry.geometryData.triangles.indexBuffer = nullptr;
            geometry.geometryData.triangles.vertexBuffer = nullptr;
        }

        return rt::AccelStructHandle::Create(as);
    }

    MemoryRequirements Device::getAccelStructMemoryRequirements(rt::IAccelStruct* _as)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        if (as->dataBuffer)
            return getBufferMemoryRequirements(as->dataBuffer);

        return MemoryRequirements();
    }

    rt::cluster::OperationSizeInfo Device::getClusterOperationSizeInfo(const rt::cluster::OperationParams&)
    {
        utils::NotSupported();
        return rt::cluster::OperationSizeInfo();
    }

    bool Device::bindAccelStructMemory(rt::IAccelStruct* _as, IHeap* heap, uint64_t offset)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        if (!as->dataBuffer)
            return false;

        const bool bound = bindBufferMemory(as->dataBuffer, heap, offset);

        if (bound)
        {
            auto addressInfo = vk::AccelerationStructureDeviceAddressInfoKHR()
                .setAccelerationStructure(as->accelStruct);

            as->accelStructDeviceAddress = m_Context.device.getAccelerationStructureAddressKHR(addressInfo);
        }

        return bound;
    }

    void CommandList::buildOpacityMicromap(rt::IOpacityMicromap* pOpacityMicromap, const rt::OpacityMicromapDesc& desc)
    {
        OpacityMicromap* omm = checked_cast<OpacityMicromap*>(pOpacityMicromap);

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(desc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
            requireBufferState(desc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);

            requireBufferState(omm->dataBuffer, nvrhi::ResourceStates::OpacityMicromapWrite);
        }

        if (desc.trackLiveness)
        {
            m_CurrentCmdBuf->referencedResources.push_back(desc.inputBuffer);
            m_CurrentCmdBuf->referencedResources.push_back(desc.perOmmDescs);
            m_CurrentCmdBuf->referencedResources.push_back(omm->dataBuffer);
        }

        commitBarriers();

        auto buildInfo = vk::MicromapBuildInfoEXT()
            .setType(vk::MicromapTypeEXT::eOpacityMicromap)
            .setFlags(GetAsVkBuildMicromapFlagBitsEXT(desc.flags))
            .setMode(vk::BuildMicromapModeEXT::eBuild)
            .setDstMicromap(omm->opacityMicromap.get())
            .setPUsageCounts(GetAsVkOpacityMicromapUsageCounts(desc.counts.data()))
            .setUsageCountsCount((uint32_t)desc.counts.size())
            .setData(getBufferAddress(desc.inputBuffer, desc.inputBufferOffset))
            .setTriangleArray(getBufferAddress(desc.perOmmDescs, desc.perOmmDescsOffset))
            .setTriangleArrayStride((VkDeviceSize)sizeof(vk::MicromapTriangleEXT))
            ;

        vk::MicromapBuildSizesInfoEXT buildSize;
        m_Context.device.getMicromapBuildSizesEXT(vk::AccelerationStructureBuildTypeKHR::eDevice, &buildInfo, &buildSize);

        if (buildSize.buildScratchSize != 0)
        {
            Buffer* scratchBuffer = nullptr;
            uint64_t scratchOffset = 0;
            uint64_t currentVersion = MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false);

            bool allocated = m_ScratchManager->suballocateBuffer(buildSize.buildScratchSize, &scratchBuffer, &scratchOffset, nullptr,
                currentVersion, m_Context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

            if (!allocated)
            {
                std::stringstream ss;
                ss << "Couldn't suballocate a scratch buffer for OMM " << utils::DebugNameToString(omm->desc.debugName) << " build. "
                    "The build requires " << buildSize.buildScratchSize << " bytes of scratch space.";

                m_Context.error(ss.str());
                return;
            }

            buildInfo.setScratchData(getMutableBufferAddress(scratchBuffer, scratchOffset));
        }

        m_CurrentCmdBuf->cmdBuf.buildMicromapsEXT(1, &buildInfo);
    }

    void CommandList::buildBottomLevelAccelStruct(rt::IAccelStruct* _as, const rt::GeometryDesc* pGeometries, size_t numGeometries, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        const bool performUpdate = (buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
        }

        std::vector<vk::AccelerationStructureGeometryKHR> geometries;
        std::vector<vk::AccelerationStructureTrianglesOpacityMicromapEXT> omms;
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> buildRanges;
        std::vector<uint32_t> maxPrimitiveCounts;
        geometries.resize(numGeometries);
        omms.resize(numGeometries);
        maxPrimitiveCounts.resize(numGeometries);
        buildRanges.resize(numGeometries);

        for (size_t i = 0; i < numGeometries; i++)
        {
            convertBottomLevelGeometry(pGeometries[i], geometries[i], omms[i], maxPrimitiveCounts[i], &buildRanges[i], m_Context);

            const rt::GeometryDesc& src = pGeometries[i];

            switch (src.geometryType)
            {
            case rt::GeometryType::Triangles: {
                const rt::GeometryTriangles& srct = src.geometryData.triangles;
                if (m_EnableAutomaticBarriers)
                {
                    if (srct.indexBuffer)
                        requireBufferState(srct.indexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
                    if (srct.vertexBuffer)
                        requireBufferState(srct.vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
                    if (OpacityMicromap* om = checked_cast<OpacityMicromap*>(srct.opacityMicromap))
                        requireBufferState(om->dataBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
                }
                break;
            }
            case rt::GeometryType::AABBs: {
                const rt::GeometryAABBs& srca = src.geometryData.aabbs;
                if (m_EnableAutomaticBarriers)
                {
                    if (srca.buffer)
                        requireBufferState(srca.buffer, nvrhi::ResourceStates::AccelStructBuildInput);
                }
                break;
            }
            }
        }

        auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eBottomLevel)
            .setMode(performUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries)
            .setFlags(convertAccelStructBuildFlags(buildFlags))
            .setDstAccelerationStructure(as->accelStruct);

        if (as->allowUpdate)
            buildInfo.flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        if (performUpdate)
            buildInfo.setSrcAccelerationStructure(as->accelStruct);
        
#ifdef NVRHI_WITH_RTXMU
        commitBarriers();

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };
        std::array<const uint32_t*, 1> maxPrimArrays = { maxPrimitiveCounts.data() };

        if(as->rtxmuId == ~0ull)
        {
            std::vector<uint64_t> accelStructsToBuild;
            m_Context.rtxMemUtil->PopulateBuildCommandList(m_CurrentCmdBuf->cmdBuf,
                                                           buildInfos.data(),
                                                           buildRangeArrays.data(),
                                                           maxPrimArrays.data(),
                                                           (uint32_t)buildInfos.size(),
                                                           accelStructsToBuild);


            as->rtxmuId = accelStructsToBuild[0];
            
            as->rtxmuBuffer = m_Context.rtxMemUtil->GetBuffer(as->rtxmuId);
            as->accelStruct = m_Context.rtxMemUtil->GetAccelerationStruct(as->rtxmuId);
            as->accelStructDeviceAddress = m_Context.rtxMemUtil->GetDeviceAddress(as->rtxmuId);

            m_CurrentCmdBuf->rtxmuBuildIds.push_back(as->rtxmuId);
        }
        else
        {
            std::vector<uint64_t> buildsToUpdate(1, as->rtxmuId);

            m_Context.rtxMemUtil->PopulateUpdateCommandList(m_CurrentCmdBuf->cmdBuf,
                                                            buildInfos.data(),
                                                            buildRangeArrays.data(),
                                                            maxPrimArrays.data(),
                                                            (uint32_t)buildInfos.size(),
                                                            buildsToUpdate);
        }
#else

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
        }
        commitBarriers();

        auto buildSizes = m_Context.device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

        if (buildSizes.accelerationStructureSize > as->dataBuffer->getDesc().byteSize)
        {
            std::stringstream ss;
            ss << "BLAS " << utils::DebugNameToString(as->desc.debugName) << " build requires at least "
                << buildSizes.accelerationStructureSize << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->getDesc().byteSize << " bytes";

            m_Context.error(ss.str());
            return;
        }

        size_t scratchSize = performUpdate
            ? buildSizes.updateScratchSize
            : buildSizes.buildScratchSize;

        Buffer* scratchBuffer = nullptr;
        uint64_t scratchOffset = 0;
        uint64_t currentVersion = MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false);

        bool allocated = m_ScratchManager->suballocateBuffer(scratchSize, &scratchBuffer, &scratchOffset, nullptr,
            currentVersion, m_Context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

        if (!allocated)
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for BLAS " << utils::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            m_Context.error(ss.str());
            return;
        }
        
        assert(scratchBuffer->deviceAddress);
        buildInfo.setScratchData(scratchBuffer->deviceAddress + scratchOffset);

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };

        m_CurrentCmdBuf->cmdBuf.buildAccelerationStructuresKHR(buildInfos, buildRangeArrays);
#endif
        if (as->desc.trackLiveness)
            m_CurrentCmdBuf->referencedResources.push_back(as);
    }

    void CommandList::compactBottomLevelAccelStructs()
    {
#ifdef NVRHI_WITH_RTXMU

        if (!m_Context.rtxMuResources->asBuildsCompleted.empty())
        {
            std::lock_guard lockGuard(m_Context.rtxMuResources->asListMutex);

            if (!m_Context.rtxMuResources->asBuildsCompleted.empty())
            {
                m_Context.rtxMemUtil->PopulateCompactionCommandList(m_CurrentCmdBuf->cmdBuf, m_Context.rtxMuResources->asBuildsCompleted);

                m_CurrentCmdBuf->rtxmuCompactionIds.insert(m_CurrentCmdBuf->rtxmuCompactionIds.end(), m_Context.rtxMuResources->asBuildsCompleted.begin(), m_Context.rtxMuResources->asBuildsCompleted.end());

                m_Context.rtxMuResources->asBuildsCompleted.clear();
            }
        }
#endif
    }

    void CommandList::buildTopLevelAccelStructInternal(AccelStruct* as, VkDeviceAddress instanceData, size_t numInstances, rt::AccelStructBuildFlags buildFlags, uint64_t currentVersion)
    {
        // Remove the internal flag
        buildFlags = buildFlags & ~rt::AccelStructBuildFlags::AllowEmptyInstances;

        const bool performUpdate = (buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
            assert(as->instances.size() == numInstances);
        }

        auto geometry = vk::AccelerationStructureGeometryKHR()
            .setGeometryType(vk::GeometryTypeKHR::eInstances);

        geometry.geometry.setInstances(vk::AccelerationStructureGeometryInstancesDataKHR()
            .setData(instanceData)
            .setArrayOfPointers(false));

        std::array<vk::AccelerationStructureGeometryKHR, 1> geometries = { geometry };
        std::array<vk::AccelerationStructureBuildRangeInfoKHR, 1> buildRanges = {
            vk::AccelerationStructureBuildRangeInfoKHR().setPrimitiveCount(uint32_t(numInstances)) };
        std::array<uint32_t, 1> maxPrimitiveCounts = { uint32_t(numInstances) };

        auto buildInfo = vk::AccelerationStructureBuildGeometryInfoKHR()
            .setType(vk::AccelerationStructureTypeKHR::eTopLevel)
            .setMode(performUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild)
            .setGeometries(geometries)
            .setFlags(convertAccelStructBuildFlags(buildFlags))
            .setDstAccelerationStructure(as->accelStruct);

        if (as->allowUpdate)
            buildInfo.flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        if (performUpdate)
            buildInfo.setSrcAccelerationStructure(as->accelStruct);

        auto buildSizes = m_Context.device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, maxPrimitiveCounts);

        if (buildSizes.accelerationStructureSize > as->dataBuffer->getDesc().byteSize)
        {
            std::stringstream ss;
            ss << "TLAS " << utils::DebugNameToString(as->desc.debugName) << " build requires at least "
                << buildSizes.accelerationStructureSize << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->getDesc().byteSize << " bytes";

            m_Context.error(ss.str());
            return;
        }

        size_t scratchSize = performUpdate
            ? buildSizes.updateScratchSize
            : buildSizes.buildScratchSize;

        Buffer* scratchBuffer = nullptr;
        uint64_t scratchOffset = 0;

        bool allocated = m_ScratchManager->suballocateBuffer(scratchSize, &scratchBuffer, &scratchOffset, nullptr,
            currentVersion, m_Context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment);

        if (!allocated)
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for TLAS " << utils::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            m_Context.error(ss.str());
            return;
        }
        
        assert(scratchBuffer->deviceAddress);
        buildInfo.setScratchData(scratchBuffer->deviceAddress + scratchOffset);

        std::array<vk::AccelerationStructureBuildGeometryInfoKHR, 1> buildInfos = { buildInfo };
        std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> buildRangeArrays = { buildRanges.data() };

        m_CurrentCmdBuf->cmdBuf.buildAccelerationStructuresKHR(buildInfos, buildRangeArrays);
    }

    void CommandList::buildTopLevelAccelStruct(rt::IAccelStruct* _as, const rt::InstanceDesc* pInstances, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        as->instances.resize(numInstances);

        for (size_t i = 0; i < numInstances; i++)
        {
            const rt::InstanceDesc& src = pInstances[i];
            vk::AccelerationStructureInstanceKHR& dst = as->instances[i];

            if (src.bottomLevelAS)
            {
                AccelStruct* blas = checked_cast<AccelStruct*>(src.bottomLevelAS);
#ifdef NVRHI_WITH_RTXMU
                blas->rtxmuBuffer = m_Context.rtxMemUtil->GetBuffer(blas->rtxmuId);
                blas->accelStruct = m_Context.rtxMemUtil->GetAccelerationStruct(blas->rtxmuId);
                blas->accelStructDeviceAddress = m_Context.rtxMemUtil->GetDeviceAddress(blas->rtxmuId);
                dst.setAccelerationStructureReference(blas->accelStructDeviceAddress);
#else
                dst.setAccelerationStructureReference(blas->accelStructDeviceAddress);

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(blas->dataBuffer, nvrhi::ResourceStates::AccelStructBuildBlas);
                }
#endif
            }
            else // !src.bottomLevelAS
            {
                dst.setAccelerationStructureReference(0);
            }

            dst.setInstanceCustomIndex(src.instanceID);
            dst.setInstanceShaderBindingTableRecordOffset(src.instanceContributionToHitGroupIndex);
            dst.setFlags(convertInstanceFlags(src.flags));
            dst.setMask(src.instanceMask);
            memcpy(dst.transform.matrix.data(), src.transform, sizeof(float) * 12);
        }

#ifdef NVRHI_WITH_RTXMU
        m_Context.rtxMemUtil->PopulateUAVBarriersCommandList(m_CurrentCmdBuf->cmdBuf, m_CurrentCmdBuf->rtxmuBuildIds);
#endif

        uint64_t currentVersion = MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false);

        Buffer* uploadBuffer = nullptr;
        uint64_t uploadOffset = 0;
        void* uploadCpuVA = nullptr;
        m_UploadManager->suballocateBuffer(as->instances.size() * sizeof(vk::AccelerationStructureInstanceKHR),
            &uploadBuffer, &uploadOffset, &uploadCpuVA, currentVersion);

        // Copy the instance data to GPU-visible memory.
        // The vk::AccelerationStructureInstanceKHR struct should be directly copyable, but ReSharper/clang thinks it's not,
        // so the inspection is disabled with a comment below.
        memcpy(uploadCpuVA, as->instances.data(), // NOLINT(bugprone-undefined-memory-manipulation)
            as->instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
        }
        commitBarriers();

        buildTopLevelAccelStructInternal(as, uploadBuffer->deviceAddress + uploadOffset, numInstances, buildFlags, currentVersion);

        if (as->desc.trackLiveness)
            m_CurrentCmdBuf->referencedResources.push_back(as);
    }

    void CommandList::buildTopLevelAccelStructFromBuffer(rt::IAccelStruct* _as, nvrhi::IBuffer* _instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);
        Buffer* instanceBuffer = checked_cast<Buffer*>(_instanceBuffer);

        as->instances.clear();

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
            requireBufferState(instanceBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        }
        commitBarriers();

        uint64_t currentVersion = MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false);
        
        buildTopLevelAccelStructInternal(as, instanceBuffer->deviceAddress + instanceBufferOffset, numInstances, buildFlags, currentVersion);

        if (as->desc.trackLiveness)
            m_CurrentCmdBuf->referencedResources.push_back(as);
    }

    void CommandList::executeMultiIndirectClusterOperation(const rt::cluster::OperationDesc&)
    {
        utils::NotSupported();
    }

    AccelStruct::~AccelStruct()
    {
#ifdef NVRHI_WITH_RTXMU
        bool isManaged = desc.isTopLevel;
        if (!isManaged && rtxmuId != ~0ull)
        {
            std::vector<uint64_t> delAccel = { rtxmuId };
            m_Context.rtxMemUtil->RemoveAccelerationStructures(delAccel);
            rtxmuId = ~0ull;
        }
#else
        bool isManaged = true;
#endif

        if (accelStruct && isManaged)
        {
            m_Context.device.destroyAccelerationStructureKHR(accelStruct, m_Context.allocationCallbacks);
            accelStruct = nullptr;
        }
    }

    Object AccelStruct::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Buffer:
        case ObjectTypes::VK_DeviceMemory:
            if (dataBuffer)
                return dataBuffer->getNativeObject(objectType);
            return nullptr;
        case ObjectTypes::VK_AccelerationStructureKHR:
            return Object(accelStruct);
        default:
            return nullptr;
        }
    }

    uint64_t AccelStruct::getDeviceAddress() const
    {
#ifdef NVRHI_WITH_RTXMU
        if (!desc.isTopLevel)
            return m_Context.rtxMemUtil->GetDeviceAddress(rtxmuId);
#endif
        return getBufferAddress(dataBuffer, 0).deviceAddress;
    }

    OpacityMicromap::~OpacityMicromap()
    {
    }

    Object OpacityMicromap::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Buffer:
        case ObjectTypes::VK_DeviceMemory:
            if (dataBuffer)
                return dataBuffer->getNativeObject(objectType);
            return nullptr;
        case ObjectTypes::VK_Micromap:
            return Object(opacityMicromap.get());
        default:
            return nullptr;
        }
    }

    uint64_t OpacityMicromap::getDeviceAddress() const
    {
        return getBufferAddress(dataBuffer, 0).deviceAddress;
    }

    void CommandList::setRayTracingState(const rt::State& state)
    {
        if (!state.shaderTable)
            return;

        ShaderTable* shaderTable = checked_cast<ShaderTable*>(state.shaderTable);
        RayTracingPipeline* pso = shaderTable->pipeline;

        if (shaderTable->rayGenerationShader < 0)
        {
            m_Context.error("The STB does not have a valid RayGen shader set");
            return;
        }

        if (m_EnableAutomaticBarriers)
        {
            for (size_t i = 0; i < state.bindings.size() && i < pso->desc.globalBindingLayouts.size(); i++)
            {
                BindingLayout* layout = checked_cast<BindingLayout*>(pso->desc.globalBindingLayouts[i].Get());

                if ((layout->desc.visibility & ShaderType::AllRayTracing) == 0)
                    continue;
                
                setResourceStatesForBindingSet(state.bindings[i]);
            }
        }

        if (m_CurrentRayTracingState.shaderTable != state.shaderTable)
        {
            m_CurrentCmdBuf->referencedResources.push_back(state.shaderTable);
        }

        if (!m_CurrentRayTracingState.shaderTable || m_CurrentRayTracingState.shaderTable->getPipeline() != pso)
        {
            m_CurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pso->pipeline);
            m_CurrentPipelineLayout = pso->pipelineLayout;
            m_CurrentPushConstantsVisibility = pso->pushConstantVisibility;
        }

        if (arraysAreDifferent(m_CurrentRayTracingState.bindings, state.bindings) || m_AnyVolatileBufferWrites)
        {
            bindBindingSets(vk::PipelineBindPoint::eRayTracingKHR, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        // Rebuild the SBT if we're binding a new one or if it's been changed since the previous bind.

        if (m_CurrentRayTracingState.shaderTable != shaderTable || m_CurrentShaderTablePointers.version != shaderTable->version)
        {
            const uint32_t shaderGroupHandleSize = m_Context.rayTracingPipelineProperties.shaderGroupHandleSize;
            const uint32_t shaderGroupBaseAlignment = m_Context.rayTracingPipelineProperties.shaderGroupBaseAlignment;

            const uint32_t shaderTableSize = shaderTable->getNumEntries() * shaderGroupBaseAlignment;

            // First, allocate a piece of the upload buffer. That will be our SBT on the device.

            Buffer* uploadBuffer = nullptr;
            uint64_t uploadOffset = 0;
            uint8_t* uploadCpuVA = nullptr;
            bool allocated = m_UploadManager->suballocateBuffer(shaderTableSize, &uploadBuffer, &uploadOffset, (void**)&uploadCpuVA,
                MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false),
                shaderGroupBaseAlignment);

            if (!allocated)
            {
                m_Context.error("Failed to suballocate an upload buffer for the SBT");
                return;
            }

            assert(uploadCpuVA);
            assert(uploadBuffer);

            // Copy the shader and group handles into the device SBT, record the pointers.

            vk::StridedDeviceAddressRegionKHR rayGenHandle;
            vk::StridedDeviceAddressRegionKHR missHandles;
            vk::StridedDeviceAddressRegionKHR hitGroupHandles;
            vk::StridedDeviceAddressRegionKHR callableHandles;

            // ... RayGen

            uint32_t sbtIndex = 0;
            memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                pso->shaderGroupHandles.data() + shaderGroupHandleSize * shaderTable->rayGenerationShader,
                shaderGroupHandleSize);
            rayGenHandle.setDeviceAddress(uploadBuffer->deviceAddress + uploadOffset + sbtIndex * shaderGroupBaseAlignment);
            rayGenHandle.setSize(shaderGroupBaseAlignment);
            rayGenHandle.setStride(shaderGroupBaseAlignment);
            sbtIndex++;

            // ... Miss

            if (!shaderTable->missShaders.empty())
            {
                missHandles.setDeviceAddress(uploadBuffer->deviceAddress + uploadOffset + sbtIndex * shaderGroupBaseAlignment);
                for (uint32_t shaderGroupIndex : shaderTable->missShaders)
                {
                    memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                        pso->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                        shaderGroupHandleSize);
                    sbtIndex++;
                }
                missHandles.setSize(shaderGroupBaseAlignment * uint32_t(shaderTable->missShaders.size()));
                missHandles.setStride(shaderGroupBaseAlignment);
            }

            // ... Hit Groups

            if (!shaderTable->hitGroups.empty())
            {
                hitGroupHandles.setDeviceAddress(uploadBuffer->deviceAddress + uploadOffset + sbtIndex * shaderGroupBaseAlignment);
                for (uint32_t shaderGroupIndex : shaderTable->hitGroups)
                {
                    memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                        pso->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                        shaderGroupHandleSize);
                    sbtIndex++;
                }
                hitGroupHandles.setSize(shaderGroupBaseAlignment * uint32_t(shaderTable->hitGroups.size()));
                hitGroupHandles.setStride(shaderGroupBaseAlignment);
            }

            // ... Callable

            if (!shaderTable->callableShaders.empty())
            {
                callableHandles.setDeviceAddress(uploadBuffer->deviceAddress + uploadOffset + sbtIndex * shaderGroupBaseAlignment);
                for (uint32_t shaderGroupIndex : shaderTable->callableShaders)
                {
                    memcpy(uploadCpuVA + sbtIndex * shaderGroupBaseAlignment,
                        pso->shaderGroupHandles.data() + shaderGroupHandleSize * shaderGroupIndex,
                        shaderGroupHandleSize);
                    sbtIndex++;
                }
                callableHandles.setSize(shaderGroupBaseAlignment * uint32_t(shaderTable->callableShaders.size()));
                callableHandles.setStride(shaderGroupBaseAlignment);
            }

            // Store the device pointers to the SBT for use in dispatchRays later, and the version.

            m_CurrentShaderTablePointers.rayGen = rayGenHandle;
            m_CurrentShaderTablePointers.miss = missHandles;
            m_CurrentShaderTablePointers.hitGroups = hitGroupHandles;
            m_CurrentShaderTablePointers.callable = callableHandles;
            m_CurrentShaderTablePointers.version = shaderTable->version;
        }
        
        commitBarriers();

        m_CurrentGraphicsState = GraphicsState();
        m_CurrentComputeState = ComputeState();
        m_CurrentMeshletState = MeshletState();
        m_CurrentRayTracingState = state;
        m_AnyVolatileBufferWrites = false;
    }

    void CommandList::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        assert(m_CurrentCmdBuf);

        updateRayTracingVolatileBuffers();

        m_CurrentCmdBuf->cmdBuf.traceRaysKHR(
            &m_CurrentShaderTablePointers.rayGen,
            &m_CurrentShaderTablePointers.miss,
            &m_CurrentShaderTablePointers.hitGroups,
            &m_CurrentShaderTablePointers.callable,
            args.width, args.height, args.depth);
    }

    void CommandList::updateRayTracingVolatileBuffers()
    {
        if (m_AnyVolatileBufferWrites && m_CurrentRayTracingState.shaderTable)
        {
            RayTracingPipeline* pso = checked_cast<RayTracingPipeline*>(m_CurrentRayTracingState.shaderTable->getPipeline());

            bindBindingSets(vk::PipelineBindPoint::eRayTracingKHR, pso->pipelineLayout, m_CurrentComputeState.bindings, pso->descriptorSetIdxToBindingIdx);

            m_AnyVolatileBufferWrites = false;
        }
    }

    static void registerShaderModule(
        IShader* _shader,
        std::unordered_map<Shader*, uint32_t>& shaderStageIndices,
        size_t& numShaders,
        size_t& numShadersWithSpecializations,
        size_t& numSpecializationConstants)
    {
        if (!_shader)
            return;
        
        Shader* shader = checked_cast<Shader*>(_shader);
        auto it = shaderStageIndices.find(shader);
        if (it == shaderStageIndices.end())
        {
            countSpecializationConstants(shader, numShaders, numShadersWithSpecializations, numSpecializationConstants);
            shaderStageIndices[shader] = uint32_t(shaderStageIndices.size());
        }
    }

    rt::PipelineHandle Device::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        RayTracingPipeline* pso = new RayTracingPipeline(m_Context);
        pso->desc = desc;

        vk::Result res = createPipelineLayout(
            pso->pipelineLayout,
            pso->pipelineBindingLayouts,
            pso->pushConstantVisibility,
            pso->descriptorSetIdxToBindingIdx,
            m_Context,
            desc.globalBindingLayouts);
        CHECK_VK_FAIL(res)

        // Count all shader modules with their specializations,
        // place them into a dictionary to remove duplicates.

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        std::unordered_map<Shader*, uint32_t> shaderStageIndices; // shader -> index

        for (const auto& shaderDesc : desc.shaders)
        {
            if (shaderDesc.bindingLayout)
            {
                utils::NotSupported();
                return nullptr;
            }

            registerShaderModule(shaderDesc.shader, shaderStageIndices, numShaders, 
                numShadersWithSpecializations, numSpecializationConstants);
        }

        for (const auto& hitGroupDesc : desc.hitGroups)
        {
            if (hitGroupDesc.bindingLayout)
            {
                utils::NotSupported();
                return nullptr;
            }

            registerShaderModule(hitGroupDesc.closestHitShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);

            registerShaderModule(hitGroupDesc.anyHitShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);

            registerShaderModule(hitGroupDesc.intersectionShader, shaderStageIndices, numShaders,
                numShadersWithSpecializations, numSpecializationConstants);
        }

        assert(numShaders == shaderStageIndices.size());

        // Populate the shader stages, shader groups, and specializations arrays.

        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
        std::vector<vk::SpecializationInfo> specInfos;
        std::vector<vk::SpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;

        shaderStages.resize(numShaders);
        shaderGroups.reserve(desc.shaders.size() + desc.hitGroups.size());
        specInfos.reserve(numShadersWithSpecializations);
        specMapEntries.reserve(numSpecializationConstants);
        specData.reserve(numSpecializationConstants);

        // ... Individual shaders (RayGen, Miss, Callable)

        for (const auto& shaderDesc : desc.shaders)
        {
            std::string exportName = shaderDesc.exportName;

            auto shaderGroupCreateInfo = vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
                .setClosestHitShader(VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(VK_SHADER_UNUSED_KHR);

            if (shaderDesc.shader)
            {
                Shader* shader = checked_cast<Shader*>(shaderDesc.shader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = makeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);

                if (exportName.empty())
                    exportName = shader->desc.entryName;

                shaderGroupCreateInfo.setGeneralShader(shaderStageIndex);
            }

            if (!exportName.empty())
            {
                pso->shaderGroups[exportName] = uint32_t(shaderGroups.size());
                shaderGroups.push_back(shaderGroupCreateInfo);
            }
        }

        // ... Hit groups

        for (const auto& hitGroupDesc : desc.hitGroups)
        {
            auto shaderGroupCreateInfo = vk::RayTracingShaderGroupCreateInfoKHR()
                .setType(hitGroupDesc.isProceduralPrimitive 
                    ? vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup
                    : vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup)
                .setGeneralShader(VK_SHADER_UNUSED_KHR)
                .setClosestHitShader(VK_SHADER_UNUSED_KHR)
                .setAnyHitShader(VK_SHADER_UNUSED_KHR)
                .setIntersectionShader(VK_SHADER_UNUSED_KHR);

            if (hitGroupDesc.closestHitShader)
            {
                Shader* shader = checked_cast<Shader*>(hitGroupDesc.closestHitShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = makeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setClosestHitShader(shaderStageIndex);
            }
            if (hitGroupDesc.anyHitShader)
            {
                Shader* shader = checked_cast<Shader*>(hitGroupDesc.anyHitShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = makeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setAnyHitShader(shaderStageIndex);
            }
            if (hitGroupDesc.intersectionShader)
            {
                Shader* shader = checked_cast<Shader*>(hitGroupDesc.intersectionShader.Get());
                uint32_t shaderStageIndex = shaderStageIndices[shader];
                shaderStages[shaderStageIndex] = makeShaderStageCreateInfo(shader, specInfos, specMapEntries, specData);
                shaderGroupCreateInfo.setIntersectionShader(shaderStageIndex);
            }

            assert(!hitGroupDesc.exportName.empty());
            
            pso->shaderGroups[hitGroupDesc.exportName] = uint32_t(shaderGroups.size());
            shaderGroups.push_back(shaderGroupCreateInfo);
        }

        // Create the pipeline object

        auto libraryInfo = vk::PipelineLibraryCreateInfoKHR();
        
        auto pipelineInfo = vk::RayTracingPipelineCreateInfoKHR()
            .setStages(shaderStages)
            .setGroups(shaderGroups)
            .setLayout(pso->pipelineLayout)
            .setMaxPipelineRayRecursionDepth(desc.maxRecursionDepth)
            .setPLibraryInfo(&libraryInfo);

        res = m_Context.device.createRayTracingPipelinesKHR(vk::DeferredOperationKHR(), m_Context.pipelineCache,
            1, &pipelineInfo,
            m_Context.allocationCallbacks,
            &pso->pipeline);

        CHECK_VK_FAIL(res)

        // Obtain the shader group handles to fill the SBT buffer later

        pso->shaderGroupHandles.resize(m_Context.rayTracingPipelineProperties.shaderGroupHandleSize * shaderGroups.size());

        res = m_Context.device.getRayTracingShaderGroupHandlesKHR(pso->pipeline, 0, 
            uint32_t(shaderGroups.size()), 
            pso->shaderGroupHandles.size(), pso->shaderGroupHandles.data());

        CHECK_VK_FAIL(res)

        return rt::PipelineHandle::Create(pso);
    }

    RayTracingPipeline::~RayTracingPipeline()
    {
        if (pipeline)
        {
            m_Context.device.destroyPipeline(pipeline, m_Context.allocationCallbacks);
            pipeline = nullptr;
        }

        if (pipelineLayout)
        {
            m_Context.device.destroyPipelineLayout(pipelineLayout, m_Context.allocationCallbacks);
            pipelineLayout = nullptr;
        }
    }

    rt::ShaderTableHandle RayTracingPipeline::createShaderTable()
    {
        ShaderTable* st = new ShaderTable(m_Context, this);
        return rt::ShaderTableHandle::Create(st);
    }

    Object RayTracingPipeline::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_PipelineLayout:
            return Object(pipelineLayout);
        case ObjectTypes::VK_Pipeline:
            return Object(pipeline);
        default:
            return nullptr;
        }
    }

    int RayTracingPipeline::findShaderGroup(const std::string& name)
    {
        auto it = shaderGroups.find(name);
        if (it == shaderGroups.end())
            return -1;

        return int(it->second);
    }

    bool ShaderTable::verifyShaderGroupExists(const char* exportName, int shaderGroupIndex) const
    {
        if (shaderGroupIndex >= 0)
            return true;

        std::stringstream ss;
        ss << "Cannot find a RT pipeline shader group for RayGen shader with name " << exportName;
        m_Context.error(ss.str());
        return false;
    }

    void ShaderTable::setRayGenerationShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            utils::NotSupported();

        const int shaderGroupIndex = pipeline->findShaderGroup(exportName);

        if (verifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            rayGenerationShader = shaderGroupIndex;
            ++version;
        }
    }

    int ShaderTable::addMissShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            utils::NotSupported();

        const int shaderGroupIndex = pipeline->findShaderGroup(exportName);

        if (verifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            missShaders.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(missShaders.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::addHitGroup(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            utils::NotSupported();

        const int shaderGroupIndex = pipeline->findShaderGroup(exportName);

        if (verifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            hitGroups.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(hitGroups.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::addCallableShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        if (bindings != nullptr)
            utils::NotSupported();

        const int shaderGroupIndex = pipeline->findShaderGroup(exportName);

        if (verifyShaderGroupExists(exportName, shaderGroupIndex))
        {
            callableShaders.push_back(uint32_t(shaderGroupIndex));
            ++version;

            return int(callableShaders.size()) - 1;
        }

        return -1;
    }

    void ShaderTable::clearMissShaders()
    {
        missShaders.clear();
        ++version;
    }

    void ShaderTable::clearHitShaders()
    {
        hitGroups.clear();
        ++version;
    }

    void ShaderTable::clearCallableShaders()
    {
        callableShaders.clear();
        ++version;
    }
    
    uint32_t ShaderTable::getNumEntries() const
    {
        return 1 + // rayGeneration
            uint32_t(missShaders.size()) +
            uint32_t(hitGroups.size()) +
            uint32_t(callableShaders.size());
    }
} // namespace nvrhi::vulkan
