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

#include <nvrhi/utils.h>
#include <sstream>
#include <iomanip>

namespace nvrhi::d3d11
{
    void Context::error(const std::string& message) const
    {
        messageCallback->message(MessageSeverity::Error, message.c_str());
    }

    void SetDebugName(ID3D11DeviceChild* pObject, const char* name)
    {
        D3D_SET_OBJECT_NAME_N_A(pObject, UINT(strlen(name)), name);
    }

    DeviceHandle createDevice(const DeviceDesc& desc)
    {
        Device* device = new Device(desc);
        return DeviceHandle::Create(device);
    }

    Device::Device(const DeviceDesc& desc)
    {
        m_Context.messageCallback = desc.messageCallback;
        m_Context.immediateContext = desc.context;
        m_Context.immediateContext->QueryInterface(IID_PPV_ARGS(&m_Context.immediateContext1));
        desc.context->GetDevice(&m_Context.device);

#if NVRHI_D3D11_WITH_NVAPI
        m_Context.nvapiAvailable = NvAPI_Initialize() == NVAPI_OK;

        if (m_Context.nvapiAvailable)
        {
            NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams{};
            stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;

            if (NvAPI_D3D_QuerySinglePassStereoSupport(m_Context.device, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
            {
                m_SinglePassStereoSupported = true;
            }

            // There is no query for FastGS, so query support for FP16 atomics as a proxy.
            // Both features were introduced in the same architecture (Maxwell).
            bool supported = false;
            if (NvAPI_D3D11_IsNvShaderExtnOpCodeSupported(m_Context.device, NV_EXTN_OP_FP16_ATOMIC, &supported) == NVAPI_OK && supported)
            {
                m_FastGeometryShaderSupported = true;
            }
        }
#endif

#if NVRHI_WITH_AFTERMATH
        if (desc.aftermathEnabled)
        {
            auto CheckAftermathResult = [this](GFSDK_Aftermath_Result result)
            {
                if (!GFSDK_Aftermath_SUCCEED(result))
                {
                    std::stringstream ss;
                    ss << "Aftermath initialize call failed, result = 0x" << std::hex << std::setw(8) << result;
                    m_Context.error(ss.str());
                    return false;
                }
                return true;
            };
            const uint32_t aftermathFlags = GFSDK_Aftermath_FeatureFlags_EnableMarkers | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking | GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo | GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;
            bool success = CheckAftermathResult(GFSDK_Aftermath_DX11_Initialize(GFSDK_Aftermath_Version_API, aftermathFlags, m_Context.device));
            if (success)
                success = CheckAftermathResult(GFSDK_Aftermath_DX11_CreateContextHandle(m_Context.immediateContext, &m_Context.aftermathContext));
            if (success)
                m_AftermathEnabled = true;
        }
#endif

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = c_MaxPushConstantSize;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.CPUAccessFlags = 0;
        const HRESULT res = m_Context.device->CreateBuffer(&bufferDesc, nullptr, &m_Context.pushConstantBuffer);

        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateBuffer call failed for the push constants buffer, HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
        }

        m_ImmediateCommandList = CommandListHandle::Create(new CommandList(m_Context, this, CommandListParameters()));   
    }

    Device::~Device()
    {
        // Release the command list so that it unregisters the Aftermath marker tracker before the device is destroyed
        m_ImmediateCommandList = nullptr;

#if NVRHI_WITH_AFTERMATH
        if (m_Context.aftermathContext)
        {
            GFSDK_Aftermath_ReleaseContextHandle(m_Context.aftermathContext);
            m_Context.aftermathContext = nullptr;
        }
        m_AftermathEnabled = false;
#endif
    }

    GraphicsAPI Device::getGraphicsAPI()
    {
        return GraphicsAPI::D3D11;
    }

    Object Device::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Device:
            return Object(m_Context.device);
        case ObjectTypes::D3D11_DeviceContext:
            return Object(m_Context.immediateContext);
        case ObjectTypes::Nvrhi_D3D11_Device:
            return this;
        default:
            return nullptr;
        }
    }

    HeapHandle Device::createHeap(const HeapDesc&)
    {
        utils::NotSupported();
        return nullptr;
    }

    CommandListHandle Device::createCommandList(const CommandListParameters& params)
    {
        if (!params.enableImmediateExecution)
        {
            m_Context.error("Deferred command lists are not supported by the D3D11 backend.");
            return nullptr;
        }

        if (params.queueType != CommandQueue::Graphics)
        {
            m_Context.error("Non-graphics queues are not supported by the D3D11 backend.");
            return nullptr;
        }
        
        return m_ImmediateCommandList;
    }

    void Device::getTextureTiling(ITexture* texture, uint32_t* numTiles, PackedMipDesc* desc, TileShape* tileShape, uint32_t* subresourceTilingsNum, SubresourceTiling* subresourceTilings)
    {
        (void)texture;
        (void)numTiles;
        (void)desc;
        (void)tileShape;
        (void)subresourceTilingsNum;
        (void)subresourceTilings;

        utils::NotSupported();
    }

    void Device::updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, uint32_t numTileMappings, CommandQueue executionQueue)
    {
        (void)texture;
        (void)tileMappings;
        (void)numTileMappings;
        (void)executionQueue;

        utils::NotSupported();
    }

    bool Device::queryFeatureSupport(Feature feature, void* pInfo, size_t infoSize)
    {
        (void)pInfo;
        (void)infoSize;

        switch (feature)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case Feature::DeferredCommandLists:
            return false;
        case Feature::SinglePassStereo:
            return m_SinglePassStereoSupported;
        case Feature::FastGeometryShader:
            return m_FastGeometryShaderSupported;
        case Feature::ConservativeRasterization:
#if NVRHI_D3D11_WITH_NVAPI
            return true;
#else
            return false;
#endif
        case Feature::ConstantBufferRanges:
            return m_Context.immediateContext1 != nullptr;
        default:
            return false;
        }
    }

    FormatSupport Device::queryFormatSupport(Format format)
    {
        const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(format);

        FormatSupport result = FormatSupport::None;

        UINT flags = 0;
        m_Context.device->CheckFormatSupport(formatMapping.rtvFormat, &flags);

        if (flags & D3D11_FORMAT_SUPPORT_BUFFER)
            result = result | FormatSupport::Buffer;
        if (flags & (D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_TEXTURECUBE))
            result = result | FormatSupport::Texture;
        if (flags & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
            result = result | FormatSupport::DepthStencil;
        if (flags & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
            result = result | FormatSupport::RenderTarget;
        if (flags & D3D11_FORMAT_SUPPORT_BLENDABLE)
            result = result | FormatSupport::Blendable;

        if (formatMapping.srvFormat != formatMapping.rtvFormat)
        {
            flags = 0;
            m_Context.device->CheckFormatSupport(formatMapping.srvFormat, &flags);
        }

        if (flags & D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER)
            result = result | FormatSupport::IndexBuffer;
        if (flags & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER)
            result = result | FormatSupport::VertexBuffer;
        if (flags & D3D11_FORMAT_SUPPORT_SHADER_LOAD)
            result = result | FormatSupport::ShaderLoad;
        if (flags & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)
            result = result | FormatSupport::ShaderSample;

        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 featureData = {};
        featureData.InFormat = formatMapping.srvFormat;
        
        m_Context.device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &featureData, sizeof(featureData));

        if (featureData.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD)
            result = result | FormatSupport::ShaderAtomic;
        if (featureData.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)
            result = result | FormatSupport::ShaderUavLoad;
        if (featureData.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE)
            result = result | FormatSupport::ShaderUavStore;
        
        return result;
    }

    rt::PipelineHandle Device::createRayTracingPipeline(const rt::PipelineDesc&)
    {
        return nullptr;
    }

    rt::OpacityMicromapHandle Device::createOpacityMicromap(const rt::OpacityMicromapDesc& )
    {
        utils::NotSupported();
        return nullptr;
    }
    
    rt::AccelStructHandle Device::createAccelStruct(const rt::AccelStructDesc&)
    {
        return nullptr;
    }

    MemoryRequirements Device::getAccelStructMemoryRequirements(rt::IAccelStruct*)
    {
        utils::NotSupported();
        return MemoryRequirements();
    }

    rt::cluster::OperationSizeInfo Device::getClusterOperationSizeInfo(const rt::cluster::OperationParams&)
    {
        utils::NotSupported();
        return rt::cluster::OperationSizeInfo();
    }

    bool Device::bindAccelStructMemory(rt::IAccelStruct*, IHeap*, uint64_t)
    {
        utils::NotSupported();
        return false;
    }

    MeshletPipelineHandle Device::createMeshletPipeline(const MeshletPipelineDesc&, IFramebuffer*)
    {
        return nullptr;
    }

    bool Device::waitForIdle()
    {
        if (!m_WaitForIdleQuery)
        {
            m_WaitForIdleQuery = createEventQuery();
        }

        if (!m_WaitForIdleQuery)
            return false;

        setEventQuery(m_WaitForIdleQuery, CommandQueue::Graphics);
        waitEventQuery(m_WaitForIdleQuery);
        resetEventQuery(m_WaitForIdleQuery);
        return true;
    }

    SamplerHandle Device::createSampler(const SamplerDesc& d)
    {
        D3D11_SAMPLER_DESC desc11;

        UINT reductionType = convertSamplerReductionType(d.reductionType);

        if (d.maxAnisotropy > 1.0f)
        {
            desc11.Filter = D3D11_ENCODE_ANISOTROPIC_FILTER(reductionType);
        }
        else
        {
            desc11.Filter = D3D11_ENCODE_BASIC_FILTER(
                d.minFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                d.magFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                d.mipFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                reductionType);
        }

        desc11.AddressU = convertSamplerAddressMode(d.addressU);
        desc11.AddressV = convertSamplerAddressMode(d.addressV);
        desc11.AddressW = convertSamplerAddressMode(d.addressW);

        desc11.MipLODBias = d.mipBias;
        desc11.MaxAnisotropy = std::max((UINT)d.maxAnisotropy, 1U);
        desc11.ComparisonFunc = D3D11_COMPARISON_LESS;
        desc11.BorderColor[0] = d.borderColor.r;
        desc11.BorderColor[1] = d.borderColor.g;
        desc11.BorderColor[2] = d.borderColor.b;
        desc11.BorderColor[3] = d.borderColor.a;
        desc11.MinLOD = 0;
        desc11.MaxLOD = D3D11_FLOAT32_MAX;

        RefCountPtr<ID3D11SamplerState> sState;
        const HRESULT res = m_Context.device->CreateSamplerState(&desc11, &sState);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateSamplerState call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
            return nullptr;
        }

        Sampler* sampler = new Sampler();
        sampler->sampler = sState;
        sampler->desc = d;
        return SamplerHandle::Create(sampler);
    }

} // namespace nvrhi::d3d11
