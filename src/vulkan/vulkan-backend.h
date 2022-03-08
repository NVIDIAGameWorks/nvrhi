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

#include <nvrhi/vulkan.h>
#include <nvrhi/utils.h>
#include "../common/state-tracking.h"
#include "../common/versioning.h"
#include <mutex>
#include <list>

#ifdef NVRHI_WITH_RTXMU
#include <rtxmu/VkAccelStructManager.h>
#endif

#if (VK_HEADER_VERSION < 162)
#error "Vulkan SDK version 1.2.162 or later is required to compile NVRHI"
#endif

namespace std
{
    template<> struct hash<std::pair<vk::PipelineStageFlags, vk::PipelineStageFlags>>
    {
        std::size_t operator()(std::pair<vk::PipelineStageFlags, vk::PipelineStageFlags> const& s) const noexcept
        {
            return (std::hash<uint32_t>()(uint32_t(s.first))
                ^ (std::hash<uint32_t>()(uint32_t(s.second)) << 16));
        }
    };
}

#define CHECK_VK_RETURN(res) if ((res) != vk::Result::eSuccess) { return res; }
#define CHECK_VK_FAIL(res) if ((res) != vk::Result::eSuccess) { return nullptr; }
#if _DEBUG
#define ASSERT_VK_OK(res) assert((res) == vk::Result::eSuccess)
#else // _DEBUG
#define ASSERT_VK_OK(res) do {(void)(res);} while(0)
#endif // _DEBUG

namespace nvrhi::vulkan
{
    class Texture;
    class StagingTexture;
    class InputLayout;
    class Buffer;
    class Shader;
    class Sampler;
    class Framebuffer;
    class GraphicsPipeline;
    class ComputePipeline;
    class BindingSet;
    class EvenetQuery;
    class TimerQuery;
    class Marker;
    class Device;

    struct ResourceStateMapping
    {
        ResourceStates nvrhiState;
        vk::PipelineStageFlags stageFlags;
        vk::AccessFlags accessMask;
        vk::ImageLayout imageLayout;
    };

    vk::SamplerAddressMode convertSamplerAddressMode(SamplerAddressMode mode);
    vk::PipelineStageFlagBits convertShaderTypeToPipelineStageFlagBits(ShaderType shaderType);
    vk::ShaderStageFlagBits convertShaderTypeToShaderStageFlagBits(ShaderType shaderType);
    ResourceStateMapping convertResourceState(ResourceStates state);
    vk::PrimitiveTopology convertPrimitiveTopology(PrimitiveType topology);
    vk::PolygonMode convertFillMode(RasterFillMode mode);
    vk::CullModeFlagBits convertCullMode(RasterCullMode mode);
    vk::CompareOp convertCompareOp(ComparisonFunc op);
    vk::StencilOp convertStencilOp(StencilOp op);
    vk::StencilOpState convertStencilState(const DepthStencilState& depthStencilState, const DepthStencilState::StencilOpDesc& desc);
    vk::BlendFactor convertBlendValue(BlendFactor value);
    vk::BlendOp convertBlendOp(BlendOp op);
    vk::ColorComponentFlags convertColorMask(ColorMask mask);
    vk::PipelineColorBlendAttachmentState convertBlendState(const BlendState::RenderTarget& state);
    vk::BuildAccelerationStructureFlagsKHR convertAccelStructBuildFlags(rt::AccelStructBuildFlags buildFlags);
    vk::GeometryInstanceFlagsKHR convertInstanceFlags(rt::InstanceFlags instanceFlags);
    vk::Extent2D convertFragmentShadingRate(VariableShadingRate shadingRate);
    vk::FragmentShadingRateCombinerOpKHR convertShadingRateCombiner(ShadingRateCombiner combiner);

    void countSpecializationConstants(
        Shader* shader,
        size_t& numShaders,
        size_t& numShadersWithSpecializations,
        size_t& numSpecializationConstants);

    vk::PipelineShaderStageCreateInfo makeShaderStageCreateInfo(
        Shader* shader,
        std::vector<vk::SpecializationInfo>& specInfos,
        std::vector<vk::SpecializationMapEntry>& specMapEntries,
        std::vector<uint32_t>& specData);

#ifdef NVRHI_WITH_RTXMU
    struct RtxMuResources
    {
        std::vector<uint64_t> asBuildsCompleted;
        std::mutex asListMutex;
    };
#endif

    // underlying vulkan context
    struct VulkanContext
    {
        VulkanContext(vk::Instance instance,
                      vk::PhysicalDevice physicalDevice,
                      vk::Device device,
                      vk::AllocationCallbacks *allocationCallbacks = nullptr)
            : instance(instance)
            , physicalDevice(physicalDevice)
            , device(device)
            , allocationCallbacks(allocationCallbacks)
            , pipelineCache(nullptr)
        { }

        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;
        vk::AllocationCallbacks *allocationCallbacks;
        vk::PipelineCache pipelineCache;

