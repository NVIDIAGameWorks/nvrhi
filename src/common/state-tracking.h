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

#pragma once

#include <nvrhi/nvrhi.h>
#include <memory>
#include <unordered_map>

namespace nvrhi
{
    struct BufferStateExtension
    {
        const BufferDesc& descRef;
        ResourceStates permanentState = ResourceStates::Unknown;

        explicit BufferStateExtension(const BufferDesc& desc)
            : descRef(desc)
        { }
    };

    struct TextureStateExtension
    {
        const TextureDesc& descRef;
        ResourceStates permanentState = ResourceStates::Unknown;
        bool stateInitialized = false;
        bool isSamplerFeedback = false;

        explicit TextureStateExtension(const TextureDesc& desc)
            : descRef(desc)
        { }
    };

    struct TextureState
    {
        std::vector<ResourceStates> subresourceStates;
        ResourceStates state = ResourceStates::Unknown;
        bool enableUavBarriers = true;
        bool firstUavBarrierPlaced = false;
        bool permanentTransition = false;
    };

    struct BufferState
    {
        ResourceStates state = ResourceStates::Unknown;
        bool enableUavBarriers = true;
        bool firstUavBarrierPlaced = false;
        bool permanentTransition = false;
    };

    struct TextureBarrier
    {
        TextureStateExtension* texture = nullptr;
        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;
        bool entireTexture = false;
        ResourceStates stateBefore = ResourceStates::Unknown;
        ResourceStates stateAfter = ResourceStates::Unknown;
    };

    struct BufferBarrier
    {
        BufferStateExtension* buffer = nullptr;
        ResourceStates stateBefore = ResourceStates::Unknown;
        ResourceStates stateAfter = ResourceStates::Unknown;
    };

    class CommandListResourceStateTracker
    {
    public:
        explicit CommandListResourceStateTracker(IMessageCallback* messageCallback)
            : m_MessageCallback(messageCallback)
        { }

        // ICommandList-like interface

        void setEnableUavBarriersForTexture(TextureStateExtension* texture, bool enableBarriers);
        void setEnableUavBarriersForBuffer(BufferStateExtension* buffer, bool enableBarriers);

        void beginTrackingTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits);
        void beginTrackingBufferState(BufferStateExtension* buffer, ResourceStates stateBits);

        void setPermanentTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits);
        void setPermanentBufferState(BufferStateExtension* buffer, ResourceStates stateBits);

        ResourceStates getTextureSubresourceState(TextureStateExtension* texture, ArraySlice arraySlice, MipLevel mipLevel);
        ResourceStates getBufferState(BufferStateExtension* buffer);

        // Internal interface
        
        void requireTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates state);
        void requireBufferState(BufferStateExtension* buffer, ResourceStates state);

        void keepBufferInitialStates();
        void keepTextureInitialStates();
        void commandListSubmitted();

        [[nodiscard]] const std::vector<TextureBarrier>& getTextureBarriers() const { return m_TextureBarriers; }
        [[nodiscard]] const std::vector<BufferBarrier>& getBufferBarriers() const { return m_BufferBarriers; }
        void clearBarriers() { m_TextureBarriers.clear(); m_BufferBarriers.clear(); }

    private:
        IMessageCallback* m_MessageCallback;

        std::unordered_map<TextureStateExtension*, std::unique_ptr<TextureState>> m_TextureStates;
        std::unordered_map<BufferStateExtension*, std::unique_ptr<BufferState>> m_BufferStates;

        // Deferred transitions of textures and buffers to permanent states.
        // They are executed only when the command list is executed, not when the app calls setPermanentTextureState or setPermanentBufferState.
        std::vector<std::pair<TextureStateExtension*, ResourceStates>> m_PermanentTextureStates;
        std::vector<std::pair<BufferStateExtension*, ResourceStates>> m_PermanentBufferStates;

        std::vector<TextureBarrier> m_TextureBarriers;
        std::vector<BufferBarrier> m_BufferBarriers;

        TextureState* getTextureStateTracking(TextureStateExtension* texture, bool allowCreate);
        BufferState* getBufferStateTracking(BufferStateExtension* buffer, bool allowCreate);
    };

    bool verifyPermanentResourceState(ResourceStates permanentState, ResourceStates requiredState, bool isTexture, const std::string& debugName, IMessageCallback* messageCallback);

} // namespace nvrhi