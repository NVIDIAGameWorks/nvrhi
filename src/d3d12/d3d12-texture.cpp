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
#include <iomanip>

namespace nvrhi::d3d12
{

    Object Texture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Resource:
            return Object(resource);
        default:
            return nullptr;
        }
    }

    Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, TextureDimension dimension, bool isReadOnlyDSV)
    {
        static_assert(sizeof(void*) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "Cannot typecast a descriptor to void*");
        
        switch (objectType)
        {
        case nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex;
            auto found = m_CustomSRVs.find(key);
            if (found == m_CustomSRVs.end())
            {
                descriptorIndex = m_Resources.shaderResourceViewHeap.allocateDescriptor();
                m_CustomSRVs[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_Resources.shaderResourceViewHeap.getCpuHandle(descriptorIndex);
                createSRV(cpuHandle.ptr, format, dimension, subresources);
                m_Resources.shaderResourceViewHeap.copyToShaderVisibleHeap(descriptorIndex);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(m_Resources.shaderResourceViewHeap.getGpuHandle(descriptorIndex).ptr);
        }

        case nvrhi::ObjectTypes::D3D12_UnorderedAccessViewGpuDescripror: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex;
            auto found = m_CustomUAVs.find(key);
            if (found == m_CustomUAVs.end())
            {
                descriptorIndex = m_Resources.shaderResourceViewHeap.allocateDescriptor();
                m_CustomUAVs[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_Resources.shaderResourceViewHeap.getCpuHandle(descriptorIndex);
                createUAV(cpuHandle.ptr, format, dimension, subresources);
                m_Resources.shaderResourceViewHeap.copyToShaderVisibleHeap(descriptorIndex);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(m_Resources.shaderResourceViewHeap.getGpuHandle(descriptorIndex).ptr);
        }
        case nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex;

            auto found = m_RenderTargetViews.find(key);
            if (found == m_RenderTargetViews.end())
            {
                descriptorIndex = m_Resources.renderTargetViewHeap.allocateDescriptor();
                m_RenderTargetViews[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_Resources.renderTargetViewHeap.getCpuHandle(descriptorIndex);
                createRTV(cpuHandle.ptr, format, subresources);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(m_Resources.renderTargetViewHeap.getCpuHandle(descriptorIndex).ptr);
        }

        case nvrhi::ObjectTypes::D3D12_DepthStencilViewDescriptor: {
            TextureBindingKey key = TextureBindingKey(subresources, format, isReadOnlyDSV);
            DescriptorIndex descriptorIndex;

            auto found = m_DepthStencilViews.find(key);
            if (found == m_DepthStencilViews.end())
            {
                descriptorIndex = m_Resources.depthStencilViewHeap.allocateDescriptor();
                m_DepthStencilViews[key] = descriptorIndex;

                const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_Resources.depthStencilViewHeap.getCpuHandle(descriptorIndex);
                createDSV(cpuHandle.ptr, subresources, isReadOnlyDSV);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(m_Resources.depthStencilViewHeap.getCpuHandle(descriptorIndex).ptr);
        }

        default:
            return nullptr;
        }
    }

    Texture::~Texture()
    {
        for (auto pair : m_RenderTargetViews)
            m_Resources.renderTargetViewHeap.releaseDescriptor(pair.second);

        for (auto pair : m_DepthStencilViews)
            m_Resources.depthStencilViewHeap.releaseDescriptor(pair.second);

        for (auto index : m_ClearMipLevelUAVs)
            m_Resources.shaderResourceViewHeap.releaseDescriptor(index);

        for (auto pair : m_CustomSRVs)
            m_Resources.shaderResourceViewHeap.releaseDescriptor(pair.second);

        for (auto pair : m_CustomUAVs)
            m_Resources.shaderResourceViewHeap.releaseDescriptor(pair.second);
    }

    StagingTexture::SliceRegion StagingTexture::getSliceRegion(ID3D12Device *device, const TextureSlice& slice)
    {
        SliceRegion ret;
        const UINT subresource = calcSubresource(slice.mipLevel, slice.arraySlice, 0,
            desc.mipLevels, desc.arraySize);

        assert(subresource < subresourceOffsets.size());

		UINT64 size = 0;
        device->GetCopyableFootprints(&resourceDesc, subresource, 1, subresourceOffsets[subresource], &ret.footprint, nullptr, nullptr, &size);
        ret.offset = off_t(ret.footprint.Offset);
		ret.size = size;
        return ret;
    }

    size_t StagingTexture::getSizeInBytes(ID3D12Device *device)
    {
        // figure out the index of the last subresource
        const UINT lastSubresource = calcSubresource(desc.mipLevels - 1, desc.arraySize - 1, 0,
            desc.mipLevels, desc.arraySize);
        assert(lastSubresource < subresourceOffsets.size());

        // compute size of last subresource
        UINT64 lastSubresourceSize;
        device->GetCopyableFootprints(&resourceDesc, lastSubresource, 1, 0,
            nullptr, nullptr, nullptr, &lastSubresourceSize);

        return subresourceOffsets[lastSubresource] + lastSubresourceSize;
    }

    void StagingTexture::computeSubresourceOffsets(ID3D12Device *device)
    {
        const UINT lastSubresource = calcSubresource(desc.mipLevels - 1, desc.arraySize - 1, 0,
            desc.mipLevels, desc.arraySize);

        const UINT numSubresources = lastSubresource + 1;
        subresourceOffsets.resize(numSubresources);

        UINT64 baseOffset = 0;
        for (UINT i = 0; i < lastSubresource + 1; i++)
        {
            UINT64 subresourceSize;
            device->GetCopyableFootprints(&resourceDesc, i, 1, 0,
                nullptr, nullptr, nullptr, &subresourceSize);

            subresourceOffsets[i] = baseOffset;
            baseOffset += subresourceSize;
            baseOffset = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT * ((baseOffset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        }
    }
    
    Object StagingTexture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Resource:
            return Object(buffer->resource);
        default:
            return nullptr;
        }
    }

    static D3D12_RESOURCE_DESC convertTextureDesc(const TextureDesc& d)
    {
        const auto& formatMapping = getDxgiFormatMapping(d.format);
        const FormatInfo& formatInfo = getFormatInfo(d.format);

        D3D12_RESOURCE_DESC desc = {};
        desc.Width = d.width;
        desc.Height = d.height;
        desc.MipLevels = UINT16(d.mipLevels);
        desc.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
        desc.SampleDesc.Count = d.sampleCount;
        desc.SampleDesc.Quality = d.sampleQuality;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            desc.DepthOrArraySize = UINT16(d.arraySize);
            break;
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.DepthOrArraySize = UINT16(d.arraySize);
            break;
        case TextureDimension::Texture3D:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            desc.DepthOrArraySize = UINT16(d.depth);
            break;
        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            break;
        }

        if (d.isRenderTarget)
        {
            if (formatInfo.hasDepth || formatInfo.hasStencil)
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            else
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }

        if (d.isUAV)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        return desc;
    }

    static D3D12_CLEAR_VALUE convertTextureClearValue(const TextureDesc& d)
    {
        const auto& formatMapping = getDxgiFormatMapping(d.format);
        const FormatInfo& formatInfo = getFormatInfo(d.format);
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = formatMapping.rtvFormat;
        if (formatInfo.hasDepth || formatInfo.hasStencil)
        {
            clearValue.DepthStencil.Depth = d.clearValue.r;
            clearValue.DepthStencil.Stencil = UINT8(d.clearValue.g);
        }
        else
        {
            clearValue.Color[0] = d.clearValue.r;
            clearValue.Color[1] = d.clearValue.g;
            clearValue.Color[2] = d.clearValue.b;
            clearValue.Color[3] = d.clearValue.a;
        }

        return clearValue;
    }

    TextureHandle Device::createTexture(const TextureDesc & d)
    {
        D3D12_RESOURCE_DESC rd = convertTextureDesc(d);
        D3D12_HEAP_PROPERTIES heapProps = {};
        D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;

        if ((d.sharedResourceFlags & SharedResourceFlags::Shared) != 0)
            heapFlags |= D3D12_HEAP_FLAG_SHARED;
        if ((d.sharedResourceFlags & SharedResourceFlags::Shared_CrossAdapter) != 0) {
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
            heapFlags |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        }

        Texture* texture = new Texture(m_Context, m_Resources, d, rd);

        if (d.isVirtual)
        {
            // The resource is created in bindTextureMemory
            return TextureHandle::Create(texture);
        }

        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clearValue = convertTextureClearValue(d);

        HRESULT hr = m_Context.device->CreateCommittedResource(
            &heapProps,
            heapFlags,
            &texture->resourceDesc,
            convertResourceStates(d.initialState),
            d.useClearValue ? &clearValue : nullptr,
            IID_PPV_ARGS(&texture->resource));

        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Failed to create texture " << utils::DebugNameToString(d.debugName) << ", error code = 0x";
            ss.setf(std::ios::hex, std::ios::basefield);
            ss << hr;
            m_Context.error(ss.str());
            
            delete texture;
            return nullptr;
        }

        texture->postCreate();

        return TextureHandle::Create(texture);
    }

    MemoryRequirements Device::getTextureMemoryRequirements(ITexture* _texture)
    {
        Texture* texture = checked_cast<Texture*>(_texture);
        
        D3D12_RESOURCE_ALLOCATION_INFO allocInfo = m_Context.device->GetResourceAllocationInfo(1, 1, &texture->resourceDesc);

        MemoryRequirements memReq;
        memReq.alignment = allocInfo.Alignment;
        memReq.size = allocInfo.SizeInBytes;
        return memReq;
    }

    bool Device::bindTextureMemory(ITexture* _texture, IHeap* _heap, uint64_t offset)
    {
        Texture* texture = checked_cast<Texture*>(_texture);
        Heap* heap = checked_cast<Heap*>(_heap);

        if (texture->resource)
            return false; // already bound

        if (!texture->desc.isVirtual)
            return false; // not supported


        D3D12_CLEAR_VALUE clearValue = convertTextureClearValue(texture->desc);

        HRESULT hr = m_Context.device->CreatePlacedResource(
            heap->heap, offset,
            &texture->resourceDesc,
            convertResourceStates(texture->desc.initialState),
            texture->desc.useClearValue ? &clearValue : nullptr,
            IID_PPV_ARGS(&texture->resource));

        if (FAILED(hr))
        {
            std::stringstream ss;
            ss << "Failed to create placed texture " << utils::DebugNameToString(texture->desc.debugName) << ", error code = 0x";
            ss.setf(std::ios::hex, std::ios::basefield);
            ss << hr;
            m_Context.error(ss.str());

            return false;
        }

        texture->heap = heap;
        texture->postCreate();

        return true;
    }
    
    TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
    {
        if (_texture.pointer == nullptr)
            return nullptr;

        if (objectType != ObjectTypes::D3D12_Resource)
            return nullptr;

        ID3D12Resource* pResource = static_cast<ID3D12Resource*>(_texture.pointer);

        Texture* texture = new Texture(m_Context, m_Resources, desc, pResource->GetDesc());
        texture->resource = pResource;
        texture->postCreate();

        return TextureHandle::Create(texture);
    }

    void Texture::postCreate()
    {
        if (!desc.debugName.empty())
        {
            std::wstring wname(desc.debugName.begin(), desc.debugName.end());
            resource->SetName(wname.c_str());
        }

        if (desc.isUAV)
        {
            m_ClearMipLevelUAVs.resize(desc.mipLevels);
            std::fill(m_ClearMipLevelUAVs.begin(), m_ClearMipLevelUAVs.end(), c_InvalidDescriptorIndex);
        }

        planeCount = m_Resources.getFormatPlaneCount(resourceDesc.Format);
    }

    DescriptorIndex Texture::getClearMipLevelUAV(uint32_t mipLevel)
    {
        assert(desc.isUAV);

        DescriptorIndex descriptorIndex = m_ClearMipLevelUAVs[mipLevel];

        if (descriptorIndex != c_InvalidDescriptorIndex)
            return descriptorIndex;

        descriptorIndex = m_Resources.shaderResourceViewHeap.allocateDescriptor();
        TextureSubresourceSet subresources(mipLevel, 1, 0, TextureSubresourceSet::AllArraySlices);
        createUAV(m_Resources.shaderResourceViewHeap.getCpuHandle(descriptorIndex).ptr, Format::UNKNOWN, TextureDimension::Unknown, subresources);
        m_Resources.shaderResourceViewHeap.copyToShaderVisibleHeap(descriptorIndex);
        m_ClearMipLevelUAVs[mipLevel] = descriptorIndex;

        return descriptorIndex;
    }

    StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        assert(cpuAccess != CpuAccessMode::None);

        StagingTexture *ret = new StagingTexture();
        ret->desc = d;
        ret->resourceDesc = convertTextureDesc(d);
        ret->computeSubresourceOffsets(m_Context.device);

        BufferDesc bufferDesc;
        bufferDesc.byteSize = ret->getSizeInBytes(m_Context.device);
        bufferDesc.structStride = 0;
        bufferDesc.debugName = d.debugName;
        bufferDesc.cpuAccess = cpuAccess;

        BufferHandle buffer = createBuffer(bufferDesc);
        ret->buffer = checked_cast<Buffer*>(buffer.Get());
        if (!ret->buffer)
        {
            delete ret;
            return nullptr;
        }

        ret->cpuAccess = cpuAccess;
        return StagingTextureHandle::Create(ret);
    }

    uint8_t DeviceResources::getFormatPlaneCount(DXGI_FORMAT format)
    {
        uint8_t& planeCount = m_DxgiFormatPlaneCounts[format];
        if (planeCount == 0)
        {
            D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { format, 1 };
            if (FAILED(m_Context.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))))
            {
                // Format not supported - store a special value in the cache to avoid querying later
                planeCount = 255;
            }
            else
            {
                // Format supported - store the plane count in the cache
                planeCount = formatInfo.PlaneCount;
            }
        }

        if (planeCount == 255)
            return 0;
         
        return planeCount;
    }

    void Texture::createSRV(size_t descriptor, const Format format, TextureDimension dimension, TextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, false);

        if (dimension == TextureDimension::Unknown)
            dimension = desc.dimension;

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};

        viewDesc.Format = getDxgiFormatMapping(format == Format::UNKNOWN ? desc.format : format).srvFormat;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        uint32_t planeSlice = (viewDesc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT) ? 1 : 0;

        switch (dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1D.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1DArray.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2D.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2D.PlaneSlice = planeSlice;
            break;
        case TextureDimension::Texture2DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2DArray.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2DArray.PlaneSlice = planeSlice;
            break;
        case TextureDimension::TextureCube:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            viewDesc.TextureCube.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCube.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            viewDesc.TextureCubeArray.First2DArrayFace = subresources.baseArraySlice;
            viewDesc.TextureCubeArray.NumCubes = subresources.numArraySlices / 6;
            viewDesc.TextureCubeArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCubeArray.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture3D.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return;
        }

        m_Context.device->CreateShaderResourceView(resource, &viewDesc, { descriptor });
    }

    void Texture::createUAV(size_t descriptor, Format format, TextureDimension dimension, TextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, true);

        if (dimension == TextureDimension::Unknown)
            dimension = desc.dimension;

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};

        viewDesc.Format = getDxgiFormatMapping(format == Format::UNKNOWN ? desc.format : format).srvFormat;

        switch (desc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = 0;
            viewDesc.Texture3D.WSize = desc.depth;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray: {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(desc.debugName)
                << " has unsupported dimension for UAV: " << utils::TextureDimensionToString(desc.dimension);
            m_Context.error(ss.str());
            return;
        }
        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return;
        }

        m_Context.device->CreateUnorderedAccessView(resource, nullptr, &viewDesc, { descriptor });
    }

    void Texture::createRTV(size_t descriptor, Format format, TextureSubresourceSet subresources) const
    {
        subresources = subresources.resolve(desc, true);

        D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};

        viewDesc.Format = getDxgiFormatMapping(format == Format::UNKNOWN ? desc.format : format).rtvFormat;

        switch (desc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = subresources.baseArraySlice;
            viewDesc.Texture3D.WSize = subresources.numArraySlices;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Unknown:
        default:
            utils::InvalidEnum();
            return;
        }

        m_Context.device->CreateRenderTargetView(resource, &viewDesc, { descriptor });
    }

    void Texture::createDSV(size_t descriptor, TextureSubresourceSet subresources, bool isReadOnly) const
    {
        subresources = subresources.resolve(desc, true);

        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};

        viewDesc.Format = getDxgiFormatMapping(desc.format).rtvFormat;

        if (isReadOnly)
        {
            viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            if (viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        }

        switch (desc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        case TextureDimension::Texture3D: {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(desc.debugName)
                << " has unsupported dimension for DSV: " << utils::TextureDimensionToString(desc.dimension);
            m_Context.error(ss.str());
            return;
        }
        case TextureDimension::Unknown: 
        default:
            utils::InvalidEnum();
            return;
        }

        m_Context.device->CreateDepthStencilView(resource, &viewDesc, { descriptor });
    }

    void *Device::mapStagingTexture(IStagingTexture* _tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        StagingTexture* tex = checked_cast<StagingTexture*>(_tex);

        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != CpuAccessMode::None);
        assert(tex->mappedRegion.size == 0);
        assert(tex->mappedAccess == CpuAccessMode::None);

        auto resolvedSlice = slice.resolve(tex->desc);
        auto region = tex->getSliceRegion(m_Context.device, resolvedSlice);

        if (tex->lastUseFence)
        {
            WaitForFence(tex->lastUseFence, tex->lastUseFenceValue, m_FenceEvent);
            tex->lastUseFence = nullptr;
        }

        D3D12_RANGE range;

        if (cpuAccess == CpuAccessMode::Read)
        {
            range = { SIZE_T(region.offset), region.offset + region.size };
        } else {
            range = { 0, 0 };
        }

        uint8_t *ret;
        const HRESULT res = tex->buffer->resource->Map(0, &range, (void**)&ret);

        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "Map call failed for textre " << utils::DebugNameToString(tex->desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());

            return nullptr;
        }
        
        tex->mappedRegion = region;
        tex->mappedAccess = cpuAccess;

        *outRowPitch = region.footprint.Footprint.RowPitch;
        return ret + tex->mappedRegion.offset;
    }

    void Device::unmapStagingTexture(IStagingTexture* _tex)
    {
        StagingTexture* tex = checked_cast<StagingTexture*>(_tex);

        assert(tex->mappedRegion.size != 0);
        assert(tex->mappedAccess != CpuAccessMode::None);

        D3D12_RANGE range;

        if (tex->mappedAccess == CpuAccessMode::Write)
        {
            range = { SIZE_T(tex->mappedRegion.offset), tex->mappedRegion.offset + tex->mappedRegion.size };
        } else {
            range = { 0, 0 };
        }

        tex->buffer->resource->Unmap(0, &range);

        tex->mappedRegion.size = 0;
        tex->mappedAccess = CpuAccessMode::None;
    }


    void CommandList::clearTextureFloat(ITexture* _t, TextureSubresourceSet subresources, const Color & clearColor)
    {
        Texture* t = checked_cast<Texture*>(_t);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(t->desc.format);
        assert(!formatInfo.hasDepth && !formatInfo.hasStencil);
        assert(t->desc.isUAV || t->desc.isRenderTarget);
#endif

        subresources = subresources.resolve(t->desc, false);

        m_Instance->referencedResources.push_back(t);

        if (t->desc.isRenderTarget)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireTextureState(t, subresources, ResourceStates::RenderTarget);
            }
            commitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE RTV = { t->getNativeView(ObjectTypes::D3D12_RenderTargetViewDescriptor, Format::UNKNOWN, subresources, TextureDimension::Unknown).integer };

                m_ActiveCommandList->commandList->ClearRenderTargetView(
                    RTV,
                    &clearColor.r,
                    0, nullptr);
            }
        }
        else
        {
            if (m_EnableAutomaticBarriers)
            {
                requireTextureState(t, subresources, ResourceStates::UnorderedAccess);
            }
            commitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                DescriptorIndex index = t->getClearMipLevelUAV(mipLevel);

                assert(index != c_InvalidDescriptorIndex);

                m_ActiveCommandList->commandList->ClearUnorderedAccessViewFloat(
                    m_Resources.shaderResourceViewHeap.getGpuHandle(index),
                    m_Resources.shaderResourceViewHeap.getCpuHandle(index),
                    t->resource, &clearColor.r, 0, nullptr);
            }
        }
    }

    void CommandList::clearDepthStencilTexture(ITexture* _t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!clearDepth && !clearStencil)
        {
            return;
        }

        Texture* t = checked_cast<Texture*>(_t);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(t->desc.format);
        assert(t->desc.isRenderTarget);
        assert(formatInfo.hasDepth || formatInfo.hasStencil);