        struct {
            bool KHR_maintenance1 = false;
            bool EXT_debug_report = false;
            bool EXT_debug_marker = false;
            bool KHR_acceleration_structure = false;
            bool KHR_buffer_device_address = false;
            bool KHR_ray_query = false;
            bool KHR_ray_tracing_pipeline = false;
            bool NV_mesh_shader = false;
            bool KHR_fragment_shading_rate = false;
        } extensions;

        vk::PhysicalDeviceProperties physicalDeviceProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties;
        vk::PhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties;
        vk::PhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures;
        IMessageCallback* messageCallback = nullptr;
#ifdef NVRHI_WITH_RTXMU
        std::unique_ptr<rtxmu::VkAccelStructManager> rtxMemUtil;
        std::unique_ptr<RtxMuResources> rtxMuResources;
#endif

        void nameVKObject(const void* handle, vk::DebugReportObjectTypeEXT objtype, const char* name) const;
        void error(const std::string& message) const;
    };

    // command buffer with resource tracking
    class TrackedCommandBuffer
    {
    public:

        // the command buffer itself
        vk::CommandBuffer cmdBuf = vk::CommandBuffer();
        vk::CommandPool cmdPool = vk::CommandPool();

        std::vector<RefCountPtr<IResource>> referencedResources; // to keep them alive

        uint64_t recordingID = 0;
        uint64_t submissionID = 0;

#ifdef NVRHI_WITH_RTXMU
        std::vector<uint64_t> rtxmuBuildIds;
        std::vector<uint64_t> rtxmuCompactionIds;
#endif

        explicit TrackedCommandBuffer(const VulkanContext& context)
            : m_Context(context)
        { }

        ~TrackedCommandBuffer();
    
    private:
        const VulkanContext& m_Context;
    };

    typedef std::shared_ptr<TrackedCommandBuffer> TrackedCommandBufferPtr;

    // represents a hardware queue
    class Queue
    {
    public:
        vk::Semaphore trackingSemaphore;

        Queue(const VulkanContext& context, CommandQueue queueID, vk::Queue queue, uint32_t queueFamilyIndex);
        ~Queue();

        // creates a command buffer and its synchronization resources
        TrackedCommandBufferPtr createCommandBuffer();

        TrackedCommandBufferPtr getOrCreateCommandBuffer();

        void addWaitSemaphore(vk::Semaphore semaphore, uint64_t value);
        void addSignalSemaphore(vk::Semaphore semaphore, uint64_t value);

        // submits a command buffer to this queue, returns submissionID
        uint64_t submit(ICommandList* const* ppCmd, size_t numCmd);

        // retire any command buffers that have finished execution from the pending execution list
        void retireCommandBuffers();

        TrackedCommandBufferPtr getCommandBufferInFlight(uint64_t submissionID);

        uint64_t updateLastFinishedID();
        uint64_t getLastSubmittedID() const { return m_LastSubmittedID; }
        uint64_t getLastFinishedID() const { return m_LastFinishedID; }
        CommandQueue getQueueID() const { return m_QueueID; }
        vk::Queue getVkQueue() const { return m_Queue; }

    private:
        const VulkanContext& m_Context;

        vk::Queue m_Queue;
        CommandQueue m_QueueID;
        uint32_t m_QueueFamilyIndex = uint32_t(-1);

        std::mutex m_Mutex;
        std::vector<vk::Semaphore> m_WaitSemaphores;
        std::vector<uint64_t> m_WaitSemaphoreValues;
        std::vector<vk::Semaphore> m_SignalSemaphores;
        std::vector<uint64_t> m_SignalSemaphoreValues;

        uint64_t m_LastRecordingID = 0;
        uint64_t m_LastSubmittedID = 0;
        uint64_t m_LastFinishedID = 0;

        // tracks the list of command buffers in flight on this queue
        std::list<TrackedCommandBufferPtr> m_CommandBuffersInFlight;
        std::list<TrackedCommandBufferPtr> m_CommandBuffersPool;
    };

    class MemoryResource
    {
    public:
        bool managed = true;
        vk::DeviceMemory memory;
    };

    class VulkanAllocator
    {
    public:
        explicit VulkanAllocator(const VulkanContext& context)
            : m_Context(context)
        { }

        vk::Result allocateBufferMemory(Buffer* buffer, bool enableBufferAddress = false) const;
        void freeBufferMemory(Buffer* buffer) const;

        vk::Result allocateTextureMemory(Texture* texture) const;
        void freeTextureMemory(Texture* texture) const;

        vk::Result allocateMemory(MemoryResource* res,
            vk::MemoryRequirements memRequirements,
            vk::MemoryPropertyFlags memPropertyFlags,
            bool enableDeviceAddress = false) const;
        void freeMemory(MemoryResource* res) const;

    private:
        const VulkanContext& m_Context;
    };

    class Heap : public MemoryResource, public RefCounter<IHeap>
    {
    public:
        explicit Heap(const VulkanContext& context, VulkanAllocator& allocator)
            : m_Context(context)
            , m_Allocator(allocator)
        { }

        ~Heap() override;

        HeapDesc desc;
        
        const HeapDesc& getDesc() override { return desc; }

    private:
        const VulkanContext& m_Context;
        VulkanAllocator& m_Allocator;
    };

