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
#include <nvrhi/common/resource.h>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#define NVRHI_ENUM_CLASS_FLAG_OPERATORS(T) \
    inline T operator | (T a, T b) { return T(uint32_t(a) | uint32_t(b)); } \
    inline T operator & (T a, T b) { return T(uint32_t(a) & uint32_t(b)); } /* NOLINT(bugprone-macro-parentheses) */ \
    inline T operator ~ (T a) { return T(~uint32_t(a)); } /* NOLINT(bugprone-macro-parentheses) */ \
    inline bool operator !(T a) { return uint32_t(a) == 0; } \
    inline bool operator ==(T a, uint32_t b) { return uint32_t(a) == b; } \
    inline bool operator !=(T a, uint32_t b) { return uint32_t(a) != b; }

#if defined(NVRHI_SHARED_LIBRARY_BUILD)
#   if defined(_MSC_VER)
#       define NVRHI_API __declspec(dllexport)
#   elif defined(__GNUC__)
#       define NVRHI_API __attribute__((visibility("default")))
#   else
#       define NVRHI_API
#       pragma warning "Unknown dynamic link import/export semantics."
#   endif
#elif defined(NVRHI_SHARED_LIBRARY_INCLUDE)
#   if defined(_MSC_VER)
#       define NVRHI_API __declspec(dllimport)
#   else
#       define NVRHI_API
#   endif
#else
#   define NVRHI_API
#endif

namespace nvrhi
{
    // Version of the public API provided by NVRHI.
    // Increment this when any changes to the API are made.
    static constexpr uint32_t c_HeaderVersion = 4;

    // Verifies that the version of the implementation matches the version of the header.
    // Returns true if they match. Use this when initializing apps using NVRHI as a shared library.
    NVRHI_API bool verifyHeaderVersion(uint32_t version = c_HeaderVersion);

    static constexpr uint32_t c_MaxRenderTargets = 8;
    static constexpr uint32_t c_MaxViewports = 16;
    static constexpr uint32_t c_MaxVertexAttributes = 16;
    static constexpr uint32_t c_MaxBindingLayouts = 5;
    static constexpr uint32_t c_MaxBindingsPerLayout = 128;
    static constexpr uint32_t c_MaxVolatileConstantBuffersPerLayout = 6;
    static constexpr uint32_t c_MaxVolatileConstantBuffers = 32;
    static constexpr uint32_t c_MaxPushConstantSize = 128; // D3D12: root signature is 256 bytes max., Vulkan: 128 bytes of push constants guaranteed

    //////////////////////////////////////////////////////////////////////////
    // Basic Types
    //////////////////////////////////////////////////////////////////////////

    struct Color
    {
        float r, g, b, a;

        Color() : r(0.f), g(0.f), b(0.f), a(0.f) { }
        Color(float c) : r(c), g(c), b(c), a(c) { }
        Color(float _r, float _g, float _b, float _a) : r(_r), g(_g), b(_b), a(_a) { }

        bool operator ==(const Color& _b) const { return r == _b.r && g == _b.g && b == _b.b && a == _b.a; }
        bool operator !=(const Color& _b) const { return !(*this == _b); }
    };

    struct Viewport
    {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;

        Viewport() : minX(0.f), maxX(0.f), minY(0.f), maxY(0.f), minZ(0.f), maxZ(1.f) { }

        Viewport(float width, float height) : minX(0.f), maxX(width), minY(0.f), maxY(height), minZ(0.f), maxZ(1.f) { }

        Viewport(float _minX, float _maxX, float _minY, float _maxY, float _minZ, float _maxZ)
            : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY), minZ(_minZ), maxZ(_maxZ)
        { }

        bool operator ==(const Viewport& b) const
        {
            return minX == b.minX
                && minY == b.minY
                && minZ == b.minZ
                && maxX == b.maxX
                && maxY == b.maxY
                && maxZ == b.maxZ;
        }
        bool operator !=(const Viewport& b) const { return !(*this == b); }

