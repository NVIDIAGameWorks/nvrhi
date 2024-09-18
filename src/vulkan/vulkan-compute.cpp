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

#include "vulkan-backend.h"
#include <nvrhi/common/misc.h>

namespace nvrhi::vulkan
{
    ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
    {
        vk::Result res;

        assert(desc.CS);
        
        ComputePipeline *pso = new ComputePipeline(m_Context);
        pso->desc = desc;

        res = createPipelineLayout(
            pso->pipelineLayout,
            pso->pipelineBindingLayouts,
            pso->pushConstantVisibility,
            pso->descriptorSetIdxToBindingIdx,
            m_Context,
            desc.bindingLayouts);
        CHECK_VK_FAIL(res)

        Shader* CS = checked_cast<Shader*>(desc.CS.Get());

        // See createGraphicsPipeline() for a more expanded implementation
        // of shader specializations with multiple shaders in the pipeline

        size_t numShaders = 0;
        size_t numShadersWithSpecializations = 0;
        size_t numSpecializationConstants = 0;

        countSpecializationConstants(CS, numShaders, numShadersWithSpecializations, numSpecializationConstants);

        assert(numShaders == 1);

        std::vector<vk::SpecializationInfo> specInfos;
        std::vector<vk::SpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;

        specInfos.reserve(numShadersWithSpecializations);
        specMapEntries.reserve(numSpecializationConstants);
        specData.reserve(numSpecializationConstants);

        auto shaderStageInfo = makeShaderStageCreateInfo(CS, 
            specInfos, specMapEntries, specData);
        
        auto pipelineInfo = vk::ComputePipelineCreateInfo()
                                .setStage(shaderStageInfo)
                                .setLayout(pso->pipelineLayout);

        res = m_Context.device.createComputePipelines(m_Context.pipelineCache,
                                                    1, &pipelineInfo,
                                                    m_Context.allocationCallbacks,
                                                    &pso->pipeline);

        CHECK_VK_FAIL(res)

        return ComputePipelineHandle::Create(pso);
    }

    ComputePipeline::~ComputePipeline()
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

    Object ComputePipeline::getNativeObject(ObjectType objectType)
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

    void CommandList::setComputeState(const ComputeState& state)
    {
        endRenderPass();

        assert(m_CurrentCmdBuf);

        ComputePipeline* pso = checked_cast<ComputePipeline*>(state.pipeline);

        if (m_EnableAutomaticBarriers && arraysAreDifferent(state.bindings, m_CurrentComputeState.bindings))
        {
            for (size_t i = 0; i < state.bindings.size() && i < pso->desc.bindingLayouts.size(); i++)
            {
                BindingLayout* layout = checked_cast<BindingLayout*>(pso->desc.bindingLayouts[i].Get());

                if ((layout->desc.visibility & ShaderType::Compute) == 0)
                    continue;

                if (m_EnableAutomaticBarriers)
                {
                    setResourceStatesForBindingSet(state.bindings[i]);
                }
            }
        }

        if (m_CurrentComputeState.pipeline != state.pipeline)
        {
            m_CurrentCmdBuf->cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pso->pipeline);

            m_CurrentCmdBuf->referencedResources.push_back(state.pipeline);
        }

        if (arraysAreDifferent(m_CurrentComputeState.bindings, state.bindings) || m_AnyVolatileBufferWrites)
        {
            bindBindingSets(vk::PipelineBindPoint::eCompute, pso->pipelineLayout, state.bindings, pso->descriptorSetIdxToBindingIdx);
        }

        m_CurrentPipelineLayout = pso->pipelineLayout;
        m_CurrentPushConstantsVisibility = pso->pushConstantVisibility;

        if (state.indirectParams && state.indirectParams != m_CurrentComputeState.indirectParams)
        {
            Buffer* indirectParams = checked_cast<Buffer*>(state.indirectParams);

            m_CurrentCmdBuf->referencedResources.push_back(state.indirectParams);

            if (m_EnableAutomaticBarriers)
            {
                requireBufferState(indirectParams, ResourceStates::IndirectArgument);
            }
        }

        commitBarriers();

        m_CurrentGraphicsState = GraphicsState();
        m_CurrentComputeState = state;
        m_CurrentMeshletState = MeshletState();
        m_CurrentRayTracingState = rt::State();
        m_AnyVolatileBufferWrites = false;
    }

    void CommandList::updateComputeVolatileBuffers()
    {
        if (m_AnyVolatileBufferWrites && m_CurrentComputeState.pipeline)
        {
            ComputePipeline* pso = checked_cast<ComputePipeline*>(m_CurrentComputeState.pipeline);

            bindBindingSets(vk::PipelineBindPoint::eCompute, pso->pipelineLayout, m_CurrentComputeState.bindings, pso->descriptorSetIdxToBindingIdx);

            m_AnyVolatileBufferWrites = false;
        }
    }

    void CommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        assert(m_CurrentCmdBuf);

        updateComputeVolatileBuffers();

        m_CurrentCmdBuf->cmdBuf.dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandList::dispatchIndirect(uint32_t offsetBytes)
    {
        assert(m_CurrentCmdBuf);

        updateComputeVolatileBuffers();

        Buffer* indirectParams = checked_cast<Buffer*>(m_CurrentComputeState.indirectParams);
        assert(indirectParams);

        m_CurrentCmdBuf->cmdBuf.dispatchIndirect(indirectParams->buffer, offsetBytes);
    }

} // namespace nvrhi::vulkan
