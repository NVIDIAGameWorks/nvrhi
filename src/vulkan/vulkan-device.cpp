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
#include <unordered_map>
#include <sstream>

#include <nvrhi/common/misc.h>

#if defined(NVRHI_SHARED_LIBRARY_BUILD)
// Define the Vulkan dynamic dispatcher - this needs to occur in exactly one cpp file in the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

namespace nvrhi::vulkan
{
    DeviceHandle createDevice(const DeviceDesc& desc)
    {
#if defined(NVRHI_SHARED_LIBRARY_BUILD)
#if VK_HEADER_VERSION >= 301
        vk::detail::DynamicLoader dl;
#else
        vk::DynamicLoader dl;
#endif
        const PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =   // NOLINT(misc-misplaced-const)
            dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(desc.instance, vkGetInstanceProcAddr, desc.device);
#endif

        Device* device = new Device(desc);
        return DeviceHandle::Create(device);
    }
        
    Device::Device(const DeviceDesc& desc)
        : m_Context(desc.instance, desc.physicalDevice, desc.device, reinterpret_cast<vk::AllocationCallbacks*>(desc.allocationCallbacks))
        , m_Allocator(m_Context)
        , m_TimerQueryAllocator(desc.maxTimerQueries, true)
    {
        if (desc.graphicsQueue)
        {
            m_Queues[uint32_t(CommandQueue::Graphics)] = std::make_unique<Queue>(m_Context,
                CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex);
        }

        if (desc.computeQueue)
        {
            m_Queues[uint32_t(CommandQueue::Compute)] = std::make_unique<Queue>(m_Context,
                CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex);
        }

        if (desc.transferQueue)
        {
            m_Queues[uint32_t(CommandQueue::Copy)] = std::make_unique<Queue>(m_Context,
                CommandQueue::Copy, desc.transferQueue, desc.transferQueueIndex);
        }

        // maps Vulkan extension strings into the corresponding boolean flags in Device
        const std::unordered_map<std::string, bool*> extensionStringMap = {
            { VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME, &m_Context.extensions.EXT_conservative_rasterization},
            { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, &m_Context.extensions.EXT_debug_marker },
            { VK_EXT_DEBUG_REPORT_EXTENSION_NAME, &m_Context.extensions.EXT_debug_report },
            { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, &m_Context.extensions.EXT_debug_utils },
            { VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME, &m_Context.extensions.EXT_opacity_micromap },
            { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &m_Context.extensions.KHR_acceleration_structure },
            { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, &m_Context.extensions.buffer_device_address },
            { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, &m_Context.extensions.KHR_fragment_shading_rate },
            { VK_KHR_MAINTENANCE1_EXTENSION_NAME, &m_Context.extensions.KHR_maintenance1 },
            { VK_KHR_RAY_QUERY_EXTENSION_NAME,&m_Context.extensions.KHR_ray_query },
            { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &m_Context.extensions.KHR_ray_tracing_pipeline },
            { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, &m_Context.extensions.KHR_synchronization2 },
            { VK_NV_MESH_SHADER_EXTENSION_NAME, &m_Context.extensions.NV_mesh_shader },
            { VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME, &m_Context.extensions.NV_ray_tracing_invocation_reorder },
#if NVRHI_WITH_AFTERMATH
            { VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME, &m_Context.extensions.NV_device_diagnostic_checkpoints },
            { VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME, &m_Context.extensions.NV_device_diagnostics_config }
#endif
        };

        // parse the extension/layer lists and figure out which extensions are enabled
        for(size_t i = 0; i < desc.numInstanceExtensions; i++)
        {
            auto ext = extensionStringMap.find(desc.instanceExtensions[i]);
            if (ext != extensionStringMap.end())
            {
                *(ext->second) = true;
            }
        }
        
        for(size_t i = 0; i < desc.numDeviceExtensions; i++)
        {
            auto ext = extensionStringMap.find(desc.deviceExtensions[i]);
            if (ext != extensionStringMap.end())
            {
                *(ext->second) = true;
            }
        }

        // The Vulkan 1.2 way of enabling bufferDeviceAddress
        if (desc.bufferDeviceAddressSupported)
            m_Context.extensions.buffer_device_address = true;

        void* pNext = nullptr;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
        vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterizationProperties;
        vk::PhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties;
        vk::PhysicalDeviceOpacityMicromapPropertiesEXT opacityMicromapProperties;
        vk::PhysicalDeviceRayTracingInvocationReorderPropertiesNV nvRayTracingInvocationReorderProperties;
        
        vk::PhysicalDeviceProperties2 deviceProperties2;

        if (m_Context.extensions.KHR_acceleration_structure)
        {
            accelStructProperties.pNext = pNext;
            pNext = &accelStructProperties;
        }

        if (m_Context.extensions.KHR_ray_tracing_pipeline)
        {
            rayTracingPipelineProperties.pNext = pNext;
            pNext = &rayTracingPipelineProperties;
        }

        if (m_Context.extensions.KHR_fragment_shading_rate)
        {
            shadingRateProperties.pNext = pNext;
            pNext = &shadingRateProperties;
        }

        if (m_Context.extensions.EXT_conservative_rasterization)
        {
            conservativeRasterizationProperties.pNext = pNext;
            pNext = &conservativeRasterizationProperties;
        }

        if (m_Context.extensions.EXT_opacity_micromap)
        {
            opacityMicromapProperties.pNext = pNext;
            pNext = &opacityMicromapProperties;
        }

        if (m_Context.extensions.NV_ray_tracing_invocation_reorder)
        {
            nvRayTracingInvocationReorderProperties.pNext = pNext;
            pNext = &nvRayTracingInvocationReorderProperties;
        }

        deviceProperties2.pNext = pNext;

        m_Context.physicalDevice.getProperties2(&deviceProperties2);

        m_Context.physicalDeviceProperties = deviceProperties2.properties;
        m_Context.accelStructProperties = accelStructProperties;
        m_Context.rayTracingPipelineProperties = rayTracingPipelineProperties;
        m_Context.conservativeRasterizationProperties = conservativeRasterizationProperties;
        m_Context.shadingRateProperties = shadingRateProperties;
        m_Context.opacityMicromapProperties = opacityMicromapProperties;
        m_Context.nvRayTracingInvocationReorderProperties = nvRayTracingInvocationReorderProperties;
        m_Context.messageCallback = desc.errorCB;

        if (m_Context.extensions.EXT_opacity_micromap && !m_Context.extensions.KHR_synchronization2)
        {
            m_Context.warning(
                "EXT_opacity_micromap is used without KHR_synchronization2 which is nessesary for OMM Array state transitions. Feature::RayTracingOpacityMicromap will be disabled.");
        }

        if (m_Context.extensions.KHR_fragment_shading_rate)
        {
            vk::PhysicalDeviceFeatures2 deviceFeatures2;
            vk::PhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures;
            deviceFeatures2.setPNext(&shadingRateFeatures);
            m_Context.physicalDevice.getFeatures2(&deviceFeatures2);
            m_Context.shadingRateFeatures = shadingRateFeatures;
        }
#ifdef NVRHI_WITH_RTXMU
        if (m_Context.extensions.KHR_acceleration_structure)
        {
            m_Context.rtxMemUtil = std::make_unique<rtxmu::VkAccelStructManager>(desc.instance, desc.device, desc.physicalDevice);

            // Initialize suballocator blocks to 8 MB
            m_Context.rtxMemUtil->Initialize(8388608);

            m_Context.rtxMuResources = std::make_unique<RtxMuResources>();
        }

        if (m_Context.extensions.EXT_opacity_micromap)
        {
            m_Context.warning("Opacity micro-maps are not currently supported by RTXMU.");
        }
#endif
        auto pipelineInfo = vk::PipelineCacheCreateInfo();
        vk::Result res = m_Context.device.createPipelineCache(&pipelineInfo,
            m_Context.allocationCallbacks,
            &m_Context.pipelineCache);

        if (res != vk::Result::eSuccess)
        {
            m_Context.error("Failed to create the pipeline cache");
        }

        // Create an empty Vk::DescriptorSetLayout
        auto descriptorSetLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
            .setBindingCount(0)
            .setPBindings(nullptr);
        res = m_Context.device.createDescriptorSetLayout(&descriptorSetLayoutInfo,
            m_Context.allocationCallbacks,
            &m_Context.emptyDescriptorSetLayout);

        if (res != vk::Result::eSuccess)
        {
            m_Context.error("Failed to create an empty descriptor set layout");
        }

#if NVRHI_WITH_AFTERMATH
        m_AftermathEnabled = desc.aftermathEnabled;
#endif
    }

