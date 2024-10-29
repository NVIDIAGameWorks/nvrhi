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
    void CommandList::setResourceStatesForBindingSet(IBindingSet* _bindingSet)
    {
        if (_bindingSet->getDesc() == nullptr)
            return; // is bindless

        BindingSet* bindingSet = checked_cast<BindingSet*>(_bindingSet);

        for (auto bindingIndex : bindingSet->bindingsThatNeedTransitions)
        {
            const BindingSetItem& binding = bindingSet->desc.bindings[bindingIndex];

            switch (binding.type)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case ResourceType::Texture_SRV:
                requireTextureState(checked_cast<ITexture*>(binding.resourceHandle), binding.subresources, ResourceStates::ShaderResource);
                break;

            case ResourceType::Texture_UAV:
                requireTextureState(checked_cast<ITexture*>(binding.resourceHandle), binding.subresources, ResourceStates::UnorderedAccess);
                break;

            case ResourceType::TypedBuffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
            case ResourceType::RawBuffer_SRV:
                requireBufferState(checked_cast<IBuffer*>(binding.resourceHandle), ResourceStates::ShaderResource);
                break;

            case ResourceType::TypedBuffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
            case ResourceType::RawBuffer_UAV:
                requireBufferState(checked_cast<IBuffer*>(binding.resourceHandle), ResourceStates::UnorderedAccess);
                break;

            case ResourceType::ConstantBuffer:
                requireBufferState(checked_cast<IBuffer*>(binding.resourceHandle), ResourceStates::ConstantBuffer);
                break;

            case ResourceType::RayTracingAccelStruct:
                requireBufferState(checked_cast<AccelStruct*>(binding.resourceHandle)->dataBuffer, ResourceStates::AccelStructRead);
                break;

            default:
                // do nothing
                break;
            }
        }
    }
    
    void CommandList::requireTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates state)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.requireTextureState(texture, subresources, state);
    }

    void CommandList::requireSamplerFeedbackTextureState(ISamplerFeedbackTexture* _texture, ResourceStates state)
    {
        SamplerFeedbackTexture* texture = checked_cast<SamplerFeedbackTexture*>(_texture);

        m_StateTracker.requireTextureState(texture, AllSubresources, state);
    }
    
    void CommandList::requireBufferState(IBuffer* _buffer, ResourceStates state)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.requireBufferState(buffer, state);
    }

    void CommandList::commitBarriers()
    {
        const auto& textureBarriers = m_StateTracker.getTextureBarriers();
        const auto& bufferBarriers = m_StateTracker.getBufferBarriers();
        const size_t barrierCount = textureBarriers.size() + bufferBarriers.size();
        if (barrierCount == 0)
            return;

        // Allocate vector space for the barriers assuming 1:1 translation.
        // For partial transitions on multi-plane textures, original barriers may translate
        // into more than 1 barrier each, but that's relatively rare.
        m_D3DBarriers.clear();
        m_D3DBarriers.reserve(barrierCount);

        // Convert the texture barriers into D3D equivalents
        for (const auto& barrier : textureBarriers)
        {
            const Texture* texture = nullptr;
            ID3D12Resource* resource = nullptr;

            if (barrier.texture->isSamplerFeedback)
            {
                resource = static_cast<const SamplerFeedbackTexture*>(barrier.texture)->resource;
            }
            else
            {
                texture = static_cast<const Texture*>(barrier.texture);
                resource = texture->resource;
            }

            D3D12_RESOURCE_BARRIER d3dbarrier{};
            const D3D12_RESOURCE_STATES stateBefore = convertResourceStates(barrier.stateBefore);
            const D3D12_RESOURCE_STATES stateAfter = convertResourceStates(barrier.stateAfter);
            if (stateBefore != stateAfter)
            {
                d3dbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                d3dbarrier.Transition.StateBefore = stateBefore;
                d3dbarrier.Transition.StateAfter = stateAfter;
                d3dbarrier.Transition.pResource = resource;
                if (barrier.entireTexture)
                {
                    d3dbarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    m_D3DBarriers.push_back(d3dbarrier);
                }
                else
                {
                    for (uint8_t plane = 0; plane < texture->planeCount; plane++)
                    {
                        d3dbarrier.Transition.Subresource = calcSubresource(barrier.mipLevel, barrier.arraySlice, plane, texture->desc.mipLevels, texture->desc.arraySize);
                        m_D3DBarriers.push_back(d3dbarrier);
                    }
                }
            }
            else if (stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                d3dbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                d3dbarrier.UAV.pResource = resource;
                m_D3DBarriers.push_back(d3dbarrier);
            }
        }

        // Convert the buffer barriers into D3D equivalents
        for (const auto& barrier : bufferBarriers)
        {
            const Buffer* buffer = static_cast<const Buffer*>(barrier.buffer);

            D3D12_RESOURCE_BARRIER d3dbarrier{};
            const D3D12_RESOURCE_STATES stateBefore = convertResourceStates(barrier.stateBefore);
            const D3D12_RESOURCE_STATES stateAfter = convertResourceStates(barrier.stateAfter);
            if (stateBefore != stateAfter && 
                (stateBefore & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0 &&
                (stateAfter & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0)
            {
                d3dbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                d3dbarrier.Transition.StateBefore = stateBefore;
                d3dbarrier.Transition.StateAfter = stateAfter;
                d3dbarrier.Transition.pResource = buffer->resource;
                d3dbarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_D3DBarriers.push_back(d3dbarrier);
            }
            else if ((barrier.stateBefore == ResourceStates::AccelStructWrite && (barrier.stateAfter & (ResourceStates::AccelStructRead | ResourceStates::AccelStructBuildBlas)) != 0) ||
                (barrier.stateAfter == ResourceStates::AccelStructWrite && (barrier.stateBefore & (ResourceStates::AccelStructRead | ResourceStates::AccelStructBuildBlas)) != 0) ||
                (barrier.stateBefore == ResourceStates::OpacityMicromapWrite && (barrier.stateAfter & (ResourceStates::AccelStructBuildInput)) != 0) ||
                (stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
            {
                d3dbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                d3dbarrier.UAV.pResource = buffer->resource;
                m_D3DBarriers.push_back(d3dbarrier);
            }
        }

        if (m_D3DBarriers.size() > 0)
            m_ActiveCommandList->commandList->ResourceBarrier(uint32_t(m_D3DBarriers.size()), m_D3DBarriers.data());

        m_StateTracker.clearBarriers();
    }

    void CommandList::setEnableAutomaticBarriers(bool enable)
    {
        m_EnableAutomaticBarriers = enable;
    }

    void CommandList::setEnableUavBarriersForTexture(ITexture* _texture, bool enableBarriers)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.setEnableUavBarriersForTexture(texture, enableBarriers);
    }

    void CommandList::setEnableUavBarriersForBuffer(IBuffer* _buffer, bool enableBarriers)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.setEnableUavBarriersForBuffer(buffer, enableBarriers);
    }
    
    ShaderTableState* CommandList::getShaderTableStateTracking(rt::IShaderTable* shaderTable)
    {
        auto it = m_ShaderTableStates.find(shaderTable);

        if (it != m_ShaderTableStates.end())
        {
            return it->second.get();
        }

        std::unique_ptr<ShaderTableState> trackingRef = std::make_unique<ShaderTableState>();

        ShaderTableState* tracking = trackingRef.get();
        m_ShaderTableStates.insert(std::make_pair(shaderTable, std::move(trackingRef)));

        return tracking;
    }

    void CommandList::beginTrackingTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.beginTrackingTextureState(texture, subresources, stateBits);
    }

    void CommandList::beginTrackingBufferState(IBuffer* _buffer, ResourceStates stateBits)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.beginTrackingBufferState(buffer, stateBits);
    }

    void CommandList::setTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.requireTextureState(texture, subresources, stateBits);

        if (m_Instance)
            m_Instance->referencedResources.push_back(texture);
    }

    void CommandList::setBufferState(IBuffer* _buffer, ResourceStates stateBits)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.requireBufferState(buffer, stateBits);

        if (m_Instance)
            m_Instance->referencedResources.push_back(buffer);
    }

    void CommandList::setAccelStructState(rt::IAccelStruct* _as, ResourceStates stateBits)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        if (as->dataBuffer)
        {
            m_StateTracker.requireBufferState(as->dataBuffer, stateBits);
            
            if (m_Instance)
                m_Instance->referencedResources.push_back(as);
        }
    }

    void CommandList::setPermanentTextureState(ITexture* _texture, ResourceStates stateBits)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.setPermanentTextureState(texture, AllSubresources, stateBits);

        if (m_Instance)
            m_Instance->referencedResources.push_back(texture);
    }

    void CommandList::setPermanentBufferState(IBuffer* _buffer, ResourceStates stateBits)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.setPermanentBufferState(buffer, stateBits);
        
        if (m_Instance)
            m_Instance->referencedResources.push_back(buffer);
    }

    ResourceStates CommandList::getTextureSubresourceState(ITexture* _texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        return m_StateTracker.getTextureSubresourceState(texture, arraySlice, mipLevel);
    }

    ResourceStates CommandList::getBufferState(IBuffer* _buffer)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        return m_StateTracker.getBufferState(buffer);
    }
    
} // namespace nvrhi::d3d12