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

    static void fillD3dGeometryDesc(D3D12_RAYTRACING_GEOMETRY_DESC& outD3dGeometryDesc, const rt::GeometryDesc& geometryDesc)
    {
        if (geometryDesc.geometryType == rt::GeometryType::Triangles)
        {
            const auto& triangles = geometryDesc.geometryData.triangles;
            outD3dGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            outD3dGeometryDesc.Flags = (D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryDesc.flags;

            if (triangles.indexBuffer)
                outD3dGeometryDesc.Triangles.IndexBuffer = checked_cast<Buffer*>(triangles.indexBuffer)->gpuVA + triangles.indexOffset;
            else
                outD3dGeometryDesc.Triangles.IndexBuffer = 0;

            if (triangles.vertexBuffer)
                outD3dGeometryDesc.Triangles.VertexBuffer.StartAddress = checked_cast<Buffer*>(triangles.vertexBuffer)->gpuVA + triangles.vertexOffset;
            else
                outD3dGeometryDesc.Triangles.VertexBuffer.StartAddress = 0;

            outD3dGeometryDesc.Triangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
            outD3dGeometryDesc.Triangles.IndexFormat = getDxgiFormatMapping(triangles.indexFormat).srvFormat;
            outD3dGeometryDesc.Triangles.VertexFormat = getDxgiFormatMapping(triangles.vertexFormat).srvFormat;
            outD3dGeometryDesc.Triangles.IndexCount = triangles.indexCount;
            outD3dGeometryDesc.Triangles.VertexCount = triangles.vertexCount;
            outD3dGeometryDesc.Triangles.Transform3x4 = 0;
        }
        else
        {
            const auto& aabbs = geometryDesc.geometryData.aabbs;
            outD3dGeometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            outD3dGeometryDesc.Flags = (D3D12_RAYTRACING_GEOMETRY_FLAGS)geometryDesc.flags;

            if (aabbs.buffer)
                outD3dGeometryDesc.AABBs.AABBs.StartAddress = checked_cast<Buffer*>(aabbs.buffer)->gpuVA + aabbs.offset;
            else
                outD3dGeometryDesc.AABBs.AABBs.StartAddress = 0;

            outD3dGeometryDesc.AABBs.AABBs.StrideInBytes = aabbs.stride;
            outD3dGeometryDesc.AABBs.AABBCount = aabbs.count;
        }
    }


    rt::AccelStructHandle Device::createAccelStruct(const rt::AccelStructDesc& desc)
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3dGeometryDescs;
        d3dGeometryDescs.resize(desc.bottomLevelGeometries.size());

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs;
        if (desc.isTopLevel)
        {
            ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            ASInputs.InstanceDescs = 0;
            ASInputs.NumDescs = UINT(desc.topLevelMaxInstances);
        }
        else
        {
            for (uint32_t i = 0; i < desc.bottomLevelGeometries.size(); i++)
            {
                fillD3dGeometryDesc(d3dGeometryDescs[i], desc.bottomLevelGeometries[i]);
            }

            ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            ASInputs.pGeometryDescs = d3dGeometryDescs.data();
            ASInputs.NumDescs = UINT(d3dGeometryDescs.size());
        }

        ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        ASInputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)desc.buildFlags;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
        m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

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

        HRESULT hr = m_Context.device5->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&pso->pipelineState));
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

    void CommandList::buildBottomLevelAccelStruct(rt::IAccelStruct* _as, const rt::GeometryDesc* pGeometries, size_t numGeometries, rt::AccelStructBuildFlags buildFlags)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        const bool performUpdate = (buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0;
        if (performUpdate)
        {
            assert(as->allowUpdate);
        }

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d3dGeometryDescs;
        d3dGeometryDescs.resize(numGeometries);

        for (uint32_t i = 0; i < numGeometries; i++)
        {
            const auto& geometryDesc = pGeometries[i];
            auto& d3dGeometryDesc = d3dGeometryDescs[i];

            fillD3dGeometryDesc(d3dGeometryDesc, geometryDesc);

            if (geometryDesc.useTransform)
            {
                void* cpuVA = nullptr;
                D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
                if (!m_UploadManager.suballocateBuffer(sizeof(rt::AffineTransform), nullptr, nullptr, nullptr, 
                    &cpuVA, &gpuVA, m_RecordingVersion, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
                {
                    m_Context.error("Couldn't suballocate an upload buffer");
                    return;
                }

                memcpy(cpuVA, &geometryDesc.transform, sizeof(rt::AffineTransform));

                d3dGeometryDesc.Triangles.Transform3x4 = gpuVA;
            }

            if (geometryDesc.geometryType == rt::GeometryType::Triangles)
            {
                const auto& triangles = geometryDesc.geometryData.triangles;

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(triangles.indexBuffer, ResourceStates::AccelStructBuildInput);
                    requireBufferState(triangles.vertexBuffer, ResourceStates::AccelStructBuildInput);
                }

                m_Instance->referencedResources.push_back(triangles.indexBuffer);
                m_Instance->referencedResources.push_back(triangles.vertexBuffer);
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

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs;
        ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        ASInputs.pGeometryDescs = d3dGeometryDescs.data();
        ASInputs.NumDescs = UINT(d3dGeometryDescs.size());
        ASInputs.Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)buildFlags;
        if (as->allowUpdate)
            ASInputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

#ifdef NVRHI_WITH_RTXMU
        std::vector<uint64_t> accelStructsToBuild;
        std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> buildInputs;
        buildInputs.push_back(ASInputs);

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
        m_Context.device5->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

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

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = ASInputs;
        buildDesc.ScratchAccelerationStructureData = scratchGpuVA;
        buildDesc.DestAccelerationStructureData = as->dataBuffer->gpuVA;
        buildDesc.SourceAccelerationStructureData = performUpdate ? as->dataBuffer->gpuVA : 0;

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(as->dataBuffer, nvrhi::ResourceStates::AccelStructWrite);
        }
        commitBarriers();

        m_ActiveCommandList->commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

#endif

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

            AccelStruct* blas = checked_cast<AccelStruct*>(instance.bottomLevelAS);

            if (blas->desc.trackLiveness)
                as->bottomLevelASes.push_back(blas);

            static_assert(sizeof(dxrInstance) == sizeof(instance));
            memcpy(&dxrInstance, &instance, sizeof(instance));

#ifdef NVRHI_WITH_RTXMU
            dxrInstance.AccelerationStructure = m_Context.rtxMemUtil->GetAccelStructGPUVA(blas->rtxmuId);
#else
            dxrInstance.AccelerationStructure = blas->dataBuffer->gpuVA;
#endif

#ifndef NVRHI_WITH_RTXMU
            if (m_EnableAutomaticBarriers)
            {
                requireBufferState(blas->dataBuffer, nvrhi::ResourceStates::AccelStructBuildBlas);
            }
#endif
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
