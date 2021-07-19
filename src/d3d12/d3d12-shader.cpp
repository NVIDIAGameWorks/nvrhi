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

#if NVRHI_D3D12_WITH_NVAPI
    static bool convertCustomSemantics(uint32_t numSemantics, const CustomSemantic* semantics, std::vector<NV_CUSTOM_SEMANTIC>& output)
    {
        output.resize(numSemantics);
        for (uint32_t i = 0; i < numSemantics; i++)
        {
            const CustomSemantic& src = semantics[i];
            NV_CUSTOM_SEMANTIC& dst = output[i];

            dst.version = NV_CUSTOM_SEMANTIC_VERSION;
            dst.RegisterMask = 0;
            dst.RegisterNum = 0;
            dst.RegisterSpecified = FALSE;
            dst.Reserved = 0;

            strncpy_s(dst.NVCustomSemanticNameString, src.name.c_str(), src.name.size());

            switch (src.type)
            {
            case CustomSemantic::XRight:
                dst.NVCustomSemanticType = NV_X_RIGHT_SEMANTIC;
                break;

            case CustomSemantic::ViewportMask:
                dst.NVCustomSemanticType = NV_VIEWPORT_MASK_SEMANTIC;
                break;

            case CustomSemantic::Undefined:
            default:
                utils::InvalidEnum();
                return false;
            }
        }

        return true;
    }
