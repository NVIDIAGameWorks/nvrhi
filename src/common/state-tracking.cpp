/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include "state-tracking.h"

#include <nvrhi/utils.h>

#include <sstream>

namespace nvrhi
{
    bool verifyPermanentResourceState(ResourceStates permanentState, ResourceStates requiredState, bool isTexture, const std::string& debugName, IMessageCallback* messageCallback)
    {
        if ((permanentState & requiredState) != requiredState)
        {
            std::stringstream ss;
            ss << "Permanent " << (isTexture ? "texture " : "buffer ") << utils::DebugNameToString(debugName)
                << " doesn't have the right state bits. Required: 0x" << std::hex << uint32_t(requiredState)
                << ", present: 0x" << std::hex << uint32_t(permanentState);
            messageCallback->message(MessageSeverity::Error, ss.str().c_str());
            return false;
        }

        return true;
    }

    static uint32_t calcSubresource(MipLevel mipLevel, ArraySlice arraySlice, const TextureDesc& desc)
    {
        return mipLevel + arraySlice * desc.mipLevels;
    }

    void CommandListResourceStateTracker::setEnableUavBarriersForTexture(TextureStateExtension* texture, bool enableBarriers)
    {
        TextureState* tracking = getTextureStateTracking(texture, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandListResourceStateTracker::setEnableUavBarriersForBuffer(BufferStateExtension* buffer, bool enableBarriers)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandListResourceStateTracker::beginTrackingTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        const TextureDesc& desc = texture->descRef;

        TextureState* tracking = getTextureStateTracking(texture, true);
        
        subresources = subresources.resolve(desc, false);

        if (subresources.isEntireTexture(desc))
        {
            tracking->state = stateBits;
            tracking->subresourceStates.clear();
        }
        else
        {
            tracking->subresourceStates.resize(desc.mipLevels * desc.arraySize, tracking->state);
            tracking->state = ResourceStates::Unknown;

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
                {
                    uint32_t subresource = calcSubresource(mipLevel, arraySlice, desc);
                    tracking->subresourceStates[subresource] = stateBits;
                }
            }
        }
    }

