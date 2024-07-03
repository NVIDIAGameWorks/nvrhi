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

#include <nvrhi/common/containers.h>
#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <vector>
#include <set>
#include <algorithm>
#include <list>
#include <sstream>

namespace
{
    class D3D12RaytracingGeometryDesc
    {
        struct RaytracingGeometryDesc
        {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
            NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_EX type;
#else
            D3D12_RAYTRACING_GEOMETRY_TYPE type;
#endif
            D3D12_RAYTRACING_GEOMETRY_FLAGS flags;
            union
            {
                D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC           triangles;
                D3D12_RAYTRACING_GEOMETRY_AABBS_DESC               aabbs;
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
                NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ommTriangles;
#endif
#if NVRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP
                // Note: this union member is currently only used to pad the structure so that it's the same size as NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX.
                // There is no support for Displacement Micro Maps in NVRHI API yet.
                NVAPI_D3D12_RAYTRACING_GEOMETRY_DMM_TRIANGLES_DESC dmmTriangles;
#endif
            };
        } m_data;
    public:

        constexpr void Validate()
        {
            {
                constexpr size_t kSize = sizeof(D3D12_RAYTRACING_GEOMETRY_TYPE) + sizeof(D3D12_RAYTRACING_GEOMETRY_FLAGS) + std::max(sizeof(D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC), sizeof(D3D12_RAYTRACING_GEOMETRY_AABBS_DESC));
                static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC) == kSize);
                static_assert(offsetof(D3D12_RAYTRACING_GEOMETRY_DESC, Type) == offsetof(RaytracingGeometryDesc, type));
                static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC::Type) == sizeof(RaytracingGeometryDesc::type));
                static_assert(offsetof(D3D12_RAYTRACING_GEOMETRY_DESC, Flags) == offsetof(RaytracingGeometryDesc, flags));
                static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC::Flags) == sizeof(RaytracingGeometryDesc::flags));
                static_assert(offsetof(D3D12_RAYTRACING_GEOMETRY_DESC, Triangles) == offsetof(RaytracingGeometryDesc, triangles));
                static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC::Triangles) == sizeof(RaytracingGeometryDesc::triangles));
                static_assert(offsetof(D3D12_RAYTRACING_GEOMETRY_DESC, AABBs) == offsetof(RaytracingGeometryDesc, aabbs));
                static_assert(sizeof(D3D12_RAYTRACING_GEOMETRY_DESC::AABBs) == sizeof(RaytracingGeometryDesc::aabbs));
            }
            {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP || NVRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX) == sizeof(RaytracingGeometryDesc));
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, type) == offsetof(RaytracingGeometryDesc, type));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::type) == sizeof(RaytracingGeometryDesc::type));
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, flags) == offsetof(RaytracingGeometryDesc, flags));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::flags) == sizeof(RaytracingGeometryDesc::flags));
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, triangles) == offsetof(RaytracingGeometryDesc, triangles));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::triangles) == sizeof(RaytracingGeometryDesc::triangles));
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, aabbs) == offsetof(RaytracingGeometryDesc, aabbs));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::aabbs) == sizeof(RaytracingGeometryDesc::aabbs));
#endif

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, ommTriangles) == offsetof(RaytracingGeometryDesc, ommTriangles));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::ommTriangles) == sizeof(RaytracingGeometryDesc::ommTriangles));
#endif
#if NVRHI_WITH_NVAPI_DISPLACEMENT_MICROMAP
                static_assert(offsetof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX, dmmTriangles) == offsetof(RaytracingGeometryDesc, dmmTriangles));
                static_assert(sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX::dmmTriangles) == sizeof(RaytracingGeometryDesc::dmmTriangles));
#endif
            }
        }

        void SetFlags(D3D12_RAYTRACING_GEOMETRY_FLAGS flags) {
            m_data.flags = flags;
        }

        void SetTriangles(const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles) {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
            m_data.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
#else
            m_data.type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
#endif
            m_data.triangles = triangles;
        }

        void SetAABBs(const D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& aabbs) {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
            m_data.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS_EX;
#else
            m_data.type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
#endif
            m_data.aabbs = aabbs;
        }

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
        void SetOMMTriangles(const NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC& ommTriangles) {
            m_data.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES_EX;
            m_data.ommTriangles = ommTriangles;
        }
#endif
    };

    class D3D12BuildRaytracingAccelerationStructureInputs
    {
        struct BuildRaytracingAccelerationStructure
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE Type;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS Flags;
            UINT NumDescs;
            D3D12_ELEMENTS_LAYOUT DescsLayout;

            union
            {
                D3D12_GPU_VIRTUAL_ADDRESS                   InstanceDescs;
                const D3D12RaytracingGeometryDesc* const* ppGeometryDescs;
            };
        } m_desc;

        std::vector<D3D12RaytracingGeometryDesc> m_geomDescs;
        std::vector<D3D12RaytracingGeometryDesc*> m_geomDescsPtr;

    public:

        void SetGeometryDescCount(uint32_t numDescs) {
            m_geomDescs.resize(numDescs);
            m_geomDescsPtr.resize(numDescs);
            for (uint32_t i = 0; i < numDescs; ++i)
                m_geomDescsPtr[i] = m_geomDescs.data() + i;
            m_desc.ppGeometryDescs = m_geomDescsPtr.data();
            m_desc.NumDescs = numDescs;
            m_desc.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;
        }

        void SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE type) {
            m_desc.Type = type;
        }

        void SetFlags(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags) {
            m_desc.Flags = flags;
        }

        void SetInstanceDescs(D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, UINT numDescs) {
            m_desc.InstanceDescs = instanceDescs;
            m_desc.NumDescs = numDescs;
        }

        D3D12RaytracingGeometryDesc& GetGeometryDesc(uint32_t index) {
            return m_geomDescs[index];
        }

        template<class T>
        const T GetAs();
    };

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
    template<>
    const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX D3D12BuildRaytracingAccelerationStructureInputs::GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>() {
        NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputs = {};
        inputs.type = m_desc.Type;
        inputs.flags = (NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX)m_desc.Flags;
        inputs.numDescs = m_desc.NumDescs;
        inputs.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
        inputs.descsLayout = m_desc.DescsLayout;
        inputs.instanceDescs = m_desc.InstanceDescs;
        static_assert(sizeof(BuildRaytracingAccelerationStructure::ppGeometryDescs) == sizeof(BuildRaytracingAccelerationStructure::InstanceDescs));
        static_assert(sizeof(NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX::ppGeometryDescs) == sizeof(NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX::instanceDescs));
        return inputs;
    }
#endif

    template<>
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS D3D12BuildRaytracingAccelerationStructureInputs::GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>() {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = m_desc.Type;
        inputs.Flags = m_desc.Flags;
        inputs.NumDescs = m_desc.NumDescs;
        inputs.DescsLayout = m_desc.DescsLayout;
        inputs.InstanceDescs = m_desc.InstanceDescs;
        static_assert(sizeof(BuildRaytracingAccelerationStructure::ppGeometryDescs) == sizeof(BuildRaytracingAccelerationStructure::InstanceDescs));
        static_assert(sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::ppGeometryDescs) == sizeof(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS::InstanceDescs));
        return inputs;
    }
}

namespace nvrhi::d3d12
{
    uint32_t ShaderTable::getNumEntries() const
    {
        return 1 + // rayGeneration
            uint32_t(missShaders.size()) +
            uint32_t(hitGroups.size()) +
            uint32_t(callableShaders.size());
    }