#endif

    ShaderHandle Device::createShader(const ShaderDesc & d, const void * binary, const size_t binarySize)
    {
        if (binarySize == 0)
            return nullptr;

        Shader* shader = new Shader();
        shader->bytecode.resize(binarySize);
        shader->desc = d;
        memcpy(&shader->bytecode[0], binary, binarySize);

#if NVRHI_D3D12_WITH_NVAPI
        // Save the custom semantics structure because it may be on the stack or otherwise dynamic.
        // Note that this has to be a deep copy; currently NV_CUSTOM_SEMANTIC has no pointers, but that might change.
        if (d.numCustomSemantics && d.pCustomSemantics)
        {
            convertCustomSemantics(d.numCustomSemantics, d.pCustomSemantics, shader->customSemantics);
        }

        // Save the coordinate swizzling patterns for the same reason
        if (d.pCoordinateSwizzling)
        {
            const uint32_t numSwizzles = 16;
            shader->coordinateSwizzling.resize(numSwizzles);
            memcpy(&shader->coordinateSwizzling[0], d.pCoordinateSwizzling, sizeof(uint32_t) * numSwizzles);
        }

        if (d.hlslExtensionsUAV >= 0)
        {
            NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC* pExtn = new NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC();
            memset(pExtn, 0, sizeof(*pExtn));
            pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
            pExtn->psoExtension = NV_PSO_SET_SHADER_EXTNENSION_SLOT_AND_SPACE;
            pExtn->version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;

            pExtn->uavSlot = d.hlslExtensionsUAV;
            pExtn->registerSpace = 0;

            shader->extensions.push_back(pExtn);
        }

        switch (d.shaderType)
        {
        case ShaderType::Vertex:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_VERTEX_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_VERTEX_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_VERTEX_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::Hull:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_HULL_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_HULL_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_HULL_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::Domain:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_DOMAIN_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_DOMAIN_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_DOMAIN_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::Geometry:
            if ((d.fastGSFlags & FastGeometryShaderFlags::ForceFastGS) != 0 || d.numCustomSemantics || d.pCoordinateSwizzling)
            {
                NVAPI_D3D12_PSO_GEOMETRY_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_GEOMETRY_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_GEOMETRY_SHADER_EXTENSION;
                pExtn->version = NV_GEOMETRY_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = d.numCustomSemantics ? &shader->customSemantics[0] : nullptr;
                pExtn->UseCoordinateSwizzle = d.pCoordinateSwizzling != nullptr;
                pExtn->pCoordinateSwizzling = d.pCoordinateSwizzling != nullptr ? &shader->coordinateSwizzling[0] : nullptr;
                pExtn->ForceFastGS = (d.fastGSFlags & FastGeometryShaderFlags::ForceFastGS) != 0;
                pExtn->UseViewportMask = (d.fastGSFlags & FastGeometryShaderFlags::UseViewportMask) != 0;
                pExtn->OffsetRtIndexByVpIndex = (d.fastGSFlags & FastGeometryShaderFlags::OffsetTargetIndexByViewportIndex) != 0;
                pExtn->DontUseViewportOrder = (d.fastGSFlags & FastGeometryShaderFlags::StrictApiOrder) != 0;
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;
                pExtn->UseAttributeSkipMask = false;

                shader->extensions.push_back(pExtn);
            }
            break;

        case ShaderType::Compute:
        case ShaderType::Pixel:
        case ShaderType::Amplification:
        case ShaderType::Mesh:
        case ShaderType::AllGraphics:
        case ShaderType::RayGeneration:
        case ShaderType::Miss:
        case ShaderType::ClosestHit:
        case ShaderType::AnyHit:
        case ShaderType::Intersection:
            if (d.numCustomSemantics)
            {
                utils::NotSupported();
                return nullptr;
            }
            break;

        case ShaderType::None:
        case ShaderType::AllRayTracing:
        case ShaderType::All:
        default:
            utils::InvalidEnum();
            return nullptr;
        }
#else
        if (d.numCustomSemantics || d.pCoordinateSwizzling || (d.fastGSFlags != 0) || d.hlslExtensionsUAV >= 0)
        {
            utils::NotSupported();
            delete shader;

            // NVAPI is unavailable
            return nullptr;
        }
#endif
        
        return ShaderHandle::Create(shader);
    }
    
    ShaderHandle Device::createShaderSpecialization(IShader*, const ShaderSpecialization*, uint32_t)
    {
        utils::NotSupported();
        return nullptr;
    }

    nvrhi::ShaderLibraryHandle Device::createShaderLibrary(const void* binary, const size_t binarySize)
    {
        ShaderLibrary* shaderLibrary = new ShaderLibrary();

        shaderLibrary->bytecode.resize(binarySize);
        memcpy(&shaderLibrary->bytecode[0], binary, binarySize);

        return ShaderLibraryHandle::Create(shaderLibrary);
    }
    
    InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc * d, uint32_t attributeCount, IShader* vertexShader)
    {
        // The shader is not needed here, there are no separate IL objects in DX12
        (void)vertexShader;

        InputLayout* layout = new InputLayout();
        layout->attributes.resize(attributeCount);

        for (uint32_t index = 0; index < attributeCount; index++)
        {
            VertexAttributeDesc& attr = layout->attributes[index];

            // Copy the description to get a stable name pointer in desc
            attr = d[index];

            assert(attr.arraySize > 0);

            const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(attr.format);
            const FormatInfo& formatInfo = getFormatInfo(attr.format);

            for (uint32_t semanticIndex = 0; semanticIndex < attr.arraySize; semanticIndex++)
            {
                D3D12_INPUT_ELEMENT_DESC desc;

                desc.SemanticName = attr.name.c_str();
                desc.AlignedByteOffset = attr.offset + semanticIndex * formatInfo.bytesPerBlock;
                desc.Format = formatMapping.srvFormat;
                desc.InputSlot = attr.bufferIndex;
                desc.SemanticIndex = semanticIndex;

                if (attr.isInstanced)
                {
                    desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                    desc.InstanceDataStepRate = 1;
                }
                else
                {
                    desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    desc.InstanceDataStepRate = 0;
                }

                layout->inputElements.push_back(desc);
            }

            if (layout->elementStrides.find(attr.bufferIndex) == layout->elementStrides.end())
            {
                layout->elementStrides[attr.bufferIndex] = attr.elementStride;
            } else {
                assert(layout->elementStrides[attr.bufferIndex] == attr.elementStride);
            }
        }

        return InputLayoutHandle::Create(layout);
    }

    uint32_t InputLayout::getNumAttributes() const
    {
        return uint32_t(attributes.size());
    }

    const VertexAttributeDesc* InputLayout::getAttributeDesc(uint32_t index) const
    {
        if (index < uint32_t(attributes.size())) return &attributes[index];
        else return nullptr;
    }

    void Shader::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode) *ppBytecode = bytecode.data();
        if (pSize) *pSize = bytecode.size();
    }

    void ShaderLibraryEntry::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        library->getBytecode(ppBytecode, pSize);
    }

    void ShaderLibrary::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode) *ppBytecode = bytecode.data();
        if (pSize) *pSize = bytecode.size();
    }

    ShaderHandle ShaderLibrary::getShader(const char* entryName, ShaderType shaderType)
    {
        return ShaderHandle::Create(new ShaderLibraryEntry(this, entryName, shaderType));
    }
} // namespace nvrhi::d3d12
