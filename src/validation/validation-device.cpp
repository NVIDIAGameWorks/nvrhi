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

#include "validation-backend.h"

#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include <sstream>

namespace nvrhi::validation
{
    DeviceHandle createValidationLayer(IDevice* underlyingDevice)
    {
        DeviceWrapper* wrapper = new DeviceWrapper(underlyingDevice);
        return DeviceHandle::Create(wrapper);
    }

    DeviceWrapper::DeviceWrapper(IDevice* device)
        : m_Device(device)
        , m_MessageCallback(device->getMessageCallback())
    {

    }

    void DeviceWrapper::error(const std::string& messageText) const
    {
        m_MessageCallback->message(MessageSeverity::Error, messageText.c_str());
    }

    void DeviceWrapper::warning(const std::string& messageText) const
    {
        m_MessageCallback->message(MessageSeverity::Warning, messageText.c_str());
    }

    Object DeviceWrapper::getNativeObject(ObjectType objectType)
    {
        return m_Device->getNativeObject(objectType);
    }

    HeapHandle DeviceWrapper::createHeap(const HeapDesc& d)
    {
        if (d.capacity == 0)
        {
            error("Cannot create a Heap with capacity = 0");
            return nullptr;
        }

        HeapDesc patchedDesc = d;
        if (patchedDesc.debugName.empty())
            patchedDesc.debugName = utils::GenerateHeapDebugName(patchedDesc);

        return m_Device->createHeap(patchedDesc);
    }

    TextureHandle DeviceWrapper::createTexture(const TextureDesc& d)
    {
        bool anyErrors = false;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
        case TextureDimension::Texture3D:
            break;

        case TextureDimension::Unknown:
        default:
            error("Unknown texture dimension");
            return nullptr;
        }

        const char* dimensionStr = utils::TextureDimensionToString(d.dimension);
        const char* debugName = utils::DebugNameToString(d.debugName);

        if (d.width == 0 || d.height == 0 || d.depth == 0 || d.arraySize == 0 || d.mipLevels == 0)
        {
            std::stringstream ss;
            ss << dimensionStr << " " << debugName << ": width(" << d.width << "), height(" << d.height << "), depth(" << d.depth
                << "), arraySize(" << d.arraySize << ") and mipLevels(" << d.mipLevels << " must not be zero";
            error(ss.str());
            return nullptr;
        }

