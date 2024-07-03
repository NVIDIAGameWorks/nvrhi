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

    Object GraphicsPipeline::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_RootSignature:
            return rootSignature->getNativeObject(objectType);
        case ObjectTypes::D3D12_PipelineState:
            return Object(pipelineState.Get());
        default:
            return nullptr;
        }
    }
    
    RefCountPtr<ID3D12PipelineState> Device::createPipelineState(const GraphicsPipelineDesc & state, RootSignature* pRS, const FramebufferInfo& fbinfo) const
    {
        if (state.renderState.singlePassStereo.enabled && !m_SinglePassStereoSupported)
        {
            m_Context.error("Single-pass stereo is not supported by this device");
            return nullptr;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = pRS->handle;

        Shader* shader;
        shader = checked_cast<Shader*>(state.VS.Get());
        if (shader) desc.VS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = checked_cast<Shader*>(state.HS.Get());
        if (shader) desc.HS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = checked_cast<Shader*>(state.DS.Get());
        if (shader) desc.DS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = checked_cast<Shader*>(state.GS.Get());
        if (shader) desc.GS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = checked_cast<Shader*>(state.PS.Get());
        if (shader) desc.PS = { &shader->bytecode[0], shader->bytecode.size() };


        TranslateBlendState(state.renderState.blendState, desc.BlendState);
        

        const DepthStencilState& depthState = state.renderState.depthStencilState;
        TranslateDepthStencilState(depthState, desc.DepthStencilState);

        if ((depthState.depthTestEnable || depthState.stencilEnable) && fbinfo.depthFormat == Format::UNKNOWN)
        {
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
            m_Context.messageCallback->message(MessageSeverity::Warning, "depthEnable or stencilEnable is true, but no depth target is bound");
        }

        const RasterState& rasterState = state.renderState.rasterState;
        TranslateRasterizerState(rasterState, desc.RasterizerState);

        switch (state.primType)
        {
        case PrimitiveType::PointList:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            break;
        case PrimitiveType::LineList:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            break;
        case PrimitiveType::TriangleList:
        case PrimitiveType::TriangleStrip:
        case PrimitiveType::TriangleFan:
        case PrimitiveType::TriangleListWithAdjacency:
        case PrimitiveType::TriangleStripWithAdjacency:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            break;
        case PrimitiveType::PatchList:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            break;
        }

        desc.DSVFormat = getDxgiFormatMapping(fbinfo.depthFormat).rtvFormat;

        desc.SampleDesc.Count = fbinfo.sampleCount;
        desc.SampleDesc.Quality = fbinfo.sampleQuality;

        for (uint32_t i = 0; i < uint32_t(fbinfo.colorFormats.size()); i++)
        {
            desc.RTVFormats[i] = getDxgiFormatMapping(fbinfo.colorFormats[i]).rtvFormat;
        }

        InputLayout* inputLayout = checked_cast<InputLayout*>(state.inputLayout.Get());
        if (inputLayout && !inputLayout->inputElements.empty())
        {
            desc.InputLayout.NumElements = uint32_t(inputLayout->inputElements.size());
            desc.InputLayout.pInputElementDescs = &(inputLayout->inputElements[0]);
        }

        desc.NumRenderTargets = uint32_t(fbinfo.colorFormats.size());
        desc.SampleMask = ~0u;

        RefCountPtr<ID3D12PipelineState> pipelineState;

#if NVRHI_D3D12_WITH_NVAPI
        std::vector<const NVAPI_D3D12_PSO_EXTENSION_DESC*> extensions;

        shader = checked_cast<Shader*>(state.VS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = checked_cast<Shader*>(state.HS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = checked_cast<Shader*>(state.DS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = checked_cast<Shader*>(state.GS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = checked_cast<Shader*>(state.PS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());

        if (rasterState.programmableSamplePositionsEnable || rasterState.quadFillEnable)
        {
            NVAPI_D3D12_PSO_RASTERIZER_STATE_DESC rasterizerDesc = {};
            rasterizerDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
            rasterizerDesc.psoExtension = NV_PSO_RASTER_EXTENSION;
            rasterizerDesc.version = NV_RASTERIZER_PSO_EXTENSION_DESC_VER;

            rasterizerDesc.ProgrammableSamplePositionsEnable = rasterState.programmableSamplePositionsEnable;
            rasterizerDesc.SampleCount = rasterState.forcedSampleCount;
            memcpy(rasterizerDesc.SamplePositionsX, rasterState.samplePositionsX, sizeof(rasterState.samplePositionsX));
            memcpy(rasterizerDesc.SamplePositionsY, rasterState.samplePositionsY, sizeof(rasterState.samplePositionsY));
            rasterizerDesc.QuadFillMode = rasterState.quadFillEnable ? NVAPI_QUAD_FILLMODE_BBOX : NVAPI_QUAD_FILLMODE_DISABLED;

            extensions.push_back(&rasterizerDesc);
        }

        if (!extensions.empty())
        {
            NvAPI_Status status = NvAPI_D3D12_CreateGraphicsPipelineState(m_Context.device, &desc, NvU32(extensions.size()), &extensions[0], &pipelineState);

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                m_Context.error("Failed to create a graphics pipeline state object with NVAPI extensions");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        const HRESULT hr = m_Context.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));

        if (FAILED(hr))
        {
            m_Context.error("Failed to create a graphics pipeline state object");
            return nullptr;
        }

        return pipelineState;
    }

    
    GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb)
    {
        RefCountPtr<RootSignature> pRS = getRootSignature(desc.bindingLayouts, desc.inputLayout != nullptr);

        RefCountPtr<ID3D12PipelineState> pPSO = createPipelineState(desc, pRS, fb->getFramebufferInfo());

        return createHandleForNativeGraphicsPipeline(pRS, pPSO, desc, fb->getFramebufferInfo());
    }

    nvrhi::GraphicsPipelineHandle Device::createHandleForNativeGraphicsPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const GraphicsPipelineDesc& desc, const FramebufferInfo& framebufferInfo)
    {
        if (rootSignature == nullptr)
            return nullptr;

        if (pipelineState == nullptr)
            return nullptr;

        GraphicsPipeline *pso = new GraphicsPipeline();
        pso->desc = desc;
        pso->framebufferInfo = framebufferInfo;
        pso->rootSignature = checked_cast<RootSignature*>(rootSignature);
        pso->pipelineState = pipelineState;
        pso->requiresBlendFactor = desc.renderState.blendState.usesConstantColor(uint32_t(pso->framebufferInfo.colorFormats.size()));
        
        return GraphicsPipelineHandle::Create(pso);
    }

    FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc)
    {
        Framebuffer *fb = new Framebuffer(m_Resources);
        fb->desc = desc;
        fb->framebufferInfo = FramebufferInfoEx(desc);

        if (!desc.colorAttachments.empty())
        {
            Texture* texture = checked_cast<Texture*>(desc.colorAttachments[0].texture);
            fb->rtWidth = texture->desc.width;
            fb->rtHeight = texture->desc.height;
        } else if (desc.depthAttachment.valid())
        {
            Texture* texture = checked_cast<Texture*>(desc.depthAttachment.texture);
            fb->rtWidth = texture->desc.width;
            fb->rtHeight = texture->desc.height;
        }

        for (size_t rt = 0; rt < desc.colorAttachments.size(); rt++)
        {
            auto& attachment = desc.colorAttachments[rt];

            Texture* texture = checked_cast<Texture*>(attachment.texture);
            assert(texture->desc.width == fb->rtWidth);
            assert(texture->desc.height == fb->rtHeight);

            DescriptorIndex index = m_Resources.renderTargetViewHeap.allocateDescriptor();

            const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_Resources.renderTargetViewHeap.getCpuHandle(index);
            texture->createRTV(descriptorHandle.ptr, attachment.format, attachment.subresources);

            fb->RTVs.push_back(index);
            fb->textures.push_back(texture);
        }

        if (desc.depthAttachment.valid())
        {
            Texture* texture = checked_cast<Texture*>(desc.depthAttachment.texture);
            assert(texture->desc.width == fb->rtWidth);
            assert(texture->desc.height == fb->rtHeight);

            DescriptorIndex index = m_Resources.depthStencilViewHeap.allocateDescriptor();

            const D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_Resources.depthStencilViewHeap.getCpuHandle(index);
            texture->createDSV(descriptorHandle.ptr, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly);

            fb->DSV = index;
            fb->textures.push_back(texture);
        }

        return FramebufferHandle::Create(fb);
    }

    Framebuffer::~Framebuffer()
    {
        for (DescriptorIndex RTV : RTVs)
            m_Resources.renderTargetViewHeap.releaseDescriptor(RTV);

        if (DSV != c_InvalidDescriptorIndex)
            m_Resources.depthStencilViewHeap.releaseDescriptor(DSV);
    }
    
    void CommandList::bindFramebuffer(Framebuffer *fb)
    {
        if (m_EnableAutomaticBarriers)
        {
            setResourceStatesForFramebuffer(fb);
        }
        
        static_vector<D3D12_CPU_DESCRIPTOR_HANDLE, 16> RTVs;
        for (uint32_t rtIndex = 0; rtIndex < fb->RTVs.size(); rtIndex++)
        {
            RTVs.push_back(m_Resources.renderTargetViewHeap.getCpuHandle(fb->RTVs[rtIndex]));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE DSV = {};
        if (fb->desc.depthAttachment.valid())
            DSV = m_Resources.depthStencilViewHeap.getCpuHandle(fb->DSV);

        m_ActiveCommandList->commandList->OMSetRenderTargets(UINT(RTVs.size()), RTVs.data(), false, fb->desc.depthAttachment.valid() ? &DSV : nullptr);
    }

    void CommandList::setGraphicsState(const GraphicsState& state)
    {
        GraphicsPipeline* pso = checked_cast<GraphicsPipeline*>(state.pipeline);
        Framebuffer* framebuffer = checked_cast<Framebuffer*>(state.framebuffer);

        const bool updateFramebuffer = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.framebuffer != state.framebuffer;
        const bool updateRootSignature = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.pipeline == nullptr ||
            checked_cast<GraphicsPipeline*>(m_CurrentGraphicsState.pipeline)->rootSignature != pso->rootSignature;

        const bool updatePipeline = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.pipeline != state.pipeline;
        const bool updateIndirectParams = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.indirectParams != state.indirectParams;

        const bool updateViewports = !m_CurrentGraphicsStateValid ||
            arraysAreDifferent(m_CurrentGraphicsState.viewport.viewports, state.viewport.viewports) ||
            arraysAreDifferent(m_CurrentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);

        const bool updateBlendFactor = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.blendConstantColor != state.blendConstantColor;
        
        const uint8_t effectiveStencilRefValue = pso->desc.renderState.depthStencilState.dynamicStencilRef
            ? state.dynamicStencilRefValue
            : pso->desc.renderState.depthStencilState.stencilRefValue;
        const bool updateStencilRef = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.dynamicStencilRefValue != effectiveStencilRefValue;
        
        const bool updateIndexBuffer = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.indexBuffer != state.indexBuffer;
        const bool updateVertexBuffers = !m_CurrentGraphicsStateValid || arraysAreDifferent(m_CurrentGraphicsState.vertexBuffers, state.vertexBuffers);

        const bool updateShadingRate = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.shadingRateState != state.shadingRateState;

        uint32_t bindingUpdateMask = 0;
        if (!m_CurrentGraphicsStateValid || updateRootSignature)
            bindingUpdateMask = ~0u;

        if (commitDescriptorHeaps())
            bindingUpdateMask = ~0u;

        if (bindingUpdateMask == 0)
            bindingUpdateMask = arrayDifferenceMask(m_CurrentGraphicsState.bindings, state.bindings);

        if (updatePipeline)
        {
            bindGraphicsPipeline(pso, updateRootSignature);
            m_Instance->referencedResources.push_back(pso);
        }

        if (pso->desc.renderState.depthStencilState.stencilEnable && (updatePipeline || updateStencilRef))
        {
            m_ActiveCommandList->commandList->OMSetStencilRef(effectiveStencilRefValue);
        }

        if (pso->requiresBlendFactor && updateBlendFactor)
        {
            m_ActiveCommandList->commandList->OMSetBlendFactor(&state.blendConstantColor.r);
        }

        if (updateFramebuffer)
        {
            bindFramebuffer(framebuffer);
            m_Instance->referencedResources.push_back(framebuffer);
        }

        setGraphicsBindings(state.bindings, bindingUpdateMask, state.indirectParams, updateIndirectParams, pso->rootSignature);

        if (updateIndexBuffer)
        {
            D3D12_INDEX_BUFFER_VIEW IBV = {};

            if (state.indexBuffer.buffer)
            {
                Buffer* buffer = checked_cast<Buffer*>(state.indexBuffer.buffer);

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(buffer, ResourceStates::IndexBuffer);
                }

                IBV.Format = getDxgiFormatMapping(state.indexBuffer.format).srvFormat;
                IBV.SizeInBytes = (UINT)(buffer->desc.byteSize - state.indexBuffer.offset);
                IBV.BufferLocation = buffer->gpuVA + state.indexBuffer.offset;

                m_Instance->referencedResources.push_back(state.indexBuffer.buffer);
            }

            m_ActiveCommandList->commandList->IASetIndexBuffer(&IBV);
        }

        if (updateVertexBuffers)
        {
            D3D12_VERTEX_BUFFER_VIEW VBVs[c_MaxVertexAttributes] = {};
            uint32_t maxVbIndex = 0;
            InputLayout* inputLayout = checked_cast<InputLayout*>(pso->desc.inputLayout.Get());

            for (const VertexBufferBinding& binding : state.vertexBuffers)
            {
                Buffer* buffer = checked_cast<Buffer*>(binding.buffer);

                if (m_EnableAutomaticBarriers)
                {
                    requireBufferState(buffer, ResourceStates::VertexBuffer);
                }

                // This is tested by the validation layer, skip invalid slots here if VL is not used.
                if (binding.slot >= c_MaxVertexAttributes)
                    continue;

                VBVs[binding.slot].StrideInBytes = inputLayout->elementStrides[binding.slot];
                VBVs[binding.slot].SizeInBytes = (UINT)(std::min(buffer->desc.byteSize - binding.offset, (uint64_t)ULONG_MAX));
                VBVs[binding.slot].BufferLocation = buffer->gpuVA + binding.offset;
                maxVbIndex = std::max(maxVbIndex, binding.slot);

                m_Instance->referencedResources.push_back(buffer);
            }

            if (m_CurrentGraphicsStateValid)
            {
                for (const VertexBufferBinding& binding : m_CurrentGraphicsState.vertexBuffers)
                {
                    if (binding.slot < c_MaxVertexAttributes)
                        maxVbIndex = std::max(maxVbIndex, binding.slot);
                }
            }

            m_ActiveCommandList->commandList->IASetVertexBuffers(0, maxVbIndex + 1, VBVs);
        }

        if (updateShadingRate || updateFramebuffer)
        {
            auto framebufferDesc = framebuffer->getDesc();
            bool shouldEnableVariableRateShading = framebufferDesc.shadingRateAttachment.valid() && state.shadingRateState.enabled;
            bool variableRateShadingCurrentlyEnabled = m_CurrentGraphicsStateValid
                && m_CurrentGraphicsState.framebuffer->getDesc().shadingRateAttachment.valid() && m_CurrentGraphicsState.shadingRateState.enabled;

            if (shouldEnableVariableRateShading)
            {
                setTextureState(framebufferDesc.shadingRateAttachment.texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), nvrhi::ResourceStates::ShadingRateSurface);
                Texture* texture = checked_cast<Texture*>(framebufferDesc.shadingRateAttachment.texture);
                m_ActiveCommandList->commandList6->RSSetShadingRateImage(texture->resource);
            }
            else if (variableRateShadingCurrentlyEnabled)
            {
                // shading rate attachment is not enabled in framebuffer, or VRS is turned off, so unbind VRS image
                m_ActiveCommandList->commandList6->RSSetShadingRateImage(nullptr);
            }
        }

        if (updateShadingRate)
        {
            if (state.shadingRateState.enabled)
            {
                static_assert(D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT == 2);
                D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT];
                combiners[0] = convertShadingRateCombiner(state.shadingRateState.pipelinePrimitiveCombiner);
                combiners[1] = convertShadingRateCombiner(state.shadingRateState.imageCombiner);
                m_ActiveCommandList->commandList6->RSSetShadingRate(convertPixelShadingRate(state.shadingRateState.shadingRate), combiners);
            }
            else if (m_CurrentGraphicsStateValid && m_CurrentGraphicsState.shadingRateState.enabled)
            {
                // only call if the old state had VRS enabled and we need to disable it
                m_ActiveCommandList->commandList6->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
            }
        }
        
        commitBarriers();

        if (updateViewports)
        {
            DX12_ViewportState vpState = convertViewportState(pso->desc.renderState.rasterState, framebuffer->framebufferInfo, state.viewport);

            if (vpState.numViewports)
            {
                m_ActiveCommandList->commandList->RSSetViewports(vpState.numViewports, vpState.viewports);
            }

            if (vpState.numScissorRects)
            {
                m_ActiveCommandList->commandList->RSSetScissorRects(vpState.numScissorRects, vpState.scissorRects);
            }
        }

