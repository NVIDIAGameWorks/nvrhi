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
    
    void CommandList::setResourceStatesForBindingSet(IBindingSet* _bindingSet)
    {
        if (_bindingSet == nullptr)
            return;
        if (_bindingSet->getDesc() == nullptr)
            return; // is bindless

        BindingSet* bindingSet = checked_cast<BindingSet*>(_bindingSet);

        for (auto bindingIndex : bindingSet->bindingsThatNeedTransitions)
        {
            const BindingSetItem& binding = bindingSet->desc.bindings[bindingIndex];

            switch(binding.type)  // NOLINT(clang-diagnostic-switch-enum)
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

                default:
                    // do nothing
                    break;
            }
        }
    }

    void CommandList::trackResourcesAndBarriers(const GraphicsState& state)
    {
        assert(m_EnableAutomaticBarriers);

        if (arraysAreDifferent(state.bindings, m_CurrentGraphicsState.bindings))
        {
            for (size_t i = 0; i < state.bindings.size(); i++)
            {
                setResourceStatesForBindingSet(state.bindings[i]);
            }
        }

        if (state.indexBuffer.buffer && state.indexBuffer.buffer != m_CurrentGraphicsState.indexBuffer.buffer)
        {
            requireBufferState(state.indexBuffer.buffer, ResourceStates::IndexBuffer);
        }

        if (arraysAreDifferent(state.vertexBuffers, m_CurrentGraphicsState.vertexBuffers))
        {
            for (const auto& vb : state.vertexBuffers)
            {
                requireBufferState(vb.buffer, ResourceStates::VertexBuffer);
            }
        }

        if (m_CurrentGraphicsState.framebuffer != state.framebuffer)
        {
            setResourceStatesForFramebuffer(state.framebuffer);
        }

        if (state.indirectParams && state.indirectParams != m_CurrentGraphicsState.indirectParams)
        {
            requireBufferState(state.indirectParams, ResourceStates::IndirectArgument);
        }
    }

    void CommandList::trackResourcesAndBarriers(const MeshletState& state)
    {
        assert(m_EnableAutomaticBarriers);
        
        if (arraysAreDifferent(state.bindings, m_CurrentMeshletState.bindings))
        {
            for (size_t i = 0; i < state.bindings.size(); i++)
            {
                setResourceStatesForBindingSet(state.bindings[i]);
            }
        }

        if (m_CurrentMeshletState.framebuffer != state.framebuffer)
        {
            setResourceStatesForFramebuffer(state.framebuffer);
        }

        if (state.indirectParams && state.indirectParams != m_CurrentMeshletState.indirectParams)
        {
            requireBufferState(state.indirectParams, ResourceStates::IndirectArgument);
        }
    }

    void CommandList::requireTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates state)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.requireTextureState(texture, subresources, state);
    }

    void CommandList::requireBufferState(IBuffer* _buffer, ResourceStates state)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.requireBufferState(buffer, state);
    }

    bool CommandList::anyBarriers() const
    {
        return !m_StateTracker.getBufferBarriers().empty() || !m_StateTracker.getTextureBarriers().empty();
    }

    void CommandList::commitBarriersInternal()
    {
        std::vector<vk::ImageMemoryBarrier> imageBarriers;
        std::vector<vk::BufferMemoryBarrier> bufferBarriers;
        vk::PipelineStageFlags beforeStageFlags = vk::PipelineStageFlags(0);
        vk::PipelineStageFlags afterStageFlags = vk::PipelineStageFlags(0);

        for (const TextureBarrier& barrier : m_StateTracker.getTextureBarriers())
        {
            ResourceStateMapping before = convertResourceState(barrier.stateBefore);
            ResourceStateMapping after = convertResourceState(barrier.stateAfter);

            if ((before.stageFlags != beforeStageFlags || after.stageFlags != afterStageFlags) && !imageBarriers.empty())
            {
                m_CurrentCmdBuf->cmdBuf.pipelineBarrier(beforeStageFlags, afterStageFlags,
                    vk::DependencyFlags(), {}, {}, imageBarriers);

                imageBarriers.clear();
            }

            beforeStageFlags = before.stageFlags;
            afterStageFlags = after.stageFlags;

            assert(after.imageLayout != vk::ImageLayout::eUndefined);

            Texture* texture = static_cast<Texture*>(barrier.texture);

            const FormatInfo& formatInfo = getFormatInfo(texture->desc.format);

            vk::ImageAspectFlags aspectMask = (vk::ImageAspectFlagBits)0;
            if (formatInfo.hasDepth) aspectMask |= vk::ImageAspectFlagBits::eDepth;
            if (formatInfo.hasStencil) aspectMask |= vk::ImageAspectFlagBits::eStencil;
            if (!aspectMask) aspectMask = vk::ImageAspectFlagBits::eColor;

            vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
                .setBaseArrayLayer(barrier.entireTexture ? 0 : barrier.arraySlice)
                .setLayerCount(barrier.entireTexture ? texture->desc.arraySize : 1)
                .setBaseMipLevel(barrier.entireTexture ? 0 : barrier.mipLevel)
                .setLevelCount(barrier.entireTexture ? texture->desc.mipLevels : 1)
                .setAspectMask(aspectMask);

            imageBarriers.push_back(vk::ImageMemoryBarrier()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setOldLayout(before.imageLayout)
                .setNewLayout(after.imageLayout)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(texture->image)
                .setSubresourceRange(subresourceRange));
        }

        if (!imageBarriers.empty())
        {
            m_CurrentCmdBuf->cmdBuf.pipelineBarrier(beforeStageFlags, afterStageFlags,
                vk::DependencyFlags(), {}, {}, imageBarriers);
        }

        beforeStageFlags = vk::PipelineStageFlags(0);
        afterStageFlags = vk::PipelineStageFlags(0);
        imageBarriers.clear();

        for (const BufferBarrier& barrier : m_StateTracker.getBufferBarriers())
        {
            ResourceStateMapping before = convertResourceState(barrier.stateBefore);
            ResourceStateMapping after = convertResourceState(barrier.stateAfter);

            if ((before.stageFlags != beforeStageFlags || after.stageFlags != afterStageFlags) && !bufferBarriers.empty())
            {
                m_CurrentCmdBuf->cmdBuf.pipelineBarrier(beforeStageFlags, afterStageFlags,
                    vk::DependencyFlags(), {}, bufferBarriers, {});

                bufferBarriers.clear();
            }

            beforeStageFlags = before.stageFlags;
            afterStageFlags = after.stageFlags;

            Buffer* buffer = static_cast<Buffer*>(barrier.buffer);

            bufferBarriers.push_back(vk::BufferMemoryBarrier()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(buffer->buffer)
                .setOffset(0)
                .setSize(buffer->desc.byteSize));
        }

        if (!bufferBarriers.empty())
        {
            m_CurrentCmdBuf->cmdBuf.pipelineBarrier(beforeStageFlags, afterStageFlags,
                vk::DependencyFlags(), {}, bufferBarriers, {});
        }
        bufferBarriers.clear();

        m_StateTracker.clearBarriers();
    }

    void CommandList::commitBarriersInternal_synchronization2()
    {
        std::vector<vk::ImageMemoryBarrier2> imageBarriers;
        std::vector<vk::BufferMemoryBarrier2> bufferBarriers;

        for (const TextureBarrier& barrier : m_StateTracker.getTextureBarriers())
        {
            ResourceStateMapping2 before = convertResourceState2(barrier.stateBefore);
            ResourceStateMapping2 after = convertResourceState2(barrier.stateAfter);

            assert(after.imageLayout != vk::ImageLayout::eUndefined);

            Texture* texture = static_cast<Texture*>(barrier.texture);

            const FormatInfo& formatInfo = getFormatInfo(texture->desc.format);

            vk::ImageAspectFlags aspectMask = (vk::ImageAspectFlagBits)0;
            if (formatInfo.hasDepth) aspectMask |= vk::ImageAspectFlagBits::eDepth;
            if (formatInfo.hasStencil) aspectMask |= vk::ImageAspectFlagBits::eStencil;
            if (!aspectMask) aspectMask = vk::ImageAspectFlagBits::eColor;

            vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
                .setBaseArrayLayer(barrier.entireTexture ? 0 : barrier.arraySlice)
                .setLayerCount(barrier.entireTexture ? texture->desc.arraySize : 1)
                .setBaseMipLevel(barrier.entireTexture ? 0 : barrier.mipLevel)
                .setLevelCount(barrier.entireTexture ? texture->desc.mipLevels : 1)
                .setAspectMask(aspectMask);

            imageBarriers.push_back(vk::ImageMemoryBarrier2()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setSrcStageMask(before.stageFlags)
                .setDstStageMask(after.stageFlags)
                .setOldLayout(before.imageLayout)
                .setNewLayout(after.imageLayout)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(texture->image)
                .setSubresourceRange(subresourceRange));
        }

        if (!imageBarriers.empty())
        {
            vk::DependencyInfo dep_info;
            dep_info.setImageMemoryBarriers(imageBarriers);

            m_CurrentCmdBuf->cmdBuf.pipelineBarrier2(dep_info);
        }

        imageBarriers.clear();

        for (const BufferBarrier& barrier : m_StateTracker.getBufferBarriers())
        {
            ResourceStateMapping2 before = convertResourceState2(barrier.stateBefore);
            ResourceStateMapping2 after = convertResourceState2(barrier.stateAfter);

            Buffer* buffer = static_cast<Buffer*>(barrier.buffer);

            bufferBarriers.push_back(vk::BufferMemoryBarrier2()
                .setSrcAccessMask(before.accessMask)
                .setDstAccessMask(after.accessMask)
                .setSrcStageMask(before.stageFlags)
                .setDstStageMask(after.stageFlags)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(buffer->buffer)
                .setOffset(0)
                .setSize(buffer->desc.byteSize));
        }

        if (!bufferBarriers.empty())
        {
            vk::DependencyInfo dep_info;
            dep_info.setBufferMemoryBarriers(bufferBarriers);

            m_CurrentCmdBuf->cmdBuf.pipelineBarrier2(dep_info);
        }
        bufferBarriers.clear();

        m_StateTracker.clearBarriers();
    }

    void CommandList::commitBarriers()
    {
        if (m_StateTracker.getBufferBarriers().empty() && m_StateTracker.getTextureBarriers().empty())
            return;

        endRenderPass();

        if (m_Context.extensions.KHR_synchronization2)
        {
            commitBarriersInternal_synchronization2();
        }
        else
        {
            commitBarriersInternal();
        }
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

        if (m_CurrentCmdBuf)
            m_CurrentCmdBuf->referencedResources.push_back(texture);
    }

    void CommandList::setBufferState(IBuffer* _buffer, ResourceStates stateBits)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.requireBufferState(buffer, stateBits);
        
        if (m_CurrentCmdBuf)
            m_CurrentCmdBuf->referencedResources.push_back(buffer);
    }
    
    void CommandList::setAccelStructState(rt::IAccelStruct* _as, ResourceStates stateBits)
    {
        AccelStruct* as = checked_cast<AccelStruct*>(_as);

        if (as->dataBuffer)
        {
            Buffer* buffer = checked_cast<Buffer*>(as->dataBuffer.Get());
            m_StateTracker.requireBufferState(buffer, stateBits);

            if (m_CurrentCmdBuf)
                m_CurrentCmdBuf->referencedResources.push_back(as);
        }
    }

    void CommandList::setPermanentTextureState(ITexture* _texture, ResourceStates stateBits)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        m_StateTracker.setPermanentTextureState(texture, AllSubresources, stateBits);

        if (m_CurrentCmdBuf)
            m_CurrentCmdBuf->referencedResources.push_back(texture);
    }

    void CommandList::setPermanentBufferState(IBuffer* _buffer, ResourceStates stateBits)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_StateTracker.setPermanentBufferState(buffer, stateBits);
        
        if (m_CurrentCmdBuf)
            m_CurrentCmdBuf->referencedResources.push_back(buffer);
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

} // namespace nvrhi::vulkan
