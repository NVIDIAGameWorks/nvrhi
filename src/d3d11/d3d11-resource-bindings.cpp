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
#include <nvrhi/common/containers.h>
#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <algorithm>


namespace nvrhi::d3d11
{

BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc)
{
    BindingLayout* layout = new BindingLayout();
    layout->desc = desc;
    return BindingLayoutHandle::Create(layout);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc&)
{
    return nullptr;
}

BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)
{
    BindingSet *ret = new BindingSet();
    ret->desc = desc;
    ret->layout = layout;
    ret->visibility = layout->getDesc()->visibility;

    // See https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-vssetconstantbuffers1 
    const uint32_t sizeOfConstantInBytes = 16u;

    for (const BindingSetItem& binding : desc.bindings)
    {
        const uint32_t& slot = binding.slot;

        switch (binding.type)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ResourceType::Texture_SRV:
        {
            const auto texture = checked_cast<Texture*>(binding.resourceHandle);

            assert(ret->SRVs[slot] == nullptr);
            ret->SRVs[slot] = texture->getSRV(binding.format, binding.subresources, binding.dimension);

            ret->minSRVSlot = std::min(ret->minSRVSlot, slot);
            ret->maxSRVSlot = std::max(ret->maxSRVSlot, slot);
        }

        break;

        case ResourceType::Texture_UAV:
        {
            const auto texture = checked_cast<Texture*>(binding.resourceHandle);

            ret->UAVs[slot] = texture->getUAV(binding.format, binding.subresources, binding.dimension);

            ret->minUAVSlot = std::min(ret->minUAVSlot, slot);
            ret->maxUAVSlot = std::max(ret->maxUAVSlot, slot);
        }

        break;

        case ResourceType::TypedBuffer_SRV:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::RawBuffer_SRV:
        {
            const auto buffer = checked_cast<Buffer*>(binding.resourceHandle);

            assert(ret->SRVs[slot] == nullptr);
            ret->SRVs[slot] = buffer->getSRV(binding.format, binding.range, binding.type);

            ret->minSRVSlot = std::min(ret->minSRVSlot, slot);
            ret->maxSRVSlot = std::max(ret->maxSRVSlot, slot);
        }

        break;

        case ResourceType::TypedBuffer_UAV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_UAV:
        {
            const auto buffer = checked_cast<Buffer*>(binding.resourceHandle);
            ret->UAVs[slot] = buffer->getUAV(binding.format, binding.range, binding.type);

            ret->minUAVSlot = std::min(ret->minUAVSlot, slot);
            ret->maxUAVSlot = std::max(ret->maxUAVSlot, slot);
        }

        break;

        // DX11 makes no distinction between regular and volatile CBs
        case ResourceType::ConstantBuffer:
        case ResourceType::VolatileConstantBuffer:
        {
            assert(ret->constantBuffers[slot] == nullptr);

            const auto buffer = checked_cast<Buffer*>(binding.resourceHandle);
            const BufferRange range = binding.range.resolve(buffer->desc);

            ret->constantBuffers[slot] = buffer->resource.Get();

            // Calculate the offset and size of the CB range, in 16-byte constants.
            ret->constantBufferOffsets[slot] = (UINT)range.byteOffset / sizeOfConstantInBytes;
            ret->constantBufferCounts[slot] = align((UINT)range.byteSize, c_ConstantBufferOffsetSizeAlignment) / sizeOfConstantInBytes;

            ret->minConstantBufferSlot = std::min(ret->minConstantBufferSlot, slot);
            ret->maxConstantBufferSlot = std::max(ret->maxConstantBufferSlot, slot);
        }

        break;

        case ResourceType::Sampler:
        {
            assert(ret->samplers[slot] == nullptr);

            const auto sampler = checked_cast<Sampler*>(binding.resourceHandle);
            ret->samplers[slot] = sampler->sampler.Get();

            ret->minSamplerSlot = std::min(ret->minSamplerSlot, slot);
            ret->maxSamplerSlot = std::max(ret->maxSamplerSlot, slot);
        }

        break;

        case ResourceType::PushConstants:
        {
            ret->constantBuffers[slot] = m_Context.pushConstantBuffer;
            // Set the offset and size of the CB range, in 16-byte constants, same as for constant buffers.
            ret->constantBufferOffsets[slot] = 0;
            ret->constantBufferCounts[slot] = align(c_MaxPushConstantSize, c_ConstantBufferOffsetSizeAlignment) / sizeOfConstantInBytes;

            ret->minConstantBufferSlot = std::min(ret->minConstantBufferSlot, slot);
            ret->maxConstantBufferSlot = std::max(ret->maxConstantBufferSlot, slot);
        }

        break;

        default: {
            const std::string message = std::string("Unsupported resource binding type: ") + utils::ResourceTypeToString(binding.type);
            m_Context.error(message);
            continue;
        }
        }

        if (binding.resourceHandle)
        {
            ret->resources.push_back(binding.resourceHandle);
        }
    }

    return BindingSetHandle::Create(ret);
}