        [[nodiscard]] float width() const { return maxX - minX; }
        [[nodiscard]] float height() const { return maxY - minY; }
    };

    struct Rect
    {
        int minX, maxX;
        int minY, maxY;

        Rect() : minX(0), maxX(0), minY(0), maxY(0) { }
        Rect(int width, int height) : minX(0), maxX(width), minY(0), maxY(height) { }
        Rect(int _minX, int _maxX, int _minY, int _maxY) : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY) { }
        explicit Rect(const Viewport& viewport)
            : minX(int(floorf(viewport.minX)))
            , maxX(int(ceilf(viewport.maxX)))
            , minY(int(floorf(viewport.minY)))
            , maxY(int(ceilf(viewport.maxY)))
        {
        }

        bool operator ==(const Rect& b) const {
            return minX == b.minX && minY == b.minY && maxX == b.maxX && maxY == b.maxY;
        }
        bool operator !=(const Rect& b) const { return !(*this == b); }

        [[nodiscard]] int width() const { return maxX - minX; }
        [[nodiscard]] int height() const { return maxY - minY; }
    };

    enum class GraphicsAPI : uint8_t
    {
        D3D11,
        D3D12,
        VULKAN
    };

    enum class Format : uint8_t
    {
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,
        
        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        COUNT,
    };
    
    enum class FormatKind : uint8_t
    {
        Integer,
        Normalized,
        Float,
        DepthStencil
    };

    struct FormatInfo
    {
        Format format;
        const char* name;
        uint8_t bytesPerBlock;
        uint8_t blockSize;
        FormatKind kind;
        bool hasRed : 1;
        bool hasGreen : 1;
        bool hasBlue : 1;
        bool hasAlpha : 1;
        bool hasDepth : 1;
        bool hasStencil : 1;
        bool isSigned : 1;
        bool isSRGB : 1;
    };

    NVRHI_API const FormatInfo& getFormatInfo(Format format);

    enum class FormatSupport : uint32_t
    {
        None            = 0,

        Buffer          = 0x00000001,
        IndexBuffer     = 0x00000002,
        VertexBuffer    = 0x00000004,

        Texture         = 0x00000008,
        DepthStencil    = 0x00000010,
        RenderTarget    = 0x00000020,
        Blendable       = 0x00000040,

        ShaderLoad      = 0x00000080,
        ShaderSample    = 0x00000100,
        ShaderUavLoad   = 0x00000200,
        ShaderUavStore  = 0x00000400,
        ShaderAtomic    = 0x00000800,
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(FormatSupport)

    //////////////////////////////////////////////////////////////////////////
    // Heap
    //////////////////////////////////////////////////////////////////////////

    enum class HeapType : uint8_t
    {
        DeviceLocal,
        Upload,
        Readback
    };

    struct HeapDesc
    {
        uint64_t capacity = 0;
        HeapType type;
        std::string debugName;

        constexpr HeapDesc& setCapacity(uint64_t value) { capacity = value; return *this; }
        constexpr HeapDesc& setType(HeapType value) { type = value; return *this; }
                  HeapDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
    };

    class IHeap : public IResource
    {
    public:
        virtual const HeapDesc& getDesc() = 0;
    };

    typedef RefCountPtr<IHeap> HeapHandle;

    struct MemoryRequirements
    {
        uint64_t size = 0;
        uint64_t alignment = 0;
    };

    //////////////////////////////////////////////////////////////////////////
    // Texture
    //////////////////////////////////////////////////////////////////////////

    enum class TextureDimension : uint8_t
    {
        Unknown,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D
    };

    enum class CpuAccessMode : uint8_t
    {
        None,
        Read,
        Write
    };
    
    enum class ResourceStates : uint32_t
    {
        Unknown               = 0,
        Common                = 0x00000001,
        ConstantBuffer        = 0x00000002,
        VertexBuffer          = 0x00000004,
        IndexBuffer           = 0x00000008,
        IndirectArgument      = 0x00000010,
        ShaderResource        = 0x00000020,
        UnorderedAccess       = 0x00000040,
        RenderTarget          = 0x00000080,
        DepthWrite            = 0x00000100,
        DepthRead             = 0x00000200,
        StreamOut             = 0x00000400,
        CopyDest              = 0x00000800,
        CopySource            = 0x00001000,
        ResolveDest           = 0x00002000,
        ResolveSource         = 0x00004000,
        Present               = 0x00008000,
        AccelStructRead       = 0x00010000,
        AccelStructWrite      = 0x00020000,
        AccelStructBuildInput = 0x00040000,
        AccelStructBuildBlas  = 0x00080000,
        ShadingRateSurface    = 0x00100000,
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(ResourceStates)

    typedef uint32_t MipLevel;
    typedef uint32_t ArraySlice;

    // Flags for resources that need to be shared with other graphics APIs or other GPU devices.
    enum class SharedResourceFlags : uint32_t
    {
        None                = 0,

        // D3D11: adds D3D11_RESOURCE_MISC_SHARED
        // D3D12: adds D3D12_HEAP_FLAG_SHARED
        // Vulkan: ignored
        Shared              = 0x01,

        // D3D11: adds (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        // D3D12, Vulkan: ignored
        Shared_NTHandle     = 0x02,

        // D3D12: adds D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER and D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
        // D3D11, Vulkan: ignored
        Shared_CrossAdapter = 0x04,
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(SharedResourceFlags)

    struct TextureDesc
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;
        Format format = Format::UNKNOWN;
        TextureDimension dimension = TextureDimension::Texture2D;
        std::string debugName;

        bool isRenderTarget = false;
        bool isUAV = false;
        bool isTypeless = false;
        bool isShadingRateSurface = false;

        SharedResourceFlags sharedResourceFlags = SharedResourceFlags::None;

        // Indicates that the texture is created with no backing memory,
        // and memory is bound to the texture later using bindTextureMemory.
        // On DX12, the texture resource is created at the time of memory binding.
        bool isVirtual = false;

        Color clearValue;
        bool useClearValue = false;

        ResourceStates initialState = ResourceStates::Unknown;

        // If keepInitialState is true, command lists that use the texture will automatically
        // begin tracking the texture from the initial state and transition it to the initial state 
        // on command list close.
        bool keepInitialState = false;

        constexpr TextureDesc& setWidth(uint32_t value) { width = value; return *this; }
        constexpr TextureDesc& setHeight(uint32_t value) { height = value; return *this; }
        constexpr TextureDesc& setDepth(uint32_t value) { depth = value; return *this; }
        constexpr TextureDesc& setArraySize(uint32_t value) { arraySize = value; return *this; }
        constexpr TextureDesc& setMipLevels(uint32_t value) { mipLevels = value; return *this; }
        constexpr TextureDesc& setSampleCount(uint32_t value) { sampleCount = value; return *this; }
        constexpr TextureDesc& setSampleQuality(uint32_t value) { sampleQuality = value; return *this; }
        constexpr TextureDesc& setFormat(Format value) { format = value; return *this; }
        constexpr TextureDesc& setDimension(TextureDimension value) { dimension = value; return *this; }
                  TextureDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
        constexpr TextureDesc& setIsRenderTarget(bool value) { isRenderTarget = value; return *this; }
        constexpr TextureDesc& setIsUAV(bool value) { isUAV = value; return *this; }
        constexpr TextureDesc& setIsTypeless(bool value) { isTypeless = value; return *this; }
        constexpr TextureDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        constexpr TextureDesc& setClearValue(const Color& value) { clearValue = value; useClearValue = true; return *this; }
        constexpr TextureDesc& setUseClearValue(bool value) { useClearValue = value; return *this; }
        constexpr TextureDesc& setInitialState(ResourceStates value) { initialState = value; return *this; }
        constexpr TextureDesc& setKeepInitialState(bool value) { keepInitialState = value; return *this; }
    };

    // describes a 2D section of a single mip level + single slice of a texture
    struct TextureSlice
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        // -1 means the entire dimension is part of the region
        // resolve() below will translate these values into actual dimensions
        uint32_t width = uint32_t(-1);
        uint32_t height = uint32_t(-1);
        uint32_t depth = uint32_t(-1);

        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;

        [[nodiscard]] NVRHI_API TextureSlice resolve(const TextureDesc& desc) const;

        constexpr TextureSlice& setOrigin(uint32_t vx = 0, uint32_t vy = 0, uint32_t vz = 0) { x = vx; y = vy; z = vz; return *this; }
        constexpr TextureSlice& setWidth(uint32_t value) { width = value; return *this; }
        constexpr TextureSlice& setHeight(uint32_t value) { height = value; return *this; }
        constexpr TextureSlice& setDepth(uint32_t value) { depth = value; return *this; }
        constexpr TextureSlice& setSize(uint32_t vx = uint32_t(-1), uint32_t vy = uint32_t(-1), uint32_t vz = uint32_t(-1)) { width = vx; height = vy; depth = vz; return *this; }
        constexpr TextureSlice& setMipLevel(MipLevel level) { mipLevel = level; return *this; }
        constexpr TextureSlice& setArraySlice(ArraySlice slice) { arraySlice = slice; return *this; }
    };

    struct TextureSubresourceSet
    {
        static constexpr MipLevel AllMipLevels = MipLevel(-1);
        static constexpr ArraySlice AllArraySlices = ArraySlice(-1);
        
        MipLevel baseMipLevel = 0;
        MipLevel numMipLevels = 1;
        ArraySlice baseArraySlice = 0;
        ArraySlice numArraySlices = 1;

        TextureSubresourceSet() = default;

        TextureSubresourceSet(MipLevel _baseMipLevel, MipLevel _numMipLevels, ArraySlice _baseArraySlice, ArraySlice _numArraySlices)
            : baseMipLevel(_baseMipLevel)
            , numMipLevels(_numMipLevels)
            , baseArraySlice(_baseArraySlice)
            , numArraySlices(_numArraySlices)
        {
        }

        [[nodiscard]] NVRHI_API TextureSubresourceSet resolve(const TextureDesc& desc, bool singleMipLevel) const;
        [[nodiscard]] NVRHI_API bool isEntireTexture(const TextureDesc& desc) const;

        bool operator ==(const TextureSubresourceSet& other) const
        {
            return baseMipLevel == other.baseMipLevel &&
                numMipLevels == other.numMipLevels &&
                baseArraySlice == other.baseArraySlice &&
                numArraySlices == other.numArraySlices;
        }
        bool operator !=(const TextureSubresourceSet& other) const { return !(*this == other); }

        constexpr TextureSubresourceSet& setBaseMipLevel(MipLevel value) { baseMipLevel = value; return *this; }
        constexpr TextureSubresourceSet& setNumMipLevels(MipLevel value) { numMipLevels = value; return *this; }
        constexpr TextureSubresourceSet& setMipLevels(MipLevel base, MipLevel num) { baseMipLevel = base; numMipLevels = num; return *this; }
        constexpr TextureSubresourceSet& setBaseArraySlice(ArraySlice value) { baseArraySlice = value; return *this; }
        constexpr TextureSubresourceSet& setNumArraySlices(ArraySlice value) { numArraySlices = value; return *this; }
        constexpr TextureSubresourceSet& setArraySlices(ArraySlice base, ArraySlice num) { baseArraySlice = base; numArraySlices = num; return *this; }

        // see the bottom of this file for a specialization of std::hash<TextureSubresourceSet>
    };

    static const TextureSubresourceSet AllSubresources = TextureSubresourceSet(0, TextureSubresourceSet::AllMipLevels, 0, TextureSubresourceSet::AllArraySlices);

    class ITexture : public IResource
    {
    public:
        [[nodiscard]] virtual const TextureDesc& getDesc() const = 0;

        // Similar to getNativeObject, returns a native view for a specified set of subresources. Returns nullptr if unavailable.
        // TODO: on D3D12, the views might become invalid later if the view heap is grown/reallocated, we should do something about that.
        virtual Object getNativeView(ObjectType objectType, Format format = Format::UNKNOWN, TextureSubresourceSet subresources = AllSubresources, TextureDimension dimension = TextureDimension::Unknown, bool isReadOnlyDSV = false) = 0;
    };
    typedef RefCountPtr<ITexture> TextureHandle;

    class IStagingTexture : public IResource
    {
    public:
        [[nodiscard]] virtual const TextureDesc& getDesc() const = 0;
    };
    typedef RefCountPtr<IStagingTexture> StagingTextureHandle;

    //////////////////////////////////////////////////////////////////////////
    // Input Layout
    //////////////////////////////////////////////////////////////////////////
    
    struct VertexAttributeDesc
    {
        std::string name;
        Format format = Format::UNKNOWN;
        uint32_t arraySize = 1;
        uint32_t bufferIndex = 0;
        uint32_t offset = 0;
        // note: for most APIs, all strides for a given bufferIndex must be identical
        uint32_t elementStride = 0;
        bool isInstanced = false;

                  VertexAttributeDesc& setName(const std::string& value) { name = value; return *this; }
        constexpr VertexAttributeDesc& setFormat(Format value) { format = value; return *this; }
        constexpr VertexAttributeDesc& setArraySize(uint32_t value) { arraySize = value; return *this; }
        constexpr VertexAttributeDesc& setBufferIndex(uint32_t value) { bufferIndex = value; return *this; }
        constexpr VertexAttributeDesc& setOffset(uint32_t value) { offset = value; return *this; }
        constexpr VertexAttributeDesc& setElementStride(uint32_t value) { elementStride = value; return *this; }
        constexpr VertexAttributeDesc& setIsInstanced(bool value) { isInstanced = value; return *this; }
    };

    class IInputLayout : public IResource
    {
    public:
        [[nodiscard]] virtual uint32_t getNumAttributes() const = 0;
        [[nodiscard]] virtual const VertexAttributeDesc* getAttributeDesc(uint32_t index) const = 0;
    };

    typedef RefCountPtr<IInputLayout> InputLayoutHandle;

    //////////////////////////////////////////////////////////////////////////
    // Buffer
    //////////////////////////////////////////////////////////////////////////

    struct BufferDesc
    {
        uint64_t byteSize = 0;
        uint32_t structStride = 0; // if non-zero it's structured
        uint32_t maxVersions = 0; // only valid and required to be nonzero for volatile buffers on Vulkan
        std::string debugName;
        Format format = Format::UNKNOWN; // for typed buffer views
        bool canHaveUAVs = false;
        bool canHaveTypedViews = false;
        bool canHaveRawViews = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isConstantBuffer = false;
        bool isDrawIndirectArgs = false;
        bool isAccelStructBuildInput = false;
        bool isAccelStructStorage = false;

        // A dynamic/upload buffer whose contents only live in the current command list
        bool isVolatile = false;

        // Indicates that the buffer is created with no backing memory,
        // and memory is bound to the texture later using bindBufferMemory.
        // On DX12, the buffer resource is created at the time of memory binding.
        bool isVirtual = false;

        ResourceStates initialState = ResourceStates::Common;

        // see TextureDesc::keepInitialState
        bool keepInitialState = false;

        CpuAccessMode cpuAccess = CpuAccessMode::None;

        SharedResourceFlags sharedResourceFlags = SharedResourceFlags::None;

        constexpr BufferDesc& setByteSize(uint64_t value) { byteSize = value; return *this; }
        constexpr BufferDesc& setStructStride(uint32_t value) { structStride = value; return *this; }
        constexpr BufferDesc& setMaxVersions(uint32_t value) { maxVersions = value; return *this; }
                  BufferDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
        constexpr BufferDesc& setFormat(Format value) { format = value; return *this; }
        constexpr BufferDesc& setCanHaveUAVs(bool value) { canHaveUAVs = value; return *this; }
        constexpr BufferDesc& setCanHaveTypedViews(bool value) { canHaveTypedViews = value; return *this; }
        constexpr BufferDesc& setCanHaveRawViews(bool value) { canHaveRawViews = value; return *this; }
        constexpr BufferDesc& setIsVertexBuffer(bool value) { isVertexBuffer = value; return *this; }
        constexpr BufferDesc& setIsIndexBuffer(bool value) { isIndexBuffer = value; return *this; }
        constexpr BufferDesc& setIsConstantBuffer(bool value) { isConstantBuffer = value; return *this; }
        constexpr BufferDesc& setIsDrawIndirectArgs(bool value) { isDrawIndirectArgs = value; return *this; }
        constexpr BufferDesc& setIsAccelStructBuildInput(bool value) { isAccelStructBuildInput = value; return *this; }
        constexpr BufferDesc& setIsAccelStructStorage(bool value) { isAccelStructStorage = value; return *this; }
        constexpr BufferDesc& setIsVolatile(bool value) { isVolatile = value; return *this; }
        constexpr BufferDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        constexpr BufferDesc& setInitialState(ResourceStates value) { initialState = value; return *this; }
        constexpr BufferDesc& setKeepInitialState(bool value) { keepInitialState = value; return *this; }
        constexpr BufferDesc& setCpuAccess(CpuAccessMode value) { cpuAccess = value; return *this; }
    };

    struct BufferRange
    {
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
        
        BufferRange() = default;

        BufferRange(uint64_t _byteOffset, uint64_t _byteSize)
            : byteOffset(_byteOffset)
            , byteSize(_byteSize)
        { }

        [[nodiscard]] NVRHI_API BufferRange resolve(const BufferDesc& desc) const;
        [[nodiscard]] constexpr bool isEntireBuffer(const BufferDesc& desc) const { return (byteOffset == 0) && (byteSize == ~0ull || byteSize == desc.byteSize); }
        constexpr bool operator== (const BufferRange& other) const { return byteOffset == other.byteOffset && byteSize == other.byteSize; }

        constexpr BufferRange& setByteOffset(uint64_t value) { byteOffset = value; return *this; }
        constexpr BufferRange& setByteSize(uint64_t value) { byteSize = value; return *this; }
    };

    static const BufferRange EntireBuffer = BufferRange(0, ~0ull);

    class IBuffer : public IResource
    {
    public:
        [[nodiscard]] virtual const BufferDesc& getDesc() const = 0;
    };

    typedef RefCountPtr<IBuffer> BufferHandle;

    //////////////////////////////////////////////////////////////////////////
    // Shader
    //////////////////////////////////////////////////////////////////////////

    // Shader type mask. The values match ones used in Vulkan.
    enum class ShaderType : uint16_t
    {
        None            = 0x0000,

        Compute         = 0x0020,

        Vertex          = 0x0001,
        Hull            = 0x0002,
        Domain          = 0x0004,
        Geometry        = 0x0008,
        Pixel           = 0x0010,
        Amplification   = 0x0040,
        Mesh            = 0x0080,
        AllGraphics     = 0x00FE,

        RayGeneration   = 0x0100,
        AnyHit          = 0x0200,
        ClosestHit      = 0x0400,
        Miss            = 0x0800,
        Intersection    = 0x1000,
        Callable        = 0x2000,
        AllRayTracing   = 0x3F00,

        All             = 0x3FFF,
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(ShaderType)

    enum class FastGeometryShaderFlags : uint8_t
    {
        ForceFastGS                      = 0x01,
        UseViewportMask                  = 0x02,
        OffsetTargetIndexByViewportIndex = 0x04,
        StrictApiOrder                   = 0x08
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(FastGeometryShaderFlags)

    struct CustomSemantic
    {
        enum Type
        {
            Undefined = 0,
            XRight = 1,
            ViewportMask = 2
        };

        Type type;
        std::string name;
    };

    struct ShaderDesc
    {
        ShaderType shaderType = ShaderType::None;
        std::string debugName;
        std::string entryName = "main";

        int hlslExtensionsUAV = -1;

        bool useSpecificShaderExt = false;
        uint32_t numCustomSemantics = 0;
        CustomSemantic* pCustomSemantics = nullptr;

        FastGeometryShaderFlags fastGSFlags = FastGeometryShaderFlags(0);
        uint32_t* pCoordinateSwizzling = nullptr;

        ShaderDesc() = default;

        ShaderDesc(ShaderType type)
            : shaderType(type)
        { }
    };

    struct ShaderSpecialization
    {
        uint32_t constantID = 0;
        union
        {
            uint32_t u = 0;
            int32_t i;
            float f;
        } value;

        static ShaderSpecialization UInt32(uint32_t constantID, uint32_t u) {
            ShaderSpecialization s;
            s.constantID = constantID;
            s.value.u = u;
            return s;
        }

        static ShaderSpecialization Int32(uint32_t constantID, int32_t i) {
            ShaderSpecialization s;
            s.constantID = constantID;
            s.value.i = i;
            return s;
        }

        static ShaderSpecialization Float(uint32_t constantID, float f) {
            ShaderSpecialization s;
            s.constantID = constantID;
            s.value.f = f;
            return s;
        }
    };

    class IShader : public IResource
    {
    public:
        [[nodiscard]] virtual const ShaderDesc& getDesc() const = 0;
        virtual void getBytecode(const void** ppBytecode, size_t* pSize) const = 0;
    };

    typedef RefCountPtr<IShader> ShaderHandle;

    //////////////////////////////////////////////////////////////////////////
    // Shader Library
    //////////////////////////////////////////////////////////////////////////

    class IShaderLibrary : public IResource
    {
    public:
        virtual void getBytecode(const void** ppBytecode, size_t* pSize) const = 0;
        virtual ShaderHandle getShader(const char* entryName, ShaderType shaderType) = 0;
    };

    typedef RefCountPtr<IShaderLibrary> ShaderLibraryHandle;

    //////////////////////////////////////////////////////////////////////////
    // Blend State
    //////////////////////////////////////////////////////////////////////////

    enum class BlendFactor : uint8_t
    {
        Zero = 1,
        One = 2,
        SrcColor = 3,
        InvSrcColor = 4,
        SrcAlpha = 5,
        InvSrcAlpha = 6,
        DstAlpha  = 7,
        InvDstAlpha = 8,
        DstColor = 9,
        InvDstColor = 10,
        SrcAlphaSaturate = 11,
        ConstantColor = 14,
        InvConstantColor = 15,
        Src1Color = 16,
        InvSrc1Color = 17,
        Src1Alpha = 18,
        InvSrc1Alpha = 19,

        // Vulkan names
        OneMinusSrcColor = InvSrcColor,
        OneMinusSrcAlpha = InvSrcAlpha,
        OneMinusDstAlpha = InvDstAlpha,
        OneMinusDstColor = InvDstColor,
        OneMinusConstantColor = InvConstantColor,
        OneMinusSrc1Color = InvSrc1Color,
        OneMinusSrc1Alpha = InvSrc1Alpha,
    };
    
    enum class BlendOp : uint8_t
    {
        Add = 1,
        Subrtact = 2,
        ReverseSubtract = 3,
        Min = 4,
        Max = 5
    };

    enum class ColorMask : uint8_t
    {
        // These values are equal to their counterparts in DX11, DX12, and Vulkan.
        Red = 1,
        Green = 2,
        Blue = 4,
        Alpha = 8,
        All = 0xF
    };

    NVRHI_ENUM_CLASS_FLAG_OPERATORS(ColorMask)

    struct BlendState
    {
        struct RenderTarget
        {
            bool        blendEnable = false;
            BlendFactor srcBlend = BlendFactor::One;
            BlendFactor destBlend = BlendFactor::Zero;
            BlendOp     blendOp = BlendOp::Add;
            BlendFactor srcBlendAlpha = BlendFactor::One;
            BlendFactor destBlendAlpha = BlendFactor::Zero;
            BlendOp     blendOpAlpha = BlendOp::Add;
            ColorMask   colorWriteMask = ColorMask::All;

            constexpr RenderTarget& setBlendEnable(bool enable) { blendEnable = enable; return *this; }
            constexpr RenderTarget& enableBlend() { blendEnable = true; return *this; }
            constexpr RenderTarget& disableBlend() { blendEnable = false; return *this; }
            constexpr RenderTarget& setSrcBlend(BlendFactor value) { srcBlend = value; return *this; }
            constexpr RenderTarget& setDestBlend(BlendFactor value) { destBlend = value; return *this; }
            constexpr RenderTarget& setBlendOp(BlendOp value) { blendOp = value; return *this; }
            constexpr RenderTarget& setSrcBlendAlpha(BlendFactor value) { srcBlendAlpha = value; return *this; }
            constexpr RenderTarget& setDestBlendAlpha(BlendFactor value) { destBlendAlpha = value; return *this; }
            constexpr RenderTarget& setBlendOpAlpha(BlendOp value) { blendOpAlpha = value; return *this; }
            constexpr RenderTarget& setColorWriteMask(ColorMask value) { colorWriteMask = value; return *this; }

            [[nodiscard]] NVRHI_API bool usesConstantColor() const;

            constexpr bool operator ==(const RenderTarget& other) const
            {
                return blendEnable == other.blendEnable
                    && srcBlend == other.srcBlend
                    && destBlend == other.destBlend
                    && blendOp == other.blendOp
                    && srcBlendAlpha == other.srcBlendAlpha
                    && destBlendAlpha == other.destBlendAlpha
                    && blendOpAlpha == other.blendOpAlpha
                    && colorWriteMask == other.colorWriteMask;
            }

            constexpr bool operator !=(const RenderTarget& other) const
            {
                return !(*this == other);
            }
        };

        RenderTarget targets[c_MaxRenderTargets];
        bool alphaToCoverageEnable = false;

        constexpr BlendState& setRenderTarget(uint32_t index, const RenderTarget& target) { targets[index] = target; return *this; }
        constexpr BlendState& setAlphaToCoverageEnable(bool enable) { alphaToCoverageEnable = enable; return *this; }
        constexpr BlendState& enableAlphaToCoverage() { alphaToCoverageEnable = true; return *this; }
        constexpr BlendState& disableAlphaToCoverage() { alphaToCoverageEnable = false; return *this; }

        [[nodiscard]] bool usesConstantColor(uint32_t numTargets) const;

        constexpr bool operator ==(const BlendState& other) const
        {
            if (alphaToCoverageEnable != other.alphaToCoverageEnable)
                return false;

            for (uint32_t i = 0; i < c_MaxRenderTargets; ++i)
            {
                if (targets[i] != other.targets[i])
                    return false;
            }

            return true;
        }

        constexpr bool operator !=(const BlendState& other) const
        {
            return !(*this == other);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Raster State
    //////////////////////////////////////////////////////////////////////////

    enum class RasterFillMode : uint8_t
    {
        Solid,
        Wireframe,

        // Vulkan names
        Fill = Solid,
        Line = Wireframe
    };

    enum class RasterCullMode : uint8_t
    {
        Back,
        Front,
        None
    };

    struct RasterState
    {
        RasterFillMode fillMode = RasterFillMode::Solid;
        RasterCullMode cullMode = RasterCullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClipEnable = false;
        bool scissorEnable = false;
        bool multisampleEnable = false;
        bool antialiasedLineEnable = false;
        int depthBias = 0;
        float depthBiasClamp = 0.f;
        float slopeScaledDepthBias = 0.f;

        // Extended rasterizer state supported by Maxwell
        // In D3D11, use NvAPI_D3D11_CreateRasterizerState to create such rasterizer state.
        uint8_t forcedSampleCount = 0;
        bool programmableSamplePositionsEnable = false;
        bool conservativeRasterEnable = false;
        bool quadFillEnable = false;
        char samplePositionsX[16]{};
        char samplePositionsY[16]{};
        
        constexpr RasterState& setFillMode(RasterFillMode value) { fillMode = value; return *this; }
        constexpr RasterState& setFillSolid() { fillMode = RasterFillMode::Solid; return *this; }
        constexpr RasterState& setFillWireframe() { fillMode = RasterFillMode::Wireframe; return *this; }
        constexpr RasterState& setCullMode(RasterCullMode value) { cullMode = value; return *this; }
        constexpr RasterState& setCullBack() { cullMode = RasterCullMode::Back; return *this; }
        constexpr RasterState& setCullFront() { cullMode = RasterCullMode::Front; return *this; }
        constexpr RasterState& setCullNone() { cullMode = RasterCullMode::None; return *this; }
        constexpr RasterState& setFrontCounterClockwise(bool value) { frontCounterClockwise = value; return *this; }
        constexpr RasterState& setDepthClipEnable(bool value) { depthClipEnable = value; return *this; }
        constexpr RasterState& enableDepthClip() { depthClipEnable = true; return *this; }
        constexpr RasterState& disableDepthClip() { depthClipEnable = false; return *this; }
        constexpr RasterState& setScissorEnable(bool value) { scissorEnable = value; return *this; }
        constexpr RasterState& enableScissor() { scissorEnable = true; return *this; }
        constexpr RasterState& disableScissor() { scissorEnable = false; return *this; }
        constexpr RasterState& setMultisampleEnable(bool value) { multisampleEnable = value; return *this; }
        constexpr RasterState& enableMultisample() { multisampleEnable = true; return *this; }
        constexpr RasterState& disableMultisample() { multisampleEnable = false; return *this; }
        constexpr RasterState& setAntialiasedLineEnable(bool value) { antialiasedLineEnable = value; return *this; }
        constexpr RasterState& enableAntialiasedLine() { antialiasedLineEnable = true; return *this; }
        constexpr RasterState& disableAntialiasedLine() { antialiasedLineEnable = false; return *this; }
        constexpr RasterState& setDepthBias(int value) { depthBias = value; return *this; }
        constexpr RasterState& setDepthBiasClamp(float value) { depthBiasClamp = value; return *this; }
        constexpr RasterState& setSlopeScaleDepthBias(float value) { slopeScaledDepthBias = value; return *this; }
        constexpr RasterState& setForcedSampleCount(uint8_t value) { forcedSampleCount = value; return *this; }
        constexpr RasterState& setProgrammableSamplePositionsEnable(bool value) { programmableSamplePositionsEnable = value; return *this; }
        constexpr RasterState& enableProgrammableSamplePositions() { programmableSamplePositionsEnable = true; return *this; }
        constexpr RasterState& disableProgrammableSamplePositions() { programmableSamplePositionsEnable = false; return *this; }
        constexpr RasterState& setConservativeRasterEnable(bool value) { conservativeRasterEnable = value; return *this; }
        constexpr RasterState& enableConservativeRaster() { conservativeRasterEnable = true; return *this; }
        constexpr RasterState& disableConservativeRaster() { conservativeRasterEnable = false; return *this; }
        constexpr RasterState& setQuadFillEnable(bool value) { quadFillEnable = value; return *this; }
        constexpr RasterState& enableQuadFill() { quadFillEnable = true; return *this; }
        constexpr RasterState& disableQuadFill() { quadFillEnable = false; return *this; }
        constexpr RasterState& setSamplePositions(const char* x, const char* y, int count) { for (int i = 0; i < count; i++) { samplePositionsX[i] = x[i]; samplePositionsY[i] = y[i]; } return *this; }
    };

    //////////////////////////////////////////////////////////////////////////
    // Depth Stencil State
    //////////////////////////////////////////////////////////////////////////
    
    enum class StencilOp : uint8_t
    {
        Keep = 1,
        Zero = 2,
        Replace = 3,
        IncrementAndClamp = 4,
        DecrementAndClamp = 5,
        Invert = 6,
        IncrementAndWrap = 7,
        DecrementAndWrap = 8
    };

    enum class ComparisonFunc : uint8_t
    {
        Never = 1,
        Less = 2,
        Equal = 3,
        LessOrEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterOrEqual = 7,
        Always = 8
    };

    struct DepthStencilState
    {
        struct StencilOpDesc
        {
            StencilOp failOp = StencilOp::Keep;
            StencilOp depthFailOp = StencilOp::Keep;
            StencilOp passOp = StencilOp::Keep;
            ComparisonFunc stencilFunc = ComparisonFunc::Always;

            constexpr StencilOpDesc& setFailOp(StencilOp value) { failOp = value; return *this; }
            constexpr StencilOpDesc& setDepthFailOp(StencilOp value) { depthFailOp = value; return *this; }
            constexpr StencilOpDesc& setPassOp(StencilOp value) { passOp = value; return *this; }
            constexpr StencilOpDesc& setStencilFunc(ComparisonFunc value) { stencilFunc = value; return *this; }
        };

        bool            depthTestEnable = true;
        bool            depthWriteEnable = true;
        ComparisonFunc  depthFunc = ComparisonFunc::Less;
        bool            stencilEnable = false;
        uint8_t         stencilReadMask = 0xff;
        uint8_t         stencilWriteMask = 0xff;
        uint8_t         stencilRefValue = 0;
        StencilOpDesc   frontFaceStencil;
        StencilOpDesc   backFaceStencil;

        constexpr DepthStencilState& setDepthTestEnable(bool value) { depthTestEnable = value; return *this; }
        constexpr DepthStencilState& enableDepthTest() { depthTestEnable = true; return *this; }
        constexpr DepthStencilState& disableDepthTest() { depthTestEnable = false; return *this; }
        constexpr DepthStencilState& setDepthWriteEnable(bool value) { depthWriteEnable = value; return *this; }
        constexpr DepthStencilState& enableDepthWrite() { depthWriteEnable = true; return *this; }
        constexpr DepthStencilState& disableDepthWrite() { depthWriteEnable = false; return *this; }
        constexpr DepthStencilState& setDepthFunc(ComparisonFunc value) { depthFunc = value; return *this; }
        constexpr DepthStencilState& setStencilEnable(bool value) { stencilEnable = value; return *this; }
        constexpr DepthStencilState& enableStencil() { stencilEnable = true; return *this; }
        constexpr DepthStencilState& disableStencil() { stencilEnable = false; return *this; }
        constexpr DepthStencilState& setStencilReadMask(uint8_t value) { stencilReadMask = value; return *this; }
        constexpr DepthStencilState& setStencilWriteMask(uint8_t value) { stencilWriteMask = value; return *this; }
        constexpr DepthStencilState& setStencilRefValue(uint8_t value) { stencilRefValue = value; return *this; }
        constexpr DepthStencilState& setFrontFaceStencil(const StencilOpDesc& value) { frontFaceStencil = value; return *this; }
        constexpr DepthStencilState& setBackFaceStencil(const StencilOpDesc& value) { backFaceStencil = value; return *this; }
    };

    //////////////////////////////////////////////////////////////////////////
    // Viewport State
    //////////////////////////////////////////////////////////////////////////
    
    struct ViewportState
    {
        //These are in pixels
        // note: you can only set each of these either in the PSO or per draw call in DrawArguments
        // it is not legal to have the same state set in both the PSO and DrawArguments
        // leaving these vectors empty means no state is set
        static_vector<Viewport, c_MaxViewports> viewports;
        static_vector<Rect, c_MaxViewports> scissorRects;

        ViewportState& addViewport(const Viewport& v) { viewports.push_back(v); return *this; }
        ViewportState& addScissorRect(const Rect& r) { scissorRects.push_back(r); return *this; }
        ViewportState& addViewportAndScissorRect(const Viewport& v) { return addViewport(v).addScissorRect(Rect(v)); }
    };

    //////////////////////////////////////////////////////////////////////////
    // Sampler
    //////////////////////////////////////////////////////////////////////////

    enum class SamplerAddressMode : uint8_t
    {
        // D3D names
        Clamp,
        Wrap,
        Border,
        Mirror,
        MirrorOnce,

        // Vulkan names
        ClampToEdge = Clamp,
        Repeat = Wrap,
        ClampToBorder = Border,
        MirroredRepeat = Mirror,
        MirrorClampToEdge = MirrorOnce
    };

    enum class SamplerReductionType : uint8_t
    {
        Standard,
        Comparison,
        Minimum,
        Maximum
    };

    struct SamplerDesc
    {
        Color borderColor = 1.f;
        float maxAnisotropy = 1.f;
        float mipBias = 0.f;

        bool minFilter = true;
        bool magFilter = true;
        bool mipFilter = true;
        SamplerAddressMode addressU = SamplerAddressMode::Clamp;
        SamplerAddressMode addressV = SamplerAddressMode::Clamp;
        SamplerAddressMode addressW = SamplerAddressMode::Clamp;
        SamplerReductionType reductionType = SamplerReductionType::Standard;

        SamplerDesc& setBorderColor(const Color& color) { borderColor = color; return *this; }
        SamplerDesc& setMaxAnisotropy(float value) { maxAnisotropy = value; return *this; }
        SamplerDesc& setMipBias(float value) { mipBias = value; return *this; }
        SamplerDesc& setMinFilter(bool enable) { minFilter = enable; return *this; }
        SamplerDesc& setMagFilter(bool enable) { magFilter = enable; return *this; }
        SamplerDesc& setMipFilter(bool enable) { mipFilter = enable; return *this; }
        SamplerDesc& setAllFilters (bool enable) { minFilter = magFilter = mipFilter = enable; return *this; }
        SamplerDesc& setAddressU(SamplerAddressMode mode) { addressU = mode; return *this; }
        SamplerDesc& setAddressV(SamplerAddressMode mode) { addressV = mode; return *this; }
        SamplerDesc& setAddressW(SamplerAddressMode mode) { addressW = mode; return *this; }
        SamplerDesc& setAllAddressModes(SamplerAddressMode mode) { addressU = addressV = addressW = mode; return *this; }
        SamplerDesc& setReductionType(SamplerReductionType type) { reductionType = type; return *this; }
    };

    class ISampler : public IResource 
    { 
    public:
        [[nodiscard]] virtual const SamplerDesc& getDesc() const = 0;
    };

    typedef RefCountPtr<ISampler> SamplerHandle;
    
    //////////////////////////////////////////////////////////////////////////
    // Framebuffer
    //////////////////////////////////////////////////////////////////////////

    struct FramebufferAttachment
    {
        ITexture* texture = nullptr;
        TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, 1);
        Format format = Format::UNKNOWN;
        bool isReadOnly = false;
        
        constexpr FramebufferAttachment& setTexture(ITexture* t) { texture = t; return *this; }
        constexpr FramebufferAttachment& setSubresources(TextureSubresourceSet value) { subresources = value; return *this; }
        constexpr FramebufferAttachment& setArraySlice(ArraySlice index) { subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
        constexpr FramebufferAttachment& setArraySliceRange(ArraySlice index, ArraySlice count) { subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
        constexpr FramebufferAttachment& setMipLevel(MipLevel level) { subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
        constexpr FramebufferAttachment& setFormat(Format f) { format = f; return *this; }
        constexpr FramebufferAttachment& setReadOnly(bool ro) { isReadOnly = ro; return *this; }

        [[nodiscard]] bool valid() const { return texture != nullptr; }
    };

    struct FramebufferDesc
    {
        static_vector<FramebufferAttachment, c_MaxRenderTargets> colorAttachments;
        FramebufferAttachment depthAttachment;
        FramebufferAttachment shadingRateAttachment;

        FramebufferDesc& addColorAttachment(const FramebufferAttachment& a) { colorAttachments.push_back(a); return *this; }
        FramebufferDesc& addColorAttachment(ITexture* texture) { colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }
        FramebufferDesc& addColorAttachment(ITexture* texture, TextureSubresourceSet subresources) { colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
        FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d) { depthAttachment = d; return *this; }
        FramebufferDesc& setDepthAttachment(ITexture* texture) { depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }
        FramebufferDesc& setDepthAttachment(ITexture* texture, TextureSubresourceSet subresources) { depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
        FramebufferDesc& setShadingRateAttachment(const FramebufferAttachment& d) { shadingRateAttachment = d; return *this; }
        FramebufferDesc& setShadingRateAttachment(ITexture* texture) { shadingRateAttachment = FramebufferAttachment().setTexture(texture); return *this; }
        FramebufferDesc& setShadingRateAttachment(ITexture* texture, TextureSubresourceSet subresources) { shadingRateAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    };

    struct FramebufferInfo
    {
        static_vector<Format, c_MaxRenderTargets> colorFormats;
        Format depthFormat = Format::UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;

        FramebufferInfo() = default;
        NVRHI_API FramebufferInfo(const FramebufferDesc& desc);

        bool operator==(const FramebufferInfo& other) const
        {
            return formatsEqual(colorFormats, other.colorFormats)
                && depthFormat == other.depthFormat
                && width == other.width
                && height == other.height
                && sampleCount == other.sampleCount
                && sampleQuality == other.sampleQuality;
        }
        bool operator!=(const FramebufferInfo& other) const { return !(*this == other); }

        [[nodiscard]] Viewport getViewport(float minZ = 0.f, float maxZ = 1.f) const
        {
            return Viewport(0.f, float(width), 0.f, float(height), minZ, maxZ);
        }

    private:
        static bool formatsEqual(const static_vector<Format, c_MaxRenderTargets>& a, const static_vector<Format, c_MaxRenderTargets>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return false;
            return true;
        }
    };

    class IFramebuffer : public IResource 
    {
    public:
        [[nodiscard]] virtual const FramebufferDesc& getDesc() const = 0;
        [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo() const = 0;
    };

    typedef RefCountPtr<IFramebuffer> FramebufferHandle;

    namespace rt
    {
        class IAccelStruct;

        typedef float AffineTransform[12];

        constexpr AffineTransform c_IdentityTransform = {
        //  +----+----+---------  rotation and scaling
        //  v    v    v
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f
        //                 ^
        //                 +----  translation
        };

        enum class GeometryFlags : uint8_t
        {
            None = 0,
            Opaque = 1,
            NoDuplicateAnyHitInvocation = 2
        };

        NVRHI_ENUM_CLASS_FLAG_OPERATORS(GeometryFlags)

        enum class GeometryType : uint8_t
        {
            Triangles = 0,
            AABBs = 1
        };

        struct GeometryAABB
        {
            float minX;
            float minY;
            float minZ;
            float maxX;
            float maxY;
            float maxZ;
        };

        struct GeometryTriangles
        {
            IBuffer* indexBuffer = nullptr;   // make sure the first fields in both Triangles 
            IBuffer* vertexBuffer = nullptr;  // and AABBs are IBuffer* for easier debugging
            Format indexFormat = Format::UNKNOWN;
            Format vertexFormat = Format::UNKNOWN;
            uint64_t indexOffset = 0;
            uint64_t vertexOffset = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t vertexStride = 0;

            GeometryTriangles& setIndexBuffer(IBuffer* value) { indexBuffer = value; return *this; }
            GeometryTriangles& setVertexBuffer(IBuffer* value) { vertexBuffer = value; return *this; }
            GeometryTriangles& setIndexFormat(Format value) { indexFormat = value; return *this; }
            GeometryTriangles& setVertexFormat(Format value) { vertexFormat = value; return *this; }
            GeometryTriangles& setIndexOffset(uint64_t value) { indexOffset = value; return *this; }
            GeometryTriangles& setVertexOffset(uint64_t value) { vertexOffset = value; return *this; }
            GeometryTriangles& setIndexCount(uint32_t value) { indexCount = value; return *this; }
            GeometryTriangles& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
            GeometryTriangles& setVertexStride(uint32_t value) { vertexStride = value; return *this; }
        };

        struct GeometryAABBs
        {
            IBuffer* buffer = nullptr;
            IBuffer* unused = nullptr;
            uint64_t offset = 0;
            uint32_t count = 0;
            uint32_t stride = 0;

            GeometryAABBs& setBuffer(IBuffer* value) { buffer = value; return *this; }
            GeometryAABBs& setOffset(uint64_t value) { offset = value; return *this; }
            GeometryAABBs& setCount(uint32_t value) { count = value; return *this; }
            GeometryAABBs& setStride(uint32_t value) { stride = value; return *this; }
        };

        struct GeometryDesc
        {
            union GeomTypeUnion
            {
                GeometryTriangles triangles;
                GeometryAABBs aabbs;
            } geometryData;

            bool useTransform = false;
            AffineTransform transform{};
            GeometryFlags flags = GeometryFlags::None;
            GeometryType geometryType = GeometryType::Triangles;

            GeometryDesc() : geometryData{} { }

            GeometryDesc& setTransform(const AffineTransform& value) { memcpy(&transform, &value, sizeof(AffineTransform)); useTransform = true; return *this; }
            GeometryDesc& setFlags(GeometryFlags value) { flags = value; return *this; }
            GeometryDesc& setTriangles(const GeometryTriangles& value) { geometryData.triangles = value; geometryType = GeometryType::Triangles; return *this; }
            GeometryDesc& setAABBs(const GeometryAABBs& value) { geometryData.aabbs = value; geometryType = GeometryType::AABBs; return *this; }
        };
        
        enum class InstanceFlags : unsigned
        {
            None = 0,
            TriangleCullDisable = 1,
            TriangleFrontCounterclockwise = 2,
            ForceOpaque = 4,
            ForceNonOpaque = 8
        };

        NVRHI_ENUM_CLASS_FLAG_OPERATORS(InstanceFlags)

        struct InstanceDesc
        {
            AffineTransform transform;
            unsigned instanceID : 24;
            unsigned instanceMask : 8;
            unsigned instanceContributionToHitGroupIndex : 24;
            InstanceFlags flags : 8;
            union {
                IAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
                uint64_t blasDeviceAddress;  // for buildTopLevelAccelStructFromBuffer - use IAccelStruct::getDeviceAddress()
            };

            InstanceDesc()  // NOLINT(cppcoreguidelines-pro-type-member-init)
                : instanceID(0)
                , instanceMask(0)
                , instanceContributionToHitGroupIndex(0)
                , flags(InstanceFlags::None)
                , bottomLevelAS(nullptr)
            {
                setTransform(c_IdentityTransform);
            }

            InstanceDesc& setInstanceID(uint32_t value) { instanceID = value; return *this; }
            InstanceDesc& setInstanceContributionToHitGroupIndex(uint32_t value) { instanceContributionToHitGroupIndex = value; return *this; }
            InstanceDesc& setInstanceMask(uint32_t value) { instanceMask = value; return *this; }
            InstanceDesc& setTransform(const AffineTransform& value) { memcpy(&transform, &value, sizeof(AffineTransform)); return *this; }
            InstanceDesc& setFlags(InstanceFlags value) { flags = value; return *this; }
            InstanceDesc& setBLAS(IAccelStruct* value) { bottomLevelAS = value; return *this; }
        };

        static_assert(sizeof(InstanceDesc) == 64);

        enum class AccelStructBuildFlags : uint8_t
        {
            None = 0,
            AllowUpdate = 1,
            AllowCompaction = 2,
            PerferFastTrace = 4,
            PerferFastBuild = 8,
            MinimizeMemory = 0x10,
            PerformUpdate = 0x20
        };

        NVRHI_ENUM_CLASS_FLAG_OPERATORS(AccelStructBuildFlags)

        struct AccelStructDesc
        {
            size_t topLevelMaxInstances = 0; // only applies when isTopLevel = true
            std::vector<GeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
            AccelStructBuildFlags buildFlags = AccelStructBuildFlags::None;
            std::string debugName;
            bool trackLiveness = true;
            bool isTopLevel = false;
            bool isVirtual = false;

            AccelStructDesc& setTopLevelMaxInstances(size_t value) { topLevelMaxInstances = value; isTopLevel = true; return *this; }
            AccelStructDesc& addBottomLevelGeometry(const GeometryDesc& value) { bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
            AccelStructDesc& setBuildFlags(AccelStructBuildFlags value) { buildFlags = value; return *this; }
            AccelStructDesc& setDebugName(const std::string& value) { debugName = value; return *this; }
            AccelStructDesc& setTrackLiveness(bool value) { trackLiveness = value; return *this; }
            AccelStructDesc& setIsTopLevel(bool value) { isTopLevel = value; return *this; }
            AccelStructDesc& setIsVirtual(bool value) { isVirtual = value; return *this; }
        };

        //////////////////////////////////////////////////////////////////////////
        // rt::AccelStruct
        //////////////////////////////////////////////////////////////////////////

        class IAccelStruct : public IResource
        {
        public:
            [[nodiscard]] virtual const AccelStructDesc& getDesc() const = 0;
            [[nodiscard]] virtual bool isCompacted() const = 0;
            [[nodiscard]] virtual uint64_t getDeviceAddress() const = 0;
        };

        typedef RefCountPtr<IAccelStruct> AccelStructHandle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Binding Layouts
    //////////////////////////////////////////////////////////////////////////

    // identifies the underlying resource type in a binding
    enum class ResourceType : uint8_t
    {
        None,
        Texture_SRV,
        Texture_UAV,
        TypedBuffer_SRV,
        TypedBuffer_UAV,
        StructuredBuffer_SRV,
        StructuredBuffer_UAV,
        RawBuffer_SRV,
        RawBuffer_UAV,
        ConstantBuffer,
        VolatileConstantBuffer,
        Sampler,
        RayTracingAccelStruct,
        PushConstants,

        Count
    };

    
    struct BindingLayoutItem
    {
        uint32_t slot;

        ResourceType type : 8;
        uint8_t unused : 8;
        uint16_t size : 16;

        bool operator ==(const BindingLayoutItem& b) const {
            return slot == b.slot
                && type == b.type
                && size == b.size;
        }
        bool operator !=(const BindingLayoutItem& b) const { return !(*this == b); }


        // Helper functions for strongly typed initialization
#define NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TYPE) /* NOLINT(cppcoreguidelines-macro-usage) */ \
        static BindingLayoutItem TYPE(const uint32_t slot) { \
            BindingLayoutItem result{}; \
            result.slot = slot; \
            result.type = ResourceType::TYPE; \
            return result; }

        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_SRV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_UAV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_SRV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_UAV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_SRV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_UAV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_SRV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_UAV)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(ConstantBuffer)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(VolatileConstantBuffer)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(Sampler)
        NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER(RayTracingAccelStruct)

        static BindingLayoutItem PushConstants(const uint32_t slot, const size_t size)
        {
            BindingLayoutItem result{};
            result.slot = slot;
            result.type = ResourceType::PushConstants;
            result.size = uint16_t(size);
            return result;
        }
#undef NVRHI_BINDING_LAYOUT_ITEM_INITIALIZER
    };

    // verify the packing of BindingLayoutItem for good alignment
    static_assert(sizeof(BindingLayoutItem) == 8, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

    typedef static_vector<BindingLayoutItem, c_MaxBindingsPerLayout> BindingLayoutItemArray;

    // Describes compile-time settings for HLSL -> SPIR-V register allocation.
    // The default values match the offsets used by the NVRHI shaderCompiler tool.
    struct VulkanBindingOffsets
    {
        uint32_t shaderResource = 0;
        uint32_t sampler = 128;
        uint32_t constantBuffer = 256;
        uint32_t unorderedAccess = 384;

        constexpr VulkanBindingOffsets& setShaderResourceOffset(uint32_t value) { shaderResource = value; return *this; }
        constexpr VulkanBindingOffsets& setSamplerOffset(uint32_t value) { sampler = value; return *this; }
        constexpr VulkanBindingOffsets& setConstantBufferOffset(uint32_t value) { constantBuffer = value; return *this; }
        constexpr VulkanBindingOffsets& setUnorderedAccessViewOffset(uint32_t value) { unorderedAccess = value; return *this; }
    };

    struct BindingLayoutDesc
    {
        ShaderType visibility = ShaderType::None;
        uint32_t registerSpace = 0;
        BindingLayoutItemArray bindings;
        VulkanBindingOffsets bindingOffsets;

        BindingLayoutDesc& setVisibility(ShaderType value) { visibility = value; return *this; }
        BindingLayoutDesc& setRegisterSpace(uint32_t value) { registerSpace = value; return *this; }
        BindingLayoutDesc& addItem(const BindingLayoutItem& value) { bindings.push_back(value); return *this; }
        BindingLayoutDesc& setBindingOffsets(const VulkanBindingOffsets& value) { bindingOffsets = value; return *this; }
    };

    // Bindless layouts allow applications to attach a descriptor table to an unbounded
    // resource array in the shader. The size of the array is not known ahead of time.
    // The same table can be bound to multiple register spaces on DX12, in order to 
    // access different types of resources stored in the table through different arrays.
    // The `registerSpaces` vector specifies which spaces will the table be bound to,
    // with the table type (SRV or UAV) derived from the resource type assigned to each space.
    struct BindlessLayoutDesc
    {
        ShaderType visibility = ShaderType::None;
        uint32_t firstSlot = 0;
        uint32_t maxCapacity = 0;
        static_vector<BindingLayoutItem, 16> registerSpaces;

        BindlessLayoutDesc& setVisibility(ShaderType value) { visibility = value; return *this; }
        BindlessLayoutDesc& setFirstSlot(uint32_t value) { firstSlot = value; return *this; }
        BindlessLayoutDesc& setMaxCapacity(uint32_t value) { maxCapacity = value; return *this; }
        BindlessLayoutDesc& addRegisterSpace(const BindingLayoutItem& value) { registerSpaces.push_back(value); return *this; }
    };

    class IBindingLayout : public IResource
    {
    public:
        [[nodiscard]] virtual const BindingLayoutDesc* getDesc() const = 0;           // returns nullptr for bindless layouts
        [[nodiscard]] virtual const BindlessLayoutDesc* getBindlessDesc() const = 0;  // returns nullptr for regular layouts
    };

    typedef RefCountPtr<IBindingLayout> BindingLayoutHandle;

    //////////////////////////////////////////////////////////////////////////
    // Binding Sets
    //////////////////////////////////////////////////////////////////////////

    struct BindingSetItem
    {
        IResource* resourceHandle;

        uint32_t slot;

        ResourceType type          : 8;
        TextureDimension dimension : 8; // valid for Texture_SRV, Texture_UAV
        Format format              : 8; // valid for Texture_SRV, Texture_UAV, Buffer_SRV, Buffer_UAV
        uint8_t unused             : 8;

        union 
        {
            TextureSubresourceSet subresources; // valid for Texture_SRV, Texture_UAV
            BufferRange range; // valid for Buffer_SRV, Buffer_UAV, ConstantBuffer
            uint64_t rawData[2];
        };

        // verify that the `subresources` and `range` have the same size and are covered by `rawData`
        static_assert(sizeof(TextureSubresourceSet) == 16, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
        static_assert(sizeof(BufferRange) == 16, "sizeof(BufferRange) is supposed to be 16 bytes");

        bool operator ==(const BindingSetItem& b) const
        {
            return resourceHandle == b.resourceHandle
                && slot == b.slot
                && type == b.type
                && dimension == b.dimension
                && format == b.format
                && rawData[0] == b.rawData[0]
                && rawData[1] == b.rawData[1];
        }

        bool operator !=(const BindingSetItem& b) const
        {
            return !(*this == b);
        }

        // Default constructor that doesn't initialize anything for performance:
        // BindingSetItem's are stored in large statically sized arrays.
        BindingSetItem() { }  // NOLINT(cppcoreguidelines-pro-type-member-init, modernize-use-equals-default)

        // Helper functions for strongly typed initialization

        static BindingSetItem None(uint32_t slot = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::None;
            result.resourceHandle = nullptr;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            return result;
        }

        static BindingSetItem Texture_SRV(uint32_t slot, ITexture* texture, Format format = Format::UNKNOWN,
            TextureSubresourceSet subresources = AllSubresources, TextureDimension dimension = TextureDimension::Unknown)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Texture_SRV;
            result.resourceHandle = texture;
            result.format = format;
            result.dimension = dimension;
            result.subresources = subresources;
            result.unused = 0;
            return result;
        }

        static BindingSetItem Texture_UAV(uint32_t slot, ITexture* texture, Format format = Format::UNKNOWN,
            TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::AllArraySlices),
            TextureDimension dimension = TextureDimension::Unknown)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Texture_UAV;
            result.resourceHandle = texture;
            result.format = format;
            result.dimension = dimension;
            result.subresources = subresources;
            result.unused = 0;
            return result;
        }

        static BindingSetItem TypedBuffer_SRV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::TypedBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem TypedBuffer_UAV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::TypedBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem ConstantBuffer(uint32_t slot, IBuffer* buffer)
        {
            bool isVolatile = buffer && buffer->getDesc().isVolatile;

            BindingSetItem result;
            result.slot = slot;
            result.type = isVolatile ? ResourceType::VolatileConstantBuffer : ResourceType::ConstantBuffer;
            result.resourceHandle = buffer;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.range = EntireBuffer;
            result.unused = 0;
            return result;
        }

        static BindingSetItem Sampler(uint32_t slot, ISampler* sampler)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Sampler;
            result.resourceHandle = sampler;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            return result;
        }

        static BindingSetItem RayTracingAccelStruct(uint32_t slot, rt::IAccelStruct* as)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::RayTracingAccelStruct;
            result.resourceHandle = as;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.rawData[0] = 0;
            result.rawData[1] = 0;
            result.unused = 0;
            return result;
        }

        static BindingSetItem StructuredBuffer_SRV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::StructuredBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem StructuredBuffer_UAV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::StructuredBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = format;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem RawBuffer_SRV(uint32_t slot, IBuffer* buffer, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::RawBuffer_SRV;
            result.resourceHandle = buffer;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem RawBuffer_UAV(uint32_t slot, IBuffer* buffer, BufferRange range = EntireBuffer)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::RawBuffer_UAV;
            result.resourceHandle = buffer;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.range = range;
            result.unused = 0;
            return result;
        }

        static BindingSetItem PushConstants(uint32_t slot, uint32_t byteSize)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::PushConstants;
            result.resourceHandle = nullptr;
            result.format = Format::UNKNOWN;
            result.dimension = TextureDimension::Unknown;
            result.range.byteOffset = 0;
            result.range.byteSize = byteSize;
            result.unused = 0;
            return result;
        }

        BindingSetItem& setFormat(Format value) { format = value; return *this; }
        BindingSetItem& setDimension(TextureDimension value) { dimension = value; return *this; }
        BindingSetItem& setSubresources(TextureSubresourceSet value) { subresources = value; return *this; }
        BindingSetItem& setRange(BufferRange value) { range = value; return *this; }
    };

    // verify the packing of BindingSetItem for good alignment
    static_assert(sizeof(BindingSetItem) == 32, "sizeof(BindingSetItem) is supposed to be 32 bytes");

    // describes the resource bindings for a single pipeline stage
    typedef static_vector<BindingSetItem, c_MaxBindingsPerLayout> BindingSetItemArray;

    // describes a set of bindings across all stages of the pipeline
    // (not all bindings need to be present in the set, but the set must be defined by a single BindingSetItem object)
    struct BindingSetDesc
    {
        BindingSetItemArray bindings;
       
        // Enables automatic liveness tracking of this binding set by nvrhi command lists.
        // By setting trackLiveness to false, you take the responsibility of not releasing it 
        // until all rendering commands using the binding set are finished.
        bool trackLiveness = true;

        bool operator ==(const BindingSetDesc& b) const
        {
            if (bindings.size() != b.bindings.size())
                return false;

            for (size_t i = 0; i < bindings.size(); ++i)
            {
                if (bindings[i] != b.bindings[i])
                    return false;
            }

            return true;
        }

        bool operator !=(const BindingSetDesc& b) const
        {
            return !(*this == b);
        }

        BindingSetDesc& addItem(const BindingSetItem& value) { bindings.push_back(value); return *this; }
        BindingSetDesc& setTrackLiveness(bool value) { trackLiveness = value; return *this; }
    };

    class IBindingSet : public IResource
    {
    public:
        [[nodiscard]] virtual const BindingSetDesc* getDesc() const = 0;  // returns nullptr for descriptor tables
        [[nodiscard]] virtual IBindingLayout* getLayout() const = 0;
    };

    typedef RefCountPtr<IBindingSet> BindingSetHandle;

    // Descriptor tables are bare, without extra mappings, state, or liveness tracking.
    // Unlike binding sets, descriptor tables are mutable - moreover, modification is the only way to populate them.
    // They can be grown or shrunk, and they are not tied to any binding layout.
    // All tracking is off, so applications should use descriptor tables with great care.
    // IDescriptorTable is derived from IBindingSet to allow mixing them in the binding arrays.
    class IDescriptorTable : public IBindingSet
    {
    public:
        [[nodiscard]] virtual uint32_t getCapacity() const = 0;
    };

    typedef RefCountPtr<IDescriptorTable> DescriptorTableHandle;

    //////////////////////////////////////////////////////////////////////////
    // Draw State
    //////////////////////////////////////////////////////////////////////////

    enum class PrimitiveType : uint8_t
    {
        PointList,
        LineList,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList
    };

    struct SinglePassStereoState
    {
        bool enabled = false;
        bool independentViewportMask = false;
        uint16_t renderTargetIndexOffset = 0;

        bool operator ==(const SinglePassStereoState& b) const {
            return enabled == b.enabled
                && independentViewportMask == b.independentViewportMask
                && renderTargetIndexOffset == b.renderTargetIndexOffset;
        }

        bool operator !=(const SinglePassStereoState& b) const { return !(*this == b); }

        constexpr SinglePassStereoState& setEnabled(bool value) { enabled = value; return *this; }
        constexpr SinglePassStereoState& setIndependentViewportMask(bool value) { independentViewportMask= value; return *this; }
        constexpr SinglePassStereoState& setRenderTargetIndexOffset(uint16_t value) { renderTargetIndexOffset = value; return *this; }
    };

    struct RenderState
    {
        BlendState blendState;
        DepthStencilState depthStencilState;
        RasterState rasterState;
        SinglePassStereoState singlePassStereo;

        constexpr RenderState& setBlendState(const BlendState& value) { blendState = value; return *this; }
        constexpr RenderState& setDepthStencilState(const DepthStencilState& value) { depthStencilState = value; return *this; }
        constexpr RenderState& setRasterState(const RasterState& value) { rasterState = value; return *this; }
        constexpr RenderState& setSinglePassStereoState(const SinglePassStereoState& value) { singlePassStereo = value; return *this; }
    };

    enum class VariableShadingRate : uint8_t
    {
        e1x1,
        e1x2,
        e2x1,
        e2x2,
        e2x4,
        e4x2,
        e4x4
    };

    enum class ShadingRateCombiner : uint8_t
    {
        Passthrough,
        Override,
        Min,
        Max,
        ApplyRelative
    };

    struct VariableRateShadingState
    {
        bool enabled = false;
        VariableShadingRate shadingRate = VariableShadingRate::e1x1;
        ShadingRateCombiner pipelinePrimitiveCombiner = ShadingRateCombiner::Passthrough;
        ShadingRateCombiner imageCombiner = ShadingRateCombiner::Passthrough;

        bool operator ==(const VariableRateShadingState& b) const {
            return enabled == b.enabled
                && shadingRate == b.shadingRate
                && pipelinePrimitiveCombiner == b.pipelinePrimitiveCombiner
                && imageCombiner == b.imageCombiner;
        }

        bool operator !=(const VariableRateShadingState& b) const { return !(*this == b); }

        constexpr VariableRateShadingState& setEnabled(bool value) { enabled = value; return *this; }
        constexpr VariableRateShadingState& setShadingRate(VariableShadingRate value) { shadingRate = value; return *this; }
        constexpr VariableRateShadingState& setPipelinePrimitiveCombiner(ShadingRateCombiner value) { pipelinePrimitiveCombiner = value; return *this; }
        constexpr VariableRateShadingState& setImageCombiner(ShadingRateCombiner value) { imageCombiner = value; return *this; }
    };

    typedef static_vector<BindingLayoutHandle, c_MaxBindingLayouts> BindingLayoutVector;
    
    struct GraphicsPipelineDesc
    {
        PrimitiveType primType = PrimitiveType::TriangleList;
        uint32_t patchControlPoints = 0;
        InputLayoutHandle inputLayout;

        ShaderHandle VS;
        ShaderHandle HS;
        ShaderHandle DS;
        ShaderHandle GS;
        ShaderHandle PS;

        RenderState renderState;
        VariableRateShadingState shadingRateState;

        BindingLayoutVector bindingLayouts;
        
        GraphicsPipelineDesc& setPrimType(PrimitiveType value) { primType = value; return *this; }
        GraphicsPipelineDesc& setPatchControlPoints(uint32_t value) { patchControlPoints = value; return *this; }
        GraphicsPipelineDesc& setInputLayout(IInputLayout* value) { inputLayout = value; return *this; }
        GraphicsPipelineDesc& setVertexShader(IShader* value) { VS = value; return *this; }
        GraphicsPipelineDesc& setHullShader(IShader* value) { HS = value; return *this; }
        GraphicsPipelineDesc& setTessellationControlShader(IShader* value) { HS = value; return *this; }
        GraphicsPipelineDesc& setDomainShader(IShader* value) { DS = value; return *this; }
        GraphicsPipelineDesc& setTessellationEvaluationShader(IShader* value) { DS = value; return *this; }
        GraphicsPipelineDesc& setGeometryShader(IShader* value) { GS = value; return *this; }
        GraphicsPipelineDesc& setPixelShader(IShader* value) { PS = value; return *this; }
        GraphicsPipelineDesc& setFragmentShader(IShader* value) { PS = value; return *this; }
        GraphicsPipelineDesc& setRenderState(const RenderState& value) { renderState = value; return *this; }
        GraphicsPipelineDesc& setVariableRateShadingState(const VariableRateShadingState& value) { shadingRateState = value; return *this; }
        GraphicsPipelineDesc& addBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IGraphicsPipeline : public IResource 
    {
    public:
        [[nodiscard]] virtual const GraphicsPipelineDesc& getDesc() const = 0;
        [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo() const = 0;
    };

    typedef RefCountPtr<IGraphicsPipeline> GraphicsPipelineHandle;

    struct ComputePipelineDesc
    {
        ShaderHandle CS;

        BindingLayoutVector bindingLayouts;

        ComputePipelineDesc& setComputeShader(IShader* value) { CS = value; return *this; }
        ComputePipelineDesc& addBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IComputePipeline : public IResource 
    {
    public:
        [[nodiscard]] virtual const ComputePipelineDesc& getDesc() const = 0;
    };

    typedef RefCountPtr<IComputePipeline> ComputePipelineHandle;

    struct MeshletPipelineDesc
    {
        PrimitiveType primType = PrimitiveType::TriangleList;
        
        ShaderHandle AS;
        ShaderHandle MS;
        ShaderHandle PS;

        RenderState renderState;

        BindingLayoutVector bindingLayouts;
        
        MeshletPipelineDesc& setPrimType(PrimitiveType value) { primType = value; return *this; }
        MeshletPipelineDesc& setTaskShader(IShader* value) { AS = value; return *this; }
        MeshletPipelineDesc& setAmplificationShader(IShader* value) { AS = value; return *this; }
        MeshletPipelineDesc& setMeshShader(IShader* value) { MS = value; return *this; }
        MeshletPipelineDesc& setPixelShader(IShader* value) { PS = value; return *this; }
        MeshletPipelineDesc& setFragmentShader(IShader* value) { PS = value; return *this; }
        MeshletPipelineDesc& setRenderState(const RenderState& value) { renderState = value; return *this; }
        MeshletPipelineDesc& addBindingLayout(IBindingLayout* layout) { bindingLayouts.push_back(layout); return *this; }
    };

    class IMeshletPipeline : public IResource
    {
    public:
        [[nodiscard]] virtual const MeshletPipelineDesc& getDesc() const = 0;
        [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo() const = 0;
    };

    typedef RefCountPtr<IMeshletPipeline> MeshletPipelineHandle;

    //////////////////////////////////////////////////////////////////////////
    // Draw and Dispatch
    //////////////////////////////////////////////////////////////////////////

    class IEventQuery : public IResource { };
    typedef RefCountPtr<IEventQuery> EventQueryHandle;

    class ITimerQuery : public IResource { };
    typedef RefCountPtr<ITimerQuery> TimerQueryHandle;

    struct VertexBufferBinding
    {
        IBuffer* buffer = nullptr;
        uint32_t slot;
        uint64_t offset;

        bool operator ==(const VertexBufferBinding& b) const
        {
            return buffer == b.buffer
                && slot == b.slot
                && offset == b.offset;
        }
        bool operator !=(const VertexBufferBinding& b) const { return !(*this == b); }

        VertexBufferBinding& setBuffer(IBuffer* value) { buffer = value; return *this; }
        VertexBufferBinding& setSlot(uint32_t value) { slot = value; return *this; }
        VertexBufferBinding& setOffset(uint64_t value) { offset = value; return *this; }
    };

    struct IndexBufferBinding
    {
        IBuffer* buffer = nullptr;
        Format format;
        uint32_t offset;

        bool operator ==(const IndexBufferBinding& b) const
        {
            return buffer == b.buffer
                && format == b.format
                && offset == b.offset;
        }
        bool operator !=(const IndexBufferBinding& b) const { return !(*this == b); }

        IndexBufferBinding& setBuffer(IBuffer* value) { buffer = value; return *this; }
        IndexBufferBinding& setFormat(Format value) { format = value; return *this; }
        IndexBufferBinding& setOffset(uint32_t value) { offset = value; return *this; }
    };

    typedef static_vector<IBindingSet*, c_MaxBindingLayouts> BindingSetVector;


    struct GraphicsState
    {
        IGraphicsPipeline* pipeline = nullptr;
        IFramebuffer* framebuffer = nullptr;
        ViewportState viewport;
        Color blendConstantColor{};
        VariableRateShadingState shadingRateState;

        BindingSetVector bindings;

        static_vector<VertexBufferBinding, c_MaxVertexAttributes> vertexBuffers;
        IndexBufferBinding indexBuffer;

        IBuffer* indirectParams = nullptr;

        GraphicsState& setPipeline(IGraphicsPipeline* value) { pipeline = value; return *this; }
        GraphicsState& setFramebuffer(IFramebuffer* value) { framebuffer = value; return *this; }
        GraphicsState& setViewport(const ViewportState& value) { viewport = value; return *this; }
        GraphicsState& setBlendColor(const Color& value) { blendConstantColor = value; return *this; }
        GraphicsState& addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
        GraphicsState& addVertexBuffer(const VertexBufferBinding& value) { vertexBuffers.push_back(value); return *this; }
        GraphicsState& setIndexBuffer(const IndexBufferBinding& value) { indexBuffer = value; return *this; }
        GraphicsState& setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }
    };

    struct DrawArguments
    {
        uint32_t vertexCount = 0;
        uint32_t instanceCount = 1;
        uint32_t startIndexLocation = 0;
        uint32_t startVertexLocation = 0;
        uint32_t startInstanceLocation = 0;

        constexpr DrawArguments& setVertexCount(uint32_t value) { vertexCount = value; return *this; }
        constexpr DrawArguments& setInstanceCount(uint32_t value) { instanceCount = value; return *this; }
        constexpr DrawArguments& setStartIndexLocation(uint32_t value) { startIndexLocation = value; return *this; }
        constexpr DrawArguments& setStartVertexLocation(uint32_t value) { startVertexLocation = value; return *this; }
        constexpr DrawArguments& setStartInstanceLocation(uint32_t value) { startInstanceLocation = value; return *this; }
    };

    struct ComputeState
    {
        IComputePipeline* pipeline = nullptr;

        BindingSetVector bindings;

        IBuffer* indirectParams = nullptr;

        ComputeState& setPipeline(IComputePipeline* value) { pipeline = value; return *this; }
        ComputeState& addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
        ComputeState& setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }
    };

    struct MeshletState
    {
        IMeshletPipeline* pipeline = nullptr;
        IFramebuffer* framebuffer = nullptr;
        ViewportState viewport;
        Color blendConstantColor{};

        BindingSetVector bindings;

        IBuffer* indirectParams = nullptr;

        MeshletState& setPipeline(IMeshletPipeline* value) { pipeline = value; return *this; }
        MeshletState& setFramebuffer(IFramebuffer* value) { framebuffer = value; return *this; }
        MeshletState& setViewport(const ViewportState& value) { viewport = value; return *this; }
        MeshletState& setBlendColor(const Color& value) { blendConstantColor = value; return *this; }
        MeshletState& addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
        MeshletState& setIndirectParams(IBuffer* value) { indirectParams = value; return *this; }
    };

    //////////////////////////////////////////////////////////////////////////
    // Ray Tracing
    //////////////////////////////////////////////////////////////////////////

    namespace rt
    {
        struct PipelineShaderDesc
        {
            std::string exportName;
            ShaderHandle shader;
            BindingLayoutHandle bindingLayout;

            PipelineShaderDesc& setExportName(const std::string& value) { exportName = value; return *this; }
            PipelineShaderDesc& setShader(IShader* value) { shader = value; return *this; }
            PipelineShaderDesc& setBindingLayout(IBindingLayout* value) { bindingLayout = value; return *this; }
        };

        struct PipelineHitGroupDesc
        {
            std::string exportName;
            ShaderHandle closestHitShader;
            ShaderHandle anyHitShader;
            ShaderHandle intersectionShader;
            BindingLayoutHandle bindingLayout;
            bool isProceduralPrimitive = false;

            PipelineHitGroupDesc& setExportName(const std::string& value) { exportName = value; return *this; }
            PipelineHitGroupDesc& setClosestHitShader(IShader* value) { closestHitShader = value; return *this; }
            PipelineHitGroupDesc& setAnyHitShader(IShader* value) { anyHitShader = value; return *this; }
            PipelineHitGroupDesc& setIntersectionShader(IShader* value) { intersectionShader = value; return *this; }
            PipelineHitGroupDesc& setBindingLayout(IBindingLayout* value) { bindingLayout = value; return *this; }
            PipelineHitGroupDesc& setIsProceduralPrimitive(bool value) { isProceduralPrimitive = value; return *this; }
        };

        struct PipelineDesc
        {
            std::vector<PipelineShaderDesc> shaders;
            std::vector<PipelineHitGroupDesc> hitGroups;
            BindingLayoutVector globalBindingLayouts;
            uint32_t maxPayloadSize = 0;
            uint32_t maxAttributeSize = sizeof(float) * 2; // typical case: float2 uv;
            uint32_t maxRecursionDepth = 1;

            PipelineDesc& addShader(const PipelineShaderDesc& value) { shaders.push_back(value); return *this; }
            PipelineDesc& addHitGroup(const PipelineHitGroupDesc& value) { hitGroups.push_back(value); return *this; }
            PipelineDesc& addBindingLayout(IBindingLayout* value) { globalBindingLayouts.push_back(value); return *this; }
            PipelineDesc& setMaxPayloadSize(uint32_t value) { maxPayloadSize = value; return *this; }
            PipelineDesc& setMaxAttributeSize(uint32_t value) { maxAttributeSize = value; return *this; }
            PipelineDesc& setMaxRecursionDepth(uint32_t value) { maxRecursionDepth = value; return *this; }
        };

        class IPipeline;

        class IShaderTable : public IResource
        {
        public:
            virtual void setRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int addMissShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int addHitGroup(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int addCallableShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual void clearMissShaders() = 0;
            virtual void clearHitShaders() = 0;
            virtual void clearCallableShaders() = 0;
            virtual IPipeline* getPipeline() = 0;
        };

        typedef RefCountPtr<IShaderTable> ShaderTableHandle;

        class IPipeline : public IResource
        {
        public:
            [[nodiscard]] virtual const rt::PipelineDesc& getDesc() const = 0;
            virtual ShaderTableHandle createShaderTable() = 0;
        };

        typedef RefCountPtr<IPipeline> PipelineHandle;

        struct State
        {
            IShaderTable* shaderTable = nullptr;

            BindingSetVector bindings;

            State& setShaderTable(IShaderTable* value) { shaderTable = value; return *this; }
            State& addBindingSet(IBindingSet* value) { bindings.push_back(value); return *this; }
        };

        struct DispatchRaysArguments
        {
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t depth = 1;

            constexpr DispatchRaysArguments& setWidth(uint32_t value) { width = value; return *this; }
            constexpr DispatchRaysArguments& setHeight(uint32_t value) { height = value; return *this; }
            constexpr DispatchRaysArguments& setDepth(uint32_t value) { depth = value; return *this; }
            constexpr DispatchRaysArguments& setDimensions(uint32_t w, uint32_t h = 1, uint32_t d = 1) { width = w; height = h; depth = d; return *this; }
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Misc
    //////////////////////////////////////////////////////////////////////////

    enum class Feature : uint8_t
    {
        DeferredCommandLists,
        SinglePassStereo,
        RayTracingAccelStruct,
        RayTracingPipeline,
        RayQuery,
        FastGeometryShader,
        Meshlets,
        VariableRateShading,
        ShaderSpecializations,
        VirtualResources,
        ComputeQueue,
        CopyQueue
    };

    enum class MessageSeverity : uint8_t
    {
        Info,
        Warning,
        Error,
        Fatal
    };

    enum class CommandQueue : uint8_t
    {
        Graphics = 0,
        Compute,
        Copy,

        Count
    };

    struct VariableRateShadingFeatureInfo
    {
        uint32_t shadingRateImageTileSize;
    };

    // IMessageCallback should be implemented by the application.
    class IMessageCallback
    {
    protected:
        IMessageCallback() = default;
        virtual ~IMessageCallback() = default;

    public:
        // NVRHI will call message(...) whenever it needs to signal something.
        // The application is free to ignore the messages, show message boxes, or terminate.
        virtual void message(MessageSeverity severity, const char* messageText) = 0;

        IMessageCallback(const IMessageCallback&) = delete;
        IMessageCallback(const IMessageCallback&&) = delete;
        IMessageCallback& operator=(const IMessageCallback&) = delete;
        IMessageCallback& operator=(const IMessageCallback&&) = delete;
    };
    
    class IDevice;

    struct CommandListParameters
    {
        // A command list with enableImmediateExecution = true maps to the immediate context on DX11.
        // Two immediate command lists cannot be open at the same time, which is checked by the validation layer.
        bool enableImmediateExecution = true;

        // Minimum size of memory chunks created to upload data to the device on DX12.
        size_t uploadChunkSize = 64 * 1024;

        // Minimum size of memory chunks created for AS build scratch buffers.
        size_t scratchChunkSize = 64 * 1024;

        // Maximum total memory size used for all AS build scratch buffers owned by this command list.
        size_t scratchMaxMemory = 1024 * 1024 * 1024;

        // Type of the queue that this command list is to be executed on.
        // COPY and COMPUTE queues have limited subsets of methods available.
        CommandQueue queueType = CommandQueue::Graphics;

        CommandListParameters& setEnableImmediateExecution(bool value) { enableImmediateExecution = value; return *this; }
        CommandListParameters& setUploadChunkSize(size_t value) { uploadChunkSize = value; return *this; }
        CommandListParameters& setScratchChunkSize(size_t value) { scratchChunkSize = value; return *this; }
        CommandListParameters& setScratchMaxMemory(size_t value) { scratchMaxMemory = value; return *this; }
        CommandListParameters& setQueueType(CommandQueue value) { queueType = value; return *this; }
    };
    
    //////////////////////////////////////////////////////////////////////////
    // ICommandList
    //////////////////////////////////////////////////////////////////////////

    class ICommandList : public IResource
    {
    public:
        virtual void open() = 0;
        virtual void close() = 0;

        // Clears the graphics state of the underlying command list object and resets the state cache.
        virtual void clearState() = 0;

        virtual void clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor) = 0;
        virtual void clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) = 0;
        virtual void clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, uint32_t clearColor) = 0;

        virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;
        virtual void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;
        virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice) = 0;
        virtual void writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch = 0) = 0;
        virtual void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) = 0;

        virtual void writeBuffer(IBuffer* b, const void* data, size_t dataSize, uint64_t destOffsetBytes = 0) = 0;
        virtual void clearBufferUInt(IBuffer* b, uint32_t clearValue) = 0;
        virtual void copyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes) = 0;

        // Sets the push constants block on the command list, aka "root constants" on DX12.
        // Only valid after setGraphicsState or setComputeState etc.
        virtual void setPushConstants(const void* data, size_t byteSize) = 0;

        virtual void setGraphicsState(const GraphicsState& state) = 0;
        virtual void draw(const DrawArguments& args) = 0;
        virtual void drawIndexed(const DrawArguments& args) = 0;
        virtual void drawIndirect(uint32_t offsetBytes) = 0;
        
        virtual void setComputeState(const ComputeState& state) = 0;
        virtual void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
        virtual void dispatchIndirect(uint32_t offsetBytes) = 0;

        virtual void setMeshletState(const MeshletState& state) = 0;
        virtual void dispatchMesh(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;

        virtual void setRayTracingState(const rt::State& state) = 0;
        virtual void dispatchRays(const rt::DispatchRaysArguments& args) = 0;
        
        virtual void buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::GeometryDesc* pGeometries, size_t numGeometries,
            rt::AccelStructBuildFlags buildFlags = rt::AccelStructBuildFlags::None) = 0;
        virtual void compactBottomLevelAccelStructs() = 0;
        virtual void buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::InstanceDesc* pInstances, size_t numInstances,
            rt::AccelStructBuildFlags buildFlags = rt::AccelStructBuildFlags::None) = 0;

        // A version of buildTopLevelAccelStruct that takes the instance data from a buffer on the GPU.
        // The buffer must be pre-filled with rt::InstanceDesc structures using a copy operation or a shader.
        // No validation on the buffer contents is performed by NVRHI, and no state or liveness tracking for the referenced BLAS'es.
        virtual void buildTopLevelAccelStructFromBuffer(rt::IAccelStruct* as, nvrhi::IBuffer* instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances,
            rt::AccelStructBuildFlags buildFlags = rt::AccelStructBuildFlags::None) = 0;

        virtual void beginTimerQuery(ITimerQuery* query) = 0;
        virtual void endTimerQuery(ITimerQuery* query) = 0;

        // Command list range markers
        virtual void beginMarker(const char *name) = 0;
        virtual void endMarker() = 0;

        // Enables or disables the automatic barrier placement on set[...]State, copy, write, and clear operations.
        // By default, automatic barriers are enabled, but can be optionally disabled to improve CPU performance and/or specific barrier placement.
        // When automatic barriers are disabled, it is application's responsibility to set correct states for all used resources.
        virtual void setEnableAutomaticBarriers(bool enable) = 0;

        // Sets the necessary resource states for all non-permanent resources used in the binding set.
        virtual void setResourceStatesForBindingSet(IBindingSet* bindingSet) = 0;
        
        // Sets the necessary resource states for all targets of the framebuffer.
        NVRHI_API void setResourceStatesForFramebuffer(IFramebuffer* framebuffer);

        // Tells the D3D12/VK backend whether UAV barriers should be used for the given texture or buffer between draw calls.
        // A barrier should still be placed before the first draw call in the group and after the last one.
        virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) = 0;
        virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) = 0;

        // Informs the command list of the state of a texture subresource or buffer prior to command list execution
        virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits) = 0;
        virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates stateBits) = 0;

        // Resource state transitions - these put barriers into the pending list. Call commitBarriers() after.
        virtual void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits) = 0;
        virtual void setBufferState(IBuffer* buffer, ResourceStates stateBits) = 0;
        virtual void setAccelStructState(rt::IAccelStruct* as, ResourceStates stateBits) = 0;

        // Permanent resource state transitions - these make resource usage cheaper by excluding it from state tracking in the future.
        // Like setTexture/BufferState, these methods put barriers into the pending list. Call commitBarriers() after.
        virtual void setPermanentTextureState(ITexture* texture, ResourceStates stateBits) = 0;
        virtual void setPermanentBufferState(IBuffer* buffer, ResourceStates stateBits) = 0;

        // Flushes the barriers from the pending list into the GAPI command list.
        virtual void commitBarriers() = 0;

        // Returns the current tracked state of a texture subresource or a buffer.
        virtual ResourceStates getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) = 0;
        virtual ResourceStates getBufferState(IBuffer* buffer) = 0;

        // Returns the owning device, does NOT call AddRef on it
        virtual IDevice* getDevice() = 0;
        virtual const CommandListParameters& getDesc() = 0;
    };

    typedef RefCountPtr<ICommandList> CommandListHandle;

    //////////////////////////////////////////////////////////////////////////
    // IDevice
    //////////////////////////////////////////////////////////////////////////

    class IDevice : public IResource
    {
    public:
        virtual HeapHandle createHeap(const HeapDesc& d) = 0;

        virtual TextureHandle createTexture(const TextureDesc& d) = 0;
        virtual MemoryRequirements getTextureMemoryRequirements(ITexture* texture) = 0;
        virtual bool bindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset) = 0;

        virtual TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) = 0;

        virtual StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess) = 0;
        virtual void *mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch) = 0;
        virtual void unmapStagingTexture(IStagingTexture* tex) = 0;

        virtual BufferHandle createBuffer(const BufferDesc& d) = 0;
        virtual void *mapBuffer(IBuffer* buffer, CpuAccessMode cpuAccess) = 0;
        virtual void unmapBuffer(IBuffer* buffer) = 0;
        virtual MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer) = 0;
        virtual bool bindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset) = 0;

        virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) = 0;

        virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, size_t binarySize) = 0;
        virtual ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, uint32_t numConstants) = 0;
        virtual ShaderLibraryHandle createShaderLibrary(const void* binary, size_t binarySize) = 0;
        
        virtual SamplerHandle createSampler(const SamplerDesc& d) = 0;

        // Note: vertexShader is only necessary on D3D11, otherwise it may be null
        virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) = 0;
        
        // Event queries
        virtual EventQueryHandle createEventQuery() = 0;
        virtual void setEventQuery(IEventQuery* query, CommandQueue queue) = 0;
        virtual bool pollEventQuery(IEventQuery* query) = 0;
        virtual void waitEventQuery(IEventQuery* query) = 0;
        virtual void resetEventQuery(IEventQuery* query) = 0;

        // Timer queries - see also begin/endTimerQuery in ICommandList
        virtual TimerQueryHandle createTimerQuery() = 0;
        virtual bool pollTimerQuery(ITimerQuery* query) = 0;
        // returns time in seconds
        virtual float getTimerQueryTime(ITimerQuery* query) = 0;
        virtual void resetTimerQuery(ITimerQuery* query) = 0;

        // Returns the API kind that the RHI backend is running on top of.
        virtual GraphicsAPI getGraphicsAPI() = 0;
        
        virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) = 0;
        
        virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb) = 0;
        
        virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;

        virtual MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, IFramebuffer* fb) = 0;

        virtual rt::PipelineHandle createRayTracingPipeline(const rt::PipelineDesc& desc) = 0;
        
        virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) = 0;
        virtual BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc) = 0;

        virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) = 0;
        virtual DescriptorTableHandle createDescriptorTable(IBindingLayout* layout) = 0;

        virtual void resizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents = true) = 0;
        virtual bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item) = 0;

        virtual rt::AccelStructHandle createAccelStruct(const rt::AccelStructDesc& desc) = 0;
        virtual MemoryRequirements getAccelStructMemoryRequirements(rt::IAccelStruct* as) = 0;
        virtual bool bindAccelStructMemory(rt::IAccelStruct* as, IHeap* heap, uint64_t offset) = 0;
        
        virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) = 0;
        virtual uint64_t executeCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, CommandQueue executionQueue = CommandQueue::Graphics) = 0;
        virtual void queueWaitForCommandList(CommandQueue waitQueue, CommandQueue executionQueue, uint64_t instance) = 0;
        virtual void waitForIdle() = 0;

        // Releases the resources that were referenced in the command lists that have finished executing.
        // IMPORTANT: Call this method at least once per frame.
        virtual void runGarbageCollection() = 0;

        virtual bool queryFeatureSupport(Feature feature, void* pInfo = nullptr, size_t infoSize = 0) = 0;

        virtual FormatSupport queryFormatSupport(Format format) = 0;

        virtual Object getNativeQueue(ObjectType objectType, CommandQueue queue) = 0;

        virtual IMessageCallback* getMessageCallback() = 0;

        // Front-end for executeCommandLists(..., 1) for compatibility and convenience
        uint64_t executeCommandList(ICommandList* commandList, CommandQueue executionQueue = CommandQueue::Graphics)
        {
            return executeCommandLists(&commandList, 1, executionQueue);
        }
    };

    typedef RefCountPtr<IDevice> DeviceHandle;

    template <class T>
    void hash_combine(size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
}

