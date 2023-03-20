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

#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>
#include <sstream>
#include <iomanip>

namespace nvrhi::d3d11
{

    Object Buffer::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Resource:
        case ObjectTypes::D3D11_Buffer:
            return Object(resource.Get());
        case ObjectTypes::SharedHandle:
            return Object(sharedHandle);
        default:
            return nullptr;
        }
    }

    BufferHandle Device::createBuffer(const BufferDesc& d)
    {
        assert(d.byteSize <= UINT_MAX);

        D3D11_BUFFER_DESC desc11 = {};
        desc11.ByteWidth = (UINT)d.byteSize;

        // These don't map exactly, but it should be generally correct
        switch(d.cpuAccess)
        {
            case CpuAccessMode::None:
                desc11.Usage = D3D11_USAGE_DEFAULT;
                desc11.CPUAccessFlags = 0;
                break;

            case CpuAccessMode::Read:
                desc11.Usage = D3D11_USAGE_STAGING;
                desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                break;

            case CpuAccessMode::Write:
                desc11.Usage = D3D11_USAGE_DYNAMIC;
                desc11.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                break;
        }

        if (d.isConstantBuffer)
        {
            desc11.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc11.ByteWidth = align(desc11.ByteWidth, 16u);
        }
        else 
        {
            desc11.BindFlags = 0;

            if (desc11.Usage != D3D11_USAGE_STAGING)
                desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            
            if (d.canHaveUAVs)
                desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

            if (d.isIndexBuffer)
                desc11.BindFlags |= D3D11_BIND_INDEX_BUFFER;

            if (d.isVertexBuffer)
                desc11.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        }

        desc11.MiscFlags = 0;
        if (d.isDrawIndirectArgs)
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

        if (d.structStride != 0)
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        if (d.canHaveRawViews)
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

        desc11.StructureByteStride = (UINT)d.structStride;

        bool isShared = false;
        if ((d.sharedResourceFlags & SharedResourceFlags::Shared_NTHandle) != 0) {
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            isShared = true;
        }
        else if ((d.sharedResourceFlags & SharedResourceFlags::Shared) != 0) {
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
            isShared = true;
        }

        RefCountPtr<ID3D11Buffer> newBuffer;
        const HRESULT res = m_Context.device->CreateBuffer(&desc11, nullptr, &newBuffer);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreataeBuffer call failed for buffer " << utils::DebugNameToString(d.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
            return nullptr;
        }

        HANDLE sharedHandle = nullptr;
        if (isShared)
        {
            RefCountPtr<IDXGIResource1 > pDxgiResource1;
            if (SUCCEEDED(newBuffer->QueryInterface(IID_PPV_ARGS(&pDxgiResource1))))
                pDxgiResource1->GetSharedHandle(&sharedHandle);
        }

        if (!d.debugName.empty())
            SetDebugName(newBuffer, d.debugName.c_str());

        Buffer* buffer = new Buffer(m_Context);
        buffer->desc = d;
        buffer->resource = newBuffer;
        buffer->sharedHandle = sharedHandle;
        return BufferHandle::Create(buffer);
    }

    void CommandList::writeBuffer(IBuffer* _buffer, const void* data, size_t dataSize, uint64_t destOffsetBytes)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        assert(destOffsetBytes + dataSize <= UINT_MAX);

        if (buffer->desc.cpuAccess == CpuAccessMode::Write)
        {
            // we can map if it it's D3D11_USAGE_DYNAMIC, but not UpdateSubresource
            D3D11_MAPPED_SUBRESOURCE mappedData;
            D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD;
            if (destOffsetBytes > 0 || dataSize + destOffsetBytes < buffer->desc.byteSize)
                mapType = D3D11_MAP_WRITE;

            const HRESULT res = m_Context.immediateContext->Map(buffer->resource, 0, mapType, 0, &mappedData);
            if (FAILED(res))
            {
                std::stringstream ss;
                ss << "Map call failed for buffer " << utils::DebugNameToString(buffer->desc.debugName)
                    << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
                m_Context.error(ss.str());
                return;
            }

            memcpy((char*)mappedData.pData + destOffsetBytes, data, dataSize);
            m_Context.immediateContext->Unmap(buffer->resource, 0);
        }
        else
        {
            D3D11_BOX box = { UINT(destOffsetBytes), 0, 0, UINT(destOffsetBytes + dataSize), 1, 1 };
            bool useBox = destOffsetBytes > 0 || dataSize < buffer->desc.byteSize;

            m_Context.immediateContext->UpdateSubresource(buffer->resource, 0, useBox ? &box : nullptr, data, (UINT)dataSize, 0);
        }
    }

    void CommandList::clearBufferUInt(IBuffer* buffer, uint32_t clearValue)
    {
        const BufferDesc& bufferDesc = buffer->getDesc();
        ResourceType viewType = bufferDesc.structStride != 0
            ? ResourceType::StructuredBuffer_UAV
            : (bufferDesc.canHaveRawViews && bufferDesc.format == Format::UNKNOWN)
                ? ResourceType::RawBuffer_UAV
                : ResourceType::TypedBuffer_UAV;
        ID3D11UnorderedAccessView* uav = checked_cast<Buffer*>(buffer)->getUAV(Format::UNKNOWN, EntireBuffer, viewType);

        UINT clearValues[4] = { clearValue, clearValue, clearValue, clearValue };
        m_Context.immediateContext->ClearUnorderedAccessViewUint(uav, clearValues);
    }

    void CommandList::copyBuffer(IBuffer* _dest, uint64_t destOffsetBytes, IBuffer* _src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes)
    {
        Buffer* dest = checked_cast<Buffer*>(_dest);
        Buffer* src = checked_cast<Buffer*>(_src);

        assert(destOffsetBytes + dataSizeBytes <= UINT_MAX);
        assert(srcOffsetBytes + dataSizeBytes <= UINT_MAX);

        //Do a 1D copy
        D3D11_BOX srcBox;
        srcBox.left = (UINT)srcOffsetBytes;
        srcBox.right = (UINT)(srcOffsetBytes + dataSizeBytes);
        srcBox.bottom = 1;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.back = 1;
        m_Context.immediateContext->CopySubresourceRegion(dest->resource, 0, (UINT)destOffsetBytes, 0, 0, src->resource, 0, &srcBox);
    }
    
    void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        D3D11_MAP mapType;
        switch(flags)  // NOLINT(clang-diagnostic-switch-enum)
        {
            case CpuAccessMode::Read:
                assert(buffer->desc.cpuAccess == CpuAccessMode::Read);
                mapType = D3D11_MAP_READ;
                break;

            case CpuAccessMode::Write:
                assert(buffer->desc.cpuAccess == CpuAccessMode::Write);
                mapType = D3D11_MAP_WRITE_DISCARD;
                break;

            default:
                m_Context.error("Unsupported CpuAccessMode in mapBuffer");
                return nullptr;
        }

        D3D11_MAPPED_SUBRESOURCE res;
        if (SUCCEEDED(m_Context.immediateContext->Map(buffer->resource, 0, mapType, 0, &res)))
        {
            return res.pData;
        } else {
            return nullptr;
        }
    }

    void Device::unmapBuffer(IBuffer* _buffer)
    {
        Buffer* buffer = checked_cast<Buffer*>(_buffer);

        m_Context.immediateContext->Unmap(buffer->resource, 0);
    }

    MemoryRequirements Device::getBufferMemoryRequirements(IBuffer*)
    {
        utils::NotSupported();
        return MemoryRequirements();
    }

    bool Device::bindBufferMemory(IBuffer*, IHeap*, uint64_t)
    {
        utils::NotSupported();
        return false;
    }

    BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc)
    {
        if (!_buffer.pointer)
            return nullptr;

        if (objectType != ObjectTypes::D3D11_Buffer)
            return nullptr;

        ID3D11Buffer* pBuffer = static_cast<ID3D11Buffer*>(_buffer.pointer);

        Buffer* buffer = new Buffer(m_Context);
        buffer->desc = desc;
        buffer->resource = pBuffer;
        return BufferHandle::Create(buffer);
    }

    ID3D11ShaderResourceView* Buffer::getSRV(Format format, BufferRange range, ResourceType type)
    {
        if (format == Format::UNKNOWN)
        {
            format = desc.format;
        }

        range = range.resolve(desc);

        RefCountPtr<ID3D11ShaderResourceView>& srv = m_ShaderResourceViews[BufferBindingKey(range, format, type)];
        if (srv)
            return srv;


        D3D11_SHADER_RESOURCE_VIEW_DESC desc11;
        desc11.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        desc11.BufferEx.Flags = 0;

        switch (type)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ResourceType::StructuredBuffer_SRV:
            assert(desc.structStride != 0);
            desc11.Format = DXGI_FORMAT_UNKNOWN;
            desc11.BufferEx.FirstElement = (UINT)(range.byteOffset / desc.structStride);
            desc11.BufferEx.NumElements = (UINT)(range.byteSize / desc.structStride);
            break;

        case ResourceType::RawBuffer_SRV:
            desc11.Format = DXGI_FORMAT_R32_TYPELESS;
            desc11.BufferEx.FirstElement = (UINT)(range.byteOffset / 4);
            desc11.BufferEx.NumElements = (UINT)(range.byteSize / 4);
            desc11.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            break;

        case ResourceType::TypedBuffer_SRV:
        {
            assert(format != Format::UNKNOWN);
            const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(format);
            const FormatInfo& formatInfo = getFormatInfo(format);

            desc11.Format = formatMapping.srvFormat;
            desc11.BufferEx.FirstElement = (UINT)(range.byteOffset / formatInfo.bytesPerBlock);
            desc11.BufferEx.NumElements = (UINT)(range.byteSize / formatInfo.bytesPerBlock);
            break;
        }

        default: 
            utils::InvalidEnum();
            return nullptr;
        }

        const HRESULT res = m_Context.device->CreateShaderResourceView(resource, &desc11, &srv);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateUnorderedAccessView call failed for buffer " << utils::DebugNameToString(desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
        }

        return srv;
    }

    ID3D11UnorderedAccessView* Buffer::getUAV(Format format, BufferRange range, ResourceType type)
    {
        if (format == Format::UNKNOWN)
        {
            format = desc.format;
        }

        range = range.resolve(desc);

        RefCountPtr<ID3D11UnorderedAccessView>& uav = m_UnorderedAccessViews[BufferBindingKey(range, format, type)];
        if (uav)
            return uav;
        
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc11;
        desc11.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc11.Buffer.Flags = 0;

        switch (type)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ResourceType::StructuredBuffer_UAV:
            assert(desc.structStride != 0);
            desc11.Format = DXGI_FORMAT_UNKNOWN;
            desc11.Buffer.FirstElement = (UINT)(range.byteOffset / desc.structStride);
            desc11.Buffer.NumElements = (UINT)(range.byteSize / desc.structStride);
            break;

        case ResourceType::RawBuffer_UAV:
            desc11.Format = DXGI_FORMAT_R32_TYPELESS;
            desc11.Buffer.FirstElement = (UINT)(range.byteOffset / 4);
            desc11.Buffer.NumElements = (UINT)(range.byteSize / 4);
            desc11.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            break;

        case ResourceType::TypedBuffer_UAV:
        {
            assert(format != Format::UNKNOWN);
            const DxgiFormatMapping& formatMapping = getDxgiFormatMapping(format);
            const FormatInfo& formatInfo = getFormatInfo(format);

            desc11.Format = formatMapping.srvFormat;
            desc11.Buffer.FirstElement = (UINT)(range.byteOffset / formatInfo.bytesPerBlock);
            desc11.Buffer.NumElements = (UINT)(range.byteSize / formatInfo.bytesPerBlock);
            break;
        }

        default:
            utils::InvalidEnum();
            return nullptr;
        }

        const HRESULT res = m_Context.device->CreateUnorderedAccessView(resource, &desc11, &uav);
        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateUnorderedAccessView call failed for buffer " << utils::DebugNameToString(desc.debugName)
                << ", HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());
        }

        return uav;
    }
    
	
} // nanmespace nvrhi::d3d11