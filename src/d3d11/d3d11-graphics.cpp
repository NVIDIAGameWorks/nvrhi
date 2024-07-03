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

    FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc)
    {
        Framebuffer *ret = new Framebuffer();
        ret->desc = desc;
        ret->framebufferInfo = FramebufferInfoEx(desc);
        
        for(auto colorAttachment : desc.colorAttachments)
        {
            assert(colorAttachment.valid());
            ret->RTVs.push_back(getRTVForAttachment(colorAttachment));
        }

        if (desc.depthAttachment.valid())
        {
            ret->DSV = getDSVForAttachment(desc.depthAttachment);
        }

        return FramebufferHandle::Create(ret);
    }
    
    GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb)
    {
        const RenderState& renderState = desc.renderState;

        if (desc.renderState.singlePassStereo.enabled && !m_SinglePassStereoSupported)
        {
            m_Context.error("Single-pass stereo is not supported by this device");
            return nullptr;
        }

        GraphicsPipeline *pso = new GraphicsPipeline();
        pso->desc = desc;
        pso->framebufferInfo = fb->getFramebufferInfo();

        pso->primitiveTopology = convertPrimType(desc.primType, desc.patchControlPoints);
        pso->inputLayout = checked_cast<InputLayout*>(desc.inputLayout.Get());

        pso->pRS = getRasterizerState(renderState.rasterState);
        pso->pBlendState = getBlendState(renderState.blendState);
        pso->pDepthStencilState = getDepthStencilState(renderState.depthStencilState);
        pso->requiresBlendFactor = renderState.blendState.usesConstantColor(uint32_t(pso->framebufferInfo.colorFormats.size()));
        
        pso->shaderMask = ShaderType::None;

        if (desc.VS) { pso->pVS = checked_cast<Shader*>(desc.VS.Get())->VS; pso->shaderMask = pso->shaderMask | ShaderType::Vertex; }
        if (desc.HS) { pso->pHS = checked_cast<Shader*>(desc.HS.Get())->HS; pso->shaderMask = pso->shaderMask | ShaderType::Hull; }
        if (desc.DS) { pso->pDS = checked_cast<Shader*>(desc.DS.Get())->DS; pso->shaderMask = pso->shaderMask | ShaderType::Domain; }
        if (desc.GS) { pso->pGS = checked_cast<Shader*>(desc.GS.Get())->GS; pso->shaderMask = pso->shaderMask | ShaderType::Geometry; }
        if (desc.PS) { pso->pPS = checked_cast<Shader*>(desc.PS.Get())->PS; pso->shaderMask = pso->shaderMask | ShaderType::Pixel; }

        // Set a flag if the PS has any UAV bindings in the layout
        for (auto& _layout : desc.bindingLayouts)
        {
            BindingLayout* layout = checked_cast<BindingLayout*>(_layout.Get());
            
            if ((layout->desc.visibility & ShaderType::Pixel) == 0)
                continue;

            for (const auto& item : layout->desc.bindings)
            {
                if (item.type == ResourceType::TypedBuffer_UAV || item.type == ResourceType::Texture_UAV || item.type == ResourceType::StructuredBuffer_UAV)
                {
                    pso->pixelShaderHasUAVs = true;
                    break;
                }
            }

            if (pso->pixelShaderHasUAVs)
                break;
        }
        
        return GraphicsPipelineHandle::Create(pso);
    }

    void CommandList::bindGraphicsPipeline(const GraphicsPipeline* pso) const
    {
        m_Context.immediateContext->IASetPrimitiveTopology(pso->primitiveTopology);
        m_Context.immediateContext->IASetInputLayout(pso->inputLayout ? pso->inputLayout->layout : nullptr);

        m_Context.immediateContext->RSSetState(pso->pRS);

        m_Context.immediateContext->VSSetShader(pso->pVS, nullptr, 0);
        m_Context.immediateContext->HSSetShader(pso->pHS, nullptr, 0);
        m_Context.immediateContext->DSSetShader(pso->pDS, nullptr, 0);
        m_Context.immediateContext->GSSetShader(pso->pGS, nullptr, 0);
        m_Context.immediateContext->PSSetShader(pso->pPS, nullptr, 0);
    }

    static DX11_ViewportState convertViewportState(const ViewportState& vpState)
    {
        DX11_ViewportState ret;

        ret.numViewports = UINT(vpState.viewports.size());
        for (size_t rt = 0; rt < vpState.viewports.size(); rt++)
        {
            ret.viewports[rt].TopLeftX = vpState.viewports[rt].minX;
            ret.viewports[rt].TopLeftY = vpState.viewports[rt].minY;
            ret.viewports[rt].Width = vpState.viewports[rt].maxX - vpState.viewports[rt].minX;
            ret.viewports[rt].Height = vpState.viewports[rt].maxY - vpState.viewports[rt].minY;
            ret.viewports[rt].MinDepth = vpState.viewports[rt].minZ;
            ret.viewports[rt].MaxDepth = vpState.viewports[rt].maxZ;
        }

        ret.numScissorRects = UINT(vpState.scissorRects.size());
        for(size_t rt = 0; rt < vpState.scissorRects.size(); rt++)
        {
            ret.scissorRects[rt].left = (LONG)vpState.scissorRects[rt].minX;
            ret.scissorRects[rt].top = (LONG)vpState.scissorRects[rt].minY;
            ret.scissorRects[rt].right = (LONG)vpState.scissorRects[rt].maxX;
            ret.scissorRects[rt].bottom = (LONG)vpState.scissorRects[rt].maxY;
        }

        return ret;
    }

    void CommandList::setGraphicsState(const GraphicsState& state)
    {
        GraphicsPipeline* pipeline = checked_cast<GraphicsPipeline*>(state.pipeline);
        Framebuffer* framebuffer = checked_cast<Framebuffer*>(state.framebuffer);

        if (m_CurrentComputeStateValid)
        {
            // If the previous operation has been a Dispatch call, there is a possibility of RT/UAV/SRV hazards.
            // Unbind everything to be sure, and to avoid checking the binding sets against each other. 
            // This only happens on switches between compute and graphics modes.

            clearState();
        }

        const bool updateFramebuffer = !m_CurrentGraphicsStateValid || m_CurrentFramebuffer != state.framebuffer;
        const bool updatePipeline = !m_CurrentGraphicsStateValid || m_CurrentGraphicsPipeline != state.pipeline;
        const bool updateBindings = updateFramebuffer || arraysAreDifferent(m_CurrentBindings, state.bindings);

        const bool updateViewports = !m_CurrentGraphicsStateValid ||
            arraysAreDifferent(m_CurrentViewports.viewports, state.viewport.viewports) ||
            arraysAreDifferent(m_CurrentViewports.scissorRects, state.viewport.scissorRects);

        const bool updateBlendState = !m_CurrentGraphicsStateValid || 
            (pipeline->requiresBlendFactor && state.blendConstantColor != m_CurrentBlendConstantColor);
        const bool updateStencilRef = !m_CurrentGraphicsStateValid ||
            (pipeline->desc.renderState.depthStencilState.dynamicStencilRef && state.dynamicStencilRefValue != m_CurrentStencilRefValue);
            
        const bool updateIndexBuffer = !m_CurrentGraphicsStateValid || m_CurrentIndexBufferBinding != state.indexBuffer;
        const bool updateVertexBuffers = !m_CurrentGraphicsStateValid || arraysAreDifferent(m_CurrentVertexBufferBindings, state.vertexBuffers);

        BindingSetVector setsToBind;
        if (updateBindings)
        {
            prepareToBindGraphicsResourceSets(state.bindings, m_CurrentGraphicsStateValid ? &m_CurrentBindings : nullptr, m_CurrentGraphicsPipeline, state.pipeline, updateFramebuffer, setsToBind);
        }

        if (updateFramebuffer || checked_cast<GraphicsPipeline*>(m_CurrentGraphicsPipeline.Get())->pixelShaderHasUAVs != pipeline->pixelShaderHasUAVs)
        {
            static_vector<ID3D11RenderTargetView*, c_MaxRenderTargets> RTVs;

            // Convert from RefCountPtr<T>[] to T[]
            for (const auto& RTV : framebuffer->RTVs)
                RTVs.push_back(RTV.Get());

            if (pipeline->pixelShaderHasUAVs)
            {
                m_Context.immediateContext->OMSetRenderTargetsAndUnorderedAccessViews(
                    UINT(RTVs.size()), RTVs.data(),
                    framebuffer->DSV,
                    D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);
            }
            else
            {
                m_Context.immediateContext->OMSetRenderTargets(
                    UINT(RTVs.size()),RTVs.data(),
                    framebuffer->DSV);
            }
        }

        if (updatePipeline)
        {
            bindGraphicsPipeline(pipeline);
        }

        if (updatePipeline || updateStencilRef)
        {
            m_CurrentStencilRefValue = pipeline->desc.renderState.depthStencilState.dynamicStencilRef
                ? state.dynamicStencilRefValue
                : pipeline->desc.renderState.depthStencilState.stencilRefValue;
            m_Context.immediateContext->OMSetDepthStencilState(pipeline->pDepthStencilState, m_CurrentStencilRefValue);
        }

        if (updatePipeline || updateBlendState)
        {
            float blendFactor[4]{ state.blendConstantColor.r, state.blendConstantColor.g, state.blendConstantColor.b, state.blendConstantColor.a };
            m_Context.immediateContext->OMSetBlendState(pipeline->pBlendState, blendFactor, D3D11_DEFAULT_SAMPLE_MASK);
        }

        if (updateBindings)
        {
            bindGraphicsResourceSets(setsToBind, state.pipeline);

            if (pipeline->pixelShaderHasUAVs)
            {
                ID3D11UnorderedAccessView* UAVs[D3D11_1_UAV_SLOT_COUNT] = {};
                static const UINT initialCounts[D3D11_1_UAV_SLOT_COUNT] = {};
                uint32_t minUAVSlot = D3D11_1_UAV_SLOT_COUNT;
                uint32_t maxUAVSlot = 0;
                for (auto _bindingSet : state.bindings)
                {
                    BindingSet* bindingSet = checked_cast<BindingSet*>(_bindingSet);

                    if ((bindingSet->visibility & ShaderType::Pixel) == 0)
                        continue;

                    for (uint32_t slot = bindingSet->minUAVSlot; slot <= bindingSet->maxUAVSlot; slot++)
                    {
                        UAVs[slot] = bindingSet->UAVs[slot];
                    }
                    minUAVSlot = std::min(minUAVSlot, bindingSet->minUAVSlot);
                    maxUAVSlot = std::max(maxUAVSlot, bindingSet->maxUAVSlot);
                }

                m_Context.immediateContext->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, minUAVSlot, maxUAVSlot - minUAVSlot + 1, UAVs + minUAVSlot, initialCounts);
            }
        }

        if (updateViewports)
        {
            DX11_ViewportState vpState = convertViewportState(state.viewport);

            if (vpState.numViewports)
            {
                m_Context.immediateContext->RSSetViewports(vpState.numViewports, vpState.viewports);
            }

            if (vpState.numScissorRects)
            {
                m_Context.immediateContext->RSSetScissorRects(vpState.numScissorRects, vpState.scissorRects);
            }
        }

