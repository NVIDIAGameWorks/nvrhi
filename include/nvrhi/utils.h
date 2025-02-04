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

#pragma once

#include <mutex>
#include <nvrhi/nvrhi.h>

namespace nvrhi::utils
{
    NVRHI_API BlendState::RenderTarget CreateAddBlendState(
        BlendFactor srcBlend,
        BlendFactor dstBlend);


    NVRHI_API BufferDesc CreateStaticConstantBufferDesc(
        uint32_t byteSize,
        const char* debugName);

    NVRHI_API BufferDesc CreateVolatileConstantBufferDesc(
        uint32_t byteSize,
        const char* debugName,
        uint32_t maxVersions);

    NVRHI_API bool CreateBindingSetAndLayout(
        IDevice* device, 
        nvrhi::ShaderType visibility,
        uint32_t registerSpace,
        const BindingSetDesc& bindingSetDesc, 
        BindingLayoutHandle& bindingLayout, 
        BindingSetHandle& bindingSet);

    NVRHI_API void ClearColorAttachment(
        ICommandList* commandList,
        IFramebuffer* framebuffer,
        uint32_t attachmentIndex,
        Color color
    );

    NVRHI_API void ClearDepthStencilAttachment(
        ICommandList* commandList,
        IFramebuffer* framebuffer,
        float depth,
        uint32_t stencil
    );

    NVRHI_API void BuildBottomLevelAccelStruct(
        ICommandList* commandList,
        rt::IAccelStruct* as,
        const rt::AccelStructDesc& desc
    );

    // Places a UAV barrier on the provided texture.
    // Useful when doing multiple consecutive dispatch calls with the same resources but different constants.
    // Ignored if there was a call to setEnableUavBarriersForTexrure(..., false) on this texture.
    NVRHI_API void TextureUavBarrier(
        ICommandList* commandList,
        ITexture* texture);

    // Places a UAV barrier on the provided buffer.
    // Useful when doing multiple consecutive dispatch calls with the same resources but different constants.
    // Ignored if there was a call to setEnableUavBarriersForBuffer(..., false) on this buffer.
    NVRHI_API void BufferUavBarrier(
        ICommandList* commandList,
        IBuffer* buffer);

    // Selects a format from the supplied list that supports all the required features on the given device.
    // The formats are tested in the same order they're provided, and the first matching one is returned.
    // If no formats are matching, Format::UNKNOWN is returned.
    NVRHI_API Format ChooseFormat(
        IDevice* device,
        nvrhi::FormatSupport requiredFeatures,
        const nvrhi::Format* requestedFormats,
        size_t requestedFormatCount);
    
    NVRHI_API const char* GraphicsAPIToString(GraphicsAPI api);
    NVRHI_API const char* TextureDimensionToString(TextureDimension dimension);
    NVRHI_API const char* DebugNameToString(const std::string& debugName);
    NVRHI_API const char* ShaderStageToString(ShaderType stage);
    NVRHI_API const char* ResourceTypeToString(ResourceType type);
    NVRHI_API const char* FormatToString(Format format);
    NVRHI_API const char* CommandQueueToString(CommandQueue queue);

    std::string GenerateHeapDebugName(const HeapDesc& desc);
    std::string GenerateTextureDebugName(const TextureDesc& desc);
    std::string GenerateBufferDebugName(const BufferDesc& desc);

    void NotImplemented();
    void NotSupported();
    void InvalidEnum();

    class BitSetAllocator
    {
    public:
        explicit BitSetAllocator(size_t capacity, bool multithreaded);

        int allocate();
        void release(int index);
        [[nodiscard]] size_t getCapacity() const { return m_Allocated.size(); }

    private:
        int m_NextAvailable = 0;
        std::vector<bool> m_Allocated;
        bool m_MultiThreaded;
        std::mutex m_Mutex;
    };

    // Automatic begin/end marker for command list
    class ScopedMarker
    {
    public:
        ICommandList* m_commandList;
        ScopedMarker(ICommandList* commandList, const char* markerName) : m_commandList(commandList)
        {
            m_commandList->beginMarker(markerName);
        }

        ScopedMarker(CommandListHandle* commandList, const char* markerName) :
            ScopedMarker(commandList->Get(), markerName)
        {}

        ~ScopedMarker()
        {
            m_commandList->endMarker();
        }
    };

}