    bool ShaderTable::verifyExport(const RayTracingPipeline::ExportTableEntry* pExport, IBindingSet* bindings) const
    {
        if (!pExport)
        {
            m_Context.error("Couldn't find a DXR PSO export with a given name");
            return false;
        }

        if (pExport->bindingLayout && !bindings)
        {
            m_Context.error("A shader table entry does not provide required local bindings");
            return false;
        }

        if (!pExport->bindingLayout && bindings)
        {
            m_Context.error("A shader table entry provides local bindings, but none are required");
            return false;
        }

        if (bindings && (checked_cast<d3d12::BindingSet*>(bindings)->layout != pExport->bindingLayout))
        {
            m_Context.error("A shader table entry provides local bindings that do not match the expected layout");
            return false;
        }

        return true;
    }

    void ShaderTable::setRayGenerationShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const RayTracingPipeline::ExportTableEntry* pipelineExport = pipeline->getExport(exportName);

        if (verifyExport(pipelineExport, bindings))
        {
            rayGenerationShader.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            rayGenerationShader.localBindings = bindings;

            ++version;
        }
    }

    int ShaderTable::addMissShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const RayTracingPipeline::ExportTableEntry* pipelineExport = pipeline->getExport(exportName);

        if (verifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            missShaders.push_back(entry);

            ++version;

            return int(missShaders.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::addHitGroup(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const RayTracingPipeline::ExportTableEntry* pipelineExport = pipeline->getExport(exportName);

        if (verifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            hitGroups.push_back(entry);

            ++version;

            return int(hitGroups.size()) - 1;
        }

        return -1;
    }

    int ShaderTable::addCallableShader(const char* exportName, IBindingSet* bindings /*= nullptr*/)
    {
        const RayTracingPipeline::ExportTableEntry* pipelineExport = pipeline->getExport(exportName);

        if (verifyExport(pipelineExport, bindings))
        {
            Entry entry;
            entry.pShaderIdentifier = pipelineExport->pShaderIdentifier;
            entry.localBindings = bindings;
            callableShaders.push_back(entry);

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

    rt::IPipeline* ShaderTable::getPipeline()
    {
        return pipeline;
    }


    const RayTracingPipeline::ExportTableEntry* RayTracingPipeline::getExport(const char* name)
    {
        const auto exportEntryIt = exports.find(name);
        if (exportEntryIt == exports.end())
        {
            return nullptr;
        }

        return &exportEntryIt->second;
    }

    rt::ShaderTableHandle RayTracingPipeline::createShaderTable()
    { 
        return rt::ShaderTableHandle::Create(new ShaderTable(m_Context, this));
    }

    uint32_t RayTracingPipeline::getShaderTableEntrySize() const
    {
        uint32_t requiredSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + sizeof(UINT64) * maxLocalRootParameters;
        return align(requiredSize, uint32_t(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT));
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
#endif // NVRHI_WITH_RTXMU
    }

    Object OpacityMicromap::getNativeObject(ObjectType objectType)
    {
        if (dataBuffer)
            return dataBuffer->getNativeObject(objectType);

        return nullptr;
    }

    uint64_t OpacityMicromap::getDeviceAddress() const
    {
        return dataBuffer->gpuVA;
    }

    Object AccelStruct::getNativeObject(ObjectType objectType)
    {
        if (dataBuffer)
            return dataBuffer->getNativeObject(objectType);

        return nullptr;
    }

    uint64_t AccelStruct::getDeviceAddress() const
    {
#ifdef NVRHI_WITH_RTXMU
        if (!desc.isTopLevel)
            return m_Context.rtxMemUtil->GetAccelStructGPUVA(rtxmuId);
#endif
        return dataBuffer->gpuVA;
    }

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
    static const NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT* CastToUsageCount(const nvrhi::rt::OpacityMicromapUsageCount* desc)
    {
        static_assert(sizeof(nvrhi::rt::OpacityMicromapUsageCount) == sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT));
        static_assert(offsetof(nvrhi::rt::OpacityMicromapUsageCount, count) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, count));
        static_assert(sizeof(nvrhi::rt::OpacityMicromapUsageCount::count) == sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT::count));
        static_assert(offsetof(nvrhi::rt::OpacityMicromapUsageCount, subdivisionLevel) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, subdivisionLevel));
        static_assert(sizeof(nvrhi::rt::OpacityMicromapUsageCount::subdivisionLevel) == sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT::subdivisionLevel));
        static_assert(offsetof(nvrhi::rt::OpacityMicromapUsageCount, format) == offsetof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT, format));
        static_assert(sizeof(nvrhi::rt::OpacityMicromapUsageCount::format) == sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT::format));
        return reinterpret_cast<const NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT*>(desc);
    }

    static void fillD3dOpacityMicromapDesc(
        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS& outD3dDesc,
        const rt::OpacityMicromapDesc& desc) {
        outD3dDesc.flags = (NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAGS)desc.flags;
        outD3dDesc.numOMMUsageCounts = (NvU32)desc.counts.size();
        outD3dDesc.pOMMUsageCounts = CastToUsageCount(desc.counts.data());
        outD3dDesc.inputBuffer = checked_cast<Buffer*>(desc.inputBuffer)->gpuVA + desc.inputBufferOffset;
        outD3dDesc.perOMMDescs = { checked_cast<Buffer*>(desc.perOmmDescs)->gpuVA + desc.perOmmDescsOffset, sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC) };
    }
#endif

    static void fillD3dGeometryTrianglesDesc(D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& outDxrTriangles, const rt::GeometryDesc& geometryDesc, D3D12_GPU_VIRTUAL_ADDRESS transform4x4)
    {
        const auto& triangles = geometryDesc.geometryData.triangles;

        if (triangles.indexBuffer)
            outDxrTriangles.IndexBuffer = checked_cast<Buffer*>(triangles.indexBuffer)->gpuVA + triangles.indexOffset;
        else
            outDxrTriangles.IndexBuffer = 0;

        if (triangles.vertexBuffer)
            outDxrTriangles.VertexBuffer.StartAddress = checked_cast<Buffer*>(triangles.vertexBuffer)->gpuVA + triangles.vertexOffset;
        else
            outDxrTriangles.VertexBuffer.StartAddress = 0;

        outDxrTriangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
        outDxrTriangles.IndexFormat = getDxgiFormatMapping(triangles.indexFormat).srvFormat;
        outDxrTriangles.VertexFormat = getDxgiFormatMapping(triangles.vertexFormat).srvFormat;
        outDxrTriangles.IndexCount = triangles.indexCount;
        outDxrTriangles.VertexCount = triangles.vertexCount;
        outDxrTriangles.Transform3x4 = transform4x4;
    }

    static void fillD3dAABBDesc(D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& outDxrAABB, const rt::GeometryDesc& geometryDesc)
    {
        const auto& aabbs = geometryDesc.geometryData.aabbs;

        if (aabbs.buffer)
            outDxrAABB.AABBs.StartAddress = checked_cast<Buffer*>(aabbs.buffer)->gpuVA + aabbs.offset;
        else
            outDxrAABB.AABBs.StartAddress = 0;

        outDxrAABB.AABBs.StrideInBytes = aabbs.stride;
        outDxrAABB.AABBCount = aabbs.count;
    }

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
    static void fillOmmAttachmentDesc(NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_ATTACHMENT_DESC& ommAttachment, const rt::GeometryDesc& geometryDesc)
    {
        const auto& triangles = geometryDesc.geometryData.triangles;

        // There's currently a bug that disables VMs if the input buffer is null.
        // just assign 128 in case it's null and index buffer is set.
        ommAttachment.opacityMicromapArray = triangles.opacityMicromap == nullptr ? 128 : checked_cast<OpacityMicromap*>(triangles.opacityMicromap)->getDeviceAddress();
        ommAttachment.opacityMicromapBaseLocation = 0;
        ommAttachment.opacityMicromapIndexBuffer.StartAddress = triangles.ommIndexBuffer == nullptr ? 0 : checked_cast<Buffer*>(triangles.ommIndexBuffer)->gpuVA + triangles.ommIndexBufferOffset;
        ommAttachment.opacityMicromapIndexBuffer.StrideInBytes = triangles.ommIndexFormat == Format::R32_UINT ? 4 : 2;
        ommAttachment.opacityMicromapIndexFormat = getDxgiFormatMapping(triangles.ommIndexFormat).srvFormat;

        if (triangles.pOmmUsageCounts)
        {
			assert(triangles.opacityMicromap);
            ommAttachment.pOMMUsageCounts = CastToUsageCount(triangles.pOmmUsageCounts);
            ommAttachment.numOMMUsageCounts = triangles.numOmmUsageCounts;
        }
        else
        {
            ommAttachment.pOMMUsageCounts = nullptr;
            ommAttachment.numOMMUsageCounts = 0;
        }
    }
