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
    Object ComputePipeline::getNativeObject(ObjectType objectType)
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


    RefCountPtr<ID3D12PipelineState> Device::createPipelineState(const ComputePipelineDesc & state, RootSignature* pRS) const
    {
        RefCountPtr<ID3D12PipelineState> pipelineState;

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};

        desc.pRootSignature = pRS->handle;
        Shader* shader = checked_cast<Shader*>(state.CS.Get());
        desc.CS = { &shader->bytecode[0], shader->bytecode.size() };

#if NVRHI_D3D12_WITH_NVAPI
        if (!shader->extensions.empty())
        {
            NvAPI_Status status = NvAPI_D3D12_CreateComputePipelineState(m_Context.device, &desc, 
                NvU32(shader->extensions.size()), const_cast<const NVAPI_D3D12_PSO_EXTENSION_DESC**>(shader->extensions.data()), &pipelineState);

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                m_Context.error("Failed to create a compute pipeline state object with NVAPI extensions");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        const HRESULT hr = m_Context.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState));

        if (FAILED(hr))
        {
            m_Context.error("Failed to create a compute pipeline state object");
            return nullptr;
        }

        return pipelineState;
    }

    ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
    {
        RefCountPtr<RootSignature> pRS = getRootSignature(desc.bindingLayouts, false);
        RefCountPtr<ID3D12PipelineState> pPSO = createPipelineState(desc, pRS);

        if (pPSO == nullptr)
            return nullptr;

        ComputePipeline *pso = new ComputePipeline();

        pso->desc = desc;

        pso->rootSignature = pRS;
        pso->pipelineState = pPSO;

        return ComputePipelineHandle::Create(pso);
    }

    void CommandList::setComputeState(const ComputeState& state)
    {
        ComputePipeline* pso = checked_cast<ComputePipeline*>(state.pipeline);

        const bool updateRootSignature = !m_CurrentComputeStateValid || m_CurrentComputeState.pipeline == nullptr ||
            checked_cast<ComputePipeline*>(m_CurrentComputeState.pipeline)->rootSignature != pso->rootSignature;

        bool updatePipeline = !m_CurrentComputeStateValid || m_CurrentComputeState.pipeline != state.pipeline;
        bool updateIndirectParams = !m_CurrentComputeStateValid || m_CurrentComputeState.indirectParams != state.indirectParams;

        uint32_t bindingUpdateMask = 0;
        if (!m_CurrentComputeStateValid || updateRootSignature)
            bindingUpdateMask = ~0u;

        if (commitDescriptorHeaps())
            bindingUpdateMask = ~0u;

        if (bindingUpdateMask == 0)
            bindingUpdateMask = arrayDifferenceMask(m_CurrentComputeState.bindings, state.bindings);

        if (updateRootSignature)
        {
            m_ActiveCommandList->commandList->SetComputeRootSignature(pso->rootSignature->handle);
        }

        if (updatePipeline)
        {
            m_ActiveCommandList->commandList->SetPipelineState(pso->pipelineState);
            
            m_Instance->referencedResources.push_back(pso);
        }

        setComputeBindings(state.bindings, bindingUpdateMask, state.indirectParams, updateIndirectParams, pso->rootSignature);

        unbindShadingRateState();
        
        m_CurrentGraphicsStateValid = false;
        m_CurrentComputeStateValid = true;
        m_CurrentMeshletStateValid = false;
        m_CurrentRayTracingStateValid = false;
        m_CurrentComputeState = state;
        
        commitBarriers();
    }

    void CommandList::updateComputeVolatileBuffers()
    {
        // If there are some volatile buffers bound, and they have been written into since the last dispatch or setComputeState, patch their views
        if (!m_AnyVolatileBufferWrites)
            return;

        for (VolatileConstantBufferBinding& parameter : m_CurrentComputeVolatileCBs)
        {
            const D3D12_GPU_VIRTUAL_ADDRESS currentGpuVA = m_VolatileConstantBufferAddresses[parameter.buffer];

            if (currentGpuVA != parameter.address)
            {
                m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(parameter.bindingPoint, currentGpuVA);

                parameter.address = currentGpuVA;
            }
        }

        m_AnyVolatileBufferWrites = false;
    }

    void CommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        updateComputeVolatileBuffers();

        m_ActiveCommandList->commandList->Dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandList::dispatchIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentComputeState.indirectParams);
        assert(indirectParams); // validation layer handles this

        updateComputeVolatileBuffers();

        m_ActiveCommandList->commandList->ExecuteIndirect(m_Context.dispatchIndirectSignature, 1, indirectParams->resource, offsetBytes, nullptr, 0);
    }

} // namespace nvrhi::d3d12
