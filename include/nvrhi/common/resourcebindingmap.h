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

#include <nvrhi/common/containers.h>
#include <nvrhi/nvrhi.h>
#include <unordered_map>

namespace nvrhi {

// describes a texture binding --- used to manage SRV / VkImageView per texture
struct TextureBindingKey : public TextureSubresourceSet
{
    Format format;
    bool isReadOnlyDSV;

    TextureBindingKey()
    {
    }

    TextureBindingKey(const TextureSubresourceSet& b, Format _format, bool _isReadOnlyDSV = false)
        : TextureSubresourceSet(b)
        , format(_format)
        , isReadOnlyDSV(_isReadOnlyDSV)
    {
    }

    bool operator== (const TextureBindingKey& other) const
    {
        return format == other.format &&
            static_cast<const TextureSubresourceSet&>(*this) == static_cast<const TextureSubresourceSet&>(other) &&
            isReadOnlyDSV == other.isReadOnlyDSV;
    }
};

template <typename T>
using TextureBindingKey_HashMap = std::unordered_map<TextureBindingKey, T>;

struct BufferBindingKey : public BufferRange
{
    Format format;
    ResourceType type;

    BufferBindingKey()
    { }

    BufferBindingKey(const BufferRange& range, Format _format, ResourceType _type)
        : BufferRange(range)
        , format(_format)
        , type(_type)
    { }

    bool operator== (const BufferBindingKey& other) const
    {
        return format == other.format &&
            type == other.type &&
            static_cast<const BufferRange&>(*this) == static_cast<const BufferRange&>(other);
    }
};

} // namespace nvrhi

namespace std
{
    template<> struct hash<nvrhi::TextureBindingKey>
    {
        std::size_t operator()(nvrhi::TextureBindingKey const& s) const noexcept
        {
            return std::hash<nvrhi::Format>()(s.format)
                ^ std::hash<nvrhi::TextureSubresourceSet>()(s)
                ^ std::hash<bool>()(s.isReadOnlyDSV);
        }
    };

    template<> struct hash<nvrhi::BufferBindingKey>
    {
        std::size_t operator()(nvrhi::BufferBindingKey const& s) const noexcept
        {
            return std::hash<nvrhi::Format>()(s.format)
                ^ std::hash<nvrhi::BufferRange>()(s);
        }
    };
}