#endif

    static void fillD3dGeometryDesc(D3D12RaytracingGeometryDesc& outD3dGeometryDesc, const rt::GeometryDesc& geometryDesc, D3D12_GPU_VIRTUAL_ADDRESS transform4x4)
    {
        outD3dGeometryDesc.SetFlags((D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryDesc.flags);

        if (geometryDesc.geometryType == rt::GeometryType::Triangles)
        {
            const auto& triangles = geometryDesc.geometryData.triangles;
            if (triangles.opacityMicromap != nullptr || triangles.ommIndexBuffer != nullptr) {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
                NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC ommTriangles = {};
                fillD3dGeometryTrianglesDesc(ommTriangles.triangles, geometryDesc, transform4x4);
                fillOmmAttachmentDesc(ommTriangles.ommAttachment, geometryDesc);
                outD3dGeometryDesc.SetOMMTriangles(ommTriangles);
#else
                utils::NotSupported();
#endif
            }
            else {
                D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC dxrTriangles = {};
                fillD3dGeometryTrianglesDesc(dxrTriangles, geometryDesc, transform4x4);
                outD3dGeometryDesc.SetTriangles(dxrTriangles);
            }
        }
        else
        {
            D3D12_RAYTRACING_GEOMETRY_AABBS_DESC dxrAABBs = {};
            fillD3dAABBDesc(dxrAABBs, geometryDesc);
            outD3dGeometryDesc.SetAABBs(dxrAABBs);
        }
    }

    static void fillAsInputDescForPreBuildInfo(
        D3D12BuildRaytracingAccelerationStructureInputs& outASInputs,
        const rt::AccelStructDesc& desc)
    {
        if (desc.isTopLevel)
        {
            outASInputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL);
            outASInputs.SetFlags((D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags);
            outASInputs.SetInstanceDescs(D3D12_GPU_VIRTUAL_ADDRESS{ 0, }, (UINT)desc.topLevelMaxInstances);
        }
        else
        {
            outASInputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL);
            outASInputs.SetFlags((D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags);
            outASInputs.SetGeometryDescCount((UINT)desc.bottomLevelGeometries.size());
            for (uint32_t i = 0; i < desc.bottomLevelGeometries.size(); i++)
            {
                const rt::GeometryDesc& srcDesc = desc.bottomLevelGeometries[i];
                // useTransform sets a non-null dummy GPU VA. The reason is explained in the spec:
                // "It (read: GetRaytracingAccelerationStructurePrebuildInfo) may not inspect/dereference
                // any GPU virtual addresses, other than to check to see if a pointer is NULL or not,
                // such as the optional Transform in D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC, without dereferencing it."
                // Omitting this here will trigger a gpu hang due to incorrect memory calculation.
                D3D12_GPU_VIRTUAL_ADDRESS transform4x4 = srcDesc.useTransform ? 16 : 0; 
                D3D12RaytracingGeometryDesc& geomDesc = outASInputs.GetGeometryDesc(i);
                fillD3dGeometryDesc(geomDesc, srcDesc, transform4x4);
            }
        }
    }

    rt::OpacityMicromapHandle Device::createOpacityMicromap([[maybe_unused]] const rt::OpacityMicromapDesc& desc)
    {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
        assert(m_OpacityMicromapSupported && "Opacity Micromap not supported");
        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS inputs = {};
        fillD3dOpacityMicromapDesc(inputs, desc);

        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO omPreBuildInfo = {};

        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS params = {};
        params.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;
        params.pDesc = &inputs;
        params.pInfo = &omPreBuildInfo;
        NvAPI_Status status = NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(m_Context.device5.Get(), &params);
        assert(status == S_OK);
        if (status != S_OK)
            return nullptr;

        OpacityMicromap* om = new OpacityMicromap();
        om->desc = desc;
        om->compacted = false;

        {
            BufferDesc bufferDesc;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.byteSize = omPreBuildInfo.resultDataMaxSizeInBytes;
            bufferDesc.initialState = ResourceStates::OpacityMicromapWrite;
            bufferDesc.keepInitialState = true;
            bufferDesc.isAccelStructStorage = true;
            bufferDesc.debugName = desc.debugName;
            bufferDesc.isVirtual = false;
            BufferHandle buffer = createBuffer(bufferDesc);
            om->dataBuffer = checked_cast<Buffer*>(buffer.Get());
            assert((om->dataBuffer->gpuVA % NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BYTE_ALIGNMENT) == 0);
        }
        return rt::OpacityMicromapHandle::Create(om);
#else
        utils::NotSupported();
        return nullptr;
#endif
    }

    bool Device::GetAccelStructPreBuildInfo(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outPreBuildInfo, const rt::AccelStructDesc& desc) const
    {
        D3D12BuildRaytracingAccelerationStructureInputs ASInputs;
        fillAsInputDescForPreBuildInfo(ASInputs, desc);

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
        if (m_NvapiIsInitialized)
        {
            const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputs = ASInputs.GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>();

            NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params;
            params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
            params.pDesc = &inputs;
            params.pInfo = &outPreBuildInfo;

            NvAPI_Status status = NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(m_Context.device5, &params);
            assert(status == S_OK);
            return status == S_OK;
        }
        else
#endif
        {
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = ASInputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>();
            m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &outPreBuildInfo);
            return true;
        }
    }

    rt::AccelStructHandle Device::createAccelStruct(const rt::AccelStructDesc& desc)
    {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
        if (!GetAccelStructPreBuildInfo(ASPreBuildInfo, desc))
            return nullptr;

        AccelStruct* as = new AccelStruct(m_Context);
        as->desc = desc;
        as->allowUpdate = (desc.buildFlags & rt::AccelStructBuildFlags::AllowUpdate) != 0;

        assert(ASPreBuildInfo.ResultDataMaxSizeInBytes <= ~0u);

#ifdef NVRHI_WITH_RTXMU
        bool needBuffer = desc.isTopLevel;
#else
        bool needBuffer = true;
#endif

        if (needBuffer)
        {
            BufferDesc bufferDesc;
            bufferDesc.canHaveUAVs = true;
            bufferDesc.byteSize = ASPreBuildInfo.ResultDataMaxSizeInBytes;
            bufferDesc.initialState = desc.isTopLevel ? ResourceStates::AccelStructRead : ResourceStates::AccelStructBuildBlas;
            bufferDesc.keepInitialState = true;
            bufferDesc.isAccelStructStorage = true;
            bufferDesc.debugName = desc.debugName;
            bufferDesc.isVirtual = desc.isVirtual;
            BufferHandle buffer = createBuffer(bufferDesc);
            as->dataBuffer = checked_cast<Buffer*>(buffer.Get());
        }
        
        // Sanitize the geometry data to avoid dangling pointers, we don't need these buffers in the Desc
        for (auto& geometry : as->desc.bottomLevelGeometries)
        {
            static_assert(offsetof(rt::GeometryTriangles, indexBuffer)
                == offsetof(rt::GeometryAABBs, buffer));
            static_assert(offsetof(rt::GeometryTriangles, vertexBuffer)
                == offsetof(rt::GeometryAABBs, unused));

            // Clear only the triangles' data, because the AABBs' data is aliased to triangles (verified above)
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

    bool Device::bindAccelStructMemory(rt::IAccelStruct* _as, IHeap* heap, uint64_t offset)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        if (as->dataBuffer)
            return bindBufferMemory(as->dataBuffer, heap, offset);

        return false;
    }

    void AccelStruct::createSRV(size_t descriptor) const
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = dataBuffer->gpuVA;

        m_Context.device->CreateShaderResourceView(nullptr, &srvDesc, { descriptor });
    }

    bool Device::setHlslExtensionsUAV(uint32_t slot)
    {
#if NVRHI_D3D12_WITH_NVAPI
        if (GetNvapiIsInitialized())
        {
            NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(m_Context.device.Get(), slot, 0);
            if (status != S_OK)
            {
                m_Context.error("Failed to set NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread");
                return false;
            }
            return true;
        }
        else
        {
            m_Context.error("HLSL extensions require an NVIDIA graphics device with NVAPI support");
        }
#else
        (void)slot;
        m_Context.error("This version of NVRHI does not support NVIDIA HLSL extensions, please rebuild with NVAPI.");
#endif
        return false;
    }