        switch (d.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
            if (d.height != 1)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": height(" << d.height << ") must be equal to 1";
                error(ss.str());
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            if (d.depth != 1)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": depth(" << d.depth << ") must be equal to 1";
                error(ss.str());
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture3D:
            if (d.arraySize != 1)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": arraySize(" << d.arraySize << ") must be equal to 1";
                error(ss.str());
                anyErrors = true;
            }
            break;
        case TextureDimension::TextureCube:
            if (d.arraySize != 6)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": arraySize(" << d.arraySize << ") must be equal to 6";
                error(ss.str());
                anyErrors = true;
            }
            break;
        case TextureDimension::TextureCubeArray:
            if ((d.arraySize % 6) != 0)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": arraySize(" << d.arraySize << ") must be a multiple of 6";
                error(ss.str());
                anyErrors = true;
            }
            break;
        default:;
        }

        switch (d.dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture3D:
            if (d.sampleCount != 1)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": sampleCount(" << d.sampleCount << ") must be equal to 1";
                error(ss.str());
                anyErrors = true;
            }
            break;
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            if (d.sampleCount != 2 && d.sampleCount != 4 && d.sampleCount != 8)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": sampleCount(" << d.sampleCount << ") must be equal to 2, 4 or 8";
                error(ss.str());
                anyErrors = true;
            }
            if (d.isUAV)
            {
                std::stringstream ss;
                ss << dimensionStr << " " << debugName << ": multi-sampled textures cannot have UAVs (isUAV flag)";
                error(ss.str());
                anyErrors = true;
            }
            break;
        default:;
        }

        if (d.isVirtual && !m_Device->queryFeatureSupport(Feature::VirtualResources))
        {
            std::stringstream ss;
            ss << dimensionStr << " " << debugName << ": The device does not support virtual resources";
            error(ss.str());
            anyErrors = true;
        }
        
        if(anyErrors)
            return nullptr;
        
        TextureDesc patchedDesc = d;
        if (patchedDesc.debugName.empty())
            patchedDesc.debugName = utils::GenerateTextureDebugName(patchedDesc);

        return m_Device->createTexture(patchedDesc);
    }

    MemoryRequirements DeviceWrapper::getTextureMemoryRequirements(ITexture* texture)
    {
        if (texture == nullptr)
        {
            error("getTextureMemoryRequirements: texture is NULL");
            return MemoryRequirements();
        }

        const MemoryRequirements memReq = m_Device->getTextureMemoryRequirements(texture);

        if (memReq.size == 0)
        {
            std::stringstream ss;
            ss << "Invalid texture " << utils::DebugNameToString(texture->getDesc().debugName) << ": "
                "getTextureMemoryRequirements returned zero size";

            error(ss.str());
        }

        return memReq;
    }

    bool DeviceWrapper::bindTextureMemory(ITexture* texture, IHeap* heap, uint64_t offset)
    {
        if (texture == nullptr)
        {
            error("bindTextureMemory: texture is NULL");
            return false;
        }

        if (heap == nullptr)
        {
            error("bindTextureMemory: heap is NULL");
            return false;
        }

        const HeapDesc& heapDesc = heap->getDesc();
        const TextureDesc& textureDesc = texture->getDesc();

        if (!textureDesc.isVirtual)
        {
            std::stringstream ss;
            ss << "Cannot perform bindTextureMemory on texture " << utils::DebugNameToString(textureDesc.debugName)
                << " because it was created with isVirtual = false";

            error(ss.str());
            return false;
        }

        MemoryRequirements memReq = m_Device->getTextureMemoryRequirements(texture);

        if (offset + memReq.size > heapDesc.capacity)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName)
                << " does not fit into heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at offset " << offset << " because it requires " << memReq.size << " bytes,"
                << " and the heap capacity is " << heapDesc.capacity << " bytes";

            error(ss.str());
            return false;
        }

        if (memReq.alignment != 0 && (offset % memReq.alignment) != 0)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName)
                << " is placed in heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at invalid alignment: required alignment to " << memReq.alignment << " bytes,"
                << " actual offset is " << offset << " bytes";

            error(ss.str());
            return false;
        }

        return m_Device->bindTextureMemory(texture, heap, offset);
    }
    
    TextureHandle DeviceWrapper::createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc)
    {
        return m_Device->createHandleForNativeTexture(objectType, texture, desc);
    }

    StagingTextureHandle DeviceWrapper::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        TextureDesc patchedDesc = d;
        if (patchedDesc.debugName.empty())
            patchedDesc.debugName = utils::GenerateTextureDebugName(patchedDesc);

        return m_Device->createStagingTexture(patchedDesc, cpuAccess);
    }

    void * DeviceWrapper::mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        return m_Device->mapStagingTexture(tex, slice, cpuAccess, outRowPitch);
    }

    void DeviceWrapper::unmapStagingTexture(IStagingTexture* tex)
    {
        m_Device->unmapStagingTexture(tex);
    }

    BufferHandle DeviceWrapper::createBuffer(const BufferDesc& d)
    {
        BufferDesc patchedDesc = d;
        if (patchedDesc.debugName.empty())
            patchedDesc.debugName = utils::GenerateBufferDebugName(patchedDesc);
        
        if (d.isVolatile && !d.isConstantBuffer)
        {
            std::stringstream ss;
            ss << "Buffer " << patchedDesc.debugName << " is volatile but is not a constant buffer. Only constant buffers can be made volatile.";
            error(ss.str());
            return nullptr;
        }

        if (d.isVolatile && d.maxVersions == 0)
        {
            std::stringstream ss;
            ss << "Volatile constant buffer " << patchedDesc.debugName << " has maxVersions = 0";
            error(ss.str());
            return nullptr;
        }

        if (d.isVolatile && (d.isVertexBuffer || d.isIndexBuffer || d.isDrawIndirectArgs || d.canHaveUAVs || d.isAccelStructBuildInput || d.isAccelStructStorage || d.isVirtual))
        {
            std::stringstream ss;
            ss << "Buffer " << patchedDesc.debugName << " is volatile but has unsupported usage flags:";
            if (d.isVertexBuffer) ss << " IsVertexBuffer";
            if (d.isIndexBuffer) ss << " IsIndexBuffer";
            if (d.isDrawIndirectArgs) ss << " IsDrawIndirectArgs";
            if (d.canHaveUAVs) ss << " CanHaveUAVs";
            if (d.isAccelStructBuildInput) ss << " IsAccelStructBuildInput";
            if (d.isAccelStructStorage) ss << " IsAccelStructStorage";
            if (d.isVirtual) ss << " IsVirtual";
            ss << "." << std::endl << "Only constant buffers can be made volatile, and volatile buffers cannot be virtual.";
            error(ss.str());
            return nullptr;
        }

        if (d.isVolatile && d.cpuAccess != CpuAccessMode::None)
        {
            std::stringstream ss;
            ss << "Volatile constant buffer " << patchedDesc.debugName << " must have cpuAccess set to None. Write-discard access is implied.";
            error(ss.str());
            return nullptr;
        }

        if (d.isVirtual && !m_Device->queryFeatureSupport(Feature::VirtualResources))
        {
            error("The device does not support virtual resources");
            return nullptr;
        }

        return m_Device->createBuffer(patchedDesc);
    }

    void * DeviceWrapper::mapBuffer(IBuffer* b, CpuAccessMode mapFlags)
    {
        return m_Device->mapBuffer(b, mapFlags);
    }

    void DeviceWrapper::unmapBuffer(IBuffer* b)
    {
        m_Device->unmapBuffer(b);
    }

    MemoryRequirements DeviceWrapper::getBufferMemoryRequirements(IBuffer* buffer)
    {
        if (buffer == nullptr)
        {
            error("getBufferMemoryRequirements: buffer is NULL");
            return MemoryRequirements();
        }

        const MemoryRequirements memReq = m_Device->getBufferMemoryRequirements(buffer);

        if (memReq.size == 0)
        {
            std::stringstream ss;
            ss << "Invalid buffer " << utils::DebugNameToString(buffer->getDesc().debugName) << ": "
                "getBufferMemoryRequirements returned zero size";

            error(ss.str());
        }

        return memReq;
    }

    bool DeviceWrapper::bindBufferMemory(IBuffer* buffer, IHeap* heap, uint64_t offset)
    {
        if (buffer == nullptr)
        {
            error("bindBufferMemory: texture is NULL");
            return false;
        }

        if (heap == nullptr)
        {
            error("bindBufferMemory: heap is NULL");
            return false;
        }

        const HeapDesc& heapDesc = heap->getDesc();
        const BufferDesc& bufferDesc = buffer->getDesc();

        if (!bufferDesc.isVirtual)
        {
            std::stringstream ss;
            ss << "Cannot perform bindBufferMemory on buffer " << utils::DebugNameToString(bufferDesc.debugName)
                << " because it was created with isVirtual = false";

            error(ss.str());
            return false;
        }

        MemoryRequirements memReq = m_Device->getBufferMemoryRequirements(buffer);

        if (offset + memReq.size > heapDesc.capacity)
        {
            std::stringstream ss;
            ss << "Buffer " << utils::DebugNameToString(bufferDesc.debugName)
                << " does not fit into heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at offset " << offset << " because it requires " << memReq.size << " bytes,"
                << " and the heap capacity is " << heapDesc.capacity << " bytes";

            error(ss.str());
            return false;
        }

        if (memReq.alignment != 0 && (offset % memReq.alignment) != 0)
        {
            std::stringstream ss;
            ss << "Buffer " << utils::DebugNameToString(bufferDesc.debugName)
                << " is placed in heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at invalid alignment: required alignment to " << memReq.alignment << " bytes,"
                << " actual offset is " << offset << " bytes";

            error(ss.str());
            return false;
        }

        return m_Device->bindBufferMemory(buffer, heap, offset);
    }

    BufferHandle DeviceWrapper::createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)
    {
        return m_Device->createHandleForNativeBuffer(objectType, buffer, desc);
    }

    ShaderHandle DeviceWrapper::createShader(const ShaderDesc& d, const void* binary, const size_t binarySize)
    {
        return m_Device->createShader(d, binary, binarySize);
    }
    
    ShaderHandle DeviceWrapper::createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, uint32_t numConstants)
    {
        if (!m_Device->queryFeatureSupport(Feature::ShaderSpecializations))
        {
            std::stringstream ss;
            ss << "The current graphics API (" << utils::GraphicsAPIToString(m_Device->getGraphicsAPI()) << ") "
                "doesn't support shader specializations";
            error(ss.str());
            return nullptr;
        }

        if (constants == nullptr || numConstants == 0)
        {
            error("Both 'constants' and 'numConstatns' must be non-zero in createShaderSpecialization");
            return nullptr;
        }

        if (baseShader == nullptr)
        {
            error("baseShader must be non-null in createShaderSpecialization");
            return nullptr;
        }

        return m_Device->createShaderSpecialization(baseShader, constants, numConstants);
    }

    nvrhi::ShaderLibraryHandle DeviceWrapper::createShaderLibrary(const void* binary, const size_t binarySize)
    {
        return m_Device->createShaderLibrary(binary, binarySize);
    }
    
    SamplerHandle DeviceWrapper::createSampler(const SamplerDesc& d)
    {
        return m_Device->createSampler(d);
    }

    InputLayoutHandle DeviceWrapper::createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader)
    {
        return m_Device->createInputLayout(d, attributeCount, vertexShader);
    }

    EventQueryHandle DeviceWrapper::createEventQuery()
    {
        return m_Device->createEventQuery();
    }

    void DeviceWrapper::setEventQuery(IEventQuery* query, CommandQueue queue)
    {
        m_Device->setEventQuery(query, queue);
    }

    bool DeviceWrapper::pollEventQuery(IEventQuery* query)
    {
        return m_Device->pollEventQuery(query);
    }

    void DeviceWrapper::waitEventQuery(IEventQuery* query)
    {
        m_Device->waitEventQuery(query);
    }

    void DeviceWrapper::resetEventQuery(IEventQuery* query)
    {
        m_Device->resetEventQuery(query);
    }

    TimerQueryHandle DeviceWrapper::createTimerQuery()
    {
        return m_Device->createTimerQuery();
    }

    bool DeviceWrapper::pollTimerQuery(ITimerQuery* query)
    {
        return m_Device->pollTimerQuery(query);
    }

    float DeviceWrapper::getTimerQueryTime(ITimerQuery* query)
    {
        return m_Device->getTimerQueryTime(query);
    }

    void DeviceWrapper::resetTimerQuery(ITimerQuery* query)
    {
        return m_Device->resetTimerQuery(query);
    }

    GraphicsAPI DeviceWrapper::getGraphicsAPI()
    {
        return m_Device->getGraphicsAPI();
    }

    FramebufferHandle DeviceWrapper::createFramebuffer(const FramebufferDesc& desc)
    {
        return m_Device->createFramebuffer(desc);
    }

    template<typename DescType>
    void FillShaderBindingSetFromDesc(IMessageCallback* messageCallback, const DescType& desc, ShaderBindingSet& bindingSet, ShaderBindingSet& duplicates)
    {
        for (const auto& item : desc)
        {
            switch (item.type)
            {
            case ResourceType::Texture_SRV:
            case ResourceType::TypedBuffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
            case ResourceType::RawBuffer_SRV:
            case ResourceType::RayTracingAccelStruct:
                if (bindingSet.SRV[item.slot])
                {
                    duplicates.SRV[item.slot] = true;
                }
                else
                {
                    bindingSet.SRV[item.slot] = true;
                    bindingSet.rangeSRV.add(item.slot);
                }
                break;

            case ResourceType::Texture_UAV:
            case ResourceType::TypedBuffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
            case ResourceType::RawBuffer_UAV:
                if (bindingSet.UAV[item.slot])
                {
                    duplicates.UAV[item.slot] = true;
                }
                else
                {
                    bindingSet.UAV[item.slot] = true;
                    bindingSet.rangeUAV.add(item.slot);
                }
                break;

            case ResourceType::ConstantBuffer:
            case ResourceType::VolatileConstantBuffer:
            case ResourceType::PushConstants:
                if (bindingSet.CB[item.slot])
                {
                    duplicates.CB[item.slot] = true;
                }
                else
                {
                    bindingSet.CB[item.slot] = true;

                    if (item.type == ResourceType::VolatileConstantBuffer)
                        ++bindingSet.numVolatileCBs;

                    bindingSet.rangeCB.add(item.slot);
                }
                break;

            case ResourceType::Sampler:
                if (bindingSet.Sampler[item.slot])
                {
                    duplicates.Sampler[item.slot] = true;
                }
                else
                {
                    bindingSet.Sampler[item.slot] = true;
                    bindingSet.rangeSampler.add(item.slot);
                }
                break;
            
            case ResourceType::None:
            case ResourceType::Count:
            default: {
                std::stringstream ss;
                ss << "Invalid layout item type " << (int)item.type;
                messageCallback->message(MessageSeverity::Error, ss.str().c_str());
                break;
            }
            }
        }
    }

    template<size_t N>
    void BitsetToStream(const std::bitset<N>& bits, std::ostream& os, const char* prefix, bool &first)
    {
        if (bits.any())
        {
            for (uint32_t slot = 0; slot < bits.size(); slot++)
            {
                if (bits[slot])
                {
                    if (!first)
                        os << ", ";
                    os << prefix << slot;
                    first = false;
                }
            }
        }
    }

    template<typename ItemType, typename DescType>
    const ItemType* SelectShaderStage(const DescType& desc, ShaderType stage)
    {
        switch (stage)
        {
        case ShaderType::Vertex: return &desc.VS;
        case ShaderType::Hull: return &desc.HS;
        case ShaderType::Domain: return &desc.DS;
        case ShaderType::Geometry: return &desc.GS;
        case ShaderType::Pixel: return &desc.PS;
        case ShaderType::Compute: return &desc.CS;
        default:
            utils::InvalidEnum();
            return nullptr;
        }
    }

    template<typename ItemType, typename DescType>
    const ItemType* SelectGraphicsShaderStage(const DescType& desc, ShaderType stage)
    {
        switch (stage)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ShaderType::Vertex: return &desc.VS;
        case ShaderType::Hull: return &desc.HS;
        case ShaderType::Domain: return &desc.DS;
        case ShaderType::Geometry: return &desc.GS;
        case ShaderType::Pixel: return &desc.PS;
        default: 
            utils::InvalidEnum();
            return nullptr;
        }
    }

    template<typename ItemType, typename DescType>
    const ItemType* SelectMeshletShaderStage(const DescType& desc, ShaderType stage)
    {
        switch (stage)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ShaderType::Amplification: return &desc.AS;
        case ShaderType::Mesh: return &desc.MS;
        case ShaderType::Pixel: return &desc.PS;
        default:
            utils::InvalidEnum();
            return nullptr;
        }
    }

    static const ShaderType g_GraphicsShaderStages[] = {
        ShaderType::Vertex,
        ShaderType::Hull,
        ShaderType::Domain,
        ShaderType::Geometry,
        ShaderType::Pixel
    };

    static const ShaderType g_MeshletShaderStages[] = {
        ShaderType::Amplification,
        ShaderType::Mesh,
        ShaderType::Pixel
    };
    
    bool DeviceWrapper::validatePipelineBindingLayouts(const static_vector<BindingLayoutHandle, c_MaxBindingLayouts>& bindingLayouts, const std::vector<IShader*>& shaders, GraphicsAPI api) const
    {
        const int numBindingLayouts = int(bindingLayouts.size());
        bool anyErrors = false;
        bool anyDuplicateBindings = false;
        bool anyOverlappingBindings = false;
        std::stringstream ssDuplicateBindings;
        std::stringstream ssOverlappingBindings;

        for (IShader* shader : shaders)
        {
            ShaderType stage = shader->getDesc().shaderType;

            static_vector<ShaderBindingSet, c_MaxBindingLayouts> bindingsPerLayout;
            static_vector<ShaderBindingSet, c_MaxBindingLayouts> duplicatesPerLayout;
            bindingsPerLayout.resize(numBindingLayouts);
            duplicatesPerLayout.resize(numBindingLayouts);

            // Accumulate binding information about the stage from all layouts

            for (int layoutIndex = 0; layoutIndex < numBindingLayouts; layoutIndex++)
            {
                if (bindingLayouts[layoutIndex] == nullptr)
                {
                    std::stringstream ss;
                    ss << "Binding layout in slot " << layoutIndex << " is NULL";
                    error(ss.str());
                    anyErrors = true;
                }
                else
                {
                    const BindingLayoutDesc* layoutDesc = bindingLayouts[layoutIndex]->getDesc();

                    if (layoutDesc)
                    {
                        if (api != GraphicsAPI::VULKAN)
                        {
                            // Visibility does not apply to Vulkan
                            if (!(layoutDesc->visibility & stage))
                                continue;
                        }

                        if (layoutDesc->registerSpace != 0)
                        {
                            continue; // TODO: add support for multiple register spaces. 
                                      // Their indices can go up to 0xffffffef, according to the spec, so a vector won't work.
                                      // https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#note-about-register-space
                        }

                        FillShaderBindingSetFromDesc(m_MessageCallback, layoutDesc->bindings, bindingsPerLayout[layoutIndex], duplicatesPerLayout[layoutIndex]);

                        // Layouts with duplicates should not have passed validation in createBindingLayout
                        assert(!duplicatesPerLayout[layoutIndex].any());
                    }
                }
            }

            // Check for bindings to an unused shader stage

            if (shader == nullptr)
            {
                for (int layoutIndex = 0; layoutIndex < numBindingLayouts; layoutIndex++)
                {
                    if (bindingsPerLayout[layoutIndex].any())
                    {
                        std::stringstream ss;
                        ss << "Binding layout in slot " << layoutIndex <<" has bindings for a "
                            << utils::ShaderStageToString(stage) << " shader, which is not used in the pipeline";
                        error(ss.str());
                        anyErrors = true;
                    }
                }
            }

            // Check for multiple layouts declaring the same bindings

            if (numBindingLayouts > 1)
            {
                ShaderBindingSet bindings = bindingsPerLayout[0];
                ShaderBindingSet duplicates;

                for (int layoutIndex = 1; layoutIndex < numBindingLayouts; layoutIndex++)
                {
                    duplicates.SRV |= bindings.SRV & bindingsPerLayout[layoutIndex].SRV;
                    duplicates.Sampler |= bindings.Sampler & bindingsPerLayout[layoutIndex].Sampler;
                    duplicates.UAV |= bindings.UAV & bindingsPerLayout[layoutIndex].UAV;
                    duplicates.CB |= bindings.CB & bindingsPerLayout[layoutIndex].CB;

                    bindings.SRV |= bindingsPerLayout[layoutIndex].SRV;
                    bindings.Sampler |= bindingsPerLayout[layoutIndex].Sampler;
                    bindings.UAV |= bindingsPerLayout[layoutIndex].UAV;
                    bindings.CB |= bindingsPerLayout[layoutIndex].CB;
                }

                if (duplicates.any())
                {
                    if (!anyDuplicateBindings)
                        ssDuplicateBindings << "Same bindings defined by more than one layout in this pipeline:";

                    ssDuplicateBindings << std::endl << utils::ShaderStageToString(stage) << ": " << duplicates;

                    anyDuplicateBindings = true;
                }
                else
                {
                    // Check for overlapping layouts.
                    // Do this only when there are no duplicates, as with duplicates the layouts will always overlap.

                    bool overlapSRV = false;
                    bool overlapSampler = false;
                    bool overlapUAV = false;
                    bool overlapCB = false;

                    for (int i = 0; i < numBindingLayouts - 1; i++)
                    {
                        const ShaderBindingSet& set1 = bindingsPerLayout[i];

                        for (int j = i + 1; j < numBindingLayouts; j++)
                        {
                            const ShaderBindingSet& set2 = bindingsPerLayout[j];

                            overlapSRV = overlapSRV || set1.rangeSRV.overlapsWith(set2.rangeSRV);
                            overlapSampler = overlapSampler || set1.rangeSampler.overlapsWith(set2.rangeSampler);
                            overlapUAV = overlapUAV || set1.rangeUAV.overlapsWith(set2.rangeUAV);
                            overlapCB = overlapCB || set1.rangeCB.overlapsWith(set2.rangeCB);
                        }
                    }

                    if (overlapSRV || overlapSampler || overlapUAV || overlapCB)
                    {
                        if (!anyOverlappingBindings)
                            ssOverlappingBindings << "Binding layouts have overlapping register ranges:";

                        ssOverlappingBindings << std::endl << utils::ShaderStageToString(stage) << ": ";

                        bool first = true;
                        auto append = [&first, &ssOverlappingBindings](bool value, const char* text)
                        {
                            if (value)
                            {
                                if (!first) ssOverlappingBindings << ", ";
                                ssOverlappingBindings << text;
                                first = false;
                            }
                        };

                        append(overlapSRV, "SRV");
                        append(overlapSampler, "Sampler");
                        append(overlapUAV, "UAV");
                        append(overlapCB, "CB");

                        anyOverlappingBindings = true;
                    }
                }
            }
        }

        if (anyDuplicateBindings)
        {
            error(ssDuplicateBindings.str());
            anyErrors = true;
        }

        if (anyOverlappingBindings)
        {
            error(ssOverlappingBindings.str());
            anyErrors = true;
        }

        int pushConstantCount = 0;
        uint32_t pushConstantSize = 0;

        for (int layoutIndex = 0; layoutIndex < numBindingLayouts; layoutIndex++)
        {
            const BindingLayoutDesc* layoutDesc = bindingLayouts[layoutIndex]->getDesc();
            if (layoutDesc)
            {
                for (const auto& item : layoutDesc->bindings)
                {
                    if (item.type == ResourceType::PushConstants)
                    {
                        pushConstantCount++;
                        pushConstantSize = std::max(pushConstantSize, uint32_t(item.size));
                    }
                }
            }
        }

        if (pushConstantCount > 1)
        {
            std::stringstream errorStream;
            errorStream << "Binding layout contains more than one (" << pushConstantCount << ") push constant blocks";
            error(errorStream.str());
            anyErrors = true;
        }

        if (pushConstantSize > c_MaxPushConstantSize)
        {
            std::stringstream errorStream;
            errorStream << "Binding layout declares " << pushConstantSize << " bytes of push constant data, "
                "which exceeds the limit of " << c_MaxPushConstantSize << " bytes";
            error(errorStream.str());
            anyErrors = true;
        }

        return !anyErrors;
    }

    bool DeviceWrapper::validateShaderType(ShaderType expected, const ShaderDesc& shaderDesc, const char* function) const
    {
        if (expected == shaderDesc.shaderType)
            return true;

        std::stringstream ss;
        ss << "Unexpected shader type used in " << function << ": expected shaderType = "
            << utils::ShaderStageToString(expected) << ", actual shaderType = " << utils::ShaderStageToString(shaderDesc.shaderType)
            << " in " << utils::DebugNameToString(shaderDesc.debugName) << ":" << shaderDesc.entryName;
        error(ss.str());
        return false;
    }

    bool DeviceWrapper::validateRenderState(const RenderState& renderState, IFramebuffer* fb) const
    {
        if (!fb)
        {
            error("framebuffer is NULL");
            return false;
        }

        const auto fbDesc = fb->getDesc();

        if (renderState.depthStencilState.depthTestEnable ||
            renderState.depthStencilState.stencilEnable)
        {
            if (!fbDesc.depthAttachment.valid())
            {
                error("The depth-stencil state indicates that depth or stencil operations are used, "
                    "but the framebuffer has no depth attachment.");
                return false;
            }
        }

        if ((renderState.depthStencilState.depthTestEnable && renderState.depthStencilState.depthWriteEnable) ||
            (renderState.depthStencilState.stencilEnable && renderState.depthStencilState.stencilWriteMask != 0))
        {
            if (fbDesc.depthAttachment.isReadOnly)
            {
                error("The depth-stencil state indicates that depth or stencil writes are used, "
                    "but the framebuffer's depth attachment is read-only.");
                return false;
            }
        }
        else if (renderState.depthStencilState.depthTestEnable ||renderState.depthStencilState.stencilEnable)
        {
            if (!fbDesc.depthAttachment.isReadOnly)
            {
                warning("The depth-stencil state indicates read-only depth and stencil, "
                    "but the framebuffer has a read-write depth attachment, which is suboptimal.");
            }
        }

        return true;
    }

    GraphicsPipelineHandle DeviceWrapper::createGraphicsPipeline(const GraphicsPipelineDesc& pipelineDesc, IFramebuffer* fb)
    {
        std::vector<IShader*> shaders;

        for (ShaderType stage : g_GraphicsShaderStages)
        {
            IShader* shader = *SelectGraphicsShaderStage<ShaderHandle, GraphicsPipelineDesc>(pipelineDesc, stage);
            if (shader)
            {
                shaders.push_back(shader);

                if (!validateShaderType(stage, shader->getDesc(), "createGraphicsPipeline"))
                    return nullptr;
            }
        }

        if (!validatePipelineBindingLayouts(pipelineDesc.bindingLayouts, shaders, m_Device->getGraphicsAPI()))
            return nullptr;

        if (!validateRenderState(pipelineDesc.renderState, fb))
            return nullptr;

        return m_Device->createGraphicsPipeline(pipelineDesc, fb);
    }

    ComputePipelineHandle DeviceWrapper::createComputePipeline(const ComputePipelineDesc& pipelineDesc)
    {
        if (!pipelineDesc.CS)
        {
            error("createComputePipeline: CS = NULL");
            return nullptr;
        }

        std::vector<IShader*> shaders = { pipelineDesc.CS };
        
        if (!validatePipelineBindingLayouts(pipelineDesc.bindingLayouts, shaders, m_Device->getGraphicsAPI()))
            return nullptr;

        if (!validateShaderType(ShaderType::Compute, pipelineDesc.CS->getDesc(), "createComputePipeline"))
            return nullptr;

        return m_Device->createComputePipeline(pipelineDesc);
    }

    MeshletPipelineHandle DeviceWrapper::createMeshletPipeline(const MeshletPipelineDesc& pipelineDesc, IFramebuffer* fb)
    {
        std::vector<IShader*> shaders;

        for (ShaderType stage : g_MeshletShaderStages)
        {
            IShader* shader = *SelectMeshletShaderStage<ShaderHandle, MeshletPipelineDesc>(pipelineDesc, stage);
            if (shader)
            {
                shaders.push_back(shader);

                if (!validateShaderType(stage, shader->getDesc(), "createMeshletPipeline"))
                    return nullptr;
            }
        }

        if (!validatePipelineBindingLayouts(pipelineDesc.bindingLayouts, shaders, m_Device->getGraphicsAPI()))
            return nullptr;

        if (!validateRenderState(pipelineDesc.renderState, fb))
            return nullptr;

        return m_Device->createMeshletPipeline(pipelineDesc, fb);
    }

    nvrhi::rt::PipelineHandle DeviceWrapper::createRayTracingPipeline(const rt::PipelineDesc& desc)
    {
        return m_Device->createRayTracingPipeline(desc);
    }

    BindingLayoutHandle DeviceWrapper::createBindingLayout(const BindingLayoutDesc& desc)
    {
        std::stringstream errorStream;
        bool anyErrors = false;

        ShaderBindingSet bindings;
        ShaderBindingSet duplicates;

        FillShaderBindingSetFromDesc(m_MessageCallback, desc.bindings, bindings, duplicates);

        if (desc.visibility == ShaderType::None)
        {
            errorStream << "Cannot create a binding layout with visibility = None" << std::endl;
            anyErrors = true;
        }

        if (duplicates.any())
        {
            errorStream << "Binding layout contains duplicate bindings: " << duplicates << std::endl;
            anyErrors = true;
        }

        if (bindings.numVolatileCBs > c_MaxVolatileConstantBuffersPerLayout)
        {
            errorStream << "Binding layout contains too many volatile CBs (" << bindings.numVolatileCBs << ")" << std::endl;
            anyErrors = true;
        }

        uint32_t noneItemCount = 0;
        uint32_t pushConstantCount = 0;
        for (const BindingLayoutItem& item : desc.bindings)
        {
            if (item.type == ResourceType::None)
                noneItemCount++;

            if (item.type == ResourceType::PushConstants)
            {
                if (item.size == 0)
                {
                    errorStream << "Push constant block size cannot be null" << std::endl;
                    anyErrors = true;
                }

                if (item.size > c_MaxPushConstantSize)
                {
                    errorStream << "Push constant block size (" << item.size << ") cannot exceed " << c_MaxPushConstantSize << " bytes" << std::endl;
                    anyErrors = true;
                }

                if ((item.size % 4) != 0)
                {
                    errorStream << "Push constant block size (" << item.size << ") must be a multiple of 4" << std::endl;
                    anyErrors = true;
                }

                pushConstantCount++;
            }
        }

        if (noneItemCount)
        {
            errorStream << "Binding layout contains " << noneItemCount << " item(s) with type = None" << std::endl;
            anyErrors = true;
        }

        if (pushConstantCount > 1)
        {
            errorStream << "Binding layout contains more than one (" << pushConstantCount << ") push constant blocks" << std::endl;
            anyErrors = true;
        }

        if (m_Device->getGraphicsAPI() != GraphicsAPI::D3D12)
        {
            if (desc.registerSpace != 0)
            {
                errorStream << "Binding layout registerSpace = " << desc.registerSpace << ", which is unsupported by the current backend" << std::endl;
                anyErrors = true;
            }
        }

        if (anyErrors)
        {
            error(errorStream.str());
            return nullptr;
        }

        return m_Device->createBindingLayout(desc);
    }

    BindingLayoutHandle DeviceWrapper::createBindlessLayout(const BindlessLayoutDesc& desc)
    {
        std::stringstream errorStream;
        bool anyErrors = false;

        if (desc.visibility == ShaderType::None)
        {
            errorStream << "Cannot create a bindless layout with visibility = None" << std::endl;
            anyErrors = true;
        }

        if (desc.registerSpaces.empty())
        {
            errorStream << "Bindless layout has no register spaces assigned" << std::endl;
            anyErrors = true;
        }

        if (desc.maxCapacity == 0)
        {
            errorStream << "Bindless layout has maxCapacity = 0" << std::endl;
            anyErrors = true;
        }

        for (const BindingLayoutItem& item : desc.registerSpaces)
        {
            switch (item.type)
            {
            case ResourceType::Texture_SRV:
            case ResourceType::TypedBuffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
            case ResourceType::RawBuffer_SRV:
            case ResourceType::RayTracingAccelStruct:
            case ResourceType::ConstantBuffer:
            case ResourceType::Texture_UAV:
            case ResourceType::TypedBuffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
            case ResourceType::RawBuffer_UAV:
                continue;
            case ResourceType::VolatileConstantBuffer:
                errorStream << "Volatile CBs cannot be placed into a bindless layout (slot " << item.slot << ")" << std::endl;
                anyErrors = true;
                break;
            case ResourceType::Sampler:
                errorStream << "Bindless samplers are not implemented (slot " << item.slot << ")" << std::endl;
                anyErrors = true;
                break;
            case ResourceType::PushConstants:
                errorStream << "Push constants cannot be placed into a bindless layout (slot " << item.slot << ")" << std::endl;
                anyErrors = true;
                break;

            case ResourceType::None:
            case ResourceType::Count:
            default:
                errorStream << "Invalid resource type " << int(item.type) << " in slot " << item.slot << std::endl;
                anyErrors = true;
                break;
            }
        }

        if (anyErrors)
        {
            error(errorStream.str());
            return nullptr;
        }

        return m_Device->createBindlessLayout(desc);
    }

    static bool textureDimensionsCompatible(TextureDimension resourceDimension, TextureDimension viewDimension)
    {
        if (resourceDimension == viewDimension)
            return true;

        if (resourceDimension == TextureDimension::Texture3D)
            return viewDimension == TextureDimension::Texture2DArray;

        if (resourceDimension == TextureDimension::TextureCube || resourceDimension == TextureDimension::TextureCubeArray)
            return viewDimension == TextureDimension::Texture2DArray;

        return false;
    }

    bool DeviceWrapper::validateBindingSetItem(const BindingSetItem& binding, bool isDescriptorTable, std::stringstream& errorStream) const
    {
        switch (binding.type)
        {
        case ResourceType::None:
            if (!isDescriptorTable)
            {
                errorStream << "ResourceType::None bindings are not allowed in binding sets." << std::endl;
                return false;
            }
            break;

        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:
        {
            ITexture* texture = checked_cast<ITexture*>(binding.resourceHandle);

            if (texture == nullptr)
            {
                errorStream << "Null resource bindings are not allowed for textures." << std::endl;
                return false;
            }

            const TextureDesc& desc = texture->getDesc();

            TextureSubresourceSet subresources = binding.subresources.resolve(desc, false);
            if (subresources.numArraySlices == 0 || subresources.numMipLevels == 0)
            {
                errorStream << "The specified subresource set (BaseMipLevel = " << binding.subresources.baseMipLevel
                    << ", NumMipLevels = " << binding.subresources.numMipLevels
                    << ", BaseArraySlice = " << binding.subresources.baseArraySlice
                    << ", NumArraySlices = " << binding.subresources.numArraySlices
                    << ") does not intersect with the texture being bound (" << utils::DebugNameToString(desc.debugName)
                    << ", MipLevels = " << desc.mipLevels
                    << ", ArraySize = " << desc.arraySize << ")" << std::endl;

                return false;
            }

            if ((binding.type == ResourceType::Texture_UAV) && !desc.isUAV)
            {
                errorStream << "Texture " << utils::DebugNameToString(desc.debugName)
                    << " cannot be used as a UAV because it does not have the isUAV flag set." << std::endl;
                return false;
            }

            if (binding.dimension != TextureDimension::Unknown)
            {
                if (!textureDimensionsCompatible(desc.dimension, binding.dimension))
                {
                    errorStream << "Requested binding dimension (" << utils::TextureDimensionToString(binding.dimension) << ") "
                        "is incompatible with the dimension (" << utils::TextureDimensionToString(desc.dimension) << ") "
                        "of texture " << utils::DebugNameToString(desc.debugName) << std::endl;
                    return false;
                }
            }

            break;
        }

        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::RawBuffer_UAV:
        case ResourceType::ConstantBuffer:
        case ResourceType::VolatileConstantBuffer:
        {
            IBuffer* buffer = checked_cast<IBuffer*>(binding.resourceHandle);

            if (buffer == nullptr && binding.type != ResourceType::TypedBuffer_SRV && m_Device->getGraphicsAPI() != GraphicsAPI::VULKAN)
            {
                errorStream << "Null resource bindings are not allowed for buffers, unless it's a "
                    "TypedBuffer_SRV type binding on DX11 or DX12." << std::endl;
                return false;
            }

            if (buffer == nullptr)
                return true;

            const BufferDesc& desc = buffer->getDesc();

            bool isTypedView = (binding.type == ResourceType::TypedBuffer_SRV) || (binding.type == ResourceType::TypedBuffer_UAV);
            bool isStructuredView = (binding.type == ResourceType::StructuredBuffer_SRV) || (binding.type == ResourceType::StructuredBuffer_UAV);
            bool isRawView = (binding.type == ResourceType::RawBuffer_SRV) || (binding.type == ResourceType::RawBuffer_UAV);
            bool isUAV = (binding.type == ResourceType::TypedBuffer_UAV) || (binding.type == ResourceType::StructuredBuffer_UAV) || (binding.type == ResourceType::RawBuffer_UAV);
            bool isConstantView = (binding.type == ResourceType::ConstantBuffer) || (binding.type == ResourceType::VolatileConstantBuffer);
            
            if (isTypedView && !desc.canHaveTypedViews)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as " << utils::ResourceTypeToString(binding.type)
                    << " because it doesn't support typed views (BufferDesc::canHaveTypedViews)." << std::endl;
                return false;
            }

            if (isStructuredView && desc.structStride == 0)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as " << utils::ResourceTypeToString(binding.type)
                    << " because it doesn't have structStride specified at creation." << std::endl;
                return false;
            }

            if (isRawView && !desc.canHaveRawViews)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as " << utils::ResourceTypeToString(binding.type)
                    << " because it doesn't support raw views (BufferDesc::canHaveRawViews)." << std::endl;
                return false;
            }

            if (isUAV && !desc.canHaveUAVs)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as " << utils::ResourceTypeToString(binding.type)
                    << " because it doesn't support unordeded access views (BufferDesc::canHaveUAVs)." << std::endl;
                return false;
            }

            if (isConstantView && !desc.isConstantBuffer)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as " << utils::ResourceTypeToString(binding.type)
                    << " because it doesn't support constant buffer views (BufferDesc::isConstantBuffer)." << std::endl;
                return false;
            }

            if (binding.type == ResourceType::ConstantBuffer && desc.isVolatile)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as a regular ConstantBuffer because it's a VolatileConstantBuffer." << std::endl;
                return false;
            }

            if (binding.type == ResourceType::VolatileConstantBuffer && !desc.isVolatile)
            {
                errorStream << "Cannot bind buffer " << utils::DebugNameToString(desc.debugName)
                    << " as a VolatileConstantBuffer because it's a regular ConstantBuffer." << std::endl;
                return false;
            }

            if (isTypedView && (binding.format == Format::UNKNOWN && desc.format == Format::UNKNOWN))
            {
                errorStream << "Both binding for typed buffer " << utils::DebugNameToString(desc.debugName)
                    << " and its BufferDesc have format == UNKNOWN." << std::endl;
                return false;
            }

            break;
        }

        case ResourceType::Sampler:
            if (binding.resourceHandle == nullptr)
            {
                errorStream << "Null resource bindings are not allowed for samplers." << std::endl;
                return false;
            }
            break;

        case ResourceType::RayTracingAccelStruct:
            if (binding.resourceHandle == nullptr)
            {
                errorStream << "Null resource bindings are not allowed for ray tracing acceleration structures." << std::endl;
                return false;
            }
            break;

        case ResourceType::PushConstants:
            if (isDescriptorTable)
            {
                errorStream << "Push constants cannot be used in a descriptor table." << std::endl;
                return false;
            }
            if (binding.resourceHandle != nullptr)
            {
                errorStream << "Push constants cannot have a resource specified." << std::endl;
                return false;
            }
            if (binding.range.byteSize == 0)
            {
                errorStream << "Push constants must have nonzero size specified." << std::endl;
                return false;
            }
            break;

        case ResourceType::Count:
        default:
            errorStream << "Unrecognized resourceType = " << uint32_t(binding.type) << std::endl;
            return false;
        }

        return true;
    }

    BindingSetHandle DeviceWrapper::createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)
    {
        if (layout == nullptr)
        {
            error("Cannot create a binding set without a valid layout");
            return nullptr;
        }

        const BindingLayoutDesc* layoutDesc = layout->getDesc();
        if (!layoutDesc)
        {
            error("Cannot create a binding set from a bindless layout");
            return nullptr;
        }

        std::stringstream errorStream;
        bool anyErrors = false;

        ShaderBindingSet layoutBindings;
        ShaderBindingSet layoutDuplicates;

        FillShaderBindingSetFromDesc(m_MessageCallback, layoutDesc->bindings, layoutBindings, layoutDuplicates);

        ShaderBindingSet setBindings;
        ShaderBindingSet setDuplicates;

        FillShaderBindingSetFromDesc(m_MessageCallback, desc.bindings, setBindings, setDuplicates);

        ShaderBindingSet declaredNotBound;
        ShaderBindingSet boundNotDeclared;

        declaredNotBound.SRV = layoutBindings.SRV & ~setBindings.SRV;
        declaredNotBound.Sampler = layoutBindings.Sampler & ~setBindings.Sampler;
        declaredNotBound.UAV = layoutBindings.UAV & ~setBindings.UAV;
        declaredNotBound.CB = layoutBindings.CB & ~setBindings.CB;

        boundNotDeclared.SRV = ~layoutBindings.SRV & setBindings.SRV;
        boundNotDeclared.Sampler = ~layoutBindings.Sampler & setBindings.Sampler;
        boundNotDeclared.UAV = ~layoutBindings.UAV & setBindings.UAV;
        boundNotDeclared.CB = ~layoutBindings.CB & setBindings.CB;

        if (declaredNotBound.any())
        {
            errorStream << "Bindings declared in the layout are not present in the binding set: " << declaredNotBound << std::endl;
            anyErrors = true;
        }

        if (boundNotDeclared.any())
        {
            errorStream << "Bindings in the binding set are not declared in the layout: " << boundNotDeclared << std::endl;
            anyErrors = true;
        }

        if (setDuplicates.any())
        {
            errorStream << "Binding set contains duplicate bindings: " << setDuplicates << std::endl;
            anyErrors = true;
        }

        if (desc.bindings.size() != layoutDesc->bindings.size())
        {
            errorStream << "The number of items in the binding set descriptor (" << desc.bindings.size() << ") "
                "is different from the number of items in the layout (" << layoutDesc->bindings.size() << ")" << std::endl;
            anyErrors = true;
        }
        else
        {
            for (size_t index = 0; index < desc.bindings.size(); index++)
            {
                const BindingSetItem& setItem = desc.bindings[index];
                const BindingLayoutItem& layoutItem = layoutDesc->bindings[index];
                
                if ((setItem.slot != layoutItem.slot) || (setItem.type != layoutItem.type))
                {
                    errorStream << "Binding set item " << index << " doesn't match layout item " << index << ": "
                        "expected " << utils::ResourceTypeToString(layoutItem.type) << "(" << layoutItem.slot << "), "
                        "received " << utils::ResourceTypeToString(setItem.type) << "(" << setItem.slot << ")" << std::endl;

                    anyErrors = true;
                }

                if (!validateBindingSetItem(setItem, false, errorStream))
                    anyErrors = true;
            }
        }

        if (anyErrors)
        {
            error(errorStream.str());
            return nullptr;
        }

        // Unwrap the resources
        BindingSetDesc patchedDesc = desc;
        for (auto& binding : patchedDesc.bindings)
        {
            binding.resourceHandle = unwrapResource(binding.resourceHandle);
        }

        return m_Device->createBindingSet(patchedDesc, layout);
    }

    DescriptorTableHandle DeviceWrapper::createDescriptorTable(IBindingLayout* layout)
    {
        if (!layout->getBindlessDesc()) 
        {
            error("Descriptor tables can only be created with bindless layouts");
            return nullptr;
        }

        return m_Device->createDescriptorTable(layout);
    }

    void DeviceWrapper::resizeDescriptorTable(IDescriptorTable* descriptorTable, uint32_t newSize, bool keepContents)
    {
        m_Device->resizeDescriptorTable(descriptorTable, newSize, keepContents);
    }

    bool DeviceWrapper::writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item)
    {
        std::stringstream errorStream;
        
        if (!validateBindingSetItem(item, true, errorStream))
        {
            error(errorStream.str());
            return false;
        }

        BindingSetItem patchedItem = item;
        patchedItem.resourceHandle = unwrapResource(patchedItem.resourceHandle);

        return m_Device->writeDescriptorTable(descriptorTable, patchedItem);
    }

    rt::AccelStructHandle DeviceWrapper::createAccelStruct(const rt::AccelStructDesc& desc)
    {
        rt::AccelStructHandle as = m_Device->createAccelStruct(desc);

        if (!as)
            return nullptr;

        if ((desc.buildFlags & rt::AccelStructBuildFlags::AllowCompaction) != 0 &&
            desc.isTopLevel)
        {
            std::stringstream ss;
            ss << "Cannot create TLAS " << utils::DebugNameToString(desc.debugName)
                << " with the AllowCompaction flag set: compaction is not supported for TLAS'es";
            error(ss.str());
            return nullptr;
        }

        if ((desc.buildFlags & rt::AccelStructBuildFlags::AllowUpdate) != 0 &&
            (desc.buildFlags & rt::AccelStructBuildFlags::AllowCompaction) != 0)
        {
            std::stringstream ss;
            ss << "Cannot create AccelStruct " << utils::DebugNameToString(desc.debugName)
                << " with incompatible flags: AllowUpdate and AllowCompaction";
            error(ss.str());
            return nullptr;
        }

        AccelStructWrapper* wrapper = new AccelStructWrapper(as);
        wrapper->isTopLevel = desc.isTopLevel;
        wrapper->allowUpdate = !!(desc.buildFlags & rt::AccelStructBuildFlags::AllowUpdate);
        wrapper->allowCompaction = !!(desc.buildFlags & rt::AccelStructBuildFlags::AllowCompaction);
        wrapper->maxInstances = desc.topLevelMaxInstances;
        
        return rt::AccelStructHandle::Create(wrapper);
    }

    MemoryRequirements DeviceWrapper::getAccelStructMemoryRequirements(rt::IAccelStruct* as)
    {
        if (as == nullptr)
        {
            error("getAccelStructMemoryRequirements: as is NULL");
            return MemoryRequirements();
        }

        AccelStructWrapper* wrapper = dynamic_cast<AccelStructWrapper*>(as);
        if (wrapper)
            as = wrapper->getUnderlyingObject();

        const MemoryRequirements memReq = m_Device->getAccelStructMemoryRequirements(as);
        
        return memReq;
    }

    bool DeviceWrapper::bindAccelStructMemory(rt::IAccelStruct* as, IHeap* heap, uint64_t offset)
    {
        if (as == nullptr)
        {
            error("bindAccelStructMemory: texture is NULL");
            return false;
        }

        if (heap == nullptr)
        {
            error("bindAccelStructMemory: heap is NULL");
            return false;
        }

        AccelStructWrapper* wrapper = dynamic_cast<AccelStructWrapper*>(as);
        if (wrapper)
            as = wrapper->getUnderlyingObject();

        const HeapDesc& heapDesc = heap->getDesc();
        const rt::AccelStructDesc& asDesc = as->getDesc();

        if (!asDesc.isVirtual)
        {
            std::stringstream ss;
            ss << "Cannot perform bindAccelStructMemory on AccelStruct " << utils::DebugNameToString(asDesc.debugName)
                << " because it was created with isVirtual = false";

            error(ss.str());
            return false;
        }

        MemoryRequirements memReq = m_Device->getAccelStructMemoryRequirements(as);

        if (offset + memReq.size > heapDesc.capacity)
        {
            std::stringstream ss;
            ss << "AccelStruct " << utils::DebugNameToString(asDesc.debugName)
                << " does not fit into heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at offset " << offset << " because it requires " << memReq.size << " bytes,"
                << " and the heap capacity is " << heapDesc.capacity << " bytes";

            error(ss.str());
            return false;
        }

        if (memReq.alignment != 0 && (offset % memReq.alignment) != 0)
        {
            std::stringstream ss;
            ss << "AccelStruct " << utils::DebugNameToString(asDesc.debugName)
                << " is placed in heap " << utils::DebugNameToString(heapDesc.debugName)
                << " at invalid alignment: required alignment to " << memReq.alignment << " bytes,"
                << " actual offset is " << offset << " bytes";

            error(ss.str());
            return false;
        }

        return m_Device->bindAccelStructMemory(as, heap, offset);
    }

    CommandListHandle DeviceWrapper::createCommandList(const CommandListParameters& params)
    {
        switch(params.queueType)
        {
        case CommandQueue::Graphics:
            // Assume the graphics queue always exists
            break;

        case CommandQueue::Compute:
            if (!m_Device->queryFeatureSupport(Feature::ComputeQueue))
            {
                error("Compute queue is not supported or initialized in this device");
                return nullptr;
            }
            break;

        case CommandQueue::Copy:
            if (!m_Device->queryFeatureSupport(Feature::CopyQueue))
            {
                error("Copy queue is not supported or initialized in this device");
                return nullptr;
            }
            break;

        case CommandQueue::Count:
        default:
            utils::InvalidEnum();
            return nullptr;
        }

        CommandListHandle commandList = m_Device->createCommandList(params);

        if (commandList == nullptr)
            return nullptr;

        CommandListWrapper* wrapper = new CommandListWrapper(this, commandList, params.enableImmediateExecution, params.queueType);
        return CommandListHandle::Create(wrapper);
    }
    
    uint64_t DeviceWrapper::executeCommandLists(ICommandList* const* pCommandLists, size_t numCommandLists, CommandQueue executionQueue)
    {
        if (numCommandLists == 0)
            return 0;

        if (pCommandLists == nullptr)
        {
            error("executeCommandLists: pCommandLists is NULL");
            return 0;
        }

        std::vector<ICommandList*> unwrappedCommandLists;
        unwrappedCommandLists.resize(numCommandLists);

        for(size_t i = 0; i < numCommandLists; i++)
        {
            if (pCommandLists[i] == nullptr)
            {
                std::stringstream ss;
                ss << "executeCommandLists: pCommandLists[" << i << "] is NULL";
                error(ss.str());
                return 0;
            }

            const CommandListParameters& desc = pCommandLists[i]->getDesc();
            if (desc.queueType != executionQueue)
            {
                std::stringstream ss;
                ss << "executeCommandLists: The command list [" << i << "] type is " << utils::CommandQueueToString(desc.queueType)
                    << ", it cannot be executed on a " << utils::CommandQueueToString(executionQueue) << " queue";
                error(ss.str());
                return 0;
            }

            CommandListWrapper* wrapper = dynamic_cast<CommandListWrapper*>(pCommandLists[i]);
            if (wrapper)
            {
                if (!wrapper->requireExecuteState())
                    return 0;

                unwrappedCommandLists[i] = wrapper->getUnderlyingCommandList();
            }
            else
                unwrappedCommandLists[i] = pCommandLists[i];
        }

        return m_Device->executeCommandLists(unwrappedCommandLists.data(), unwrappedCommandLists.size(), executionQueue);
    }

    void DeviceWrapper::queueWaitForCommandList(CommandQueue waitQueue, CommandQueue executionQueue, uint64_t instance)
    {
        m_Device->queueWaitForCommandList(waitQueue, executionQueue, instance);
    }

    void DeviceWrapper::waitForIdle()
    {
        m_Device->waitForIdle();
    }

    void DeviceWrapper::runGarbageCollection()
    {
        m_Device->runGarbageCollection();
    }

    bool DeviceWrapper::queryFeatureSupport(Feature feature, void* pInfo, size_t infoSize)
    {
        return m_Device->queryFeatureSupport(feature, pInfo, infoSize);
    }

    FormatSupport DeviceWrapper::queryFormatSupport(Format format)
    {
        return m_Device->queryFormatSupport(format);
    }

    Object DeviceWrapper::getNativeQueue(ObjectType objectType, CommandQueue queue)
    {
        return m_Device->getNativeQueue(objectType, queue);
    }

    IMessageCallback* DeviceWrapper::getMessageCallback()
    {
        return m_MessageCallback;
    }

    void Range::add(uint32_t item)
    {
        min = std::min(min, item);
        max = std::max(max, item);
    }

    bool Range::empty() const
    {
        return min > max;
    }

    bool Range::overlapsWith(const Range& other) const
    {
        return !empty() && !other.empty() && max >= other.min && min <= other.max;
    }

    bool ShaderBindingSet::any() const
    {
        return SRV.any() || Sampler.any() || UAV.any() || CB.any();
    }

    bool ShaderBindingSet::overlapsWith(const ShaderBindingSet& other) const
    {
        return rangeSRV.overlapsWith(other.rangeSRV)
            || rangeSampler.overlapsWith(other.rangeSampler)
            || rangeUAV.overlapsWith(other.rangeUAV)
            || rangeCB.overlapsWith(other.rangeCB);
    }

    IResource* unwrapResource(IResource* resource)
    {
        if (!resource)
            return nullptr;
        
        AccelStructWrapper* asWrapper = dynamic_cast<AccelStructWrapper*>(resource);

        if (asWrapper)
            return asWrapper->getUnderlyingObject();

        // More resource types to be added here when their wrappers are implemented

        return resource;
    }
    
    std::ostream& operator<<(std::ostream& os, const ShaderBindingSet& set)
    {
        bool first = true;
        BitsetToStream(set.SRV, os, "t", first);
        BitsetToStream(set.Sampler, os, "s", first);
        BitsetToStream(set.UAV, os, "u", first);
        BitsetToStream(set.CB, os, "b", first);
        return os;
    }
} // namespace nvrhi::validation
