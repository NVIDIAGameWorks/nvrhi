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

#include <vulkan/vulkan.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>

namespace nvrhi 
{
    namespace ObjectTypes
    {
        constexpr ObjectType Nvrhi_VK_Device = 0x00030101;
    };
}

namespace nvrhi::vulkan
{
    class IDevice : public nvrhi::IDevice
    {
    public:
        // Additional Vulkan-specific public methods
        virtual vk::Semaphore getQueueSemaphore(CommandQueue queue) = 0;
        virtual void queueWaitForSemaphore(CommandQueue waitQueue, vk::Semaphore semaphore, uint64_t value) = 0;
        virtual void queueSignalSemaphore(CommandQueue executionQueue, vk::Semaphore semaphore, uint64_t value) = 0;
        virtual uint64_t queueGetCompletedInstance(CommandQueue queue) = 0;
        virtual FramebufferHandle createHandleForNativeFramebuffer(vk::RenderPass renderPass, 
            vk::Framebuffer framebuffer, const FramebufferDesc& desc, bool transferOwnership) = 0;
    };

    typedef RefCountPtr<IDevice> DeviceHandle;

    struct DeviceDesc
    {
        IMessageCallback* errorCB = nullptr;

        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;

        // any of the queues can be null if this context doesn't intend to use them
        vk::Queue graphicsQueue;
        int graphicsQueueIndex = -1;
        vk::Queue transferQueue;
        int transferQueueIndex = -1;
        vk::Queue computeQueue;
        int computeQueueIndex = -1;

        vk::AllocationCallbacks *allocationCallbacks = nullptr;

        const char **instanceExtensions = nullptr;
        size_t numInstanceExtensions = 0;
        
        const char **deviceExtensions = nullptr;
        size_t numDeviceExtensions = 0;
    };

    NVRHI_API DeviceHandle createDevice(const DeviceDesc& desc);
   
    NVRHI_API vk::Format convertFormat(nvrhi::Format format);

    NVRHI_API const char* resultToString(VkResult result);
    NVRHI_API const char* resultToString(vk::Result result);
}