#endif

        subresources = subresources.resolve(t->desc, false);

        m_Instance->referencedResources.push_back(t);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(t, subresources, ResourceStates::DepthWrite);
        }
        commitBarriers();

        D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        if (!clearDepth)
        {
            clearFlags = D3D12_CLEAR_FLAG_STENCIL;
        }
        else if (!clearStencil)
        {
            clearFlags = D3D12_CLEAR_FLAG_DEPTH;
        }

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE DSV = { t->getNativeView(ObjectTypes::D3D12_DepthStencilViewDescriptor, Format::UNKNOWN, subresources, TextureDimension::Unknown).integer };

            m_ActiveCommandList->commandList->ClearDepthStencilView(
                DSV,
                clearFlags,
                depth, stencil,
                0, nullptr);
        }
    }

    void CommandList::clearTextureUInt(ITexture* _t, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        Texture* t = checked_cast<Texture*>(_t);

#ifdef _DEBUG
        const FormatInfo& formatInfo = getFormatInfo(t->desc.format);
        assert(!formatInfo.hasDepth && !formatInfo.hasStencil);
        assert(t->desc.isUAV || t->desc.isRenderTarget);
#endif
        subresources = subresources.resolve(t->desc, false);

        uint32_t clearValues[4] = { clearColor, clearColor, clearColor, clearColor };

        m_Instance->referencedResources.push_back(t);

        if (t->desc.isUAV)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireTextureState(t, subresources, ResourceStates::UnorderedAccess);
            }
            commitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                DescriptorIndex index = t->getClearMipLevelUAV(mipLevel);

                assert(index != c_InvalidDescriptorIndex);

                m_ActiveCommandList->commandList->ClearUnorderedAccessViewUint(
                    m_Resources.shaderResourceViewHeap.getGpuHandle(index),
                    m_Resources.shaderResourceViewHeap.getCpuHandle(index),
                    t->resource, clearValues, 0, nullptr);
            }
        }
        else if (t->desc.isRenderTarget)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireTextureState(t, subresources, ResourceStates::RenderTarget);
            }
            commitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE RTV = { t->getNativeView(ObjectTypes::D3D12_RenderTargetViewDescriptor, Format::UNKNOWN, subresources, TextureDimension::Unknown).integer };

                float floatColor[4] = { (float)clearColor, (float)clearColor, (float)clearColor, (float)clearColor };
                m_ActiveCommandList->commandList->ClearRenderTargetView(RTV, floatColor, 0, nullptr);
            }
        }
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice,
        ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* dst = checked_cast<Texture*>(_dst);
        Texture* src = checked_cast<Texture*>(_src);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        assert(resolvedDstSlice.width == resolvedSrcSlice.width);
        assert(resolvedDstSlice.height == resolvedSrcSlice.height);

        UINT dstSubresource = calcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, 0, dst->desc.mipLevels, dst->desc.arraySize);
        UINT srcSubresource = calcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, 0, src->desc.mipLevels, src->desc.arraySize);

        D3D12_TEXTURE_COPY_LOCATION dstLocation;
        dstLocation.pResource = dst->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = dstSubresource;

        D3D12_TEXTURE_COPY_LOCATION srcLocation;
        srcLocation.pResource = src->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = srcSubresource;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(dst, TextureSubresourceSet(resolvedDstSlice.mipLevel, 1, resolvedDstSlice.arraySlice, 1), ResourceStates::CopyDest);
            requireTextureState(src, TextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), ResourceStates::CopySource);
        }
        commitBarriers();

        m_Instance->referencedResources.push_back(dst);
        m_Instance->referencedResources.push_back(src);

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation,
            resolvedDstSlice.x,
            resolvedDstSlice.y,
            resolvedDstSlice.z,
            &srcLocation,
            &srcBox);
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
    {
        StagingTexture* src = checked_cast<StagingTexture*>(_src);
        Texture* dst = checked_cast<Texture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        UINT dstSubresource = calcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, 0, dst->desc.mipLevels, dst->desc.arraySize);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(dst, TextureSubresourceSet(resolvedDstSlice.mipLevel, 1, resolvedDstSlice.arraySlice, 1), ResourceStates::CopyDest);
            requireBufferState(src->buffer, ResourceStates::CopySource);
        }
        commitBarriers();

        m_Instance->referencedResources.push_back(dst);
        m_Instance->referencedStagingTextures.push_back(src);

        auto srcRegion = src->getSliceRegion(m_Context.device, resolvedSrcSlice);

        D3D12_TEXTURE_COPY_LOCATION dstLocation;
        dstLocation.pResource = dst->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = dstSubresource;

        D3D12_TEXTURE_COPY_LOCATION srcLocation;
        srcLocation.pResource = src->buffer->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint = srcRegion.footprint;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation, resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
            &srcLocation, &srcBox);
    }

    void CommandList::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = checked_cast<Texture*>(_src);
        StagingTexture* dst = checked_cast<StagingTexture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        UINT srcSubresource = calcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, 0, src->desc.mipLevels, src->desc.arraySize);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(src, TextureSubresourceSet(resolvedSrcSlice.mipLevel, 1, resolvedSrcSlice.arraySlice, 1), ResourceStates::CopySource);
            requireBufferState(dst->buffer, ResourceStates::CopyDest);
        }
        commitBarriers();

        m_Instance->referencedResources.push_back(src);
        m_Instance->referencedStagingTextures.push_back(dst);

        auto dstRegion = dst->getSliceRegion(m_Context.device, resolvedDstSlice);

        D3D12_TEXTURE_COPY_LOCATION dstLocation;
        dstLocation.pResource = dst->buffer->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint = dstRegion.footprint;

        D3D12_TEXTURE_COPY_LOCATION srcLocation;
        srcLocation.pResource = src->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = srcSubresource;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation, resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
            &srcLocation, &srcBox);
    }

    void CommandList::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        Texture* dest = checked_cast<Texture*>(_dest);

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(dest, TextureSubresourceSet(mipLevel, 1, arraySlice, 1), ResourceStates::CopyDest);
        }
        commitBarriers();

        uint32_t subresource = calcSubresource(mipLevel, arraySlice, 0, dest->desc.mipLevels, dest->desc.arraySize);

        D3D12_RESOURCE_DESC resourceDesc = dest->resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        uint32_t numRows;
        uint64_t rowSizeInBytes;
        uint64_t totalBytes;

        m_Context.device->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        void* cpuVA;
        ID3D12Resource* uploadBuffer;
        size_t offsetInUploadBuffer;
        if (!m_UploadManager.suballocateBuffer(totalBytes, nullptr, &uploadBuffer, &offsetInUploadBuffer, &cpuVA, nullptr, 
            m_RecordingVersion, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
        {
            m_Context.error("Couldn't suballocate an upload buffer");
            return;
        }
        footprint.Offset = uint64_t(offsetInUploadBuffer);

        assert(numRows <= footprint.Footprint.Height);

        for (uint32_t depthSlice = 0; depthSlice < footprint.Footprint.Depth; depthSlice++)
        {
            for (uint32_t row = 0; row < numRows; row++)
            {
                void* destAddress = (char*)cpuVA + footprint.Footprint.RowPitch * (row + depthSlice * numRows);
                const void* srcAddress = (const char*)data + rowPitch * row + depthPitch * depthSlice;
                memcpy(destAddress, srcAddress, std::min(rowPitch, rowSizeInBytes));
            }
        }

        D3D12_TEXTURE_COPY_LOCATION destCopyLocation;
        destCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destCopyLocation.SubresourceIndex = subresource;
        destCopyLocation.pResource = dest->resource;

        D3D12_TEXTURE_COPY_LOCATION srcCopyLocation;
        srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcCopyLocation.PlacedFootprint = footprint;
        srcCopyLocation.pResource = uploadBuffer;

        m_Instance->referencedResources.push_back(dest);

        if (uploadBuffer != m_CurrentUploadBuffer)
        {
            m_Instance->referencedNativeResources.push_back(uploadBuffer);
            m_CurrentUploadBuffer = uploadBuffer;
        }

        m_ActiveCommandList->commandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
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

        if (m_EnableAutomaticBarriers)
        {
            requireTextureState(_dest, dstSubresources, ResourceStates::ResolveDest);
            requireTextureState(_src, srcSubresources, ResourceStates::ResolveSource);
        }
        commitBarriers();

        const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(dest->desc.format);

        for (int plane = 0; plane < dest->planeCount; plane++)
        {
            for (ArraySlice arrayIndex = 0; arrayIndex < dstSR.numArraySlices; arrayIndex++)
            {
                for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
                {
                    uint32_t dstSubresource = calcSubresource(mipLevel + dstSR.baseMipLevel, arrayIndex + dstSR.baseArraySlice, plane, dest->desc.mipLevels, dest->desc.arraySize);
                    uint32_t srcSubresource = calcSubresource(mipLevel + srcSR.baseMipLevel, arrayIndex + srcSR.baseArraySlice, plane, src->desc.mipLevels, src->desc.arraySize);
                    m_ActiveCommandList->commandList->ResolveSubresource(dest->resource, dstSubresource, src->resource, srcSubresource, formatMapping.rtvFormat);
                }
            }
        }
    }

    // helper function for texture subresource calculations
    // https://msdn.microsoft.com/en-us/library/windows/desktop/dn705766(v=vs.85).aspx
    uint32_t calcSubresource(uint32_t MipSlice, uint32_t ArraySlice, uint32_t PlaneSlice, uint32_t MipLevels, uint32_t ArraySize)
    {
        return MipSlice + (ArraySlice * MipLevels) + (PlaneSlice * MipLevels * ArraySize);
    }

} // namespace nvrhi::d3d12