DescriptorTableHandle Device::createDescriptorTable(IBindingLayout*)
{
    return nullptr;
}

void Device::resizeDescriptorTable(IDescriptorTable*, uint32_t, bool)
{
    utils::NotSupported();
}

bool Device::writeDescriptorTable(IDescriptorTable*, const BindingSetItem&)
{
    utils::NotSupported();
    return false;
}

static ID3D11Buffer *NullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = { nullptr };
static ID3D11ShaderResourceView *NullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
static ID3D11SamplerState *NullSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = { nullptr };
static ID3D11UnorderedAccessView *NullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = { 0 };
static UINT NullUAVInitialCounts[D3D11_PS_CS_UAV_REGISTER_COUNT] = { 0 };

bool BindingSet::isSupersetOf(const BindingSet& other) const
{
    return minSRVSlot <= other.minSRVSlot && maxSRVSlot >= other.maxSRVSlot
        && minUAVSlot <= other.minUAVSlot && maxUAVSlot >= other.maxUAVSlot
        && minSamplerSlot <= other.minSamplerSlot && maxSamplerSlot >= other.maxSamplerSlot
        && minConstantBufferSlot <= other.minConstantBufferSlot && maxConstantBufferSlot >= other.maxConstantBufferSlot;
}

#define D3D11_SET_ARRAY(method, min, max, array) \
        if ((max) >= (min)) \
            m_Context.immediateContext->method(min, ((max) - (min) + 1), &(array)[min])
#define D3D11_SET_ARRAY1(method, min, max, array, offsets, counts) \
        if ((max) >= (min)) \
            m_Context.immediateContext1->method(min, ((max) - (min) + 1), &(array)[min], &(offsets)[min], &(counts)[min])