#if NVRHI_D3D12_WITH_NVAPI
        bool updateSPS = m_CurrentSinglePassStereoState != pso->desc.renderState.singlePassStereo;

        if (updateSPS)
        {
            const SinglePassStereoState& spsState = pso->desc.renderState.singlePassStereo;

            NvAPI_Status Status = NvAPI_D3D12_SetSinglePassStereoMode(m_ActiveCommandList->commandList, spsState.enabled ? 2 : 1, spsState.renderTargetIndexOffset, spsState.independentViewportMask);

            if (Status != NVAPI_OK)
            {
                m_Context.error("NvAPI_D3D12_SetSinglePassStereoMode call failed");
            }

            m_CurrentSinglePassStereoState = spsState;
        }
#endif

        m_CurrentGraphicsStateValid = true;
        m_CurrentComputeStateValid = false;
        m_CurrentMeshletStateValid = false;
        m_CurrentRayTracingStateValid = false;
        m_CurrentGraphicsState = state;
        m_CurrentGraphicsState.dynamicStencilRefValue = effectiveStencilRefValue;
    }

    void CommandList::unbindShadingRateState()
    {
        if (m_CurrentGraphicsStateValid && m_CurrentGraphicsState.shadingRateState.enabled)
        {
            m_ActiveCommandList->commandList6->RSSetShadingRateImage(nullptr);
            m_ActiveCommandList->commandList6->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
            m_CurrentGraphicsState.shadingRateState.enabled = false;
            m_CurrentGraphicsState.framebuffer = nullptr;
        }
    }


    void CommandList::updateGraphicsVolatileBuffers()
    {
        // If there are some volatile buffers bound, and they have been written into since the last draw or setGraphicsState, patch their views
        if (!m_AnyVolatileBufferWrites)
            return;

        for (VolatileConstantBufferBinding& parameter : m_CurrentGraphicsVolatileCBs)
        {
            D3D12_GPU_VIRTUAL_ADDRESS currentGpuVA = m_VolatileConstantBufferAddresses[parameter.buffer];

            if (currentGpuVA != parameter.address)
            {
                m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(parameter.bindingPoint, currentGpuVA);

                parameter.address = currentGpuVA;
            }
        }

        m_AnyVolatileBufferWrites = false;
    }

    void CommandList::bindGraphicsPipeline(GraphicsPipeline *pso, bool updateRootSignature) const
    {
        const auto& pipelineDesc = pso->desc;

        if (updateRootSignature)
        {
            m_ActiveCommandList->commandList->SetGraphicsRootSignature(pso->rootSignature->handle);
        }

        m_ActiveCommandList->commandList->SetPipelineState(pso->pipelineState);

        m_ActiveCommandList->commandList->IASetPrimitiveTopology(convertPrimitiveType(pipelineDesc.primType, pipelineDesc.patchControlPoints));
    }

    void CommandList::draw(const DrawArguments& args)
    {
        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->DrawInstanced(args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndexed(const DrawArguments& args)
    {
        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->DrawIndexedInstanced(args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentGraphicsState.indirectParams);
        assert(indirectParams); // validation layer handles this

        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->ExecuteIndirect(m_Context.drawIndirectSignature, drawCount, indirectParams->resource, offsetBytes, nullptr, 0);
    }

    void CommandList::drawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentGraphicsState.indirectParams);
        assert(indirectParams);

        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->ExecuteIndirect(m_Context.drawIndexedIndirectSignature, drawCount, indirectParams->resource, offsetBytes, nullptr, 0);
    }
    
    DX12_ViewportState convertViewportState(const RasterState& rasterState, const FramebufferInfoEx& framebufferInfo, const ViewportState& vpState)
    {
        DX12_ViewportState ret;

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
            if (rasterState.scissorEnable)
            {
                ret.scissorRects[rt].left = (LONG)vpState.scissorRects[rt].minX;
                ret.scissorRects[rt].top = (LONG)vpState.scissorRects[rt].minY;
                ret.scissorRects[rt].right = (LONG)vpState.scissorRects[rt].maxX;
                ret.scissorRects[rt].bottom = (LONG)vpState.scissorRects[rt].maxY;
            }
            else
            {
                ret.scissorRects[rt].left = (LONG)vpState.viewports[rt].minX;
                ret.scissorRects[rt].top = (LONG)vpState.viewports[rt].minY;
                ret.scissorRects[rt].right = (LONG)vpState.viewports[rt].maxX;
                ret.scissorRects[rt].bottom = (LONG)vpState.viewports[rt].maxY;

                if (framebufferInfo.width > 0)
                {
                    ret.scissorRects[rt].left = std::max(ret.scissorRects[rt].left, LONG(0));
                    ret.scissorRects[rt].top = std::max(ret.scissorRects[rt].top, LONG(0));
                    ret.scissorRects[rt].right = std::min(ret.scissorRects[rt].right, LONG(framebufferInfo.width));
                    ret.scissorRects[rt].bottom = std::min(ret.scissorRects[rt].bottom, LONG(framebufferInfo.height));
                }
            }
        }

        return ret;
    }
    
    void TranslateBlendState(const BlendState& inState, D3D12_BLEND_DESC& outState)
    {
        outState.AlphaToCoverageEnable = inState.alphaToCoverageEnable;
        outState.IndependentBlendEnable = true;

        for (uint32_t i = 0; i < c_MaxRenderTargets; i++)
        {
            const BlendState::RenderTarget& src = inState.targets[i];
            D3D12_RENDER_TARGET_BLEND_DESC& dst = outState.RenderTarget[i];

            dst.BlendEnable = src.blendEnable ? TRUE : FALSE;
            dst.SrcBlend = convertBlendValue(src.srcBlend);
            dst.DestBlend = convertBlendValue(src.destBlend);
            dst.BlendOp = convertBlendOp(src.blendOp);
            dst.SrcBlendAlpha = convertBlendValue(src.srcBlendAlpha);
            dst.DestBlendAlpha = convertBlendValue(src.destBlendAlpha);
            dst.BlendOpAlpha = convertBlendOp(src.blendOpAlpha);
            dst.RenderTargetWriteMask = (D3D12_COLOR_WRITE_ENABLE)src.colorWriteMask;
        }
    }

    void TranslateDepthStencilState(const DepthStencilState& inState, D3D12_DEPTH_STENCIL_DESC& outState)
    {
        outState.DepthEnable = inState.depthTestEnable ? TRUE : FALSE;
        outState.DepthWriteMask = inState.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        outState.DepthFunc = convertComparisonFunc(inState.depthFunc);
        outState.StencilEnable = inState.stencilEnable ? TRUE : FALSE;
        outState.StencilReadMask = (UINT8)inState.stencilReadMask;
        outState.StencilWriteMask = (UINT8)inState.stencilWriteMask;
        outState.FrontFace.StencilFailOp = convertStencilOp(inState.frontFaceStencil.failOp);
        outState.FrontFace.StencilDepthFailOp = convertStencilOp(inState.frontFaceStencil.depthFailOp);
        outState.FrontFace.StencilPassOp = convertStencilOp(inState.frontFaceStencil.passOp);
        outState.FrontFace.StencilFunc = convertComparisonFunc(inState.frontFaceStencil.stencilFunc);
        outState.BackFace.StencilFailOp = convertStencilOp(inState.backFaceStencil.failOp);
        outState.BackFace.StencilDepthFailOp = convertStencilOp(inState.backFaceStencil.depthFailOp);
        outState.BackFace.StencilPassOp = convertStencilOp(inState.backFaceStencil.passOp);
        outState.BackFace.StencilFunc = convertComparisonFunc(inState.backFaceStencil.stencilFunc);
    }

    void TranslateRasterizerState(const RasterState& inState, D3D12_RASTERIZER_DESC& outState)
    {
        switch (inState.fillMode)
        {
        case RasterFillMode::Solid:
            outState.FillMode = D3D12_FILL_MODE_SOLID;
            break;
        case RasterFillMode::Wireframe:
            outState.FillMode = D3D12_FILL_MODE_WIREFRAME;
            break;
        default:
            utils::InvalidEnum();
            break;
        }

        switch (inState.cullMode)
        {
        case RasterCullMode::Back:
            outState.CullMode = D3D12_CULL_MODE_BACK;
            break;
        case RasterCullMode::Front:
            outState.CullMode = D3D12_CULL_MODE_FRONT;
            break;
        case RasterCullMode::None:
            outState.CullMode = D3D12_CULL_MODE_NONE;
            break;
        default:
            utils::InvalidEnum();
            break;
        }

        outState.FrontCounterClockwise = inState.frontCounterClockwise ? TRUE : FALSE;
        outState.DepthBias = inState.depthBias;
        outState.DepthBiasClamp = inState.depthBiasClamp;
        outState.SlopeScaledDepthBias = inState.slopeScaledDepthBias;
        outState.DepthClipEnable = inState.depthClipEnable ? TRUE : FALSE;
        outState.MultisampleEnable = inState.multisampleEnable ? TRUE : FALSE;
        outState.AntialiasedLineEnable = inState.antialiasedLineEnable ? TRUE : FALSE;
        outState.ConservativeRaster = inState.conservativeRasterEnable ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        outState.ForcedSampleCount = inState.forcedSampleCount;
    }

} // namespace nvrhi::d3d12
