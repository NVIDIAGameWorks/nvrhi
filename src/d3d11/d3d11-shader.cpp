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

#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>
#include <sstream>
#include <iomanip>


namespace nvrhi::d3d11
{
    void Shader::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode) *ppBytecode = bytecode.data();
        if (pSize) *pSize = bytecode.size();
    }
    
    const VertexAttributeDesc* InputLayout::getAttributeDesc(uint32_t index) const
    {
        if (index < uint32_t(attributes.size()))
            return &attributes[index];

        return nullptr;
    }

#if NVRHI_D3D11_WITH_NVAPI
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
                utils::InvalidEnum();
                break;

            default:
                utils::InvalidEnum();
                return false;
            }
        }

        return true;
    }
#endif

    static void createShaderFailed(const char* function, const HRESULT res, const ShaderDesc& d, const Context& context)
    {
        std::stringstream ss;
        ss << function << " call failed for shader " << utils::DebugNameToString(d.debugName)
            << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
        context.error(ss.str());
    }

    ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, const size_t binarySize)
    {
        // Attach a RefCountPtr right away so that it's destroyed on an error exit
        RefCountPtr<Shader> shader = RefCountPtr<Shader>::Create(new Shader());

        switch (d.shaderType)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ShaderType::Vertex:
        {
            // Save the bytecode for potential input layout creation later
            shader->bytecode.resize(binarySize);
            memcpy(shader->bytecode.data(), binary, binarySize);

            if (d.numCustomSemantics == 0)
            {
                const HRESULT res = m_Context.device->CreateVertexShader(binary, binarySize, nullptr, &shader->VS);
                if (FAILED(res))
                {
                    createShaderFailed("CreateVertexShader", res, d, m_Context);
                    return nullptr;
                }
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                std::vector<NV_CUSTOM_SEMANTIC> nvapiSemantics;
                convertCustomSemantics(d.numCustomSemantics, d.pCustomSemantics, nvapiSemantics);

                NvAPI_D3D11_CREATE_VERTEX_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEVERTEXSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = nvapiSemantics.data();
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateVertexShaderEx(m_Context.device, binary, binarySize, nullptr, &Args, &shader->VS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::Hull:
        {
            if (d.numCustomSemantics == 0)
            {
                const HRESULT res = m_Context.device->CreateHullShader(binary, binarySize, nullptr, &shader->HS);
                if (FAILED(res))
                {
                    createShaderFailed("CreateHullShader", res, d, m_Context);
                    return nullptr;
                }
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                std::vector<NV_CUSTOM_SEMANTIC> nvapiSemantics;
                convertCustomSemantics(d.numCustomSemantics, d.pCustomSemantics, nvapiSemantics);

                NvAPI_D3D11_CREATE_HULL_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEHULLSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = nvapiSemantics.data();
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateHullShaderEx(m_Context.device, binary, binarySize, nullptr, &Args, &shader->HS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::Domain:
        {
            if (d.numCustomSemantics == 0)
            {
                const HRESULT res = m_Context.device->CreateDomainShader(binary, binarySize, nullptr, &shader->DS);
                if (FAILED(res))
                {
                    createShaderFailed("CreateDomainShader", res, d, m_Context);
                    return nullptr;
                }
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                std::vector<NV_CUSTOM_SEMANTIC> nvapiSemantics;
                convertCustomSemantics(d.numCustomSemantics, d.pCustomSemantics, nvapiSemantics);

                NvAPI_D3D11_CREATE_DOMAIN_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEDOMAINSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = nvapiSemantics.data();
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateDomainShaderEx(m_Context.device, binary, binarySize, nullptr, &Args, &shader->DS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::Geometry:
        {
            if (d.numCustomSemantics == 0 && uint32_t(d.fastGSFlags) == 0 && d.pCoordinateSwizzling == nullptr)
            {
                const HRESULT res = m_Context.device->CreateGeometryShader(binary, binarySize, nullptr, &shader->GS);
                if (FAILED(res))
                {
                    createShaderFailed("CreateGeometryShader", res, d, m_Context);
                    return nullptr;
                }
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI           
                std::vector<NV_CUSTOM_SEMANTIC> nvapiSemantics;
                convertCustomSemantics(d.numCustomSemantics, d.pCustomSemantics, nvapiSemantics);

                NvAPI_D3D11_CREATE_GEOMETRY_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEGEOMETRYSHADEREX_2_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = nvapiSemantics.data();
                Args.UseCoordinateSwizzle = d.pCoordinateSwizzling != nullptr;
                Args.pCoordinateSwizzling = d.pCoordinateSwizzling;
                Args.ForceFastGS = (d.fastGSFlags & FastGeometryShaderFlags::ForceFastGS) != 0;
                Args.UseViewportMask = (d.fastGSFlags & FastGeometryShaderFlags::UseViewportMask) != 0;
                Args.OffsetRtIndexByVpIndex = (d.fastGSFlags & FastGeometryShaderFlags::OffsetTargetIndexByViewportIndex) != 0;
                Args.DontUseViewportOrder = (d.fastGSFlags & FastGeometryShaderFlags::StrictApiOrder) != 0;
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateGeometryShaderEx_2(m_Context.device, binary, binarySize, nullptr, &Args, &shader->GS) != NVAPI_OK)
                    return nullptr;
#else
                 return nullptr;
#endif
            }
        }
        break;
        case ShaderType::Pixel:
        {
            if (d.hlslExtensionsUAV >= 0)
            {
#if NVRHI_D3D11_WITH_NVAPI
                if (NvAPI_D3D11_SetNvShaderExtnSlot(m_Context.device, d.hlslExtensionsUAV) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }

            const HRESULT res = m_Context.device->CreatePixelShader(binary, binarySize, nullptr, &shader->PS);
            if (FAILED(res))
            {
                createShaderFailed("CreatePixelShader", res, d, m_Context);
                return nullptr;
            }

#if NVRHI_D3D11_WITH_NVAPI
            if (d.hlslExtensionsUAV >= 0)
            {
                NvAPI_D3D11_SetNvShaderExtnSlot(m_Context.device, ~0u);
            }
#endif
        }
        break;
        case ShaderType::Compute:
        {
            if (d.hlslExtensionsUAV >= 0)
            {
#if NVRHI_D3D11_WITH_NVAPI
                if (NvAPI_D3D11_SetNvShaderExtnSlot(m_Context.device, d.hlslExtensionsUAV) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }

            const HRESULT res = m_Context.device->CreateComputeShader(binary, binarySize, nullptr, &shader->CS);
            if (FAILED(res))
            {
                createShaderFailed("CreateComputeShader", res, d, m_Context);
                return nullptr;
            }

#if NVRHI_D3D11_WITH_NVAPI
            if (d.hlslExtensionsUAV >= 0)
            {
                NvAPI_D3D11_SetNvShaderExtnSlot(m_Context.device, ~0u);
            }
#endif
        }
        break;

        default:
            m_Context.error("Unsupported shaderType provided to createShader");
            return nullptr;
        }

        shader->desc = d;
        return shader;  // NOLINT(clang-diagnostic-return-std-move-in-c++11)
    }
    
    ShaderHandle Device::createShaderSpecialization(IShader*, const ShaderSpecialization*, uint32_t)
    {
        utils::NotSupported();
        return nullptr;
    }

    InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* _vertexShader)
    {
        Shader* vertexShader = checked_cast<Shader*>(_vertexShader);

        if (vertexShader == nullptr)
        {
            m_Context.error("No vertex shader provided to createInputLayout");
            return nullptr;
        }

        if (vertexShader->desc.shaderType != ShaderType::Vertex)
        {
            m_Context.error("A non-vertex shader provided to createInputLayout");
            return nullptr;
        }

        InputLayout *inputLayout = new InputLayout();

        inputLayout->attributes.resize(attributeCount);

        static_vector<D3D11_INPUT_ELEMENT_DESC, c_MaxVertexAttributes> elementDesc;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            inputLayout->attributes[i] = d[i];

            assert(d[i].arraySize > 0);

            const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(d[i].format);
            const FormatInfo& formatInfo = getFormatInfo(d[i].format);

            for (uint32_t semanticIndex = 0; semanticIndex < d[i].arraySize; semanticIndex++)
            {
                D3D11_INPUT_ELEMENT_DESC desc;

                desc.SemanticName = d[i].name.c_str();
                desc.SemanticIndex = semanticIndex;
                desc.Format = formatMapping.srvFormat;
                desc.InputSlot = d[i].bufferIndex;
                desc.AlignedByteOffset = d[i].offset + semanticIndex * formatInfo.bytesPerBlock;
                desc.InputSlotClass = d[i].isInstanced ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                desc.InstanceDataStepRate = d[i].isInstanced ? 1 : 0;

                elementDesc.push_back(desc);
            }
        }

        const HRESULT res = m_Context.device->CreateInputLayout(elementDesc.data(), uint32_t(elementDesc.size()), vertexShader->bytecode.data(), vertexShader->bytecode.size(), &inputLayout->layout);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateInputLayout call failed for shader " << utils::DebugNameToString(vertexShader->desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
        }

        for(uint32_t i = 0; i < attributeCount; i++)
        {
            const auto index = d[i].bufferIndex;

            if (inputLayout->elementStrides.find(index) == inputLayout->elementStrides.end())
            {
                inputLayout->elementStrides[index] = d[i].elementStride;
            } else {
                assert(inputLayout->elementStrides[index] == d[i].elementStride);
            }
        }

        return InputLayoutHandle::Create(inputLayout);
    }

} // nanmespace nvrhi::d3d11