void CommandList::prepareToBindGraphicsResourceSets(
    const BindingSetVector& resourceSets, 
    const static_vector<BindingSetHandle, c_MaxBindingLayouts>* currentResourceSets,
    const IGraphicsPipeline* _currentPipeline,
    const IGraphicsPipeline* _newPipeline, 
    bool updateFramebuffer, 
    BindingSetVector& outSetsToBind) const
{
    outSetsToBind = resourceSets;

    if (currentResourceSets)
    {
        assert(_currentPipeline);

        const GraphicsPipeline* currentPipeline = checked_cast<const GraphicsPipeline*>(_currentPipeline);
        const GraphicsPipeline* newPipeline = checked_cast<const GraphicsPipeline*>(_newPipeline);

        BindingSetVector setsToUnbind;
        
        for (const BindingSetHandle& bindingSet : *currentResourceSets)
        {
            setsToUnbind.push_back(bindingSet);
        }

        if (currentPipeline->shaderMask == newPipeline->shaderMask)
        {
            for (uint32_t i = 0; i < uint32_t(outSetsToBind.size()); i++)
            {
                if (outSetsToBind[i])
                    for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                    {
                        if (outSetsToBind[i] == setsToUnbind[j])
                        {
                            outSetsToBind[i] = nullptr;
                            setsToUnbind[j] = nullptr;
                            break;
                        }
                    }
            }

            if (!updateFramebuffer)
            {
                for (uint32_t i = 0; i < uint32_t(outSetsToBind.size()); i++)
                {
                    if (outSetsToBind[i])
                        for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                        {
                            if (setsToUnbind[j] && checked_cast<BindingSet*>(outSetsToBind[i])->isSupersetOf(*checked_cast<BindingSet*>(setsToUnbind[j])))
                            {
                                setsToUnbind[j] = nullptr;
                            }
                        }
                }
            }
        }

        for (IBindingSet* _set : setsToUnbind)
        {
            if (!_set)
                continue;

            BindingSet* set = checked_cast<BindingSet*>(_set);

            ShaderType stagesToUnbind = set->visibility & currentPipeline->shaderMask;

            if ((stagesToUnbind & ShaderType::Vertex) != 0)
            {
                D3D11_SET_ARRAY(VSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
                D3D11_SET_ARRAY(VSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
                D3D11_SET_ARRAY(VSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);
            }

            if ((stagesToUnbind & ShaderType::Hull) != 0)
            {
                D3D11_SET_ARRAY(HSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
                D3D11_SET_ARRAY(HSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
                D3D11_SET_ARRAY(HSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);
            }

            if ((stagesToUnbind & ShaderType::Domain) != 0)
            {
                D3D11_SET_ARRAY(DSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
                D3D11_SET_ARRAY(DSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
                D3D11_SET_ARRAY(DSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);
            }

            if ((stagesToUnbind & ShaderType::Geometry) != 0)
            {
                D3D11_SET_ARRAY(GSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
                D3D11_SET_ARRAY(GSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
                D3D11_SET_ARRAY(GSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);
            }

            if ((stagesToUnbind & ShaderType::Pixel) != 0)
            {
                D3D11_SET_ARRAY(PSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
                D3D11_SET_ARRAY(PSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
                D3D11_SET_ARRAY(PSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);
            }
        }
    }
}


void CommandList::bindGraphicsResourceSets(
    const BindingSetVector& setsToBind,
    const IGraphicsPipeline* newPipeline) const
{
    for(IBindingSet* _set : setsToBind)
    {
        if (!_set)
            continue;

        BindingSet* set = checked_cast<BindingSet*>(_set);
        const GraphicsPipeline* pipeline = checked_cast<const GraphicsPipeline*>(newPipeline);

        ShaderType stagesToBind = set->visibility & pipeline->shaderMask;

        if ((stagesToBind & ShaderType::Vertex) != 0)
        {
            if (m_Context.immediateContext1)
            {
                D3D11_SET_ARRAY1(VSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
            }
            else
            {
                D3D11_SET_ARRAY(VSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
            }
            D3D11_SET_ARRAY(VSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
            D3D11_SET_ARRAY(VSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);
        }

        if ((stagesToBind & ShaderType::Hull) != 0)
        {
            if (m_Context.immediateContext1)
            {
                D3D11_SET_ARRAY1(HSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
            }
            else
            {
                D3D11_SET_ARRAY(HSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
            }
            D3D11_SET_ARRAY(HSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
            D3D11_SET_ARRAY(HSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);
        }

        if ((stagesToBind & ShaderType::Domain) != 0)
        {
            if (m_Context.immediateContext1)
            {
                D3D11_SET_ARRAY1(DSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
            }
            else
            {
                D3D11_SET_ARRAY(DSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
            }
            D3D11_SET_ARRAY(DSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
            D3D11_SET_ARRAY(DSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);
        }

        if ((stagesToBind & ShaderType::Geometry) != 0)
        {
            if (m_Context.immediateContext1)
            {
                D3D11_SET_ARRAY1(GSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
            }
            else
            {
                D3D11_SET_ARRAY(GSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
            }
            D3D11_SET_ARRAY(GSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
            D3D11_SET_ARRAY(GSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);
        }

        if ((stagesToBind & ShaderType::Pixel) != 0)
        {
            if (m_Context.immediateContext1)
            {
                D3D11_SET_ARRAY1(PSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
            }
            else
            {
                D3D11_SET_ARRAY(PSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
            }
            D3D11_SET_ARRAY(PSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
            D3D11_SET_ARRAY(PSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);
        }
    }
}

void CommandList::bindComputeResourceSets(
    const BindingSetVector& resourceSets,
    const static_vector<BindingSetHandle,c_MaxBindingLayouts>* currentResourceSets) const
{
    BindingSetVector setsToBind = resourceSets;

    if (currentResourceSets)
    {
        BindingSetVector setsToUnbind;

        for (const BindingSetHandle& bindingSet : *currentResourceSets)
        {
            setsToUnbind.push_back(bindingSet);
        }

        for (uint32_t i = 0; i < uint32_t(setsToBind.size()); i++)
        {
            if (setsToBind[i])
                for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                {
                    if (setsToBind[i] == setsToUnbind[j])
                    {
                        setsToBind[i] = nullptr;
                        setsToUnbind[j] = nullptr;
                        break;
                    }
                }
        }
        
        for (uint32_t i = 0; i < uint32_t(setsToBind.size()); i++)
        {
            if (setsToBind[i])
                for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                {
                    BindingSet* setToBind = checked_cast<BindingSet*>(setsToBind[i]);
                    BindingSet* setToUnbind = checked_cast<BindingSet*>(setsToUnbind[j]);

                    if (setToUnbind && setToBind->isSupersetOf(*setToUnbind) && setToUnbind->maxUAVSlot < setToUnbind->minUAVSlot)
                    {
                        setsToUnbind[j] = nullptr;
                    }
                }
        }

        for (IBindingSet* _set : setsToUnbind)
        {
            if (!_set)
                continue;

            BindingSet* set = checked_cast<BindingSet*>(_set);

            if ((set->visibility & ShaderType::Compute) == 0)
                continue;

            D3D11_SET_ARRAY(CSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(CSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(CSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, NullSamplers);

            if (set->maxUAVSlot >= set->minUAVSlot)
            {
                m_Context.immediateContext->CSSetUnorderedAccessViews(set->minUAVSlot,
                    set->maxUAVSlot - set->minUAVSlot + 1,
                    NullUAVs,
                    NullUAVInitialCounts);
            }
        }
    }

    for(IBindingSet* _set : resourceSets)
    {
        BindingSet* set = checked_cast<BindingSet*>(_set);

        if ((set->visibility & ShaderType::Compute) == 0)
            continue;

        if (m_Context.immediateContext1)
        {
            D3D11_SET_ARRAY1(CSSetConstantBuffers1, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers, set->constantBufferOffsets, set->constantBufferCounts);
        }
        else
        {
            D3D11_SET_ARRAY(CSSetConstantBuffers, set->minConstantBufferSlot, set->maxConstantBufferSlot, set->constantBuffers);
        }
        D3D11_SET_ARRAY(CSSetShaderResources, set->minSRVSlot, set->maxSRVSlot, set->SRVs);
        D3D11_SET_ARRAY(CSSetSamplers, set->minSamplerSlot, set->maxSamplerSlot, set->samplers);

        if (set->maxUAVSlot >= set->minUAVSlot)
        {
            m_Context.immediateContext->CSSetUnorderedAccessViews(set->minUAVSlot,
                set->maxUAVSlot - set->minUAVSlot + 1,
                &set->UAVs[set->minUAVSlot],
                NullUAVInitialCounts);
        }
    }
}

#undef D3D11_SET_ARRAY

} // namespace nvrhi::d3d11