#undef NVRHI_ENUM_CLASS_FLAG_OPERATORS

namespace std
{
    template<typename T> struct hash<nvrhi::RefCountPtr<T>>
    {
        std::size_t operator()(nvrhi::RefCountPtr<T> const& s) const noexcept
        {
            std::hash<T*> hash;
            return hash(s.Get());
        }
    };

    template<> struct hash<nvrhi::TextureSubresourceSet>
    {
        std::size_t operator()(nvrhi::TextureSubresourceSet const& s) const noexcept
        {
            size_t hash = 0;
            nvrhi::hash_combine(hash, s.baseMipLevel);
            nvrhi::hash_combine(hash, s.numMipLevels);
            nvrhi::hash_combine(hash, s.baseArraySlice);
            nvrhi::hash_combine(hash, s.numArraySlices);
            return hash;
        }
    };

    template<> struct hash<nvrhi::BufferRange>
    {
        std::size_t operator()(nvrhi::BufferRange const& s) const noexcept
        {
            size_t hash = 0;
            nvrhi::hash_combine(hash, s.byteOffset);
            nvrhi::hash_combine(hash, s.byteSize);
            return hash;
        }
    };

    template<> struct hash<nvrhi::BindingSetItem>
    {
        std::size_t operator()(nvrhi::BindingSetItem const& s) const noexcept
        {
            size_t value = 0;
            nvrhi::hash_combine(value, s.resourceHandle);
            nvrhi::hash_combine(value, s.slot);
            nvrhi::hash_combine(value, s.type);
            nvrhi::hash_combine(value, s.dimension);
            nvrhi::hash_combine(value, s.format);
            nvrhi::hash_combine(value, s.rawData[0]);
            nvrhi::hash_combine(value, s.rawData[1]);
            return value;
        }
    };

