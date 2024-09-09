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

    extern vk::ImageAspectFlags guessImageAspectFlags(vk::Format format);

    // we follow DX conventions when mapping slices and mip levels:
    // for a 3D or array texture, array layers / 3d depth slices for a given mip slice
    // are consecutive in memory, with padding in between for alignment
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn705766(v=vs.85).aspx

    // compute the size of a mip level slice
    // this is the size of a single slice of a 3D texture / array for the given mip level
    size_t StagingTexture::computeSliceSize(uint32_t mipLevel)
    {
        const FormatInfo& formatInfo = getFormatInfo(desc.format);

        uint32_t wInBlocks = std::max(((desc.width >> mipLevel) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);
        uint32_t hInBlocks = std::max(((desc.height >> mipLevel) + formatInfo.blockSize - 1) / formatInfo.blockSize, 1u);

        size_t blockPitchBytes = wInBlocks * formatInfo.bytesPerBlock;
        return blockPitchBytes * hInBlocks;
    }

    static off_t alignBufferOffset(off_t off)
    {
        static constexpr off_t bufferAlignmentBytes = 4;
        return ((off + (bufferAlignmentBytes - 1)) / bufferAlignmentBytes) * bufferAlignmentBytes;
    }

    const StagingTextureRegion& StagingTexture::getSliceRegion(uint32_t mipLevel, uint32_t arraySlice, uint32_t z)
    {
        if (desc.depth != 1)
        {
            // Hard case, since each mip level has half the slices as the previous one.
            assert(arraySlice == 0);
            assert(z < desc.depth);

            uint32_t mipDepth = desc.depth;
            uint32_t index = 0;
            while (mipLevel-- > 0)
            {
                index += mipDepth;
                mipDepth = std::max(mipDepth, uint32_t(1));
            }
            return sliceRegions[index + z];
        }
        else if (desc.arraySize != 1)
        {
            // Easy case, since each mip level has a consistent number of slices.
            assert(z == 0);
            assert(arraySlice < desc.arraySize);
            assert(sliceRegions.size() == desc.mipLevels * desc.arraySize);
            return sliceRegions[mipLevel * desc.arraySize + arraySlice];
        }
        else
        {
            assert(arraySlice == 0);
            assert(z == 0);
            assert(sliceRegions.size() == desc.mipLevels);
            return sliceRegions[mipLevel];
        }
    }

    void StagingTexture::populateSliceRegions()
    {
        off_t curOffset = 0;

        sliceRegions.clear();

        for(uint32_t mip = 0; mip < desc.mipLevels; mip++)
        {
            auto sliceSize = computeSliceSize(mip);

            uint32_t depth = std::max(desc.depth >> mip, uint32_t(1));
            uint32_t numSlices = desc.arraySize * depth;

            for (uint32_t slice = 0; slice < numSlices; slice++)
            {
                sliceRegions.push_back({ curOffset, sliceSize });

                // update offset for the next region
                curOffset = alignBufferOffset(off_t(curOffset + sliceSize));
            }
        }
    }

    StagingTextureHandle Device::createStagingTexture(const TextureDesc& desc, CpuAccessMode cpuAccess)
    {
        assert(cpuAccess != CpuAccessMode::None);

        StagingTexture *tex = new StagingTexture();
        tex->desc = desc;
        tex->populateSliceRegions();

        BufferDesc bufDesc;
        bufDesc.byteSize = tex->getBufferSize();
        assert(bufDesc.byteSize > 0);
        bufDesc.debugName = desc.debugName;
        bufDesc.cpuAccess = cpuAccess;

        BufferHandle internalBuffer = createBuffer(bufDesc);
        tex->buffer = checked_cast<Buffer*>(internalBuffer.Get());

        if (!tex->buffer)
        {
            delete tex;
            return nullptr;
        }

        return StagingTextureHandle::Create(tex);
    }

    void *Device::mapStagingTexture(IStagingTexture* _tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != CpuAccessMode::None);

        StagingTexture* tex = checked_cast<StagingTexture*>(_tex);

        auto resolvedSlice = slice.resolve(tex->desc);

        auto region = tex->getSliceRegion(resolvedSlice.mipLevel, resolvedSlice.arraySlice, resolvedSlice.z);

        assert((region.offset & 0x3) == 0); // per vulkan spec
        assert(region.size > 0);

        const FormatInfo& formatInfo = getFormatInfo(tex->desc.format);
        assert(outRowPitch);

        auto wInBlocks = resolvedSlice.width / formatInfo.blockSize;

        *outRowPitch = wInBlocks * formatInfo.bytesPerBlock;

        return mapBuffer(tex->buffer, cpuAccess, region.offset, region.size);
    }

    void Device::unmapStagingTexture(IStagingTexture* _tex)
    {
        StagingTexture* tex = checked_cast<StagingTexture*>(_tex);

        unmapBuffer(tex->buffer);
    }

    void CommandList::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = checked_cast<Texture*>(_src);
        StagingTexture* dst = checked_cast<StagingTexture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        assert(resolvedDstSlice.depth == 1);
        
        auto dstRegion = dst->getSliceRegion(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, resolvedDstSlice.z);
        assert((dstRegion.offset & 0x3) == 0); // per Vulkan spec

        TextureSubresourceSet srcSubresource = TextureSubresourceSet(
            resolvedSrcSlice.mipLevel, 1,
            resolvedSrcSlice.arraySlice, 1
        );

        auto imageCopy = vk::BufferImageCopy()
                            .setBufferOffset(dstRegion.offset)
                            .setBufferRowLength(resolvedDstSlice.width)
                            .setBufferImageHeight(resolvedDstSlice.height)
                            .setImageSubresource(vk::ImageSubresourceLayers()
                                                    .setAspectMask(guessImageAspectFlags(src->imageInfo.format))
                                                    .setMipLevel(resolvedSrcSlice.mipLevel)
                                                    .setBaseArrayLayer(resolvedSrcSlice.arraySlice)
                                                    .setLayerCount(1))
                            .setImageOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
                            .setImageExtent(vk::Extent3D(resolvedSrcSlice.width, resolvedSrcSlice.height, resolvedSrcSlice.depth));

        assert(m_CurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(dst->buffer, ResourceStates::CopyDest);
            requireTextureState(src, srcSubresource, ResourceStates::CopySource);
        }
        commitBarriers();

        m_CurrentCmdBuf->referencedResources.push_back(src);
        m_CurrentCmdBuf->referencedResources.push_back(dst);
        m_CurrentCmdBuf->referencedStagingBuffers.push_back(dst->buffer);

        m_CurrentCmdBuf->cmdBuf.copyImageToBuffer(src->image, vk::ImageLayout::eTransferSrcOptimal,
                                      dst->buffer->buffer, 1, &imageCopy);
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
    {
        StagingTexture* src = checked_cast<StagingTexture*>(_src);
        Texture* dst = checked_cast<Texture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);
        
        auto srcRegion = src->getSliceRegion(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, resolvedSrcSlice.z);

        assert((srcRegion.offset & 0x3) == 0); // per vulkan spec
        assert(srcRegion.size > 0);

        TextureSubresourceSet dstSubresource = TextureSubresourceSet(
            resolvedDstSlice.mipLevel, 1,
            resolvedDstSlice.arraySlice, 1
        );

        vk::Offset3D dstOffset(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z);

        auto imageCopy = vk::BufferImageCopy()
                            .setBufferOffset(srcRegion.offset)
                            .setBufferRowLength(resolvedSrcSlice.width)
                            .setBufferImageHeight(resolvedSrcSlice.height)
                            .setImageSubresource(vk::ImageSubresourceLayers()
                                                    .setAspectMask(guessImageAspectFlags(dst->imageInfo.format))
                                                    .setMipLevel(resolvedDstSlice.mipLevel)
                                                    .setBaseArrayLayer(resolvedDstSlice.arraySlice)
                                                    .setLayerCount(1))
                            .setImageOffset(dstOffset)
                            .setImageExtent(vk::Extent3D(resolvedDstSlice.width, resolvedDstSlice.height, resolvedDstSlice.depth));

        assert(m_CurrentCmdBuf);

        if (m_EnableAutomaticBarriers)
        {
            requireBufferState(src->buffer, ResourceStates::CopySource);
            requireTextureState(dst, dstSubresource, ResourceStates::CopyDest);
        }
        commitBarriers();

        m_CurrentCmdBuf->referencedResources.push_back(src);
        m_CurrentCmdBuf->referencedResources.push_back(dst);
        m_CurrentCmdBuf->referencedStagingBuffers.push_back(src->buffer);

        m_CurrentCmdBuf->cmdBuf.copyBufferToImage(src->buffer->buffer,
                                      dst->image, vk::ImageLayout::eTransferDstOptimal,
                                      1, &imageCopy);
    }

} // namespace nvrhi::vulkan