    void CommandListResourceStateTracker::beginTrackingBufferState(BufferStateExtension* buffer, ResourceStates stateBits)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        tracking->state = stateBits;
    }

    void CommandListResourceStateTracker::setPermanentTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        const TextureDesc& desc = texture->descRef;

        subresources = subresources.resolve(desc, false);

        bool permanent = true;
        if (!subresources.isEntireTexture(desc))
        {
            std::stringstream ss;
            ss << "Attempted to perform a permanent state transition on a subset of subresources of texture "
                << utils::DebugNameToString(desc.debugName);
            m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
            permanent = false;
        }

        requireTextureState(texture, subresources, stateBits);

        if (permanent)
        {
            m_PermanentTextureStates.push_back(std::make_pair(texture, stateBits));
            getTextureStateTracking(texture, true)->permanentTransition = true;
        }
    }

    void CommandListResourceStateTracker::setPermanentBufferState(BufferStateExtension* buffer, ResourceStates stateBits)
    {
        requireBufferState(buffer, stateBits);

        m_PermanentBufferStates.push_back(std::make_pair(buffer, stateBits));
    }

    ResourceStates CommandListResourceStateTracker::getTextureSubresourceState(TextureStateExtension* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        TextureState* tracking = getTextureStateTracking(texture, false);
        if (!tracking)
        {
            return texture->descRef.keepInitialState ? 
                (texture->stateInitialized ? texture->descRef.initialState : ResourceStates::Common) :
                ResourceStates::Unknown;
        }

        // whole resource
        if (tracking->subresourceStates.empty())
            return tracking->state;

        uint32_t subresource = calcSubresource(mipLevel, arraySlice, texture->descRef);
        return tracking->subresourceStates[subresource];
    }

    ResourceStates CommandListResourceStateTracker::getBufferState(BufferStateExtension* buffer)
    {
        BufferState* tracking = getBufferStateTracking(buffer, false);

        if (!tracking)
            return ResourceStates::Unknown;

        return tracking->state;
    }
    
    void CommandListResourceStateTracker::requireTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates state)
    {
        if (texture->permanentState != 0)
        {
            verifyPermanentResourceState(texture->permanentState, state, true, texture->descRef.debugName, m_MessageCallback);
            return;
        }

        subresources = subresources.resolve(texture->descRef, false);

        TextureState* tracking = getTextureStateTracking(texture, true);
        
        if (subresources.isEntireTexture(texture->descRef) && tracking->subresourceStates.empty())
        {
            // We're requiring state for the entire texture, and it's been tracked as entire texture too

            bool transitionNecessary = tracking->state != state;
            bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
                && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

            if (transitionNecessary || uavNecessary)
            {
                TextureBarrier barrier;
                barrier.texture = texture;
                barrier.entireTexture = true;
                barrier.stateBefore = tracking->state;
                barrier.stateAfter = state;
                m_TextureBarriers.push_back(barrier);
            }

            tracking->state = state;

            if (uavNecessary && !transitionNecessary)
            {
                tracking->firstUavBarrierPlaced = true;
            }
        }
        else
        {
            // Transition individual subresources

            // Make sure that we're tracking the texture on subresource level
            bool stateExpanded = false;
            if (tracking->subresourceStates.empty())
            {
                if (tracking->state == ResourceStates::Unknown)
                {
                    std::stringstream ss;
                    ss << "Unknown prior state of texture " << utils::DebugNameToString(texture->descRef.debugName) << ". "
                        "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                        "keepInitialState and initialState members of TextureDesc.";
                    m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                }

                tracking->subresourceStates.resize(texture->descRef.mipLevels * texture->descRef.arraySize, tracking->state);
                tracking->state = ResourceStates::Unknown;
                stateExpanded = true;
            }
            
            bool anyUavBarrier = false;

            for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
            {
                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    uint32_t subresourceIndex = calcSubresource(mipLevel, arraySlice, texture->descRef);

                    auto priorState = tracking->subresourceStates[subresourceIndex];

                    if (priorState == ResourceStates::Unknown && !stateExpanded)
                    {
                        std::stringstream ss;
                        ss << "Unknown prior state of texture " << utils::DebugNameToString(texture->descRef.debugName)
                            << " subresource (MipLevel = " << mipLevel << ", ArraySlice = " << arraySlice << "). "
                            "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                            "keepInitialState and initialState members of TextureDesc.";
                        m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                    }
                    
                    bool transitionNecessary = priorState != state;
                    bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
                        && !anyUavBarrier && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

                    if (transitionNecessary || uavNecessary)
                    {
                        TextureBarrier barrier;
                        barrier.texture = texture;
                        barrier.entireTexture = false;
                        barrier.mipLevel = mipLevel;
                        barrier.arraySlice = arraySlice;
                        barrier.stateBefore = priorState;
                        barrier.stateAfter = state;
                        m_TextureBarriers.push_back(barrier);
                    }

                    tracking->subresourceStates[subresourceIndex] = state;

                    if (uavNecessary && !transitionNecessary)
                    {
                        anyUavBarrier = true;
                        tracking->firstUavBarrierPlaced = true;
                    }
                }
            }
        }
    }

    void CommandListResourceStateTracker::requireBufferState(BufferStateExtension* buffer, ResourceStates state)
    {
        if (buffer->descRef.isVolatile)
            return;

        if (buffer->permanentState != 0)
        {
            verifyPermanentResourceState(buffer->permanentState, state, false, buffer->descRef.debugName, m_MessageCallback);

            return;
        }

        if (buffer->descRef.cpuAccess != CpuAccessMode::None)
        {
            // CPU-visible buffers can't change state
            return;
        }

        BufferState* tracking = getBufferStateTracking(buffer, true);

        if (tracking->state == ResourceStates::Unknown)
        {
            std::stringstream ss;
            ss << "Unknown prior state of buffer " << utils::DebugNameToString(buffer->descRef.debugName) << ". "
                "Call CommandList::beginTrackingBufferState(...) before using the buffer or use the "
                "keepInitialState and initialState members of BufferDesc.";
            m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
        }

        bool transitionNecessary = tracking->state != state;
        bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
            && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

        if (transitionNecessary)
        {
            // See if this buffer is already used for a different purpose in this batch.
            // If it is, combine the state bits.
            // Example: same buffer used as index and vertex buffer, or as SRV and indirect arguments.
            for (BufferBarrier& barrier : m_BufferBarriers)
            {
                if (barrier.buffer == buffer)
                {
                    barrier.stateAfter = ResourceStates(barrier.stateAfter | state);
                    tracking->state = barrier.stateAfter;
                    return;
                }
            }
        }

        if (transitionNecessary || uavNecessary)
        {
            BufferBarrier barrier;
            barrier.buffer = buffer;
            barrier.stateBefore = tracking->state;
            barrier.stateAfter = state;
            m_BufferBarriers.push_back(barrier);
        }

        if (uavNecessary && !transitionNecessary)
        {
            tracking->firstUavBarrierPlaced = true;
        }
    
        tracking->state = state;
    }

    void CommandListResourceStateTracker::keepBufferInitialStates()
    {
        for (auto& [buffer, tracking] : m_BufferStates)
        {
            if (buffer->descRef.keepInitialState && 
                !buffer->permanentState &&
                !buffer->descRef.isVolatile &&
                !tracking->permanentTransition)
            {
                requireBufferState(buffer, buffer->descRef.initialState);
            }
        }
    }

    void CommandListResourceStateTracker::keepTextureInitialStates()
    {
        for (auto& [texture, tracking] : m_TextureStates)
        {
            if (texture->descRef.keepInitialState && 
                !texture->permanentState && 
                !tracking->permanentTransition)
            {
                requireTextureState(texture, AllSubresources, texture->descRef.initialState);
            }
        }
    }

    void CommandListResourceStateTracker::commandListSubmitted()
    {
        for (auto [texture, state] : m_PermanentTextureStates)
        {
            if (texture->permanentState != 0 && texture->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of texture " << utils::DebugNameToString(texture->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(texture->permanentState) << " to 0x" << std::hex << uint32_t(state);
                m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                continue;
            }

            texture->permanentState = state;
        }
        m_PermanentTextureStates.clear();

        for (auto [buffer, state] : m_PermanentBufferStates)
        {
            if (buffer->permanentState != 0 && buffer->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of buffer " << utils::DebugNameToString(buffer->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(buffer->permanentState) << " to 0x" << std::hex << uint32_t(state);
                m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                continue;
            }

            buffer->permanentState = state;
        }
        m_PermanentBufferStates.clear();

        for (const auto& [texture, stateTracking] : m_TextureStates)
        {
            if (texture->descRef.keepInitialState && !texture->stateInitialized)
                texture->stateInitialized = true;
        }

        m_TextureStates.clear();
        m_BufferStates.clear();
    }

    TextureState* CommandListResourceStateTracker::getTextureStateTracking(TextureStateExtension* texture, bool allowCreate)
    {
        auto it = m_TextureStates.find(texture);

        if (it != m_TextureStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;
        
        std::unique_ptr<TextureState> trackingRef = std::make_unique<TextureState>();

        TextureState* tracking = trackingRef.get();
        m_TextureStates.insert(std::make_pair(texture, std::move(trackingRef)));
        
        if (texture->descRef.keepInitialState)
        {
            tracking->state = texture->stateInitialized ? texture->descRef.initialState : ResourceStates::Common;
        }

        return tracking;
    }

    BufferState* CommandListResourceStateTracker::getBufferStateTracking(BufferStateExtension* buffer, bool allowCreate)
    {
        auto it = m_BufferStates.find(buffer);

        if (it != m_BufferStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;

        std::unique_ptr<BufferState> trackingRef = std::make_unique<BufferState>();

        BufferState* tracking = trackingRef.get();
        m_BufferStates.insert(std::make_pair(buffer, std::move(trackingRef)));
                                                   
        if (buffer->descRef.keepInitialState)
        {
            tracking->state = buffer->descRef.initialState;
        }

        return tracking;
    }
} // namespace nvrhi