    struct TextureSubresourceView
    {
        Texture& texture;
        TextureSubresourceSet subresource;

        vk::ImageView view = nullptr;
        vk::ImageSubresourceRange subresourceRange;

        TextureSubresourceView(Texture& texture)
            : texture(texture)
        { }

        TextureSubresourceView(const TextureSubresourceView&) = delete;

        bool operator==(const TextureSubresourceView& other) const
        {
            return &texture == &other.texture &&
                    subresource == other.subresource &&
                    view == other.view &&
                    subresourceRange == other.subresourceRange;
        }
    };

    class Texture : public MemoryResource, public RefCounter<ITexture>, public TextureStateExtension
    {
    public:

        enum class TextureSubresourceViewType // see getSubresourceView()
        {
            AllAspects,
            DepthOnly,
            StencilOnly
        };

        struct Hash
        {
            std::size_t operator()(std::tuple<TextureSubresourceSet, TextureSubresourceViewType, TextureDimension> const& s) const noexcept
            {
                const auto& [subresources, viewType, dimension] = s;

                size_t hash = 0;

                hash_combine(hash, subresources.baseMipLevel);
                hash_combine(hash, subresources.numMipLevels);
                hash_combine(hash, subresources.baseArraySlice);
                hash_combine(hash, subresources.numArraySlices);
                hash_combine(hash, viewType);
                hash_combine(hash, dimension);

                return hash;
            }
        };

        
        TextureDesc desc;

        vk::ImageCreateInfo imageInfo;
        vk::Image image;

        HeapHandle heap;
        
        // contains subresource views for this texture
        // note that we only create the views that the app uses, and that multiple views may map to the same subresources
        std::unordered_map<std::tuple<TextureSubresourceSet, TextureSubresourceViewType, TextureDimension>, TextureSubresourceView, Texture::Hash> subresourceViews;

        Texture(const VulkanContext& context, VulkanAllocator& allocator)
            : TextureStateExtension(desc)
            , m_Context(context)
            , m_Allocator(allocator)
        { }

        // returns a subresource view for an arbitrary range of mip levels and array layers.
        // 'viewtype' only matters when asking for a depthstencil view; in situations where only depth or stencil can be bound
        // (such as an SRV with ImageLayout::eShaderReadOnlyOptimal), but not both, then this specifies which of the two aspect bits is to be set.
        TextureSubresourceView& getSubresourceView(const TextureSubresourceSet& subresources, TextureDimension dimension, TextureSubresourceViewType viewtype = TextureSubresourceViewType::AllAspects);
        
        uint32_t getNumSubresources() const;
        uint32_t getSubresourceIndex(uint32_t mipLevel, uint32_t arrayLayer) const;

        ~Texture() override;
        const TextureDesc& getDesc() const override { return desc; }
        Object getNativeObject(ObjectType objectType) override;
        Object getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, TextureDimension dimension, bool isReadOnlyDSV = false) override;

