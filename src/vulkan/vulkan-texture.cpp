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

#include <algorithm>

#include "vulkan-backend.h"
#include <nvrhi/common/misc.h>

namespace nvrhi::vulkan
{

    static vk::ImageType textureDimensionToImageType(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
            return vk::ImageType::e1D;

        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            return vk::ImageType::e2D;

        case TextureDimension::Texture3D:
            return vk::ImageType::e3D;

        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return vk::ImageType::e2D;
        }
    }
    
    
    static vk::ImageViewType textureDimensionToImageViewType(TextureDimension dimension)
    {
        switch (dimension)
        {
        case TextureDimension::Texture1D:
            return vk::ImageViewType::e1D;

        case TextureDimension::Texture1DArray:
            return vk::ImageViewType::e1DArray;
            
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DMS:
            return vk::ImageViewType::e2D;

        case TextureDimension::Texture2DArray:
        case TextureDimension::Texture2DMSArray:
            return vk::ImageViewType::e2DArray;
            
        case TextureDimension::TextureCube:
            return vk::ImageViewType::eCube;
            
        case TextureDimension::TextureCubeArray:
            return vk::ImageViewType::eCubeArray;
            
        case TextureDimension::Texture3D:
            return vk::ImageViewType::e3D;

        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return vk::ImageViewType::e2D;
        }
    }

    static vk::Extent3D pickImageExtent(const TextureDesc& d)
    {
        return vk::Extent3D(d.width, d.height, d.depth);
    }

    static uint32_t pickImageLayers(const TextureDesc& d)
    {
        return d.arraySize;
    }

    static vk::ImageUsageFlags pickImageUsage(const TextureDesc& d)
    {
        const FormatInfo& formatInfo = getFormatInfo(d.format);

        // xxxnsubtil: may want to consider exposing this through nvrhi instead
        vk::ImageUsageFlags ret = vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eSampled;

        if (d.isRenderTarget)
        {
            if (formatInfo.hasDepth || formatInfo.hasStencil)
            {
                ret |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
            } else {
                ret |= vk::ImageUsageFlagBits::eColorAttachment;
            }
        }

        if (d.isUAV)
            ret |= vk::ImageUsageFlagBits::eStorage;

        if (d.isShadingRateSurface)
            ret |= vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;

        return ret;
    }

    static vk::SampleCountFlagBits pickImageSampleCount(const TextureDesc& d)
    {
        switch(d.sampleCount)
        {
            case 1:
                return vk::SampleCountFlagBits::e1;

            case 2:
                return vk::SampleCountFlagBits::e2;

            case 4:
                return vk::SampleCountFlagBits::e4;

            case 8:
                return vk::SampleCountFlagBits::e8;

            case 16:
                return vk::SampleCountFlagBits::e16;

            case 32:
                return vk::SampleCountFlagBits::e32;

            case 64:
                return vk::SampleCountFlagBits::e64;

            default:
                utils::InvalidEnum();
                return vk::SampleCountFlagBits::e1;
        }
    }

    // infer aspect flags for a given image format
    vk::ImageAspectFlags guessImageAspectFlags(vk::Format format)
    {
        switch(format)  // NOLINT(clang-diagnostic-switch-enum)
        {
            case vk::Format::eD16Unorm:
            case vk::Format::eX8D24UnormPack32:
            case vk::Format::eD32Sfloat:
                return vk::ImageAspectFlagBits::eDepth;

            case vk::Format::eS8Uint:
                return vk::ImageAspectFlagBits::eStencil;

            case vk::Format::eD16UnormS8Uint:
            case vk::Format::eD24UnormS8Uint:
            case vk::Format::eD32SfloatS8Uint:
                return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

            default:
                return vk::ImageAspectFlagBits::eColor;
        }
    }

    // a subresource usually shouldn't have both stencil and depth aspect flag bits set; this enforces that depending on viewType param
    vk::ImageAspectFlags guessSubresourceImageAspectFlags(vk::Format format, Texture::TextureSubresourceViewType viewType)
    {
        vk::ImageAspectFlags flags = guessImageAspectFlags(format);
        if ((flags & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
            == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
        {
            if (viewType == Texture::TextureSubresourceViewType::DepthOnly)
            {
                flags = flags & (~vk::ImageAspectFlagBits::eStencil);
            }
            else if(viewType == Texture::TextureSubresourceViewType::StencilOnly)
            {
                flags = flags & (~vk::ImageAspectFlagBits::eDepth);
            }
        }
        return flags;
    }

    vk::ImageCreateFlagBits pickImageFlags(const TextureDesc& d)
    {
        switch (d.dimension)
        {
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            return vk::ImageCreateFlagBits::eCubeCompatible;

        case TextureDimension::Texture2DArray:
        case TextureDimension::Texture2DMSArray:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture1D:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture3D:
        case TextureDimension::Texture2DMS:
            return (vk::ImageCreateFlagBits)0;

        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return (vk::ImageCreateFlagBits)0;
        }
    }

    // fills out all info fields in Texture based on a TextureDesc
    static void fillTextureInfo(Texture *texture, const TextureDesc& desc)
    {
        texture->desc = desc;

        vk::ImageType type = textureDimensionToImageType(desc.dimension);
        vk::Extent3D extent = pickImageExtent(desc);
        uint32_t numLayers = pickImageLayers(desc);
        vk::Format format = vk::Format(convertFormat(desc.format));
        vk::ImageUsageFlags usage = pickImageUsage(desc);
        vk::SampleCountFlagBits sampleCount = pickImageSampleCount(desc);
        vk::ImageCreateFlagBits flags = pickImageFlags(desc);

        texture->imageInfo = vk::ImageCreateInfo()
                                .setImageType(type)
                                .setExtent(extent)
                                .setMipLevels(desc.mipLevels)
                                .setArrayLayers(numLayers)
                                .setFormat(format)
                                .setInitialLayout(vk::ImageLayout::eUndefined)
                                .setUsage(usage)
                                .setSharingMode(vk::SharingMode::eExclusive)
                                .setSamples(sampleCount)
                                .setFlags(flags);
    }

    TextureSubresourceView& Texture::getSubresourceView(const TextureSubresourceSet& subresource, TextureDimension dimension, TextureSubresourceViewType viewtype)
    {
        // This function is called from createBindingSet etc. and therefore free-threaded.
        // It modifies the subresourceViews map associated with the texture.
        std::lock_guard lockGuard(m_Mutex);

        if (dimension == TextureDimension::Unknown)
            dimension = desc.dimension;

        auto cachekey = std::make_tuple(subresource,viewtype, dimension);
        auto iter = subresourceViews.find(cachekey);
        if (iter != subresourceViews.end())
        {
            return iter->second;
        }

        auto iter_pair = subresourceViews.emplace(cachekey, *this);
        auto& view = std::get<0>(iter_pair)->second;

        view.subresource = subresource;

        auto vkformat = nvrhi::vulkan::convertFormat(desc.format);

        vk::ImageAspectFlags aspectflags = guessSubresourceImageAspectFlags(vkformat, viewtype);
        view.subresourceRange = vk::ImageSubresourceRange()
                                    .setAspectMask(aspectflags)
                                    .setBaseMipLevel(subresource.baseMipLevel)
                                    .setLevelCount(subresource.numMipLevels)
                                    .setBaseArrayLayer(subresource.baseArraySlice)
                                    .setLayerCount(subresource.numArraySlices);

        vk::ImageViewType imageViewType = textureDimensionToImageViewType(dimension);

        auto viewInfo = vk::ImageViewCreateInfo()
                            .setImage(image)
                            .setViewType(imageViewType)
                            .setFormat(vkformat)
                            .setSubresourceRange(view.subresourceRange);

        if (viewtype == TextureSubresourceViewType::StencilOnly)
        {
            // D3D / HLSL puts stencil values in the second component to keep the illusion of combined depth/stencil.
            // Set a component swizzle so we appear to do the same.
            viewInfo.components.setG(vk::ComponentSwizzle::eR);
        }

        const vk::Result res = m_Context.device.createImageView(&viewInfo, m_Context.allocationCallbacks, &view.view);
        ASSERT_VK_OK(res);

        const std::string debugName = std::string("ImageView for: ") + utils::DebugNameToString(desc.debugName);
        m_Context.nameVKObject(VkImageView(view.view), vk::DebugReportObjectTypeEXT::eImageView, debugName.c_str());

        return view;
    }

    TextureHandle Device::createTexture(const TextureDesc& desc)
    {
        Texture *texture = new Texture(m_Context, m_Allocator);
        assert(texture);
        fillTextureInfo(texture, desc);

        vk::Result res = m_Context.device.createImage(&texture->imageInfo, m_Context.allocationCallbacks, &texture->image);
        ASSERT_VK_OK(res);
        CHECK_VK_FAIL(res)

        m_Context.nameVKObject(texture->image, vk::DebugReportObjectTypeEXT::eImage, desc.debugName.c_str());

        if (!desc.isVirtual)
        {
            res = m_Allocator.allocateTextureMemory(texture);
            ASSERT_VK_OK(res);
            CHECK_VK_FAIL(res)

            m_Context.nameVKObject(texture->memory, vk::DebugReportObjectTypeEXT::eDeviceMemory, desc.debugName.c_str());
        }

        return TextureHandle::Create(texture);
    }

    MemoryRequirements Device::getTextureMemoryRequirements(ITexture* _texture)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

        vk::MemoryRequirements vulkanMemReq;
        m_Context.device.getImageMemoryRequirements(texture->image, &vulkanMemReq);

        MemoryRequirements memReq;
        memReq.alignment = vulkanMemReq.alignment;
        memReq.size = vulkanMemReq.size;
        return memReq;
    }

    bool Device::bindTextureMemory(ITexture* _texture, IHeap* _heap, uint64_t offset)
    {
        Texture* texture = checked_cast<Texture*>(_texture);
        Heap* heap = checked_cast<Heap*>(_heap);

        if (texture->heap)
            return false;

        if (!texture->desc.isVirtual)
            return false;

        m_Context.device.bindImageMemory(texture->image, heap->memory, offset);

        texture->heap = heap;

        return true;
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice,
                                  ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* dst = checked_cast<Texture*>(_dst);
        Texture* src = checked_cast<Texture*>(_src);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        assert(m_CurrentCmdBuf);

        m_CurrentCmdBuf->referencedResources.push_back(dst);
        m_CurrentCmdBuf->referencedResources.push_back(src);

        TextureSubresourceSet srcSubresource = TextureSubresourceSet(
            resolvedSrcSlice.mipLevel, 1,
            resolvedSrcSlice.arraySlice, 1
        );

        const auto& srcSubresourceView = src->getSubresourceView(srcSubresource, TextureDimension::Unknown);

        TextureSubresourceSet dstSubresource = TextureSubresourceSet(
            resolvedDstSlice.mipLevel, 1,
            resolvedDstSlice.arraySlice, 1
        );

        const auto& dstSubresourceView = dst->getSubresourceView(dstSubresource, TextureDimension::Unknown);

        auto imageCopy = vk::ImageCopy()
                            .setSrcSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(srcSubresourceView.subresourceRange.aspectMask)
                                                .setMipLevel(srcSubresource.baseMipLevel)
                                                .setBaseArrayLayer(srcSubresource.baseArraySlice)
                                                .setLayerCount(srcSubresource.numArraySlices))
                            .setSrcOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
                            .setDstSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(dstSubresourceView.subresourceRange.aspectMask)
                                                .setMipLevel(dstSubresource.baseMipLevel)
                                                .setBaseArrayLayer(dstSubresource.baseArraySlice)
                                                .setLayerCount(dstSubresource.numArraySlices))
                            .setDstOffset(vk::Offset3D(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z))
                            .setExtent(vk::Extent3D(resolvedDstSlice.width, resolvedDstSlice.height, resolvedDstSlice.depth));


        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(src, TextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), ResourceStates::CopySource);
            requireTextureState(dst, TextureSubresourceSet(resolvedDstSlice.mipLevel, 1, resolvedDstSlice.arraySlice, 1), ResourceStates::CopyDest);
        }
        commitBarriers();

        m_CurrentCmdBuf->cmdBuf.copyImage(src->image, vk::ImageLayout::eTransferSrcOptimal,
                              dst->image, vk::ImageLayout::eTransferDstOptimal,
                              { imageCopy });
    }

    static void computeMipLevelInformation(const TextureDesc& desc, uint32_t mipLevel, uint32_t* widthOut, uint32_t* heightOut, uint32_t* depthOut)
    {
        uint32_t width = std::max(desc.width >> mipLevel, uint32_t(1));
        uint32_t height = std::max(desc.height >> mipLevel, uint32_t(1));
        uint32_t depth = std::max(desc.depth >> mipLevel, uint32_t(1));

        if (widthOut)
            *widthOut = width;
        if (heightOut)
            *heightOut = height;
        if (depthOut)
            *depthOut = depth;
    }

    void CommandList::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        endRenderPass();

        Texture* dest = checked_cast<Texture*>(_dest);

        TextureDesc desc = dest->getDesc();

        uint32_t mipWidth, mipHeight, mipDepth;
        computeMipLevelInformation(desc, mipLevel, &mipWidth, &mipHeight, &mipDepth);

        const FormatInfo& formatInfo = getFormatInfo(desc.format);
        uint32_t deviceNumCols = (mipWidth + formatInfo.blockSize - 1) / formatInfo.blockSize;
        uint32_t deviceNumRows = (mipHeight + formatInfo.blockSize - 1) / formatInfo.blockSize;
        uint32_t deviceRowPitch = deviceNumCols * formatInfo.bytesPerBlock;
        uint32_t deviceMemSize = deviceRowPitch * deviceNumRows * mipDepth;

        Buffer* uploadBuffer;
        uint64_t uploadOffset;
        void* uploadCpuVA;
        m_UploadManager->suballocateBuffer(
            deviceMemSize,
            &uploadBuffer,
            &uploadOffset,
            &uploadCpuVA,
            MakeVersion(m_CurrentCmdBuf->recordingID, m_CommandListParameters.queueType, false));

        size_t minRowPitch = std::min(size_t(deviceRowPitch), rowPitch);
        uint8_t* mappedPtr = (uint8_t*)uploadCpuVA;
        for (uint32_t slice = 0; slice < mipDepth; slice++)
        {
            const uint8_t* sourcePtr = (const uint8_t*)data + depthPitch * slice;
            for (uint32_t row = 0; row < deviceNumRows; row++)
            {
                memcpy(mappedPtr, sourcePtr, minRowPitch);
                mappedPtr += deviceRowPitch;
                sourcePtr += rowPitch;
            }
        }

        auto imageCopy = vk::BufferImageCopy()
            .setBufferOffset(uploadOffset)
            .setBufferRowLength(deviceNumCols * formatInfo.blockSize)
            .setBufferImageHeight(deviceNumRows * formatInfo.blockSize)
            .setImageSubresource(vk::ImageSubresourceLayers()
                .setAspectMask(guessImageAspectFlags(dest->imageInfo.format))
                .setMipLevel(mipLevel)
                .setBaseArrayLayer(arraySlice)
                .setLayerCount(1))
            .setImageExtent(vk::Extent3D().setWidth(mipWidth).setHeight(mipHeight).setDepth(mipDepth));

        assert(m_CurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(dest, TextureSubresourceSet(mipLevel, 1, arraySlice, 1), ResourceStates::CopyDest);
        }
        commitBarriers();

        m_CurrentCmdBuf->referencedResources.push_back(dest);

        m_CurrentCmdBuf->cmdBuf.copyBufferToImage(uploadBuffer->buffer,
            dest->image, vk::ImageLayout::eTransferDstOptimal,
            1, &imageCopy);
    }

    void CommandList::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources)
    {
        endRenderPass();

        Texture* dest = checked_cast<Texture*>(_dest);
        Texture* src = checked_cast<Texture*>(_src);

        TextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
            // let the validation layer handle the messages
            return;

        assert(m_CurrentCmdBuf);

        std::vector<vk::ImageResolve> regions;

        for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
        {
            vk::ImageSubresourceLayers dstLayers(vk::ImageAspectFlagBits::eColor, mipLevel + dstSR.baseMipLevel, dstSR.baseArraySlice, dstSR.numArraySlices);
            vk::ImageSubresourceLayers srcLayers(vk::ImageAspectFlagBits::eColor, mipLevel + srcSR.baseMipLevel, srcSR.baseArraySlice, srcSR.numArraySlices);

            regions.push_back(vk::ImageResolve()
                .setSrcSubresource(srcLayers)
                .setDstSubresource(dstLayers)
                .setExtent(vk::Extent3D(
                    dest->desc.width >> dstLayers.mipLevel, 
                    dest->desc.height >> dstLayers.mipLevel, 
                    dest->desc.depth >> dstLayers.mipLevel)));
        }

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(src, srcSR, ResourceStates::ResolveSource);
            requireTextureState(dest, dstSR, ResourceStates::ResolveDest);
        }
        commitBarriers();

        m_CurrentCmdBuf->cmdBuf.resolveImage(src->image, vk::ImageLayout::eTransferSrcOptimal, dest->image, vk::ImageLayout::eTransferDstOptimal, regions);
    }

    void CommandList::clearTexture(ITexture* _texture, TextureSubresourceSet subresources, const vk::ClearColorValue& clearValue)
    {
        endRenderPass();

        Texture* texture = checked_cast<Texture*>(_texture);
        assert(texture);
        assert(m_CurrentCmdBuf);
        
        subresources = subresources.resolve(texture->desc, false);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(texture, subresources, ResourceStates::CopyDest);
        }
        commitBarriers();

        vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseArrayLayer(subresources.baseArraySlice)
            .setLayerCount(subresources.numArraySlices)
            .setBaseMipLevel(subresources.baseMipLevel)
            .setLevelCount(subresources.numMipLevels);
        
        m_CurrentCmdBuf->cmdBuf.clearColorImage(texture->image,
            vk::ImageLayout::eTransferDstOptimal,
            &clearValue,
            1, &subresourceRange);
    }

    void CommandList::clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor)
    {
        auto clearValue = vk::ClearColorValue()
            .setFloat32({ clearColor.r, clearColor.g, clearColor.b, clearColor.a });

        clearTexture(texture, subresources, clearValue);
    }

    void CommandList::clearDepthStencilTexture(ITexture* _texture, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        endRenderPass();

        if (!clearDepth && !clearStencil)
        {
            return;
        }

        Texture* texture = checked_cast<Texture*>(_texture);
        assert(texture);
        assert(m_CurrentCmdBuf);
        
        subresources = subresources.resolve(texture->desc, false);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(texture, subresources, ResourceStates::CopyDest);
        }
        commitBarriers();

        vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlags();

        if (clearDepth)
            aspectFlags |= vk::ImageAspectFlagBits::eDepth;

        if (clearStencil)
            aspectFlags |= vk::ImageAspectFlagBits::eStencil;

        vk::ImageSubresourceRange subresourceRange = vk::ImageSubresourceRange()
            .setAspectMask(aspectFlags)
            .setBaseArrayLayer(subresources.baseArraySlice)
            .setLayerCount(subresources.numArraySlices)
            .setBaseMipLevel(subresources.baseMipLevel)
            .setLevelCount(subresources.numMipLevels);

        auto clearValue = vk::ClearDepthStencilValue(depth, uint32_t(stencil));
        m_CurrentCmdBuf->cmdBuf.clearDepthStencilImage(texture->image,
            vk::ImageLayout::eTransferDstOptimal,
            &clearValue,
            1, &subresourceRange);
    }

    void CommandList::clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        int clearColorInt = int(clearColor);

        auto clearValue = vk::ClearColorValue()
            .setUint32({ clearColor, clearColor, clearColor, clearColor })
            .setInt32({ clearColorInt, clearColorInt, clearColorInt, clearColorInt });

        clearTexture(texture, subresources, clearValue);
    }

    Object Texture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Image:
            return Object(image);
        case ObjectTypes::VK_DeviceMemory:
            return Object(memory);
        default:
            return nullptr;
        }
    }

    Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, TextureDimension dimension, bool /*isReadOnlyDSV*/)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_ImageView: 
        {
            if (format == Format::UNKNOWN)
                format = desc.format;

            const FormatInfo& formatInfo = getFormatInfo(format);

            TextureSubresourceViewType viewType = TextureSubresourceViewType::AllAspects;
            if (formatInfo.hasDepth && !formatInfo.hasStencil)
                viewType = TextureSubresourceViewType::DepthOnly;
            else if(!formatInfo.hasDepth && formatInfo.hasStencil)
                viewType = TextureSubresourceViewType::StencilOnly;

            return Object(getSubresourceView(subresources, dimension, viewType).view);
        }
        default:
            return nullptr;
        }
    }

    uint32_t Texture::getNumSubresources() const
    {
        return desc.mipLevels * desc.arraySize;
    }

    uint32_t Texture::getSubresourceIndex(uint32_t mipLevel, uint32_t arrayLayer) const
    {
        return mipLevel * desc.arraySize + arrayLayer;
    }

    Texture::~Texture()
    {
        for (auto& viewIter : subresourceViews)
        {
            auto& view = viewIter.second.view;
            m_Context.device.destroyImageView(view, m_Context.allocationCallbacks);
            view = vk::ImageView();
        }
        subresourceViews.clear();

        if (managed)
        {
            if (image)
            {
                m_Context.device.destroyImage(image, m_Context.allocationCallbacks);
                image = vk::Image();
            }

            if (memory)
            {
                m_Allocator.freeTextureMemory(this);
                memory = vk::DeviceMemory();
            }
        }
    }

    TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
    {
        if (_texture.integer == 0)
            return nullptr;

        if (objectType != ObjectTypes::VK_Image)
            return nullptr;

        vk::Image image(VkImage(_texture.integer));

        Texture *texture = new Texture(m_Context, m_Allocator);
        fillTextureInfo(texture, desc);

        texture->image = image;
        texture->managed = false;

        return TextureHandle::Create(texture);
    }

    static vk::BorderColor pickSamplerBorderColor(const SamplerDesc& d)
    {
        if (d.borderColor.r == 0.f && d.borderColor.g == 0.f && d.borderColor.b == 0.f)
        {
            if (d.borderColor.a == 0.f)
            {
                return vk::BorderColor::eFloatTransparentBlack;
            }

            if (d.borderColor.a == 1.f)
            {
                return vk::BorderColor::eFloatOpaqueBlack;
            }
        }

        if (d.borderColor.r == 1.f && d.borderColor.g == 1.f && d.borderColor.b == 1.f)
        {
            if (d.borderColor.a == 1.f)
            {
                return vk::BorderColor::eFloatOpaqueWhite;
            }
        }

        utils::NotSupported();
        return vk::BorderColor::eFloatOpaqueBlack;
    }

    SamplerHandle Device::createSampler(const SamplerDesc& desc)
    {
        Sampler *sampler = new Sampler(m_Context);

        const bool anisotropyEnable = desc.maxAnisotropy > 1.0f;

        sampler->desc = desc;
        sampler->samplerInfo = vk::SamplerCreateInfo()
                            .setMagFilter(desc.magFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                            .setMinFilter(desc.minFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                            .setMipmapMode(desc.mipFilter ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest)
                            .setAddressModeU(convertSamplerAddressMode(desc.addressU))
                            .setAddressModeV(convertSamplerAddressMode(desc.addressV))
                            .setAddressModeW(convertSamplerAddressMode(desc.addressW))
                            .setMipLodBias(desc.mipBias)
                            .setAnisotropyEnable(anisotropyEnable)
                            .setMaxAnisotropy(anisotropyEnable ? desc.maxAnisotropy : 1.f)
                            .setCompareEnable(desc.reductionType == SamplerReductionType::Comparison)
                            .setCompareOp(vk::CompareOp::eLess)
                            .setMinLod(0.f)
                            .setMaxLod(std::numeric_limits<float>::max())
                            .setBorderColor(pickSamplerBorderColor(desc));

        vk::SamplerReductionModeCreateInfoEXT samplerReductionCreateInfo;
        if (desc.reductionType == SamplerReductionType::Minimum || desc.reductionType == SamplerReductionType::Maximum)
        {
            vk::SamplerReductionModeEXT reductionMode =
                desc.reductionType == SamplerReductionType::Maximum ? vk::SamplerReductionModeEXT::eMax : vk::SamplerReductionModeEXT::eMin;
            samplerReductionCreateInfo.setReductionMode(reductionMode);

            sampler->samplerInfo.setPNext(&samplerReductionCreateInfo);
        }

        const vk::Result res = m_Context.device.createSampler(&sampler->samplerInfo, m_Context.allocationCallbacks, &sampler->sampler);
        CHECK_VK_FAIL(res)
        
        return SamplerHandle::Create(sampler);
    }

    Object Sampler::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Sampler:
            return Object(sampler);
        default:
            return nullptr;
        }
    }

    Sampler::~Sampler() 
    { 
        m_Context.device.destroySampler(sampler);
    }

} // namespace nvrhi::vulkan