#define NEW_ON_STACK(T) (T*)alloca(sizeof(T))
    
    rt::PipelineHandle Device::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        RayTracingPipeline* pso = new RayTracingPipeline(m_Context);
        pso->desc = desc;
        pso->maxLocalRootParameters = 0;

        // Collect all DXIL libraries that are referenced in `desc`, and enumerate their exports.
        // Build local root signatures for all referenced local binding layouts.
        // Convert the export names to wstring.

        struct Library
        {
            const void* pBlob = nullptr;
            size_t blobSize = 0;
            std::vector<std::pair<std::wstring, std::wstring>> exports; // vector(originalName, newName)
            std::vector<D3D12_EXPORT_DESC> d3dExports;
        };

        // Go through the individual shaders first.

        std::unordered_map<const void*, Library> dxilLibraries;

        for (const rt::PipelineShaderDesc& shaderDesc : desc.shaders)
        {
            const void* pBlob = nullptr;
            size_t blobSize = 0;
            shaderDesc.shader->getBytecode(&pBlob, &blobSize);

            // Assuming that no shader is referenced twice, we just add every shader to its library export list.

            Library& library = dxilLibraries[pBlob];
            library.pBlob = pBlob;
            library.blobSize = blobSize;

            std::string originalShaderName = shaderDesc.shader->getDesc().entryName;
            std::string newShaderName = shaderDesc.exportName.empty() ? originalShaderName : shaderDesc.exportName;

            library.exports.push_back(std::make_pair<std::wstring, std::wstring>(
                std::wstring(originalShaderName.begin(), originalShaderName.end()),
                std::wstring(newShaderName.begin(), newShaderName.end())
                ));

            // Build a local root signature for the shader, if needed.

            if (shaderDesc.bindingLayout)
            {
                RootSignatureHandle& localRS = pso->localRootSignatures[shaderDesc.bindingLayout];
                if (!localRS)
                {
                    localRS = buildRootSignature({ shaderDesc.bindingLayout }, false, true);

                    BindingLayout* layout = checked_cast<BindingLayout*>(shaderDesc.bindingLayout.Get());
                    pso->maxLocalRootParameters = std::max(pso->maxLocalRootParameters, uint32_t(layout->rootParameters.size()));
                }
            }
        }

        // Still in the collection phase - go through the hit groups.
        // Rename all exports used in the hit groups to avoid collisions between different libraries.

        std::vector<D3D12_HIT_GROUP_DESC> d3dHitGroups;
        std::unordered_map<IShader*, std::wstring> hitGroupShaderNames;
        std::vector<std::wstring> hitGroupExportNames;
        hitGroupExportNames.reserve(desc.hitGroups.size());

        for (const rt::PipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        {
            for (const ShaderHandle& shader : { hitGroupDesc.closestHitShader, hitGroupDesc.anyHitShader, hitGroupDesc.intersectionShader })
            {
                if (!shader)
                    continue;

                std::wstring& newName = hitGroupShaderNames[shader];

                // See if we've encountered this particular shader before...

                if (newName.empty())
                {
                    // No - add it to the corresponding library, come up with a new name for it.

                    const void* pBlob = nullptr;
                    size_t blobSize = 0;
                    shader->getBytecode(&pBlob, &blobSize);

                    Library& library = dxilLibraries[pBlob];
                    library.pBlob = pBlob;
                    library.blobSize = blobSize;

                    std::string originalShaderName = shader->getDesc().entryName;
                    std::string newShaderName = originalShaderName + std::to_string(hitGroupShaderNames.size());

                    library.exports.push_back(std::make_pair<std::wstring, std::wstring>(
                        std::wstring(originalShaderName.begin(), originalShaderName.end()),
                        std::wstring(newShaderName.begin(), newShaderName.end())
                        ));

                    newName = std::wstring(newShaderName.begin(), newShaderName.end());
                }
            }

            // Build a local root signature for the hit group, if needed.

            if (hitGroupDesc.bindingLayout)
            {
                RootSignatureHandle& localRS = pso->localRootSignatures[hitGroupDesc.bindingLayout];
                if (!localRS)
                {
                    localRS = buildRootSignature({ hitGroupDesc.bindingLayout }, false, true);

                    BindingLayout* layout = checked_cast<BindingLayout*>(hitGroupDesc.bindingLayout.Get());
                    pso->maxLocalRootParameters = std::max(pso->maxLocalRootParameters, uint32_t(layout->rootParameters.size()));
                }
            }

            // Create a hit group descriptor and store the new export names in it.

            D3D12_HIT_GROUP_DESC d3dHitGroupDesc = {};
            if (hitGroupDesc.anyHitShader)
                d3dHitGroupDesc.AnyHitShaderImport = hitGroupShaderNames[hitGroupDesc.anyHitShader].c_str();
            if (hitGroupDesc.closestHitShader)
                d3dHitGroupDesc.ClosestHitShaderImport = hitGroupShaderNames[hitGroupDesc.closestHitShader].c_str();
            if (hitGroupDesc.intersectionShader)
                d3dHitGroupDesc.IntersectionShaderImport = hitGroupShaderNames[hitGroupDesc.intersectionShader].c_str();

            if (hitGroupDesc.isProceduralPrimitive)
                d3dHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
            else
                d3dHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

            std::wstring hitGroupExportName = std::wstring(hitGroupDesc.exportName.begin(), hitGroupDesc.exportName.end());
            hitGroupExportNames.push_back(hitGroupExportName); // store the wstring so that it's not deallocated
            d3dHitGroupDesc.HitGroupExport = hitGroupExportNames[hitGroupExportNames.size() - 1].c_str();
            d3dHitGroups.push_back(d3dHitGroupDesc);
        }

        // Create descriptors for DXIL libraries, enumerate the exports used from each library.

        std::vector<D3D12_DXIL_LIBRARY_DESC> d3dDxilLibraries;
        d3dDxilLibraries.reserve(dxilLibraries.size());
        for (auto& it : dxilLibraries)
        {
            Library& library = it.second;

            for (const std::pair<std::wstring, std::wstring>& exportNames : library.exports)
            {
                D3D12_EXPORT_DESC d3dExportDesc = {};
                d3dExportDesc.ExportToRename = exportNames.first.c_str();
                d3dExportDesc.Name = exportNames.second.c_str();
                d3dExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
                library.d3dExports.push_back(d3dExportDesc);
            }

            D3D12_DXIL_LIBRARY_DESC d3dLibraryDesc = {};
            d3dLibraryDesc.DXILLibrary.pShaderBytecode = library.pBlob;
            d3dLibraryDesc.DXILLibrary.BytecodeLength = library.blobSize;
            d3dLibraryDesc.NumExports = UINT(library.d3dExports.size());
            d3dLibraryDesc.pExports = library.d3dExports.data();

            d3dDxilLibraries.push_back(d3dLibraryDesc);
        }

        // Start building the D3D state subobject array.

        std::vector<D3D12_STATE_SUBOBJECT> d3dSubobjects;

        // Same subobject is reused multiple times and copied to the vector each time.
        D3D12_STATE_SUBOBJECT d3dSubobject = {};

        // Subobject: Shader config

        D3D12_RAYTRACING_SHADER_CONFIG d3dShaderConfig = {};
        d3dShaderConfig.MaxAttributeSizeInBytes = desc.maxAttributeSize;
        d3dShaderConfig.MaxPayloadSizeInBytes = desc.maxPayloadSize;

        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        d3dSubobject.pDesc = &d3dShaderConfig;
        d3dSubobjects.push_back(d3dSubobject);

        // Subobject: Pipeline config

        D3D12_RAYTRACING_PIPELINE_CONFIG d3dPipelineConfig = {};
        d3dPipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;

        d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        d3dSubobject.pDesc = &d3dPipelineConfig;
        d3dSubobjects.push_back(d3dSubobject);

        // Subobjects: DXIL libraries

        for (const D3D12_DXIL_LIBRARY_DESC& d3dLibraryDesc : d3dDxilLibraries)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            d3dSubobject.pDesc = &d3dLibraryDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobjects: hit groups

        for (const D3D12_HIT_GROUP_DESC& d3dHitGroupDesc : d3dHitGroups)
        {
            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            d3dSubobject.pDesc = &d3dHitGroupDesc;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobject: global root signature

        D3D12_GLOBAL_ROOT_SIGNATURE d3dGlobalRootSignature = {};

        if (!desc.globalBindingLayouts.empty())
        {
            RootSignatureHandle rootSignature = buildRootSignature(desc.globalBindingLayouts, false, false);
            pso->globalRootSignature = checked_cast<RootSignature*>(rootSignature.Get());
            d3dGlobalRootSignature.pGlobalRootSignature = pso->globalRootSignature->getNativeObject(ObjectTypes::D3D12_RootSignature);

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = &d3dGlobalRootSignature;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Subobjects: local root signatures

        // Make sure that adding local root signatures does not resize the array,
        // because we need to store pointers to array elements there.
        d3dSubobjects.reserve(d3dSubobjects.size() + pso->localRootSignatures.size() * 2);

        // Same - pre-allocate the arrays to avoid resizing them
        size_t numAssociations = desc.shaders.size() + desc.hitGroups.size();
        std::vector<std::wstring> d3dAssociationExports;
        std::vector<LPCWSTR> d3dAssociationExportsCStr;
        d3dAssociationExports.reserve(numAssociations);
        d3dAssociationExportsCStr.reserve(numAssociations);

        for (const auto& it : pso->localRootSignatures)
        {
            auto d3dLocalRootSignature = NEW_ON_STACK(D3D12_LOCAL_ROOT_SIGNATURE);
            d3dLocalRootSignature->pLocalRootSignature = it.second->getNativeObject(ObjectTypes::D3D12_RootSignature);

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            d3dSubobject.pDesc = d3dLocalRootSignature;
            d3dSubobjects.push_back(d3dSubobject);

            auto d3dAssociation = NEW_ON_STACK(D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION);
            d3dAssociation->pSubobjectToAssociate = &d3dSubobjects[d3dSubobjects.size() - 1];
            d3dAssociation->NumExports = 0;
            size_t firstExportIndex = d3dAssociationExportsCStr.size();

            for (auto shader : desc.shaders)
            {
                if (shader.bindingLayout == it.first)
                {
                    std::string exportName = shader.exportName.empty() ? shader.shader->getDesc().entryName : shader.exportName;
                    std::wstring exportNameW = std::wstring(exportName.begin(), exportName.end());
                    d3dAssociationExports.push_back(exportNameW);
                    d3dAssociationExportsCStr.push_back(d3dAssociationExports[d3dAssociationExports.size() - 1].c_str());
                    d3dAssociation->NumExports += 1;
                }
            }

            for (auto hitGroup : desc.hitGroups)
            {
                if (hitGroup.bindingLayout == it.first)
                {
                    std::wstring exportNameW = std::wstring(hitGroup.exportName.begin(), hitGroup.exportName.end());
                    d3dAssociationExports.push_back(exportNameW);
                    d3dAssociationExportsCStr.push_back(d3dAssociationExports[d3dAssociationExports.size() - 1].c_str());
                    d3dAssociation->NumExports += 1;
                }
            }
            
            d3dAssociation->pExports = &d3dAssociationExportsCStr[firstExportIndex];

            d3dSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
            d3dSubobject.pDesc = d3dAssociation;
            d3dSubobjects.push_back(d3dSubobject);
        }

        // Top-level PSO descriptor structure

        D3D12_STATE_OBJECT_DESC pipelineDesc = {};
        pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        pipelineDesc.NumSubobjects = static_cast<UINT>(d3dSubobjects.size());
        pipelineDesc.pSubobjects = d3dSubobjects.data();

        if (desc.hlslExtensionsUAV >= 0)
        {
            if (!setHlslExtensionsUAV(desc.hlslExtensionsUAV))
                return nullptr;
        }

        HRESULT hr = m_Context.device5->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&pso->pipelineState));

        if (desc.hlslExtensionsUAV >= 0)
        {
            // Disable the magic UAV slot - do it before the test for successful pipeline creation below
            // to avoid leaving the slot set when there's an error in the pipeline.
            if (!setHlslExtensionsUAV(0xFFFFFFFF))
                return nullptr;
        }

        if (FAILED(hr))
        {
            m_Context.error("Failed to create a DXR pipeline state object");
            return nullptr;
        }


        hr = pso->pipelineState->QueryInterface(IID_PPV_ARGS(&pso->pipelineInfo));
        if (FAILED(hr))
        {
            m_Context.error("Failed to get a DXR pipeline info interface from a PSO");
            return nullptr;
        }

        for (const rt::PipelineShaderDesc& shaderDesc : desc.shaders)
        {
            std::string exportName = !shaderDesc.exportName.empty() ? shaderDesc.exportName : shaderDesc.shader->getDesc().entryName;
            std::wstring exportNameW = std::wstring(exportName.begin(), exportName.end());
            const void* pShaderIdentifier = pso->pipelineInfo->GetShaderIdentifier(exportNameW.c_str());

            if (pShaderIdentifier == nullptr)
            {
                m_Context.error("Failed to get an identifier for a shader in a fresh DXR PSO");
                return nullptr;
            }

            pso->exports[exportName] = RayTracingPipeline::ExportTableEntry{ shaderDesc.bindingLayout, pShaderIdentifier };
        }

        for(const rt::PipelineHitGroupDesc& hitGroupDesc : desc.hitGroups)
        { 
            std::wstring exportNameW = std::wstring(hitGroupDesc.exportName.begin(), hitGroupDesc.exportName.end());
            const void* pShaderIdentifier = pso->pipelineInfo->GetShaderIdentifier(exportNameW.c_str());

            if (pShaderIdentifier == nullptr)
            {
                m_Context.error("Failed to get an identifier for a hit group in a fresh DXR PSO");
                return nullptr;
            }

            pso->exports[hitGroupDesc.exportName] = RayTracingPipeline::ExportTableEntry{ hitGroupDesc.bindingLayout, pShaderIdentifier };
        }

        return rt::PipelineHandle::Create(pso);
    }

    void CommandList::setRayTracingState(const rt::State& state)
    {
        ShaderTable* shaderTable = checked_cast<ShaderTable*>(state.shaderTable);
        RayTracingPipeline* pso = shaderTable->pipeline;

        ShaderTableState* shaderTableState = getShaderTableStateTracking(shaderTable);

        bool rebuildShaderTable = shaderTableState->committedVersion != shaderTable->version ||
            shaderTableState->descriptorHeapSRV != m_Resources.shaderResourceViewHeap.getShaderVisibleHeap() ||
            shaderTableState->descriptorHeapSamplers != m_Resources.samplerHeap.getShaderVisibleHeap();

        if (rebuildShaderTable)
        {
            uint32_t entrySize = pso->getShaderTableEntrySize();
            uint32_t sbtSize = shaderTable->getNumEntries() * entrySize;

            unsigned char* cpuVA;
            D3D12_GPU_VIRTUAL_ADDRESS gpuVA;
            if (!m_UploadManager.suballocateBuffer(sbtSize, nullptr, nullptr, nullptr, 
                (void**)&cpuVA, &gpuVA, m_RecordingVersion, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT))
            {
                m_Context.error("Couldn't suballocate an upload buffer");
                return;
            }

            uint32_t entryIndex = 0;

            auto writeEntry = [this, entrySize, &cpuVA, &gpuVA, &entryIndex](const ShaderTable::Entry& entry) 
            {
                memcpy(cpuVA, entry.pShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

                if (entry.localBindings)
                {
                    d3d12::BindingSet* bindingSet = checked_cast<d3d12::BindingSet*>(entry.localBindings.Get());
                    d3d12::BindingLayout* layout = bindingSet->layout;

                    if (layout->descriptorTableSizeSamplers > 0)
                    {
                        auto pTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + layout->rootParameterSamplers * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                        *pTable = m_Resources.samplerHeap.getGpuHandle(bindingSet->descriptorTableSamplers);
                    }

                    if (layout->descriptorTableSizeSRVetc > 0)
                    {
                        auto pTable = reinterpret_cast<D3D12_GPU_DESCRIPTOR_HANDLE*>(cpuVA + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + layout->rootParameterSRVetc * sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
                        *pTable = m_Resources.shaderResourceViewHeap.getGpuHandle(bindingSet->descriptorTableSRVetc);
                    }

                    if (!layout->rootParametersVolatileCB.empty())
                    {
                        m_Context.error("Cannot use Volatile CBs in a shader binding table");
                        return;
                    }
                }

                cpuVA += entrySize;
                gpuVA += entrySize;
                entryIndex += 1;
            };

            D3D12_DISPATCH_RAYS_DESC& drd = shaderTableState->dispatchRaysTemplate;
            memset(&drd, 0, sizeof(D3D12_DISPATCH_RAYS_DESC));

            drd.RayGenerationShaderRecord.StartAddress = gpuVA;
            drd.RayGenerationShaderRecord.SizeInBytes = entrySize;
            writeEntry(shaderTable->rayGenerationShader);
            
            if (!shaderTable->missShaders.empty())
            {
                drd.MissShaderTable.StartAddress = gpuVA;
                drd.MissShaderTable.StrideInBytes = (shaderTable->missShaders.size() == 1) ? 0 : entrySize;
                drd.MissShaderTable.SizeInBytes = uint32_t(shaderTable->missShaders.size()) * entrySize;

                for (auto& entry : shaderTable->missShaders)
                    writeEntry(entry);
            }

            if (!shaderTable->hitGroups.empty())
            {
                drd.HitGroupTable.StartAddress = gpuVA;
                drd.HitGroupTable.StrideInBytes = (shaderTable->hitGroups.size() == 1) ? 0 : entrySize;
                drd.HitGroupTable.SizeInBytes = uint32_t(shaderTable->hitGroups.size()) * entrySize;

                for (auto& entry : shaderTable->hitGroups)
                    writeEntry(entry);
            }

            if (!shaderTable->callableShaders.empty())
            {
                drd.CallableShaderTable.StartAddress = gpuVA;
                drd.CallableShaderTable.StrideInBytes = (shaderTable->callableShaders.size() == 1) ? 0 : entrySize;
                drd.CallableShaderTable.SizeInBytes = uint32_t(shaderTable->callableShaders.size()) * entrySize;

                for (auto& entry : shaderTable->callableShaders)
                    writeEntry(entry);
            }

            shaderTableState->committedVersion = shaderTable->version;
            shaderTableState->descriptorHeapSRV = m_Resources.shaderResourceViewHeap.getShaderVisibleHeap();
            shaderTableState->descriptorHeapSamplers = m_Resources.samplerHeap.getShaderVisibleHeap();

            // AddRef the shaderTable only on the first use / build because build happens at least once per CL anyway
            m_Instance->referencedResources.push_back(shaderTable);
        }

        const bool updateRootSignature = !m_CurrentRayTracingStateValid || m_CurrentRayTracingState.shaderTable == nullptr ||
            checked_cast<ShaderTable*>(m_CurrentRayTracingState.shaderTable)->pipeline->globalRootSignature != pso->globalRootSignature;

        bool updatePipeline = !m_CurrentRayTracingStateValid || m_CurrentRayTracingState.shaderTable->getPipeline() != pso;

        uint32_t bindingUpdateMask = 0;
        if (!m_CurrentRayTracingStateValid || updateRootSignature)
            bindingUpdateMask = ~0u;

        if (commitDescriptorHeaps())
            bindingUpdateMask = ~0u;

        if (bindingUpdateMask == 0)
            bindingUpdateMask = arrayDifferenceMask(m_CurrentRayTracingState.bindings, state.bindings);

        if (updateRootSignature)
        {   
            m_ActiveCommandList->commandList4->SetComputeRootSignature(pso->globalRootSignature->handle);
        }

        if (updatePipeline)
        {
            m_ActiveCommandList->commandList4->SetPipelineState1(pso->pipelineState);

            m_Instance->referencedResources.push_back(pso);
        }

        setComputeBindings(state.bindings, bindingUpdateMask, nullptr, false, pso->globalRootSignature);

        unbindShadingRateState();

        m_CurrentComputeStateValid = false;
        m_CurrentGraphicsStateValid = false;
        m_CurrentRayTracingStateValid = true;
        m_CurrentRayTracingState = state;

        commitBarriers();
    }

    void CommandList::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        updateComputeVolatileBuffers();

        if (!m_CurrentRayTracingStateValid)
        {
            m_Context.error("setRayTracingState must be called before dispatchRays");
            return;
        }

        ShaderTableState* shaderTableState = getShaderTableStateTracking(m_CurrentRayTracingState.shaderTable);

        D3D12_DISPATCH_RAYS_DESC desc = shaderTableState->dispatchRaysTemplate;
        desc.Width = args.width;
        desc.Height = args.height;
        desc.Depth = args.depth;

        m_ActiveCommandList->commandList4->DispatchRays(&desc);
    }

    void CommandList::buildOpacityMicromap([[maybe_unused]] rt::IOpacityMicromap* pOmm, [[maybe_unused]] const rt::OpacityMicromapDesc& desc) {
#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
        OpacityMicromap* omm = checked_cast<OpacityMicromap*>(pOmm);

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(desc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
            requireBufferState(desc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);

            requireBufferState(omm->dataBuffer, nvrhi::ResourceStates::OpacityMicromapWrite);
        }

        if (desc.trackLiveness)
        {
            m_Instance->referencedResources.push_back(desc.inputBuffer);
            m_Instance->referencedResources.push_back(desc.perOmmDescs);
            m_Instance->referencedResources.push_back(omm->dataBuffer);
        }

        commitBarriers();

        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS inputs = {};
        fillD3dOpacityMicromapDesc(inputs, desc);

        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO vmPreBuildInfo = {};

        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS prebuildParams;
        prebuildParams.version = NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER;
        prebuildParams.pDesc = &inputs;
        prebuildParams.pInfo = &vmPreBuildInfo;
        NvAPI_Status status = NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(m_Context.device5.Get(), &prebuildParams);
        assert(status == S_OK);
        if (status != S_OK)
            return;

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (vmPreBuildInfo.scratchDataSizeInBytes != 0)
        {
            if (!m_DxrScratchManager.suballocateBuffer(vmPreBuildInfo.scratchDataSizeInBytes, m_ActiveCommandList->commandList, nullptr, nullptr, nullptr,
                &scratchGpuVA, m_RecordingVersion, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
            {
                std::stringstream ss;
                ss << "Couldn't suballocate a scratch buffer for VM " << utils::DebugNameToString(omm->desc.debugName) << " build. "
                    "The build requires " << vmPreBuildInfo.scratchDataSizeInBytes << " bytes of scratch space.";

                m_Context.error(ss.str());
                return;
            }
        }

        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC nativeDesc = {};
        nativeDesc.destOpacityMicromapArrayData = omm->getDeviceAddress();
        nativeDesc.inputs = inputs;
        nativeDesc.scratchOpacityMicromapArrayData = scratchGpuVA;

        NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS params;
        params.version = NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS_VER;
        params.pDesc = &nativeDesc;
        params.numPostbuildInfoDescs = 0;
        params.pPostbuildInfoDescs = nullptr;

        status = NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(m_ActiveCommandList->commandList4, &params);
        assert(status == S_OK);
#else
        utils::NotSupported();
#endif
    }

    void CommandList::buildBottomLevelAccelStruct(rt::IAccelStruct* _as, const rt::GeometryDesc* pGeometries, size_t numGeometries, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        const bool performUpdate = (buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
        }

        for (uint32_t i = 0; i < numGeometries; i++)
        {
            const auto& geometryDesc = pGeometries[i];
            if (geometryDesc.geometryType == rt::GeometryType::Triangles)
            {
                const auto& triangles = geometryDesc.geometryData.triangles;

                OpacityMicromap* om = triangles.opacityMicromap ? checked_cast<OpacityMicromap*>(triangles.opacityMicromap) : nullptr;

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(triangles.indexBuffer, ResourceStates::AccelStructBuildInput);
                    requireBufferState(triangles.vertexBuffer, ResourceStates::AccelStructBuildInput);
                    if (om)
                        requireBufferState(om->dataBuffer, ResourceStates::AccelStructBuildInput);
                    if (triangles.ommIndexBuffer)
                        requireBufferState(triangles.ommIndexBuffer, ResourceStates::AccelStructBuildInput);
                }

                m_Instance->referencedResources.push_back(triangles.indexBuffer);
                m_Instance->referencedResources.push_back(triangles.vertexBuffer);
                if (om && om->desc.trackLiveness)
                    m_Instance->referencedResources.push_back(om);
                if (triangles.ommIndexBuffer)
                    m_Instance->referencedResources.push_back(triangles.ommIndexBuffer);
            }
            else
            {
                const auto& aabbs = geometryDesc.geometryData.aabbs;

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(aabbs.buffer, ResourceStates::AccelStructBuildInput);
                }

                m_Instance->referencedResources.push_back(aabbs.buffer);
            }
        }

        commitBarriers();

        D3D12BuildRaytracingAccelerationStructureInputs inputs;
        inputs.SetType(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL);
        if (as->allowUpdate)
            inputs.SetFlags((D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)buildFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE);
        else
            inputs.SetFlags((D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)buildFlags);

        inputs.SetGeometryDescCount((UINT)numGeometries);
        for (uint32_t i = 0; i < numGeometries; i++)
        {
            const auto& geometryDesc = pGeometries[i];

            D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
            if (geometryDesc.useTransform)
            {
                void* cpuVA = nullptr;
                if (!m_UploadManager.suballocateBuffer(sizeof(rt::AffineTransform), nullptr, nullptr, nullptr,
                    &cpuVA, &gpuVA, m_RecordingVersion, D3D12_RAYTRACING_TRANSFORM3X4_BYTE_ALIGNMENT))
                {
                    m_Context.error("Couldn't suballocate an upload buffer");
                    return;
                }

                memcpy(cpuVA, &geometryDesc.transform, sizeof(rt::AffineTransform));
            }

            D3D12RaytracingGeometryDesc& geomDesc = inputs.GetGeometryDesc(i);
            fillD3dGeometryDesc(geomDesc, geometryDesc, gpuVA);
        }

#ifdef NVRHI_WITH_RTXMU
        std::vector<uint64_t> accelStructsToBuild;
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> buildInputs;
        buildInputs.push_back(inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>());

        if(as->rtxmuId == ~0ull)
        {
            m_Context.rtxMemUtil->PopulateBuildCommandList(m_ActiveCommandList->commandList4.Get(),
                                                           buildInputs.data(),
                                                           buildInputs.size(),
                                                           accelStructsToBuild);

            as->rtxmuId = accelStructsToBuild[0];

            as->rtxmuGpuVA = m_Context.rtxMemUtil->GetAccelStructGPUVA(as->rtxmuId);

            m_Instance->rtxmuBuildIds.push_back(as->rtxmuId);

        }
        else
        {
            std::vector<uint64_t> buildsToUpdate(1, as->rtxmuId);

            m_Context.rtxMemUtil->PopulateUpdateCommandList(m_ActiveCommandList->commandList4.Get(),
                                                            buildInputs.data(),
                                                            uint32_t(buildInputs.size()),
                                                            buildsToUpdate);
        }
#else
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};

        if (!checked_cast<d3d12::Device*>(m_Device)->GetAccelStructPreBuildInfo(ASPreBuildInfo, as->getDesc()))
            return;

        if (ASPreBuildInfo.ResultDataMaxSizeInBytes > as->dataBuffer->desc.byteSize)
        {
            std::stringstream ss;
            ss << "BLAS " << utils::DebugNameToString(as->desc.debugName) << " build requires at least "
                << ASPreBuildInfo.ResultDataMaxSizeInBytes << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->desc.byteSize << " bytes";

            m_Context.error(ss.str());
            return;
        }

        uint64_t scratchSize = performUpdate
            ? ASPreBuildInfo.UpdateScratchDataSizeInBytes
            : ASPreBuildInfo.ScratchDataSizeInBytes;

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (!m_DxrScratchManager.suballocateBuffer(scratchSize, m_ActiveCommandList->commandList, nullptr, nullptr, nullptr,
            &scratchGpuVA, m_RecordingVersion, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for BLAS " << utils::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            m_Context.error(ss.str());
            return;
        }

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
        }
        commitBarriers();

#if NVRHI_WITH_NVAPI_OPACITY_MICROMAP
        if (checked_cast<d3d12::Device*>(m_Device)->GetNvapiIsInitialized())
        {
            NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX buildDesc = {};
            buildDesc.inputs = inputs.GetAs<NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX>();
            buildDesc.scratchAccelerationStructureData = scratchGpuVA;
            buildDesc.destAccelerationStructureData = as->dataBuffer->gpuVA;
            buildDesc.sourceAccelerationStructureData = performUpdate ? as->dataBuffer->gpuVA : 0;

            NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS params = {};
            params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
            params.pDesc = &buildDesc;
            params.numPostbuildInfoDescs = 0;
            params.pPostbuildInfoDescs = nullptr;
            [[maybe_unused]] NvAPI_Status status = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(m_ActiveCommandList->commandList4, &params);
            assert(status == S_OK);
        }
        else
#endif
        {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
            buildDesc.Inputs = inputs.GetAs<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS>();
            buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
            buildDesc.DestAccelerationStructureData = as->dataBuffer->gpuVA;
            buildDesc.SourceAccelerationStructureData = performUpdate ? as->dataBuffer->gpuVA : 0;
            m_ActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        }
#endif // NVRHI_WITH_RTXMU

        if (as->desc.trackLiveness)
            m_Instance->referencedResources.push_back(as);
    }

    void CommandList::compactBottomLevelAccelStructs()
    {
#ifdef NVRHI_WITH_RTXMU

        if (!m_Resources.asBuildsCompleted.empty())
        {
            std::lock_guard lockGuard(m_Resources.asListMutex);

            if (!m_Resources.asBuildsCompleted.empty())
            {
                m_Context.rtxMemUtil->PopulateCompactionCommandList(m_ActiveCommandList->commandList4.Get(), m_Resources.asBuildsCompleted);

                m_Instance->rtxmuCompactionIds.insert(m_Instance->rtxmuCompactionIds.end(), m_Resources.asBuildsCompleted.begin(), m_Resources.asBuildsCompleted.end());

                m_Resources.asBuildsCompleted.clear();
            }
        }
#endif
    }

    void CommandList::buildTopLevelAccelStructInternal(AccelStruct* as, D3D12_GPU_VIRTUAL_ADDRESS instanceData, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        // Remove the internal flag
        buildFlags = buildFlags & ~rt::AccelStructBuildFlags::AllowEmptyInstances;

        const bool performUpdate = (buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0;

        if (performUpdate)
        {
            assert(as->allowUpdate);
            assert(as->dxrInstances.size() == numInstances); // DXR doesn't allow updating to a different instance count
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs;
        ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        ASInputs.InstanceDescs = instanceData;
        ASInputs.NumDescs = UINT(numInstances);
        ASInputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)buildFlags;
        if (as->allowUpdate)
            ASInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
        m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

        if (ASPreBuildInfo.ResultDataMaxSizeInBytes > as->dataBuffer->desc.byteSize)
        {
            std::stringstream ss;
            ss << "TLAS " << utils::DebugNameToString(as->desc.debugName) << " build requires at least "
                << ASPreBuildInfo.ResultDataMaxSizeInBytes << " bytes in the data buffer, while the allocated buffer is only "
                << as->dataBuffer->desc.byteSize << " bytes";

            m_Context.error(ss.str());
            return;
        }

        uint64_t scratchSize = performUpdate
            ? ASPreBuildInfo.UpdateScratchDataSizeInBytes
            : ASPreBuildInfo.ScratchDataSizeInBytes;

        D3D12_GPU_VIRTUAL_ADDRESS scratchGpuVA = 0;
        if (!m_DxrScratchManager.suballocateBuffer(scratchSize, m_ActiveCommandList->commandList, nullptr, nullptr, nullptr,
            &scratchGpuVA, m_RecordingVersion, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT))
        {
            std::stringstream ss;
            ss << "Couldn't suballocate a scratch buffer for TLAS " << utils::DebugNameToString(as->desc.debugName) << " build. "
                "The build requires " << scratchSize << " bytes of scratch space.";

            m_Context.error(ss.str());
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = ASInputs;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = as->dataBuffer->gpuVA;
        buildDesc.SourceAccelerationStructureData = performUpdate ? as->dataBuffer->gpuVA : 0;

        m_ActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }

    void CommandList::buildTopLevelAccelStruct(rt::IAccelStruct* _as, const rt::InstanceDesc* pInstances, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);
        
        as->bottomLevelASes.clear();

        // Keep the dxrInstances array in the AS object to avoid reallocating it on the next update
        as->dxrInstances.resize(numInstances);

        // Construct the instance array in a local vector first and then copy it over
        // because doing it in GPU memory over PCIe is much slower.
        for (uint32_t i = 0; i < numInstances; i++)
        {
            const rt::InstanceDesc& instance = pInstances[i];
            D3D12_RAYTRACING_INSTANCE_DESC& dxrInstance = as->dxrInstances[i];

            if (instance.bottomLevelAS)
            {
                AccelStruct* blas = checked_cast<AccelStruct*>(instance.bottomLevelAS);

                if (blas->desc.trackLiveness)
                    as->bottomLevelASes.push_back(blas);

                static_assert(sizeof(dxrInstance) == sizeof(instance));
                memcpy(&dxrInstance, &instance, sizeof(instance));

#ifdef NVRHI_WITH_RTXMU
                dxrInstance.AccelerationStructure = m_Context.rtxMemUtil->GetAccelStructGPUVA(blas->rtxmuId);
#else
                dxrInstance.AccelerationStructure = blas->dataBuffer->gpuVA;

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(blas->dataBuffer, nvrhi::ResourceStates::AccelStructBuildBlas);
                }
#endif
            }
            else // !instance.bottomLevelAS
            {
                dxrInstance.AccelerationStructure = 0;
            }
        }

#ifdef NVRHI_WITH_RTXMU
        m_Context.rtxMemUtil->PopulateUAVBarriersCommandList(m_ActiveCommandList->commandList4, m_Instance->rtxmuBuildIds);
#endif

        // Copy the instance array to the GPU
        D3D12_RAYTRACING_INSTANCE_DESC* cpuVA = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
        size_t uploadSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * as->dxrInstances.size();
        if (!m_UploadManager.suballocateBuffer(uploadSize, nullptr, nullptr, nullptr, (void**)&cpuVA, &gpuVA,
            m_RecordingVersion, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
        {
            m_Context.error("Couldn't suballocate an upload buffer");
            return;
        }

        memcpy(cpuVA, as->dxrInstances.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * as->dxrInstances.size());

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
        }
        commitBarriers();

        buildTopLevelAccelStructInternal(as, gpuVA, numInstances, buildFlags);

        if (as->desc.trackLiveness)
            m_Instance->referencedResources.push_back(as);
    }

    void CommandList::buildTopLevelAccelStructFromBuffer(rt::IAccelStruct* _as, nvrhi::IBuffer* instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);
        
        as->bottomLevelASes.clear();
        as->dxrInstances.clear();

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
            requireBufferState(instanceBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        }
        commitBarriers();

        buildTopLevelAccelStructInternal(as, getBufferGpuVA(instanceBuffer) + instanceBufferOffset, numInstances, buildFlags);

        if (as->desc.trackLiveness)
            m_Instance->referencedResources.push_back(as);
    }
} // namespace nvrhi::d3d12
