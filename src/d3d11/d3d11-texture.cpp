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

#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>
#include <sstream>
#include <iomanip>

namespace nvrhi::d3d11
{

    Object Texture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Resource:
            return Object(resource);
        case ObjectTypes::SharedHandle:
            return Object(resource);
        default:
            return nullptr;
        }
    }

    Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, TextureDimension dimension, bool isReadOnlyDSV)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_RenderTargetView:
            return getRTV(format, subresources);
        case ObjectTypes::D3D11_DepthStencilView:
            return getDSV(subresources, isReadOnlyDSV);
        case ObjectTypes::D3D11_ShaderResourceView:
            return getSRV(format, subresources, dimension);
        case ObjectTypes::D3D11_UnorderedAccessView:
            return getUAV(format, subresources, dimension);
        default:
            return nullptr;
        }
    }

    TextureHandle Device::createTexture(const TextureDesc& d, CpuAccessMode cpuAccess) const
    {
        if (d.isVirtual)
        {
            utils::NotSupported();
            return nullptr;
        }

        D3D11_USAGE usage = (cpuAccess == CpuAccessMode::None ? D3D11_USAGE_DEFAULT : D3D11_USAGE_STAGING);

        const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(d.format);
        const FormatInfo& formatInfo = getFormatInfo(d.format);

        // convert flags
        UINT bindFlags;
        bindFlags = 0;
        if (cpuAccess == CpuAccessMode::None)
        {
            if (d.isShaderResource)
                bindFlags |= D3D11_BIND_SHADER_RESOURCE;
            if (d.isRenderTarget)
                bindFlags |= (formatInfo.hasDepth || formatInfo.hasStencil) ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
            if (d.isUAV)
                bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        UINT cpuAccessFlags = 0;
        if (cpuAccess == CpuAccessMode::Read)
            cpuAccessFlags = D3D11_CPU_ACCESS_READ;
        if (cpuAccess == CpuAccessMode::Write)
            cpuAccessFlags = D3D11_CPU_ACCESS_WRITE;

        UINT miscFlags = 0;
        bool isShared = false;
        if ((d.sharedResourceFlags & SharedResourceFlags::Shared_NTHandle) != 0) {
            miscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            isShared = true;
        }
        else if ((d.sharedResourceFlags & SharedResourceFlags::Shared) != 0) {
            miscFlags |= D3D11_RESOURCE_MISC_SHARED;
            isShared = true;
        }

        RefCountPtr<ID3D11Resource> pResource;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray: {

            D3D11_TEXTURE1D_DESC desc11;
            desc11.Width = d.width;
            desc11.MipLevels = d.mipLevels;
            desc11.ArraySize = d.arraySize;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;
            desc11.MiscFlags = miscFlags;

            RefCountPtr<ID3D11Texture1D> newTexture;
            const HRESULT res = m_Context.device->CreateTexture1D(&desc11, nullptr, &newTexture);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateTexture1D call failed for texture " << utils::DebugNameToString(d.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
                return nullptr;
            }

            pResource = newTexture;
            break;
        }
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray: {

            D3D11_TEXTURE2D_DESC desc11;
            desc11.Width = d.width;
            desc11.Height = d.height;
            desc11.MipLevels = d.mipLevels;
            desc11.ArraySize = d.arraySize;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.SampleDesc.Count = d.sampleCount;
            desc11.SampleDesc.Quality = d.sampleQuality;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;

            if (d.dimension == TextureDimension::TextureCube || d.dimension == TextureDimension::TextureCubeArray)
                desc11.MiscFlags = miscFlags | D3D11_RESOURCE_MISC_TEXTURECUBE;
            else
                desc11.MiscFlags = miscFlags;

            RefCountPtr<ID3D11Texture2D> newTexture;
            const HRESULT res = m_Context.device->CreateTexture2D(&desc11, nullptr, &newTexture);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateTexture2D call failed for texture " << utils::DebugNameToString(d.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
                return nullptr;
            }

            pResource = newTexture;
            break;
        }

        case TextureDimension::Texture3D: {

            D3D11_TEXTURE3D_DESC desc11;
            desc11.Width = d.width;
            desc11.Height = d.height;
            desc11.Depth = d.depth;
            desc11.MipLevels = d.mipLevels;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;
            desc11.MiscFlags = miscFlags;

            RefCountPtr<ID3D11Texture3D> newTexture;
            HRESULT res = m_Context.device->CreateTexture3D(&desc11, nullptr, &newTexture);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateTexture3D call failed for texture " << utils::DebugNameToString(d.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
                return nullptr;
            }

            pResource = newTexture;
            break;
        }

        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return nullptr;
        }

        if (!d.debugName.empty())
            SetDebugName(pResource, d.debugName.c_str());
        
        HANDLE sharedHandle = nullptr;
        if(isShared)
        {
            RefCountPtr<IDXGIResource1 > pDxgiResource1;
            if (SUCCEEDED(pResource->QueryInterface(IID_PPV_ARGS(&pDxgiResource1))))
                pDxgiResource1->GetSharedHandle(&sharedHandle);    
        }

        Texture* texture = new Texture(m_Context);
        texture->desc = d;
        texture->resource = pResource;
        texture->sharedHandle = sharedHandle;
        return TextureHandle::Create(texture);
    }

    TextureHandle Device::createTexture(const TextureDesc& d)
    {
        return createTexture(d, CpuAccessMode::None);
    }

    MemoryRequirements Device::getTextureMemoryRequirements(ITexture*)
    {
        utils::NotSupported();
        return MemoryRequirements();
    }

    bool Device::bindTextureMemory(ITexture*, IHeap*, uint64_t)
    {
        utils::NotSupported();
        return false;
    }
    
    nvrhi::TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
    {
        if (!_texture.pointer)
            return nullptr;

        if (objectType != ObjectTypes::D3D11_Resource)
            return nullptr;

        Texture* texture = new Texture(m_Context);
        texture->desc = desc;
        texture->resource = static_cast<ID3D11Resource*>(_texture.pointer);

        return TextureHandle::Create(texture);
    }
    
    StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        assert(cpuAccess != CpuAccessMode::None);
        StagingTexture *ret = new StagingTexture();
        TextureHandle t = createTexture(d, cpuAccess);
        ret->texture = checked_cast<Texture*>(t.Get());
        ret->cpuAccess = cpuAccess;
        return StagingTextureHandle::Create(ret);
    }

    void CommandList::clearTextureFloat(ITexture* _texture, TextureSubresourceSet subresources, const Color& clearColor)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(texture->desc.format);
        assert(!formatInfo.hasDepth && !formatInfo.hasStencil);
        assert(texture->desc.isUAV || texture->desc.isRenderTarget);