#if NVRHI_D3D11_WITH_NVAPI
        bool updateSPS = m_CurrentSinglePassStereoState != pipeline->desc.renderState.singlePassStereo;

        if (updateSPS)
        {
            const SinglePassStereoState& spsState = pipeline->desc.renderState.singlePassStereo;

            NvAPI_Status Status = NvAPI_D3D_SetSinglePassStereoMode(m_Context.immediateContext, spsState.enabled ? 2 : 1, spsState.renderTargetIndexOffset, spsState.independentViewportMask);

            if (Status != NVAPI_OK)
            {
                m_Context.error("NvAPI_D3D_SetSinglePassStereoMode call failed");
            }

            m_CurrentSinglePassStereoState = spsState;
        }
#endif

        if (updateVertexBuffers)
        {
            ID3D11Buffer *pVertexBuffers[c_MaxVertexAttributes] = {};
            UINT pVertexBufferStrides[c_MaxVertexAttributes] = {};
            UINT pVertexBufferOffsets[c_MaxVertexAttributes] = {};
            uint32_t maxVbIndex = 0;

            const auto *inputLayout = pipeline->inputLayout;
            for (size_t i = 0; i < state.vertexBuffers.size(); i++)
            {
                const VertexBufferBinding& binding = state.vertexBuffers[i];

                // This is tested by the validation layer, skip invalid slots here if VL is not used.
                if (binding.slot >= c_MaxVertexAttributes)
                    continue;
                
                assert(binding.offset <= UINT_MAX);

                pVertexBuffers[binding.slot] = checked_cast<Buffer*>(binding.buffer)->resource;
                pVertexBufferStrides[binding.slot] = inputLayout->elementStrides.at(binding.slot);
                pVertexBufferOffsets[binding.slot] = UINT(binding.offset);
                maxVbIndex = std::max(maxVbIndex, binding.slot);
            }

            if (m_CurrentGraphicsStateValid)
            {
                for (const VertexBufferBinding& binding : m_CurrentVertexBufferBindings)
                {
                    if (binding.slot < c_MaxVertexAttributes)
                        maxVbIndex = std::max(maxVbIndex, binding.slot);
                }
            }

            m_Context.immediateContext->IASetVertexBuffers(0, maxVbIndex + 1,
                pVertexBuffers,
                pVertexBufferStrides,
                pVertexBufferOffsets);
        }

        if (updateIndexBuffer)
        {
            if (state.indexBuffer.buffer)
            {
                m_Context.immediateContext->IASetIndexBuffer(checked_cast<Buffer*>(state.indexBuffer.buffer)->resource,
                    getDxgiFormatMapping(state.indexBuffer.format).srvFormat,
                    state.indexBuffer.offset);
            }
            else
            {
                m_Context.immediateContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            }
        }

        m_CurrentIndirectBuffer = state.indirectParams;

        m_CurrentGraphicsStateValid = true;
        if (updatePipeline || updateFramebuffer || updateBindings || updateViewports || updateVertexBuffers || updateIndexBuffer || updateBlendState)
        {
            m_CurrentGraphicsPipeline = state.pipeline;
            m_CurrentFramebuffer = state.framebuffer;
            m_CurrentViewports = state.viewport;
            m_CurrentBlendConstantColor = state.blendConstantColor;

            m_CurrentBindings.resize(state.bindings.size());
            for(size_t i = 0; i < state.bindings.size(); i++)
            {
                m_CurrentBindings[i] = state.bindings[i];
            }

            m_CurrentVertexBufferBindings = state.vertexBuffers;
            m_CurrentIndexBufferBinding = state.indexBuffer;

            m_CurrentVertexBuffers.resize(state.vertexBuffers.size());
            for (size_t i = 0; i < state.vertexBuffers.size(); i++)
            {
                m_CurrentVertexBuffers[i] = state.vertexBuffers[i].buffer;
            }
            m_CurrentIndexBuffer = state.indexBuffer.buffer;
        }
    }

    void CommandList::draw(const DrawArguments& args)
    {
        m_Context.immediateContext->DrawInstanced(args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndexed(const DrawArguments& args)
    {
        m_Context.immediateContext->DrawIndexedInstanced(args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentIndirectBuffer.Get());
        
        if (indirectParams) // validation layer will issue an error otherwise
        {
            // Simulate multi-command D3D12 ExecuteIndirect or Vulkan vkCmdDrawIndirect with a loop
            for (uint32_t drawIndex = 0; drawIndex < drawCount; ++drawIndex)
            {
                m_Context.immediateContext->DrawInstancedIndirect(indirectParams->resource, offsetBytes);
                offsetBytes += sizeof(DrawIndirectArguments);
            }
        }
    }

    void CommandList::drawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentIndirectBuffer.Get());

        if (indirectParams)
        {
            // Simulate multi-command D3D12 ExecuteIndirect or Vulkan vkCmdDrawIndirect with a loop
            for (uint32_t drawIndex = 0; drawIndex < drawCount; ++drawIndex)
            {
                m_Context.immediateContext->DrawIndexedInstancedIndirect(indirectParams->resource, offsetBytes);
                offsetBytes += sizeof(DrawIndexedIndirectArguments);
            }
        }
    }

    ID3D11BlendState* Device::getBlendState(const BlendState& blendState)
    {
        size_t hash = 0;
        hash_combine(hash, blendState.alphaToCoverageEnable);

        for (const auto& target : blendState.targets)
        {
            hash_combine(hash, target.blendEnable);
            hash_combine(hash, target.srcBlend);
            hash_combine(hash, target.destBlend);
            hash_combine(hash, target.blendOp);
            hash_combine(hash, target.srcBlendAlpha);
            hash_combine(hash, target.destBlendAlpha);
            hash_combine(hash, target.blendOpAlpha);
            hash_combine(hash, target.colorWriteMask);
        }

        RefCountPtr<ID3D11BlendState> d3dBlendState = m_BlendStates[hash];

        if (d3dBlendState)
            return d3dBlendState;

        D3D11_BLEND_DESC desc11New;
        desc11New.AlphaToCoverageEnable = blendState.alphaToCoverageEnable ? TRUE : FALSE;
        //we always use this and set the states for each target explicitly
        desc11New.IndependentBlendEnable = TRUE;

        for (uint32_t i = 0; i < c_MaxRenderTargets; i++)
        {
            const BlendState::RenderTarget& src = blendState.targets[i];
            D3D11_RENDER_TARGET_BLEND_DESC& dst = desc11New.RenderTarget[i];

            dst.BlendEnable = src.blendEnable ? TRUE : FALSE;
            dst.SrcBlend = convertBlendValue(src.srcBlend);
            dst.DestBlend = convertBlendValue(src.destBlend);
            dst.BlendOp = convertBlendOp(src.blendOp);
            dst.SrcBlendAlpha = convertBlendValue(src.srcBlendAlpha);
            dst.DestBlendAlpha = convertBlendValue(src.destBlendAlpha);
            dst.BlendOpAlpha = convertBlendOp(src.blendOpAlpha);
            dst.RenderTargetWriteMask = (UINT8)src.colorWriteMask;
        }

        const HRESULT res = m_Context.device->CreateBlendState(&desc11New, &d3dBlendState);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateBlendState call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
            return nullptr;
        }

        m_BlendStates[hash] = d3dBlendState;
        return d3dBlendState;
    }

    ID3D11DepthStencilState* Device::getDepthStencilState(const DepthStencilState& depthState)
    {
        size_t hash = 0;
        hash_combine(hash, depthState.depthTestEnable);
        hash_combine(hash, depthState.depthWriteEnable);
        hash_combine(hash, depthState.depthFunc);
        hash_combine(hash, depthState.stencilEnable);
        hash_combine(hash, depthState.stencilReadMask);
        hash_combine(hash, depthState.stencilWriteMask);
        hash_combine(hash, depthState.frontFaceStencil.failOp);
        hash_combine(hash, depthState.frontFaceStencil.depthFailOp);
        hash_combine(hash, depthState.frontFaceStencil.passOp);
        hash_combine(hash, depthState.frontFaceStencil.stencilFunc);
        hash_combine(hash, depthState.backFaceStencil.failOp);
        hash_combine(hash, depthState.backFaceStencil.depthFailOp);
        hash_combine(hash, depthState.backFaceStencil.passOp);
        hash_combine(hash, depthState.backFaceStencil.stencilFunc);
        
        RefCountPtr<ID3D11DepthStencilState> d3dDepthStencilState = m_DepthStencilStates[hash];

        if (d3dDepthStencilState)
            return d3dDepthStencilState;

        D3D11_DEPTH_STENCIL_DESC desc11New;
        desc11New.DepthEnable = depthState.depthTestEnable ? TRUE : FALSE;
        desc11New.DepthWriteMask = depthState.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc11New.DepthFunc = convertComparisonFunc(depthState.depthFunc);
        desc11New.StencilEnable = depthState.stencilEnable ? TRUE : FALSE;
        desc11New.StencilReadMask = (UINT8)depthState.stencilReadMask;
        desc11New.StencilWriteMask = (UINT8)depthState.stencilWriteMask;
        desc11New.FrontFace.StencilFailOp = convertStencilOp(depthState.frontFaceStencil.failOp);
        desc11New.FrontFace.StencilDepthFailOp = convertStencilOp(depthState.frontFaceStencil.depthFailOp);
        desc11New.FrontFace.StencilPassOp = convertStencilOp(depthState.frontFaceStencil.passOp);
        desc11New.FrontFace.StencilFunc = convertComparisonFunc(depthState.frontFaceStencil.stencilFunc);
        desc11New.BackFace.StencilFailOp = convertStencilOp(depthState.backFaceStencil.failOp);
        desc11New.BackFace.StencilDepthFailOp = convertStencilOp(depthState.backFaceStencil.depthFailOp);
        desc11New.BackFace.StencilPassOp = convertStencilOp(depthState.backFaceStencil.passOp);
        desc11New.BackFace.StencilFunc = convertComparisonFunc(depthState.backFaceStencil.stencilFunc);

        const HRESULT res = m_Context.device->CreateDepthStencilState(&desc11New, &d3dDepthStencilState);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateDepthStencilState call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
            return nullptr;
        }

        m_DepthStencilStates[hash] = d3dDepthStencilState;
        return d3dDepthStencilState;
    }

    ID3D11RasterizerState* Device::getRasterizerState(const RasterState& rasterState)
    {
        size_t hash = 0;
        hash_combine(hash, rasterState.fillMode);
        hash_combine(hash, rasterState.cullMode);
        hash_combine(hash, rasterState.frontCounterClockwise);
        hash_combine(hash, rasterState.depthClipEnable);
        hash_combine(hash, rasterState.scissorEnable);
        hash_combine(hash, rasterState.multisampleEnable);
        hash_combine(hash, rasterState.antialiasedLineEnable);
        hash_combine(hash, rasterState.depthBias);
        hash_combine(hash, rasterState.depthBiasClamp);
        hash_combine(hash, rasterState.slopeScaledDepthBias);
        hash_combine(hash, rasterState.forcedSampleCount);
        hash_combine(hash, rasterState.programmableSamplePositionsEnable);
        hash_combine(hash, rasterState.conservativeRasterEnable);
        hash_combine(hash, rasterState.quadFillEnable);

        if (rasterState.programmableSamplePositionsEnable)
        {
            for (int i = 0; i < 16; i++)
            {
                hash_combine(hash, rasterState.samplePositionsX[i]);
                hash_combine(hash, rasterState.samplePositionsY[i]);
            }
        }

        RefCountPtr<ID3D11RasterizerState> d3dRasterizerState = m_RasterizerStates[hash];

        if (d3dRasterizerState)
            return d3dRasterizerState;

        D3D11_RASTERIZER_DESC desc11New;
        switch (rasterState.fillMode)
        {
        case RasterFillMode::Solid:
            desc11New.FillMode = D3D11_FILL_SOLID;
            break;
        case RasterFillMode::Wireframe:
            desc11New.FillMode = D3D11_FILL_WIREFRAME;
            break;
        default:
            utils::InvalidEnum();
        }

        switch (rasterState.cullMode)
        {
        case RasterCullMode::Back:
            desc11New.CullMode = D3D11_CULL_BACK;
            break;
        case RasterCullMode::Front:
            desc11New.CullMode = D3D11_CULL_FRONT;
            break;
        case RasterCullMode::None:
            desc11New.CullMode = D3D11_CULL_NONE;
            break;
        default:
            utils::InvalidEnum();
        }

        desc11New.FrontCounterClockwise = rasterState.frontCounterClockwise ? TRUE : FALSE;
        desc11New.DepthBias = rasterState.depthBias;
        desc11New.DepthBiasClamp = rasterState.depthBiasClamp;
        desc11New.SlopeScaledDepthBias = rasterState.slopeScaledDepthBias;
        desc11New.DepthClipEnable = rasterState.depthClipEnable ? TRUE : FALSE;
        desc11New.ScissorEnable = rasterState.scissorEnable ? TRUE : FALSE;
        desc11New.MultisampleEnable = rasterState.multisampleEnable ? TRUE : FALSE;
        desc11New.AntialiasedLineEnable = rasterState.antialiasedLineEnable ? TRUE : FALSE;

        bool extendedState = rasterState.conservativeRasterEnable
                          || rasterState.forcedSampleCount
                          || rasterState.programmableSamplePositionsEnable
                          || rasterState.quadFillEnable;

        if (extendedState)
        {
#if NVRHI_D3D11_WITH_NVAPI
            NvAPI_D3D11_RASTERIZER_DESC_EX descEx;
            memset(&descEx, 0, sizeof(descEx));
            memcpy(&descEx, &desc11New, sizeof(desc11New));

            descEx.ConservativeRasterEnable = rasterState.conservativeRasterEnable;
            descEx.ProgrammableSamplePositionsEnable = rasterState.programmableSamplePositionsEnable;
            descEx.SampleCount = rasterState.forcedSampleCount;
            descEx.ForcedSampleCount = rasterState.forcedSampleCount;
            descEx.QuadFillMode = rasterState.quadFillEnable ? NVAPI_QUAD_FILLMODE_BBOX : NVAPI_QUAD_FILLMODE_DISABLED;
            memcpy(descEx.SamplePositionsX, rasterState.samplePositionsX, sizeof(rasterState.samplePositionsX));
            memcpy(descEx.SamplePositionsY, rasterState.samplePositionsY, sizeof(rasterState.samplePositionsY));

            if (NVAPI_OK != NvAPI_D3D11_CreateRasterizerState(m_Context.device, &descEx, &d3dRasterizerState))
            {
                m_Context.error("NvAPI_D3D11_CreateRasterizerState call failed");
                return nullptr;
            }
#else
            m_Context.error("Cannot create an extended rasterizer state without NVAPI support");
            return nullptr;
#endif
        }
        else
        {
            const HRESULT res = m_Context.device->CreateRasterizerState(&desc11New, &d3dRasterizerState);

            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateRasterizerState call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
                return nullptr;
            }
        }

        m_RasterizerStates[hash] = d3dRasterizerState;
        return d3dRasterizerState;
    }


} // nanmespace nvrhi::d3d11