    private:
        const VulkanContext& m_Context;
        VulkanAllocator& m_Allocator;
        std::mutex m_Mutex;
    };

    /* ----------------------------------------------------------------------------

    The volatile buffer implementation needs some explanation, might as well be here.

    The implementation is designed around a few constraints and assumptions:

    1.  Need to efficiently represent them with core Vulkan API with minimal overhead.
        This rules out a few options:

        - Can't use regular descriptors and update the references to each volatile CB
          in every descriptor set. That would require versioning of the descriptor
          sets and tracking of every use of volatile CBs.
        - Can't use push descriptors (vkCmdPushDescriptorSetKHR) because they are not
          in core Vulkan and are not supported by e.g. AMD drivers at this time. This
          rules out the DX12 style approach where an upload manager is assigned to a
          command list and creates buffers as needed - because then one volatile CB
          might be using different buffer objects for different versions.
        - Any other options that I missed?...

        The only option left is dynamic descriptors. You create a UBO descriptor that
        points to a buffer and then bind it with different offsets within that buffer.
        So all the versions of a volatile CB must live in the same buffer because the
        descriptor may be baked into multiple descriptor sets.

    2.  A volatile buffer may be written into from different command lists, potentially
        those which are recorded concurrently or out of order, and then executed on
        different queues.

        This requirement makes it impossible to put different versions of a CB into a
        single buffer in a round-robin fashion and track their completion with chunks.
        Tracking must be more fine-grained.

    3.  The version tracking implementation should be efficient, which means we shouldn't
        do things like allocating tracking objects for each version or pooling them
        for reuse, and keep iterating over many buffers or versions to a minimum.

    The system designed with these characteristics in mind is following.

    Every volatile buffer has a fixed maximum number of versions specified at creation,
    see BufferDesc::maxVersions. For a typical once-per-frame render pass, something
    like 3-4 versions should be sufficient. Iterative passes may need more, or should
    avoid using volatile CBs in that fashion and switch to push constants or maybe
    structured buffers.

    For each version of a buffer, a tracking object is stored in the Buffer::versionTracking
    array. The object is just a 64-bit word, which contains a bitfield: 

        - c_VersionSubmittedFlag means that the version is used in a submitted 
            command list;

        - (queue & c_VersionQueueMask << c_VersionQueueShift) is the queue index, 
            see nvrhi::CommandQueue for values;

        - (id & c_VersionIDMask) is the instance ID of the command list, either 
            pending or submitted. If pending, it matches the recordingID field of 
            TrackedCommandBuffer, otherwise the submissionID.

    When a buffer version is allocated, it is transitioned into the pending state.
    When the command list containing such pending versions is submitted, all the
    pending versions are transitioned to the submitted state. In the submitted 
    state, they may be reused later if that submitted instance of the command list
    has finished executing, which is determined based on the queue's semaphore.
    Pending versions cannot be reused. Also, pending versions might be transitioned
    to the available state (tracking word == 0) if their command list is abandoned,
    but that is currently not implemented.

    See also:
        - CommandList::writeVolatileBuffer
        - CommandList::flushVolatileBufferWrites
        - CommandList::submitVolatileBuffers

    -----------------------------------------------------------------------------*/

    struct VolatileBufferState
    {
        int latestVersion = 0;
        int minVersion = 0;
        int maxVersion = 0;
        bool initialized = false;
    };
    
    // A copyable version of std::atomic to be used in an std::vector
    class BufferVersionItem : public std::atomic<uint64_t>  // NOLINT(cppcoreguidelines-special-member-functions)
    {
    public:
        BufferVersionItem()
            : std::atomic<uint64_t>()
        { }

        BufferVersionItem(const BufferVersionItem& other)
        {
            store(other);
        }

        BufferVersionItem& operator=(const uint64_t a)
        {
            store(a);
            return *this;
        }
    };

    class Buffer : public MemoryResource, public RefCounter<IBuffer>, public BufferStateExtension
    {
    public:
        BufferDesc desc;

        vk::Buffer buffer;
        vk::DeviceAddress deviceAddress = 0;

        HeapHandle heap;
        
        std::unordered_map<vk::Format, vk::BufferView> viewCache;

        std::vector<BufferVersionItem> versionTracking;
        void* mappedMemory = nullptr;
        uint32_t versionSearchStart = 0;

        Buffer(const VulkanContext& context, VulkanAllocator& allocator)
            : BufferStateExtension(desc)
            , m_Context(context)
            , m_Allocator(allocator)
        { }

        ~Buffer() override;
        const BufferDesc& getDesc() const override { return desc; }
        Object getNativeObject(ObjectType type) override;

    private:
        const VulkanContext& m_Context;
        VulkanAllocator& m_Allocator;
    };
    
    struct StagingTextureRegion
    {
        // offset, size in bytes
        off_t offset;
        size_t size;
    };

    class StagingTexture : public RefCounter<IStagingTexture>
    {
    public:
        TextureDesc desc;
        // backing store for staging texture is a buffer
        RefCountPtr<Buffer> buffer;
        // per-mip, per-slice regions
        // offset = mipLevel * numDepthSlices + depthSlice
        std::vector<StagingTextureRegion> sliceRegions;

        size_t computeSliceSize(uint32_t mipLevel);
        const StagingTextureRegion& getSliceRegion(uint32_t mipLevel, uint32_t arraySlice, uint32_t z);
        void populateSliceRegions();

        size_t getBufferSize()
        {
            assert(sliceRegions.size());
            size_t size = sliceRegions.back().offset + sliceRegions.back().size;
            assert(size > 0);
            return size;
        }
        
        const TextureDesc& getDesc() const override { return desc; }
    };

    class Sampler : public RefCounter<ISampler>
    {
    public:
        SamplerDesc desc;

        vk::SamplerCreateInfo samplerInfo;
        vk::Sampler sampler;

        explicit Sampler(const VulkanContext& context)
            : m_Context(context)
        { }

        ~Sampler() override;
        const SamplerDesc& getDesc() const override { return desc; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class Shader : public RefCounter<IShader>
    {
    public:
        ShaderDesc desc;
        
        vk::ShaderModule shaderModule;
        vk::ShaderStageFlagBits stageFlagBits{};

        // Shader specializations are just references to the original shader module
        // plus the specialization constant array.
        ResourceHandle baseShader; // Could be a Shader or ShaderLibrary
        std::vector<ShaderSpecialization> specializationConstants;

        explicit Shader(const VulkanContext& context)
            : desc(ShaderType::None)
            , m_Context(context)
        { }

        ~Shader() override;
        const ShaderDesc& getDesc() const override { return desc; }
        void getBytecode(const void** ppBytecode, size_t* pSize) const override;
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class ShaderLibrary : public RefCounter<IShaderLibrary>
    {
    public:
        vk::ShaderModule shaderModule;

        explicit ShaderLibrary(const VulkanContext& context)
            : m_Context(context)
        { }

        ~ShaderLibrary() override;
        void getBytecode(const void** ppBytecode, size_t* pSize) const override;
        ShaderHandle getShader(const char* entryName, ShaderType shaderType) override;
    private:
        const VulkanContext& m_Context;
    };

    class InputLayout : public RefCounter<IInputLayout>
    {
    public:
        std::vector<VertexAttributeDesc> inputDesc;

        std::vector<vk::VertexInputBindingDescription> bindingDesc;
        std::vector<vk::VertexInputAttributeDescription> attributeDesc;
        
        uint32_t getNumAttributes() const override;
        const VertexAttributeDesc* getAttributeDesc(uint32_t index) const override;
    };

    class EventQuery : public RefCounter<IEventQuery>
    {
    public:
        CommandQueue queue = CommandQueue::Graphics;
        uint64_t commandListID = 0;
    };
    
    class TimerQuery : public RefCounter<ITimerQuery>
    {
    public:
        int beginQueryIndex = -1;
        int endQueryIndex = -1;

        bool started = false;
        bool resolved = false;
        float time = 0.f;

        explicit TimerQuery(utils::BitSetAllocator& allocator)
            : m_QueryAllocator(allocator)
        { }

        ~TimerQuery() override;

    private:
        utils::BitSetAllocator& m_QueryAllocator;
    };

    class Framebuffer : public RefCounter<IFramebuffer>
    {
    public:
        FramebufferDesc desc;
        FramebufferInfo framebufferInfo;
        
        vk::RenderPass renderPass = vk::RenderPass();
        vk::Framebuffer framebuffer = vk::Framebuffer();

        std::vector<ResourceHandle> resources;

        bool managed = true;

        explicit Framebuffer(const VulkanContext& context)
            : m_Context(context)
        { }

        ~Framebuffer() override;
        const FramebufferDesc& getDesc() const override { return desc; }
        const FramebufferInfo& getFramebufferInfo() const override { return framebufferInfo; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class BindingLayout : public RefCounter<IBindingLayout>
    {
    public:
        BindingLayoutDesc desc;
        BindlessLayoutDesc bindlessDesc;
        bool isBindless;

        std::vector<vk::DescriptorSetLayoutBinding> vulkanLayoutBindings;

        vk::DescriptorSetLayout descriptorSetLayout;

        // descriptor pool size information per binding set
        std::vector<vk::DescriptorPoolSize> descriptorPoolSizeInfo;

        BindingLayout(const VulkanContext& context, const BindingLayoutDesc& desc);
        BindingLayout(const VulkanContext& context, const BindlessLayoutDesc& desc);
        ~BindingLayout() override;
        const BindingLayoutDesc* getDesc() const override { return isBindless ? nullptr : &desc; }
        const BindlessLayoutDesc* getBindlessDesc() const override { return isBindless ? &bindlessDesc : nullptr; }
        Object getNativeObject(ObjectType objectType) override;

        // generate the descriptor set layout
        vk::Result bake();

    private:
        const VulkanContext& m_Context;
    };

    // contains a vk::DescriptorSet
    class BindingSet : public RefCounter<IBindingSet>
    {
    public:
        BindingSetDesc desc;
        BindingLayoutHandle layout;

        // TODO: move pool to the context instead
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<ResourceHandle> resources;
        static_vector<Buffer*, c_MaxVolatileConstantBuffersPerLayout> volatileConstantBuffers;

        std::vector<uint16_t> bindingsThatNeedTransitions;

        explicit BindingSet(const VulkanContext& context)
            : m_Context(context)
        { }

        ~BindingSet() override;
        const BindingSetDesc* getDesc() const override { return &desc; }
        IBindingLayout* getLayout() const override { return layout; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class DescriptorTable : public RefCounter<IDescriptorTable>
    {
    public:
        BindingLayoutHandle layout;
        uint32_t capacity = 0;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        explicit DescriptorTable(const VulkanContext& context)
            : m_Context(context)
        { }

        ~DescriptorTable() override;
        const BindingSetDesc* getDesc() const override { return nullptr; }
        IBindingLayout* getLayout() const override { return layout; }
        uint32_t getCapacity() const override { return capacity; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    template <typename T>
    using BindingVector = static_vector<T, c_MaxBindingLayouts>;

    class GraphicsPipeline : public RefCounter<IGraphicsPipeline>
    {
    public:
        GraphicsPipelineDesc desc;
        FramebufferInfo framebufferInfo;
        ShaderType shaderMask = ShaderType::None;
        BindingVector<RefCountPtr<BindingLayout>> pipelineBindingLayouts;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        bool usesBlendConstants = false;

        explicit GraphicsPipeline(const VulkanContext& context)
            : m_Context(context)
        { }

        ~GraphicsPipeline() override;
        const GraphicsPipelineDesc& getDesc() const override { return desc; }
        const FramebufferInfo& getFramebufferInfo() const override { return framebufferInfo; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class ComputePipeline : public RefCounter<IComputePipeline>
    {
    public:
        ComputePipelineDesc desc;

        BindingVector<RefCountPtr<BindingLayout>> pipelineBindingLayouts;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;

        explicit ComputePipeline(const VulkanContext& context)
            : m_Context(context)
        { }

        ~ComputePipeline() override;
        const ComputePipelineDesc& getDesc() const override { return desc; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class MeshletPipeline : public RefCounter<IMeshletPipeline>
    {
    public:
        MeshletPipelineDesc desc;
        FramebufferInfo framebufferInfo;
        ShaderType shaderMask = ShaderType::None;
        BindingVector<RefCountPtr<BindingLayout>> pipelineBindingLayouts;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        bool usesBlendConstants = false;

        explicit MeshletPipeline(const VulkanContext& context)
            : m_Context(context)
        { }

        ~MeshletPipeline() override;
        const MeshletPipelineDesc& getDesc() const override { return desc; }
        const FramebufferInfo& getFramebufferInfo() const override { return framebufferInfo; }
        Object getNativeObject(ObjectType objectType) override;

    private:
        const VulkanContext& m_Context;
    };

    class RayTracingPipeline : public RefCounter<rt::IPipeline>
    {
    public:
        rt::PipelineDesc desc;
        BindingVector<RefCountPtr<BindingLayout>> pipelineBindingLayouts;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;

        std::unordered_map<std::string, uint32_t> shaderGroups; // name -> index
        std::vector<uint8_t> shaderGroupHandles;

        explicit RayTracingPipeline(const VulkanContext& context)
            : m_Context(context)
        { }

        ~RayTracingPipeline() override;
        const rt::PipelineDesc& getDesc() const override { return desc; }
        rt::ShaderTableHandle createShaderTable() override;
        Object getNativeObject(ObjectType objectType) override;

        int findShaderGroup(const std::string& name); // returns -1 if not found

    private:
        const VulkanContext& m_Context;
    };

    class ShaderTable : public RefCounter<rt::IShaderTable>
    {
    public:
        RefCountPtr<RayTracingPipeline> pipeline;

        int rayGenerationShader = -1;
        std::vector<uint32_t> missShaders;
        std::vector<uint32_t> callableShaders;
        std::vector<uint32_t> hitGroups;

        uint32_t version = 0;

        ShaderTable(const VulkanContext& context, RayTracingPipeline* _pipeline)
            : pipeline(_pipeline)
            , m_Context(context)
        { }
        
        void setRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        int addMissShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        int addHitGroup(const char* exportName, IBindingSet* bindings = nullptr) override;
        int addCallableShader(const char* exportName, IBindingSet* bindings = nullptr) override;
        void clearMissShaders() override;
        void clearHitShaders() override;
        void clearCallableShaders() override;
        rt::IPipeline* getPipeline() override { return pipeline; }
        uint32_t getNumEntries() const;

    private:
        const VulkanContext& m_Context;

        bool verifyShaderGroupExists(const char* exportName, int shaderGroupIndex) const;
    };

    struct BufferChunk
    {
        BufferHandle buffer;
        uint64_t version = 0;
        uint64_t bufferSize = 0;
        uint64_t writePointer = 0;
        void* mappedMemory = nullptr;

        static constexpr uint64_t c_sizeAlignment = 4096; // GPU page size
    };

    class UploadManager
    {
    public:
        UploadManager(Device* pParent, uint64_t defaultChunkSize, uint64_t memoryLimit, bool isScratchBuffer)
            : m_Device(pParent)
            , m_DefaultChunkSize(defaultChunkSize)
            , m_MemoryLimit(memoryLimit)
            , m_IsScratchBuffer(isScratchBuffer)
        { }

        std::shared_ptr<BufferChunk> CreateChunk(uint64_t size);

        bool suballocateBuffer(uint64_t size, Buffer** pBuffer, uint64_t* pOffset, void** pCpuVA, uint64_t currentVersion, uint32_t alignment = 256);
        void submitChunks(uint64_t currentVersion, uint64_t submittedVersion);

    private:
        Device* m_Device;
        uint64_t m_DefaultChunkSize = 0;
        uint64_t m_MemoryLimit = 0;
        uint64_t m_AllocatedMemory = 0;
        bool m_IsScratchBuffer = false;

        std::list<std::shared_ptr<BufferChunk>> m_ChunkPool;
        std::shared_ptr<BufferChunk> m_CurrentChunk;
    };

    class AccelStruct : public RefCounter<rt::IAccelStruct>
    {
    public:
        BufferHandle dataBuffer;
        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        vk::AccelerationStructureKHR accelStruct;
        vk::DeviceAddress accelStructDeviceAddress = 0;
        rt::AccelStructDesc desc;
        bool allowUpdate = false;
        bool compacted = false;
        size_t rtxmuId = ~0ull;
        vk::Buffer rtxmuBuffer;


        explicit AccelStruct(const VulkanContext& context)
            : m_Context(context)
        { }

        ~AccelStruct() override;

        Object getNativeObject(ObjectType objectType) override;
        const rt::AccelStructDesc& getDesc() const override { return desc; }
        bool isCompacted() const override { return compacted; }
        uint64_t getDeviceAddress() const override;

    private:
        const VulkanContext& m_Context;
    };


    class Device : public RefCounter<nvrhi::vulkan::IDevice>
    {
    public:
        // Internal backend methods

        Device(const DeviceDesc& desc);
        ~Device() override;

        Queue* getQueue(CommandQueue queue) const { return m_Queues[int(queue)].get(); }
        vk::QueryPool getTimerQueryPool() const { return m_TimerQueryPool; }

        // IResource implementation

        Object getNativeObject(ObjectType objectType) override;


        // IDevice implementation

        HeapHandle createHeap(const HeapDesc& d) override;

        TextureHandle createTexture(const TextureDesc& d) override;
        MemoryRequirements getTextureMemoryRequirements(ITexture* texture) override;
        bool bindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset) override;

        TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) override;

        StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess) override;
        void *mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch) override;
        void unmapStagingTexture(IStagingTexture* tex) override;

        BufferHandle createBuffer(const BufferDesc& d) override;
        void *mapBuffer(IBuffer* b, CpuAccessMode mapFlags) override;
        void unmapBuffer(IBuffer* b) override;
        MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer) override;
        bool bindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset) override;

        BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) override;

        ShaderHandle createShader(const ShaderDesc& d, const void* binary, size_t binarySize) override;
        ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, uint32_t numConstants) override;
        ShaderLibraryHandle createShaderLibrary(const void* binary, size_t binarySize) override;

        SamplerHandle createSampler(const SamplerDesc& d) override;

        InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) override;

        // event queries
        EventQueryHandle createEventQuery() override;
        void setEventQuery(IEventQuery* query, CommandQueue queue) override;
        bool pollEventQuery(IEventQuery* query) override;
        void waitEventQuery(IEventQuery* query) override;
        void resetEventQuery(IEventQuery* query) override;

        // timer queries
        TimerQueryHandle createTimerQuery() override;
        bool pollTimerQuery(ITimerQuery* query) override;
        float getTimerQueryTime(ITimerQuery* query) override;
        void resetTimerQuery(ITimerQuery* query) override;

        GraphicsAPI getGraphicsAPI() override;

        FramebufferHandle createFramebuffer(const FramebufferDesc& desc) override;

        GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb) override;

        ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override;

        MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, IFramebuffer* fb) override;

        rt::PipelineHandle createRayTracingPipeline(const rt::PipelineDesc& desc) override;

        BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) override;
        BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc) override;

        BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) override;
        DescriptorTableHandle createDescriptorTable(IBindingLayout* layout) override;

        void resizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents = true) override;
        bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item) override;
        
        rt::AccelStructHandle createAccelStruct(const rt::AccelStructDesc& desc) override;
        MemoryRequirements getAccelStructMemoryRequirements(rt::IAccelStruct* as) override;
        bool bindAccelStructMemory(rt::IAccelStruct* as, IHeap* heap, uint64_t offset) override;

        CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) override;
        uint64_t executeCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, CommandQueue executionQueue = CommandQueue::Graphics) override;
        void queueWaitForCommandList(CommandQueue waitQueue, CommandQueue executionQueue, uint64_t instance) override;
        void waitForIdle() override;
        void runGarbageCollection() override;
        bool queryFeatureSupport(Feature feature, void* pInfo = nullptr, size_t infoSize = 0) override;
        FormatSupport queryFormatSupport(Format format) override;
        Object getNativeQueue(ObjectType objectType, CommandQueue queue) override;
        IMessageCallback* getMessageCallback() override { return m_Context.messageCallback; }

        // vulkan::IDevice implementation
        vk::Semaphore getQueueSemaphore(CommandQueue queue) override;
        void queueWaitForSemaphore(CommandQueue waitQueue, vk::Semaphore semaphore, uint64_t value) override;
        void queueSignalSemaphore(CommandQueue executionQueue, vk::Semaphore semaphore, uint64_t value) override;
        uint64_t queueGetCompletedInstance(CommandQueue queue) override;
        FramebufferHandle createHandleForNativeFramebuffer(vk::RenderPass renderPass, vk::Framebuffer framebuffer,
            const FramebufferDesc& desc, bool transferOwnership) override;

    private:
        VulkanContext m_Context;
        VulkanAllocator m_Allocator;

        static constexpr uint32_t c_NumTimerQueries = 512;
        vk::QueryPool m_TimerQueryPool = nullptr;
        utils::BitSetAllocator m_TimerQueryAllocator;

        // array of submission queues
        std::array<std::unique_ptr<Queue>, uint32_t(CommandQueue::Count)> m_Queues;
        
        void *mapBuffer(IBuffer* b, CpuAccessMode flags, uint64_t offset, size_t size) const;
    };

    class CommandList : public RefCounter<ICommandList>
    {
    public:
        // Internal backend methods

        CommandList(Device* device, const VulkanContext& context, const CommandListParameters& parameters);

        void executed(Queue& queue, uint64_t submissionID);

        // IResource implementation

        Object getNativeObject(ObjectType objectType) override;

        // ICommandList implementation

        void open() override;
        void close() override;
        void clearState() override;

        void clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor) override;
        void clearDepthStencilTexture(ITexture* texture, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) override;
        void clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, uint32_t clearColor) override;

        void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) override;
        void copyTexture(IStagingTexture* dest, const TextureSlice& dstSlice, ITexture* src, const TextureSlice& srcSlice) override;
        void copyTexture(ITexture* dest, const TextureSlice& dstSlice, IStagingTexture* src, const TextureSlice& srcSlice) override;
        void writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch) override;
        void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) override;

        void writeBuffer(IBuffer* b, const void* data, size_t dataSize, uint64_t destOffsetBytes = 0) override;
        void clearBufferUInt(IBuffer* b, uint32_t clearValue) override;
        void copyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes) override;

        void setPushConstants(const void* data, size_t byteSize) override;

        void setGraphicsState(const GraphicsState& state) override;
        void draw(const DrawArguments& args) override;
        void drawIndexed(const DrawArguments& args) override;
        void drawIndirect(uint32_t offsetBytes) override;

        void setComputeState(const ComputeState& state) override;
        void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
        void dispatchIndirect(uint32_t offsetBytes)  override;

        void setMeshletState(const MeshletState& state) override;
        void dispatchMesh(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;

        void setRayTracingState(const rt::State& state) override;
        void dispatchRays(const rt::DispatchRaysArguments& args) override;
        
        void buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::GeometryDesc* pGeometries, size_t numGeometries, rt::AccelStructBuildFlags buildFlags) override;
        void compactBottomLevelAccelStructs() override;
        void buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::InstanceDesc* pInstances, size_t numInstances, rt::AccelStructBuildFlags buildFlags) override;
        void buildTopLevelAccelStructFromBuffer(rt::IAccelStruct* as, nvrhi::IBuffer* instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances,
            rt::AccelStructBuildFlags buildFlags = rt::AccelStructBuildFlags::None) override;

        void beginTimerQuery(ITimerQuery* query) override;
        void endTimerQuery(ITimerQuery* query) override;

        void beginMarker(const char* name) override;
        void endMarker() override;

        void setEnableAutomaticBarriers(bool enable) override;
        void setResourceStatesForBindingSet(IBindingSet* bindingSet) override;

        void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) override;
        void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) override;
        
        void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits) override;
        void beginTrackingBufferState(IBuffer* buffer, ResourceStates stateBits) override;

        void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits) override;
        void setBufferState(IBuffer* buffer, ResourceStates stateBits) override;
        void setAccelStructState(rt::IAccelStruct* _as, ResourceStates stateBits) override;

        void setPermanentTextureState(ITexture* texture, ResourceStates stateBits) override;
        void setPermanentBufferState(IBuffer* buffer, ResourceStates stateBits) override;

        void commitBarriers() override;

        ResourceStates getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) override;
        ResourceStates getBufferState(IBuffer* buffer) override;

        IDevice* getDevice() override { return m_Device; }
        const CommandListParameters& getDesc() override { return m_CommandListParameters; }

        TrackedCommandBufferPtr getCurrentCmdBuf() const { return m_CurrentCmdBuf; }

    private:
        Device* m_Device;
        const VulkanContext& m_Context;

        CommandListParameters m_CommandListParameters;

        CommandListResourceStateTracker m_StateTracker;
        bool m_EnableAutomaticBarriers = true;

        // current internal command buffer
        TrackedCommandBufferPtr m_CurrentCmdBuf = nullptr;

        vk::PipelineLayout m_CurrentPipelineLayout;
        vk::ShaderStageFlags m_CurrentPipelineShaderStages;
        GraphicsState m_CurrentGraphicsState{};
        ComputeState m_CurrentComputeState{};
        MeshletState m_CurrentMeshletState{};
        rt::State m_CurrentRayTracingState;
        bool m_AnyVolatileBufferWrites = false;

        struct ShaderTableState
        {
            vk::StridedDeviceAddressRegionKHR rayGen;
            vk::StridedDeviceAddressRegionKHR miss;
            vk::StridedDeviceAddressRegionKHR hitGroups;
            vk::StridedDeviceAddressRegionKHR callable;
            uint32_t version = 0;
        } m_CurrentShaderTablePointers;

        std::unordered_map<Buffer*, VolatileBufferState> m_VolatileBufferStates;

        std::unique_ptr<UploadManager> m_UploadManager;
        std::unique_ptr<UploadManager> m_ScratchManager;
        
        void clearTexture(ITexture* texture, TextureSubresourceSet subresources, const vk::ClearColorValue& clearValue);

        void bindBindingSets(vk::PipelineBindPoint bindPoint, vk::PipelineLayout pipelineLayout, const BindingSetVector& bindings);

        void endRenderPass();

        void trackResourcesAndBarriers(const GraphicsState& state);
        void trackResourcesAndBarriers(const MeshletState& state);
        
        void writeVolatileBuffer(Buffer* buffer, const void* data, size_t dataSize);
        void flushVolatileBufferWrites();
        void submitVolatileBuffers(uint64_t recordingID, uint64_t submittedID);

        void updateGraphicsVolatileBuffers();
        void updateComputeVolatileBuffers();
        void updateMeshletVolatileBuffers();
        void updateRayTracingVolatileBuffers();

        void requireTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates state);
        void requireBufferState(IBuffer* buffer, ResourceStates state);
        bool anyBarriers() const;

        void buildTopLevelAccelStructInternal(AccelStruct* as, VkDeviceAddress instanceData, size_t numInstances, rt::AccelStructBuildFlags buildFlags, uint64_t currentVersion);
    };

} // namespace nvrhi::vulkan