#endif

        subresources = subresources.resolve(texture->desc, false);
        
        for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);
            
            if (texture->desc.isUAV)
            {
                ID3D11UnorderedAccessView* uav = texture->getUAV(Format::UNKNOWN, currentMipSlice, TextureDimension::Unknown);

                m_Context.immediateContext->ClearUnorderedAccessViewFloat(uav, &clearColor.r);
            }
            else if (texture->desc.isRenderTarget)
            {
                ID3D11RenderTargetView* rtv = texture->getRTV(Format::UNKNOWN, currentMipSlice);

                m_Context.immediateContext->ClearRenderTargetView(rtv, &clearColor.r);
            }
            else
            {
                break;
            }
        }
    }

    void CommandList::clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!clearDepth && !clearStencil)
        {
            return;
        }

        Texture* texture = checked_cast<Texture*>(t);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(texture->desc.format);
        assert(texture->desc.isRenderTarget);
        assert(formatInfo.hasDepth || formatInfo.hasStencil);
#endif

        subresources = subresources.resolve(texture->getDesc(), false);

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);

            ID3D11DepthStencilView* dsv = texture->getDSV(currentMipSlice);

            if (dsv)
            {
                UINT clearFlags = 0;
                if (clearDepth)   clearFlags |= D3D11_CLEAR_DEPTH;
                if (clearStencil) clearFlags |= D3D11_CLEAR_STENCIL;
                m_Context.immediateContext->ClearDepthStencilView(dsv, clearFlags, depth, stencil);
            }
        }
    }

    void CommandList::clearTextureUInt(ITexture* _texture, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        Texture* texture = checked_cast<Texture*>(_texture);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(texture->desc.format);
        assert(!formatInfo.hasDepth && !formatInfo.hasStencil);
        assert(texture->desc.isUAV || texture->desc.isRenderTarget);
#endif

        subresources = subresources.resolve(texture->desc, false);

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);

            if (texture->desc.isUAV)
            {
                ID3D11UnorderedAccessView* uav = texture->getUAV(Format::UNKNOWN, currentMipSlice, TextureDimension::Unknown);

                uint32_t clearValues[4] = { clearColor, clearColor, clearColor, clearColor };
                m_Context.immediateContext->ClearUnorderedAccessViewUint(uav, clearValues);
            }
            else if (texture->desc.isRenderTarget)
            {
                ID3D11RenderTargetView* rtv = texture->getRTV(Format::UNKNOWN, currentMipSlice);

                float clearValues[4] = { float(clearColor), float(clearColor), float(clearColor), float(clearColor) };
                m_Context.immediateContext->ClearRenderTargetView(rtv, clearValues);
            }
            else
            {
                break;
            }
        }
    }

    void CommandList::copyTexture(ID3D11Resource *dst, const TextureDesc& dstDesc, const TextureSlice& dstSlice,
                                             ID3D11Resource *src, const TextureDesc& srcDesc, const TextureSlice& srcSlice)
    {
        auto resolvedSrcSlice = srcSlice.resolve(srcDesc);
        auto resolvedDstSlice = dstSlice.resolve(dstDesc);

        assert(resolvedDstSlice.width == resolvedSrcSlice.width);
        assert(resolvedDstSlice.height == resolvedSrcSlice.height);

        UINT srcSubresource = D3D11CalcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, srcDesc.mipLevels);
        UINT dstSubresource = D3D11CalcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, dstDesc.mipLevels);

        D3D11_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        m_Context.immediateContext->CopySubresourceRegion(dst,
                                       dstSubresource,
                                       resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
                                       src,
                                       srcSubresource,
                                       &srcBox);
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = checked_cast<Texture*>(_src);
        Texture* dst = checked_cast<Texture*>(_dst);

        copyTexture(dst->resource, dst->desc, dstSlice,
                    src->resource, src->desc, srcSlice);
    }

    void CommandList::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = checked_cast<Texture*>(_src);
        StagingTexture* dst = checked_cast<StagingTexture*>(_dst);

        copyTexture(dst->texture->resource, dst->texture->desc, dstSlice,
                    src->resource, src->desc, srcSlice);
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
    {
        StagingTexture* src = checked_cast<StagingTexture*>(_src);
        Texture* dst = checked_cast<Texture*>(_dst);

        copyTexture(dst->resource, dst->desc, dstSlice,
                    src->texture->resource, src->texture->desc, srcSlice);
    }

    void CommandList::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        Texture* dest = checked_cast<Texture*>(_dest);

        UINT subresource = D3D11CalcSubresource(mipLevel, arraySlice, dest->desc.mipLevels);

        m_Context.immediateContext->UpdateSubresource(dest->resource, subresource, nullptr, data, UINT(rowPitch), UINT(depthPitch));
    }

    void CommandList::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources)
    {
        Texture* dest = checked_cast<Texture*>(_dest);
        Texture* src = checked_cast<Texture*>(_src);

        TextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
            // let the validation layer handle the messages
            return;

        const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(dest->desc.format);

        for (ArraySlice arrayIndex = 0; arrayIndex < dstSR.numArraySlices; arrayIndex++)
        {
            for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
            {
                uint32_t dstSubresource = D3D11CalcSubresource(mipLevel + dstSR.baseMipLevel, arrayIndex + dstSR.baseArraySlice, dest->desc.mipLevels);
                uint32_t srcSubresource = D3D11CalcSubresource(mipLevel + srcSR.baseMipLevel, arrayIndex + srcSR.baseArraySlice, src->desc.mipLevels);
                m_Context.immediateContext->ResolveSubresource(dest->resource, dstSubresource, src->resource, srcSubresource, formatMapping.rtvFormat);
            }
        }
    }

    void *Device::mapStagingTexture(IStagingTexture* _stagingTexture, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        StagingTexture* stagingTexture = checked_cast<StagingTexture*>(_stagingTexture);

        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != CpuAccessMode::None);

        Texture *t = stagingTexture->texture;
        auto resolvedSlice = slice.resolve(t->desc);

        D3D11_MAP mapType;
        switch(cpuAccess)  // NOLINT(clang-diagnostic-switch-enum)
        {
            case CpuAccessMode::Read:
                assert(stagingTexture->cpuAccess == CpuAccessMode::Read);
                mapType = D3D11_MAP_READ;
                break;

            case CpuAccessMode::Write:
                assert(stagingTexture->cpuAccess == CpuAccessMode::Write);
                mapType = D3D11_MAP_WRITE;
                break;

            default:
                m_Context.error("Unsupported CpuAccessMode in mapStagingTexture");
                return nullptr;
        }

        UINT subresource = D3D11CalcSubresource(resolvedSlice.mipLevel, resolvedSlice.arraySlice, t->desc.mipLevels);

        D3D11_MAPPED_SUBRESOURCE res;
        if (SUCCEEDED(m_Context.immediateContext->Map(t->resource, subresource, mapType, 0, &res)))
        {
            stagingTexture->mappedSubresource = subresource;
            *outRowPitch = (size_t) res.RowPitch;
            return res.pData;
        } else {
            return nullptr;
        }
    }

    void Device::unmapStagingTexture(IStagingTexture* _t)
    {
        StagingTexture* t = checked_cast<StagingTexture*>(_t);

        assert(t->mappedSubresource != UINT(-1));
        m_Context.immediateContext->Unmap(t->texture->resource, t->mappedSubresource);
        t->mappedSubresource = UINT(-1);
    }
    
    ID3D11ShaderResourceView* Texture::getSRV(Format format, TextureSubresourceSet subresources, TextureDimension dimension)
    {
        if (format == Format::UNKNOWN)
        {
            format = desc.format;
        }

        if (dimension == TextureDimension::Unknown)
        {
            dimension = desc.dimension;
        }

        subresources = subresources.resolve(desc, false);

        RefCountPtr<ID3D11ShaderResourceView>& srvPtr = m_ShaderResourceViews[TextureBindingKey(subresources, format)];
        if (srvPtr == nullptr)
        {
            //we haven't seen this one before
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
            viewDesc.Format = getDxgiFormatMapping(format).srvFormat;

            switch (dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture1D.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture1DArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture2D.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2DArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture2DArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::TextureCube:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                viewDesc.TextureCube.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.TextureCube.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                viewDesc.TextureCubeArray.First2DArrayFace = subresources.baseArraySlice;
                viewDesc.TextureCubeArray.NumCubes = subresources.numArraySlices / 6;
                viewDesc.TextureCubeArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.TextureCubeArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture3D.MipLevels = subresources.numMipLevels;
                break;
            default: {
                std::stringstream ss;
                ss << "Texture " << utils::DebugNameToString(desc.debugName)
                    << " has unsupported dimension for SRV: " << utils::TextureDimensionToString(desc.dimension);
                m_Context.error(ss.str());
                return nullptr;
            }
            }

            const HRESULT res = m_Context.device->CreateShaderResourceView(resource, &viewDesc, &srvPtr);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateShaderResourceView call failed for texture " << utils::DebugNameToString(desc.debugName)
                   << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
            }
        }
        return srvPtr;
    }

    ID3D11RenderTargetView* Device::getRTVForAttachment(const FramebufferAttachment& attachment)
    {
        Texture* texture = checked_cast<Texture*>(attachment.texture);

        if (texture)
            return texture->getRTV(attachment.format, attachment.subresources);

        return nullptr;
    }

    ID3D11RenderTargetView* Texture::getRTV(Format format, TextureSubresourceSet subresources)
    {
        if (format == Format::UNKNOWN)
        {
            format = desc.format;
        }

        subresources = subresources.resolve(desc, true);

        RefCountPtr<ID3D11RenderTargetView>& rtvPtr = m_RenderTargetViews[TextureBindingKey(subresources, format)];
        if (rtvPtr == nullptr)
        {
            //we haven't seen this one before
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc;
            viewDesc.Format = getDxgiFormatMapping(format).rtvFormat;

            switch (desc.dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.FirstWSlice = subresources.baseArraySlice;
                viewDesc.Texture3D.WSize = subresources.numArraySlices;
                viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
                break;
            default: {
                std::stringstream ss;
                ss << "Texture " << utils::DebugNameToString(desc.debugName)
                    << " has unsupported dimension for RTV: " << utils::TextureDimensionToString(desc.dimension);
                m_Context.error(ss.str());
                return nullptr;
            }
            }

            const HRESULT res = m_Context.device->CreateRenderTargetView(resource, &viewDesc, &rtvPtr);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateRenderTargetView call failed for texture " << utils::DebugNameToString(desc.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
            }
        }
        return rtvPtr;
    }

    ID3D11DepthStencilView* Device::getDSVForAttachment(const FramebufferAttachment& attachment)
    {
        Texture* texture = checked_cast<Texture*>(attachment.texture);

        if (texture)
            return texture->getDSV(attachment.subresources, attachment.isReadOnly);

        return nullptr;
    }

    ID3D11DepthStencilView* Texture::getDSV(TextureSubresourceSet subresources, bool isReadOnly)
    {
        subresources = subresources.resolve(desc, true);


        RefCountPtr<ID3D11DepthStencilView>& dsvPtr = m_DepthStencilViews[TextureBindingKey(subresources, desc.format, isReadOnly)];
        if (dsvPtr == nullptr)
        {
            //we haven't seen this one before
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc;
            viewDesc.Format = getDxgiFormatMapping(desc.format).rtvFormat;
            viewDesc.Flags = 0;

            if (isReadOnly)
            {
                viewDesc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
                if (viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                    viewDesc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
            }

            switch (desc.dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            default: {
                std::stringstream ss;
                ss << "Texture " << utils::DebugNameToString(desc.debugName)
                    << " has unsupported dimension for DSV: " << utils::TextureDimensionToString(desc.dimension);
                m_Context.error(ss.str());
                return nullptr;
            }
            }

            const HRESULT res = m_Context.device->CreateDepthStencilView(resource, &viewDesc, &dsvPtr);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateDepthStencilView call failed for texture " << utils::DebugNameToString(desc.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
            }
        }
        return dsvPtr;
    }

    ID3D11UnorderedAccessView* Texture::getUAV(Format format, TextureSubresourceSet subresources, TextureDimension dimension)
    {
        if (format == Format::UNKNOWN)
        {
            format = desc.format;
        }

        if (dimension == TextureDimension::Unknown)
        {
            dimension = desc.dimension;
        }

        subresources = subresources.resolve(desc, true);

        RefCountPtr<ID3D11UnorderedAccessView>& uavPtr = m_UnorderedAccessViews[TextureBindingKey(subresources, format)];
        if (uavPtr == nullptr)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc;
            viewDesc.Format = getDxgiFormatMapping(format).srvFormat;

            switch (dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.FirstWSlice = 0;
                viewDesc.Texture3D.WSize = desc.depth;
                viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
                break;
            default: {
                std::stringstream ss;
                ss << "Texture " << utils::DebugNameToString(desc.debugName)
                   << " has unsupported dimension for UAV: " << utils::TextureDimensionToString(desc.dimension);
                m_Context.error(ss.str());
                return nullptr;
            }
            }

            const HRESULT res = m_Context.device->CreateUnorderedAccessView(resource, &viewDesc, &uavPtr);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "CreateUnorderedAccessView call failed for texture " << utils::DebugNameToString(desc.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
            }
        }
        return uavPtr;
    }


} // nanmespace nvrhi::d3d11