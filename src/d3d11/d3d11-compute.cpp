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

namespace nvrhi::d3d11
{

    ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
    {
        ComputePipeline *pso = new ComputePipeline();
        pso->desc = desc;

        if (desc.CS) pso->shader = checked_cast<Shader*>(desc.CS.Get())->CS;

        return ComputePipelineHandle::Create(pso);
    }

    void CommandList::setComputeState(const ComputeState& state)
    {
        ComputePipeline* pso = checked_cast<ComputePipeline*>(state.pipeline);

        if (m_CurrentGraphicsStateValid)
        {
            // If the previous operation has been a Draw call, there is a possibility of RT/UAV/SRV hazards.
            // Unbind everything to be sure, and to avoid checking the binding sets against each other. 
            // This only happens on switches between compute and graphics modes.

            clearState();
        }

        bool updatePipeline = !m_CurrentComputeStateValid || pso != m_CurrentComputePipeline;
        bool updateBindings = updatePipeline || arraysAreDifferent(m_CurrentBindings, state.bindings);

        if (updatePipeline) m_Context.immediateContext->CSSetShader(pso->shader, nullptr, 0);
        if (updateBindings) bindComputeResourceSets(state.bindings, m_CurrentComputeStateValid ? &m_CurrentBindings : nullptr);

        m_CurrentIndirectBuffer = state.indirectParams;

        if (updatePipeline || updateBindings)
        {
            m_CurrentComputePipeline = pso;

            m_CurrentBindings.resize(state.bindings.size());
            for (size_t i = 0; i < state.bindings.size(); i++)
            {
                m_CurrentBindings[i] = state.bindings[i];
            }

            m_CurrentComputeStateValid = true;
        }
    }

    void CommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        m_Context.immediateContext->Dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandList::dispatchIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentIndirectBuffer.Get());
        
        if (indirectParams) // validation layer will issue an error otherwise
        {
            m_Context.immediateContext->DispatchIndirect(indirectParams->resource, (UINT)offsetBytes);
        }
    }

} // nanmespace nvrhi::d3d11