    template<> struct hash<nvrhi::BindingSetDesc>
    {
        std::size_t operator()(nvrhi::BindingSetDesc const& s) const noexcept
        {
            size_t value = 0;
            for (const auto& item : s.bindings)
                hash_combine(value, item);
            return value;
        }
    };

    template<> struct hash<nvrhi::FramebufferInfo>
    {
        std::size_t operator()(nvrhi::FramebufferInfo const& s) const noexcept
        {
            size_t hash = 0;
            for (auto format : s.colorFormats)
                nvrhi::hash_combine(hash, format);
            nvrhi::hash_combine(hash, s.depthFormat);
            nvrhi::hash_combine(hash, s.width);
            nvrhi::hash_combine(hash, s.height);
            nvrhi::hash_combine(hash, s.sampleCount);
            nvrhi::hash_combine(hash, s.sampleQuality);
            return hash;
        }
    };

    template<> struct hash<nvrhi::BlendState::RenderTarget>
    {
        std::size_t operator()(nvrhi::BlendState::RenderTarget const& s) const noexcept
        {
            size_t hash = 0;
            nvrhi::hash_combine(hash, s.blendEnable);
            nvrhi::hash_combine(hash, s.srcBlend);
            nvrhi::hash_combine(hash, s.destBlend);
            nvrhi::hash_combine(hash, s.blendOp);
            nvrhi::hash_combine(hash, s.srcBlendAlpha);
            nvrhi::hash_combine(hash, s.destBlendAlpha);
            nvrhi::hash_combine(hash, s.blendOpAlpha);
            nvrhi::hash_combine(hash, s.colorWriteMask);
            return hash;
        }
    };

    template<> struct hash<nvrhi::BlendState>
    {
        std::size_t operator()(nvrhi::BlendState const& s) const noexcept
        {
            size_t hash = 0;
            nvrhi::hash_combine(hash, s.alphaToCoverageEnable);
            for (const auto& target : s.targets)
                nvrhi::hash_combine(hash, target);
            return hash;
        }
    };
}