    Device::~Device()
    {
        if (m_TimerQueryPool)
        {
            m_Context.device.destroyQueryPool(m_TimerQueryPool);
            m_TimerQueryPool = vk::QueryPool();
        }

        if (m_Context.pipelineCache)
        {
            m_Context.device.destroyPipelineCache(m_Context.pipelineCache);
            m_Context.pipelineCache = vk::PipelineCache();
        }

        if (m_Context.emptyDescriptorSetLayout)
        {
            m_Context.device.destroyDescriptorSetLayout(m_Context.emptyDescriptorSetLayout);
            m_Context.emptyDescriptorSetLayout = vk::DescriptorSetLayout();
        }
    }

    Object Device::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_Device:
            return Object(m_Context.device);
        case ObjectTypes::VK_PhysicalDevice:
            return Object(m_Context.physicalDevice);
        case ObjectTypes::VK_Instance:
            return Object(m_Context.instance);
        case ObjectTypes::Nvrhi_VK_Device:
            return Object(this);
        default:
            return nullptr;
        }
    }

    GraphicsAPI Device::getGraphicsAPI()
    {
        return GraphicsAPI::VULKAN;
    }

    bool Device::waitForIdle()
    {
        try {
            m_Context.device.waitIdle();
        }
        catch (vk::DeviceLostError&)
        {
            return false;
        }
        return true;
    }

    void Device::runGarbageCollection()
    {
        for (auto& m_Queue : m_Queues)
        {
            if (m_Queue)
            {
                m_Queue->retireCommandBuffers();
            }
        }
    }

    bool Device::queryFeatureSupport(Feature feature, void* pInfo, size_t infoSize)
    {
        switch (feature)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case Feature::DeferredCommandLists:
            return true;
        case Feature::RayTracingAccelStruct:
            return m_Context.extensions.KHR_acceleration_structure;
        case Feature::RayTracingPipeline:
            return m_Context.extensions.KHR_ray_tracing_pipeline;
        case Feature::RayTracingOpacityMicromap:
#ifdef NVRHI_WITH_RTXMU
            return false; // RTXMU does not support OMMs
#else
            return m_Context.extensions.EXT_opacity_micromap && m_Context.extensions.KHR_synchronization2;
#endif
        case Feature::RayQuery:
            return m_Context.extensions.KHR_ray_query;
        case Feature::ShaderExecutionReordering:
        {
            if (m_Context.extensions.NV_ray_tracing_invocation_reorder)
            {
                return vk::RayTracingInvocationReorderModeNV::eReorder == m_Context.nvRayTracingInvocationReorderProperties.rayTracingInvocationReorderReorderingHint;
            }
            return false;
        }
        case Feature::ShaderSpecializations:
            return true;
        case Feature::Meshlets:
            return m_Context.extensions.NV_mesh_shader;
        case Feature::VariableRateShading:
            if (pInfo)
            {
                if (infoSize == sizeof(VariableRateShadingFeatureInfo))
                {
                    auto* pVrsInfo = reinterpret_cast<VariableRateShadingFeatureInfo*>(pInfo);
                    const auto& tileExtent = m_Context.shadingRateProperties.minFragmentShadingRateAttachmentTexelSize;
                    pVrsInfo->shadingRateImageTileSize = std::max(tileExtent.width, tileExtent.height);
                }
                else
                    utils::NotSupported();
            }
            return m_Context.extensions.KHR_fragment_shading_rate && m_Context.shadingRateFeatures.attachmentFragmentShadingRate;
        case Feature::ConservativeRasterization:
            return m_Context.extensions.EXT_conservative_rasterization;
        case Feature::VirtualResources:
            return true;
        case Feature::ComputeQueue:
            return (m_Queues[uint32_t(CommandQueue::Compute)] != nullptr);
        case Feature::CopyQueue:
            return (m_Queues[uint32_t(CommandQueue::Copy)] != nullptr);
        case Feature::ConstantBufferRanges:
            return true;
        default:
            return false;
        }
    }

    FormatSupport Device::queryFormatSupport(Format format)
    {
        VkFormat vulkanFormat = convertFormat(format);
        
        vk::FormatProperties props;
        m_Context.physicalDevice.getFormatProperties(vk::Format(vulkanFormat), &props);

        FormatSupport result = FormatSupport::None;

        if (props.bufferFeatures)
            result = result | FormatSupport::Buffer;

        if (format == Format::R32_UINT || format == Format::R16_UINT) {
            // There is no explicit bit in vk::FormatFeatureFlags for index buffers
            result = result | FormatSupport::IndexBuffer;
        }
        
        if (props.bufferFeatures & vk::FormatFeatureFlagBits::eVertexBuffer)
            result = result | FormatSupport::VertexBuffer;

        if (props.optimalTilingFeatures)
            result = result | FormatSupport::Texture;

        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
            result = result | FormatSupport::DepthStencil;

        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachment)
            result = result | FormatSupport::RenderTarget;

        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachmentBlend)
            result = result | FormatSupport::Blendable;

        if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage) ||
            (props.bufferFeatures & vk::FormatFeatureFlagBits::eUniformTexelBuffer))
        {
            result = result | FormatSupport::ShaderLoad;
        }

        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
            result = result | FormatSupport::ShaderSample;

        if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage) ||
            (props.bufferFeatures & vk::FormatFeatureFlagBits::eStorageTexelBuffer))
        {
            result = result | FormatSupport::ShaderUavLoad;
            result = result | FormatSupport::ShaderUavStore;
        }

        if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImageAtomic) ||
            (props.bufferFeatures & vk::FormatFeatureFlagBits::eStorageTexelBufferAtomic))
        {
            result = result | FormatSupport::ShaderAtomic;
        }

        return result;
    }

    Object Device::getNativeQueue(ObjectType objectType, CommandQueue queue)
    {
        if (objectType != ObjectTypes::VK_Queue)
            return nullptr;

        if (queue >= CommandQueue::Count)
            return nullptr;

        return Object(m_Queues[uint32_t(queue)]->getVkQueue());
    }

    CommandListHandle Device::createCommandList(const CommandListParameters& params)
    {
        if (!m_Queues[uint32_t(params.queueType)])
            return nullptr;

        CommandList* cmdList = new CommandList(this, m_Context, params);

        return CommandListHandle::Create(cmdList);
    }
    
    uint64_t Device::executeCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, CommandQueue executionQueue)
    {
        Queue& queue = *m_Queues[uint32_t(executionQueue)];

        uint64_t submissionID = queue.submit(pCommandLists, numCommandLists);

        for (size_t i = 0; i < numCommandLists; i++)
        {
            checked_cast<CommandList*>(pCommandLists[i])->executed(queue, submissionID);
        }

        return submissionID;
    }

    void Device::getTextureTiling(ITexture* _texture, uint32_t* numTiles, PackedMipDesc* desc, TileShape* tileShape, uint32_t* subresourceTilingsNum, SubresourceTiling* subresourceTilings)
    {
        Texture* texture = checked_cast<Texture*>(_texture);
        uint32_t numStandardMips = 0;
        uint32_t tileWidth = 1;
        uint32_t tileHeight = 1;
        uint32_t tileDepth = 1;

        {
            auto memoryRequirements = m_Context.device.getImageSparseMemoryRequirements(texture->image);
            if (!memoryRequirements.empty())
            {
                numStandardMips = memoryRequirements[0].imageMipTailFirstLod;
            }

            if (desc)
            {
                desc->numStandardMips = numStandardMips;
                desc->numPackedMips = texture->imageInfo.mipLevels - memoryRequirements[0].imageMipTailFirstLod;
                desc->startTileIndexInOverallResource = (uint32_t)(memoryRequirements[0].imageMipTailOffset / texture->tileByteSize);
                desc->numTilesForPackedMips = (uint32_t)(memoryRequirements[0].imageMipTailSize / texture->tileByteSize);
            }
        }

        {
            auto formatProperties = m_Context.physicalDevice.getSparseImageFormatProperties(texture->imageInfo.format, texture->imageInfo.imageType, texture->imageInfo.samples, texture->imageInfo.usage, texture->imageInfo.tiling);
            if (!formatProperties.empty())
            {   
                tileWidth = formatProperties[0].imageGranularity.width;
                tileHeight = formatProperties[0].imageGranularity.height;
                tileDepth = formatProperties[0].imageGranularity.depth;
            }

            if (tileShape)
            {
                tileShape->widthInTexels = tileWidth;
                tileShape->heightInTexels = tileHeight;
                tileShape->depthInTexels = tileDepth;
            }
        }

        if (subresourceTilingsNum)
        {
            *subresourceTilingsNum = std::min(*subresourceTilingsNum, texture->desc.mipLevels);
            uint32_t startTileIndexInOverallResource = 0;

            uint32_t width = texture->desc.width;
            uint32_t height = texture->desc.height;
            uint32_t depth = texture->desc.depth;

            for (uint32_t i = 0; i < *subresourceTilingsNum; ++i)
            {
                if (i < numStandardMips)
                {
                    subresourceTilings[i].widthInTiles = (width + tileWidth - 1) / tileWidth;
                    subresourceTilings[i].heightInTiles = (height + tileHeight - 1) / tileHeight;
                    subresourceTilings[i].depthInTiles = (depth + tileDepth - 1) / tileDepth;
                    subresourceTilings[i].startTileIndexInOverallResource = startTileIndexInOverallResource;
                }
                else
                {
                    subresourceTilings[i].widthInTiles = 0;
                    subresourceTilings[i].heightInTiles = 0;
                    subresourceTilings[i].depthInTiles = 0;
                    subresourceTilings[i].startTileIndexInOverallResource = UINT32_MAX;
                }

                width = std::max(width / 2, tileWidth);
                height = std::max(height / 2, tileHeight);
                depth = std::max(depth / 2, tileDepth);

                startTileIndexInOverallResource += subresourceTilings[i].widthInTiles * subresourceTilings[i].heightInTiles * subresourceTilings[i].depthInTiles;
            }
        }

        if (numTiles)
        {
            auto memoryRequirements = m_Context.device.getImageMemoryRequirements(texture->image);
            *numTiles = (uint32_t)(memoryRequirements.size / texture->tileByteSize);
        }
    }

    HeapHandle Device::createHeap(const HeapDesc& d)
    {
        vk::MemoryRequirements memoryRequirements;
        memoryRequirements.alignment = 0;
        memoryRequirements.memoryTypeBits = ~0u; // just pick whatever fits the property flags
        memoryRequirements.size = d.capacity;

        vk::MemoryPropertyFlags memoryPropertyFlags;
        switch(d.type)
        {
        case HeapType::DeviceLocal:
            memoryPropertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
            break;
        case HeapType::Upload: 
            memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible;
            break;
        case HeapType::Readback: 
            memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
            break;
        default:
            utils::InvalidEnum();
            return nullptr;
        }

        Heap* heap = new Heap(m_Allocator);
        heap->desc = d;
        heap->managed = true;

        // Set the Device Address bit if that feature is supported, because the heap might be used to store acceleration structures
        const bool enableDeviceAddress = m_Context.extensions.buffer_device_address;

        const vk::Result res = m_Allocator.allocateMemory(heap, memoryRequirements, memoryPropertyFlags, enableDeviceAddress);

        if (res != vk::Result::eSuccess)
        {
            std::stringstream ss;
            ss << "Failed to allocate memory for Heap " << utils::DebugNameToString(d.debugName)
                << ", VkResult = " << resultToString(VkResult(res));

            m_Context.error(ss.str());

            delete heap;
            return nullptr;
        }

        if (!d.debugName.empty())
        {
            m_Context.nameVKObject(heap->memory, vk::ObjectType::eDeviceMemory, vk::DebugReportObjectTypeEXT::eDeviceMemory, d.debugName.c_str());
        }

        return HeapHandle::Create(heap);
    }

    Heap::~Heap()
    {
        if (memory && managed)
        {
            m_Allocator.freeMemory(this);
            memory = vk::DeviceMemory();
        }
    }

    void VulkanContext::nameVKObject(const void* handle, const vk::ObjectType objtype,
        const vk::DebugReportObjectTypeEXT objtypeEXT, const char* name) const
    {
        if (!(name && *name && handle))
            return;

        if (extensions.EXT_debug_utils)
        {
            auto info = vk::DebugUtilsObjectNameInfoEXT()
                .setObjectType(objtype)
                .setObjectHandle(reinterpret_cast<uint64_t>(handle))
                .setPObjectName(name);
            device.setDebugUtilsObjectNameEXT(info);
        }
        else if (extensions.EXT_debug_marker)
        {
            auto info = vk::DebugMarkerObjectNameInfoEXT()
                .setObjectType(objtypeEXT)
                .setObject(reinterpret_cast<uint64_t>(handle))
                .setPObjectName(name);

            (void)device.debugMarkerSetObjectNameEXT(&info);
        }
    }

    void VulkanContext::error(const std::string& message) const
    {
        messageCallback->message(MessageSeverity::Error, message.c_str());
    }

    void VulkanContext::warning(const std::string& message) const
    {
        messageCallback->message(MessageSeverity::Warning, message.c_str());
    }
} // namespace nvrhi::vulkan
