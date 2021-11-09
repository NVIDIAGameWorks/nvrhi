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
        const vk::DynamicLoader dl;
        const PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =   // NOLINT(misc-misplaced-const)
            dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(desc.instance);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(desc.device);
#endif

        Device* device = new Device(desc);
        return DeviceHandle::Create(device);
    }
        
    Device::Device(const DeviceDesc& desc)
        : m_Context(desc.instance, desc.physicalDevice, desc.device, desc.allocationCallbacks)
        , m_Allocator(m_Context)
        , m_TimerQueryAllocator(c_NumTimerQueries, true)
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
            { VK_KHR_MAINTENANCE1_EXTENSION_NAME, &m_Context.extensions.KHR_maintenance1 },
            { VK_EXT_DEBUG_REPORT_EXTENSION_NAME, &m_Context.extensions.EXT_debug_report },
            { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, &m_Context.extensions.EXT_debug_marker },
            { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &m_Context.extensions.KHR_acceleration_structure },
            { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, &m_Context.extensions.KHR_buffer_device_address },
            { VK_KHR_RAY_QUERY_EXTENSION_NAME,&m_Context.extensions.KHR_ray_query },
            { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &m_Context.extensions.KHR_ray_tracing_pipeline },
            { VK_NV_MESH_SHADER_EXTENSION_NAME, &m_Context.extensions.NV_mesh_shader },
            { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, &m_Context.extensions.KHR_fragment_shading_rate },
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

        // Get the device properties with supported extensions

        void* pNext = nullptr;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;
        vk::PhysicalDeviceFragmentShadingRatePropertiesKHR shadingRateProperties;
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

        deviceProperties2.pNext = pNext;

        m_Context.physicalDevice.getProperties2(&deviceProperties2);

        m_Context.physicalDeviceProperties = deviceProperties2.properties;
        m_Context.accelStructProperties = accelStructProperties;
        m_Context.rayTracingPipelineProperties = rayTracingPipelineProperties;
        m_Context.shadingRateProperties = shadingRateProperties;
        m_Context.messageCallback = desc.errorCB;

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
#endif
        auto pipelineInfo = vk::PipelineCacheCreateInfo();
        vk::Result res = m_Context.device.createPipelineCache(&pipelineInfo,
            m_Context.allocationCallbacks,
            &m_Context.pipelineCache);

        if (res != vk::Result::eSuccess)
        {
            m_Context.error("Failed to create the pipeline cache");
        }
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

    void Device::waitForIdle()
    {
        m_Context.device.waitIdle();
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
        case Feature::RayQuery:
            return m_Context.extensions.KHR_ray_query;
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
        case Feature::VirtualResources:
            return true;
        case Feature::ComputeQueue:
            return (m_Queues[uint32_t(CommandQueue::Compute)] != nullptr);
        case Feature::CopyQueue:
            return (m_Queues[uint32_t(CommandQueue::Copy)] != nullptr);
        default:
            return false;
        }
    }

    FormatSupport Device::queryFormatSupport(Format format)
    {
        vk::Format vulkanFormat = convertFormat(format);
        
        vk::FormatProperties props;
        m_Context.physicalDevice.getFormatProperties(vulkanFormat, &props);

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

        Heap* heap = new Heap(m_Context, m_Allocator);
        heap->desc = d;
        heap->managed = true;

        const vk::Result res = m_Allocator.allocateMemory(heap, memoryRequirements, memoryPropertyFlags);

        if (res != vk::Result::eSuccess)
        {
            std::stringstream ss;
            ss << "Failed to allocate memory for Heap " << utils::DebugNameToString(d.debugName)
                << ", VkResult = " << resultToString(res);

            m_Context.error(ss.str());

            delete heap;
            return nullptr;
        }

        if (!d.debugName.empty())
        {
            m_Context.nameVKObject(heap->memory, vk::DebugReportObjectTypeEXT::eDeviceMemory, d.debugName.c_str());
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

    void VulkanContext::nameVKObject(const void* handle, const vk::DebugReportObjectTypeEXT objtype, const char* name) const
    {
        if (extensions.EXT_debug_marker && name && *name && handle)
        {
            auto info = vk::DebugMarkerObjectNameInfoEXT()
                .setObjectType(objtype)
                .setObject(reinterpret_cast<uint64_t>(handle))
                .setPObjectName(name);

            (void)device.debugMarkerSetObjectNameEXT(&info);
        }
    }

    void VulkanContext::error(const std::string& message) const
    {
        messageCallback->message(MessageSeverity::Error, message.c_str());
    }

} // namespace nvrhi::vulkan
