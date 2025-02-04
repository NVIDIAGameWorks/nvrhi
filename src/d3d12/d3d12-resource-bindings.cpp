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
    static ResourceType GetNormalizedResourceType(ResourceType type)
    {
        switch (type)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_UAV:
            return ResourceType::TypedBuffer_UAV;
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::RawBuffer_SRV:
            return ResourceType::TypedBuffer_SRV;
        default:
            return type;
        }
    }

    static bool AreResourceTypesCompatible(ResourceType a, ResourceType b)
    {
        if (a == b)
            return true;

        a = GetNormalizedResourceType(a);
        b = GetNormalizedResourceType(b);

        if ((a == ResourceType::TypedBuffer_SRV && b == ResourceType::Texture_SRV) ||
            (b == ResourceType::TypedBuffer_SRV && a == ResourceType::Texture_SRV) ||
            (a == ResourceType::TypedBuffer_SRV && b == ResourceType::RayTracingAccelStruct) ||
            (a == ResourceType::Texture_SRV && b == ResourceType::RayTracingAccelStruct) ||
            (b == ResourceType::TypedBuffer_SRV && a == ResourceType::RayTracingAccelStruct) ||
            (b == ResourceType::Texture_SRV && a == ResourceType::RayTracingAccelStruct))
            return true;

        if ((a == ResourceType::TypedBuffer_UAV && b == ResourceType::Texture_UAV) ||
            (b == ResourceType::TypedBuffer_UAV && a == ResourceType::Texture_UAV))
            return true;

        return false;
    }
    
    void BindingSet::createDescriptors()
    {
        // Process the volatile constant buffers: they occupy one root parameter each
        for (const std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& parameter : layout->rootParametersVolatileCB)
        {
            IBuffer* foundBuffer = nullptr;

            RootParameterIndex rootParameterIndex = parameter.first;
            const D3D12_ROOT_DESCRIPTOR1& rootDescriptor = parameter.second;

            for (const auto& binding : desc.bindings)
            {
                if (binding.type == ResourceType::VolatileConstantBuffer && binding.slot == rootDescriptor.ShaderRegister)
                {
                    Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
                    resources.push_back(buffer);
                    
                    foundBuffer = buffer;
                    break;
                }
            }

            // Add an entry to the binding set's array, whether we found the buffer in the binding set or not.
            // Even if not found, the command list still has to bind something to the root parameter.
            rootParametersVolatileCB.push_back(std::make_pair(rootParameterIndex, foundBuffer));
        }

        if (layout->descriptorTableSizeSamplers > 0)
        {
            DescriptorIndex descriptorTableBaseIndex = m_Resources.samplerHeap.allocateDescriptors(layout->descriptorTableSizeSamplers);
            descriptorTableSamplers = descriptorTableBaseIndex;
            rootParameterIndexSamplers = layout->rootParameterSamplers;
            descriptorTableValidSamplers = true;

            for (const auto& range : layout->descriptorRangesSamplers)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; itemInRange++)
                {
                    uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_Resources.samplerHeap.getCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    for (const auto& binding : desc.bindings)
                    {
                        if (binding.type == ResourceType::Sampler && binding.slot == slot)
                        {
                            Sampler* sampler = checked_cast<Sampler*>(binding.resourceHandle);
                            resources.push_back(sampler);

                            sampler->createDescriptor(descriptorHandle.ptr);
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        // Create a default sampler
                        D3D12_SAMPLER_DESC samplerDesc = {};
                        m_Context.device->CreateSampler(&samplerDesc, descriptorHandle);
                    }
                }
            }

            m_Resources.samplerHeap.copyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSamplers);
        }

        if (layout->descriptorTableSizeSRVetc > 0)
        {
            DescriptorIndex descriptorTableBaseIndex = m_Resources.shaderResourceViewHeap.allocateDescriptors(layout->descriptorTableSizeSRVetc);
            descriptorTableSRVetc = descriptorTableBaseIndex;
            rootParameterIndexSRVetc = layout->rootParameterSRVetc;
            descriptorTableValidSRVetc = true;

            for (const auto& range : layout->descriptorRangesSRVetc)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; itemInRange++)
                {
                    uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_Resources.shaderResourceViewHeap.getCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    IResource* pResource = nullptr;

                    for (size_t bindingIndex = 0; bindingIndex < desc.bindings.size(); bindingIndex++)
                    {
                        const BindingSetItem& binding = desc.bindings[bindingIndex];

                        if (binding.slot != slot)
                            continue;

                        const auto bindingType = GetNormalizedResourceType(binding.type);

                        if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::TypedBuffer_SRV)
                        {
                            if (binding.resourceHandle)
                            {
                                Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
                                pResource = buffer;

                                buffer->createSRV(descriptorHandle.ptr, binding.format, binding.range, binding.type);

                                if (!buffer->permanentState)
                                    bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                                else
                                    verifyPermanentResourceState(buffer->permanentState, ResourceStates::ShaderResource, 
                                        false, buffer->desc.debugName, m_Context.messageCallback);
                            }
                            else
                            {
                                Buffer::createNullSRV(descriptorHandle.ptr, binding.format, m_Context);
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == ResourceType::TypedBuffer_UAV)
                        {

                            if (binding.resourceHandle)
                            {
                                Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
                                pResource = buffer;

                                buffer->createUAV(descriptorHandle.ptr, binding.format, binding.range, binding.type);

                                if (!buffer->permanentState)
                                    bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                                else
                                    verifyPermanentResourceState(buffer->permanentState, ResourceStates::UnorderedAccess,
                                        false, buffer->desc.debugName, m_Context.messageCallback);
                            }
                            else
                            {
                                Buffer::createNullUAV(descriptorHandle.ptr, binding.format, m_Context);
                            }

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::Texture_SRV)
                        {
                            Texture* texture = checked_cast<Texture*>(binding.resourceHandle);

                            TextureSubresourceSet subresources = binding.subresources;

                            texture->createSRV(descriptorHandle.ptr, binding.format, binding.dimension, subresources);
                            pResource = texture;

                            if (!texture->permanentState)
                                bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                            else
                                verifyPermanentResourceState(texture->permanentState, ResourceStates::ShaderResource,
                                    true, texture->desc.debugName, m_Context.messageCallback);

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == ResourceType::Texture_UAV)
                        {
                            Texture* texture = checked_cast<Texture*>(binding.resourceHandle);

                            TextureSubresourceSet subresources = binding.subresources;

                            texture->createUAV(descriptorHandle.ptr, binding.format, binding.dimension, subresources);
                            pResource = texture;

                            if (!texture->permanentState)
                                bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                            else
                                verifyPermanentResourceState(texture->permanentState, ResourceStates::UnorderedAccess,
                                    true, texture->desc.debugName, m_Context.messageCallback);

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::RayTracingAccelStruct)
                        {
                            AccelStruct* accelStruct = checked_cast<AccelStruct*>(binding.resourceHandle);
                            accelStruct->createSRV(descriptorHandle.ptr);
                            pResource = accelStruct;

                            bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && bindingType == ResourceType::ConstantBuffer)
                        {
                            Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);

                            buffer->createCBV(descriptorHandle.ptr, binding.range);
                            pResource = buffer;

                            if(buffer->desc.isVolatile)
                            {
                                std::stringstream ss;
                                ss << "Attempted to bind a volatile constant buffer " << utils::DebugNameToString(buffer->desc.debugName)
                                    << " to a non-volatile CB layout at slot b" << binding.slot;
                                m_Context.error(ss.str());
                                found = false;
                                break;
                            }
                            else
                            {
                                if (!buffer->permanentState)
                                    bindingsThatNeedTransitions.push_back(static_cast<uint16_t>(bindingIndex));
                                else
                                    verifyPermanentResourceState(buffer->permanentState, ResourceStates::ConstantBuffer,
                                        false, buffer->desc.debugName, m_Context.messageCallback);
                            }
                            
                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == ResourceType::SamplerFeedbackTexture_UAV)
                        {
                            SamplerFeedbackTexture* texture = checked_cast<SamplerFeedbackTexture*>(binding.resourceHandle);

                            texture->createUAV(descriptorHandle.ptr);
                            pResource = texture;

                            // TODO: Automatic state transition into Unordered Access here

                            hasUavBindings = true;
                            found = true;
                            break;
                        }
                    }

                    if (pResource)
                    {
                        resources.push_back(pResource);
                    }

                    if (!found)
                    {
                        // Create a null SRV, UAV, or CBV

                        switch (range.RangeType)
                        {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                            Buffer::createNullSRV(descriptorHandle.ptr, Format::UNKNOWN, m_Context);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                            Buffer::createNullUAV(descriptorHandle.ptr, Format::UNKNOWN, m_Context);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                            m_Context.device->CreateConstantBufferView(nullptr, descriptorHandle);
                            break;

                        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                        default:
                            utils::InvalidEnum();
                            break;
                        }
                    }
                }
            }

            m_Resources.shaderResourceViewHeap.copyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSRVetc);
        }
    }

    BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc)
    {
        BindingLayout* ret = new BindingLayout(desc);
        return BindingLayoutHandle::Create(ret);
    }

    BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc)
    {
        BindlessLayout* ret = new BindlessLayout(desc);
        return BindingLayoutHandle::Create(ret);
    }

    BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout)
    {
        BindingSet *ret = new BindingSet(m_Context, m_Resources);
        ret->desc = desc;

        BindingLayout* pipelineLayout = checked_cast<BindingLayout*>(_layout);
        ret->layout = pipelineLayout;

        ret->createDescriptors();

        return BindingSetHandle::Create(ret);
    }

    DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* layout)
    {
        (void)layout; // not necessary on DX12

        DescriptorTable* ret = new DescriptorTable(m_Resources);
        ret->capacity = 0;
        ret->firstDescriptor = 0;
        
        return DescriptorTableHandle::Create(ret);
    }

    BindingSet::~BindingSet()
    {
        m_Resources.shaderResourceViewHeap.releaseDescriptors(descriptorTableSRVetc, layout->descriptorTableSizeSRVetc);
    
        m_Resources.samplerHeap.releaseDescriptors(descriptorTableSamplers, layout->descriptorTableSizeSamplers);
    }

    DescriptorTable::~DescriptorTable()
    {
        m_Resources.shaderResourceViewHeap.releaseDescriptors(firstDescriptor, capacity);
    }

    BindingLayout::BindingLayout(const BindingLayoutDesc& _desc)
        : desc(_desc)
    {
        // Start with some invalid values, to make sure that we start a new range on the first binding
        ResourceType currentType = ResourceType(-1);
        uint32_t currentSlot = ~0u;

        D3D12_ROOT_CONSTANTS rootConstants = {};

        for (const BindingLayoutItem& binding : desc.bindings)
        {
            if (binding.type == ResourceType::VolatileConstantBuffer)
            {
                D3D12_ROOT_DESCRIPTOR1 rootDescriptor;
                rootDescriptor.ShaderRegister = binding.slot;
                rootDescriptor.RegisterSpace = desc.registerSpace;

                // Volatile CBs are static descriptors, however strange that may seem.
                // A volatile CB can only be bound to a command list after it's been written into, and 
                // after that the data will not change until the command list has finished executing.
                // Subsequent writes will be made into a newly allocated portion of an upload buffer.
                rootDescriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

                rootParametersVolatileCB.push_back(std::make_pair(-1, rootDescriptor));
            }
            else if (binding.type == ResourceType::PushConstants)
            {
                pushConstantByteSize = binding.size;
                rootConstants.ShaderRegister = binding.slot;
                rootConstants.RegisterSpace = desc.registerSpace;
                rootConstants.Num32BitValues = binding.size / 4;
            }
            else if (!AreResourceTypesCompatible(binding.type, currentType) || binding.slot != currentSlot + 1)
            {
                // Start a new range

                if (binding.type == ResourceType::Sampler)
                {
                    descriptorRangesSamplers.resize(descriptorRangesSamplers.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];

                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    range.NumDescriptors = 1;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = desc.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSamplers;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

                    descriptorTableSizeSamplers += 1;
                }
                else
                {
                    descriptorRangesSRVetc.resize(descriptorRangesSRVetc.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];

                    switch (binding.type)
                    {
                    case ResourceType::Texture_SRV:
                    case ResourceType::TypedBuffer_SRV:
                    case ResourceType::StructuredBuffer_SRV:
                    case ResourceType::RawBuffer_SRV:
                    case ResourceType::RayTracingAccelStruct:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;

                    case ResourceType::Texture_UAV:
                    case ResourceType::TypedBuffer_UAV:
                    case ResourceType::StructuredBuffer_UAV:
                    case ResourceType::RawBuffer_UAV:
                    case ResourceType::SamplerFeedbackTexture_UAV:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;

                    case ResourceType::ConstantBuffer:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;

                    case ResourceType::None:
                    case ResourceType::VolatileConstantBuffer:
                    case ResourceType::Sampler:
                    case ResourceType::PushConstants:
                    case ResourceType::Count:
                    default:
                        utils::InvalidEnum();
                        continue;
                    }
                    range.NumDescriptors = 1;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = desc.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSRVetc;

                    // We don't know how apps will use resources referenced in a binding set. They may bind 
                    // a buffer to the command list and then copy data into it.
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

                    descriptorTableSizeSRVetc += 1;

                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentType = binding.type;
                currentSlot = binding.slot;
            }
            else
            {
                // Extend the current range

                if (binding.type == ResourceType::Sampler)
                {
                    assert(!descriptorRangesSamplers.empty());
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];

                    range.NumDescriptors += 1;
                    descriptorTableSizeSamplers += 1;
                }
                else
                {
                    assert(!descriptorRangesSRVetc.empty());
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];

                    range.NumDescriptors += 1;
                    descriptorTableSizeSRVetc += 1;

                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentSlot = binding.slot;
            }
        }

        // A PipelineBindingLayout occupies a contiguous segment of a root signature.
        // The root parameter indices stored here are relative to the beginning of that segment, not to the RS item 0.

        rootParameters.resize(0);

        if (rootConstants.Num32BitValues)
        {
            D3D12_ROOT_PARAMETER1& param = rootParameters.emplace_back();

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            param.ShaderVisibility = convertShaderStage(desc.visibility);
            param.Constants = rootConstants;

            rootParameterPushConstants = RootParameterIndex(rootParameters.size() - 1);
        }

        for (std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& rootParameterVolatileCB : rootParametersVolatileCB)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.ShaderVisibility = convertShaderStage(desc.visibility);
            param.Descriptor = rootParameterVolatileCB.second;

            rootParameterVolatileCB.first = RootParameterIndex(rootParameters.size() - 1);
        }

        if (descriptorTableSizeSamplers > 0)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = convertShaderStage(desc.visibility);
            param.DescriptorTable.NumDescriptorRanges = UINT(descriptorRangesSamplers.size());
            param.DescriptorTable.pDescriptorRanges = &descriptorRangesSamplers[0];

            rootParameterSamplers = RootParameterIndex(rootParameters.size() - 1);
        }

        if (descriptorTableSizeSRVetc > 0)
        {
            rootParameters.resize(rootParameters.size() + 1);
            D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = convertShaderStage(desc.visibility);
            param.DescriptorTable.NumDescriptorRanges = UINT(descriptorRangesSRVetc.size());
            param.DescriptorTable.pDescriptorRanges = &descriptorRangesSRVetc[0];

            rootParameterSRVetc = RootParameterIndex(rootParameters.size() - 1);
        }
    }

    BindlessLayout::BindlessLayout(const BindlessLayoutDesc& _desc)
        : desc(_desc)
    {
        descriptorRanges.resize(0);

        for (const BindingLayoutItem& item : desc.registerSpaces)
        {
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType;

            switch (item.type)
            {
            case ResourceType::Texture_SRV: 
            case ResourceType::TypedBuffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
            case ResourceType::RawBuffer_SRV:
            case ResourceType::RayTracingAccelStruct:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                break;

            case ResourceType::ConstantBuffer:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                break;

            case ResourceType::Texture_UAV:
            case ResourceType::TypedBuffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
            case ResourceType::RawBuffer_UAV:
            case ResourceType::SamplerFeedbackTexture_UAV:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                break;

            case ResourceType::Sampler:
                rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                break;

            case ResourceType::None:
            case ResourceType::VolatileConstantBuffer:
            case ResourceType::PushConstants:
            case ResourceType::Count:
            default:
                utils::InvalidEnum();
                continue;
            }

            D3D12_DESCRIPTOR_RANGE1& descriptorRange = descriptorRanges.emplace_back();

            descriptorRange.RangeType = rangeType;
            descriptorRange.NumDescriptors = ~0u; // unbounded
            descriptorRange.BaseShaderRegister = desc.firstSlot;
            descriptorRange.RegisterSpace = item.slot;
            descriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
            descriptorRange.OffsetInDescriptorsFromTableStart = 0;
        }

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.ShaderVisibility = convertShaderStage(desc.visibility);
        rootParameter.DescriptorTable.NumDescriptorRanges = uint32_t(descriptorRanges.size());
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
    }

    RootSignatureHandle Device::buildRootSignature(const static_vector<BindingLayoutHandle, c_MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout, bool isLocal, const D3D12_ROOT_PARAMETER1* pCustomParameters, uint32_t numCustomParameters)
    {
        HRESULT res;

        RootSignature* rootsig = new RootSignature(m_Resources);
        
        // Assemble the root parameter table from the pipeline binding layouts
        // Also attach the root parameter offsets to the pipeline layouts

        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;

        // Add custom parameters in the beginning of the RS
        for (uint32_t index = 0; index < numCustomParameters; index++)
        {
            rootParameters.push_back(pCustomParameters[index]);
        }

        for(uint32_t layoutIndex = 0; layoutIndex < uint32_t(pipelineLayouts.size()); layoutIndex++)
        {
            if (pipelineLayouts[layoutIndex]->getDesc())
            {
                BindingLayout* layout = checked_cast<BindingLayout*>(pipelineLayouts[layoutIndex].Get());
                RootParameterIndex rootParameterOffset = RootParameterIndex(rootParameters.size());

                rootsig->pipelineLayouts.push_back(std::make_pair(layout, rootParameterOffset));

                rootParameters.insert(rootParameters.end(), layout->rootParameters.begin(), layout->rootParameters.end());

                if (layout->pushConstantByteSize)
                {
                    rootsig->pushConstantByteSize = layout->pushConstantByteSize;
                    rootsig->rootParameterPushConstants = layout->rootParameterPushConstants + rootParameterOffset;
                }
            }
            else if (pipelineLayouts[layoutIndex]->getBindlessDesc())
            {
                BindlessLayout* layout = checked_cast<BindlessLayout*>(pipelineLayouts[layoutIndex].Get());
                RootParameterIndex rootParameterOffset = RootParameterIndex(rootParameters.size());

                rootsig->pipelineLayouts.push_back(std::make_pair(layout, rootParameterOffset));

                rootParameters.push_back(layout->rootParameter);
            }
        }

        // Build the description structure

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (allowInputLayout)
        {
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        }
        if (isLocal)
        {
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        }

        if (m_HeapDirectlyIndexedEnabled)
        {
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
        }
        
        if (!rootParameters.empty())
        {
            rsDesc.Desc_1_1.pParameters = rootParameters.data();
            rsDesc.Desc_1_1.NumParameters = UINT(rootParameters.size());
        }

        // Serialize the root signature

        RefCountPtr<ID3DBlob> rsBlob;
        RefCountPtr<ID3DBlob> errorBlob;
        res = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &errorBlob);

        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "D3D12SerializeVersionedRootSignature call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
            if (errorBlob) {
                ss << std::endl << (const char*)errorBlob->GetBufferPointer();
            }
            m_Context.error(ss.str());
            
            return nullptr;
        }

        // Create the RS object

        res = m_Context.device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&rootsig->handle));

        if (FAILED(res))
        {
            std::stringstream ss;
            ss << "CreateRootSignature call failed, HRESULT = 0x" << std::hex << std::setw(8) << res;
            m_Context.error(ss.str());

            return nullptr;
        }

        return RootSignatureHandle::Create(rootsig);
    }

    RefCountPtr<RootSignature> Device::getRootSignature(const static_vector<BindingLayoutHandle, c_MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout)
    {
        size_t hash = 0;

        for (const BindingLayoutHandle& pipelineLayout : pipelineLayouts)
            hash_combine(hash, pipelineLayout.Get());
        
        hash_combine(hash, allowInputLayout ? 1u : 0u);
        
        // Get a cached RS and AddRef it (if it exists)
        RefCountPtr<RootSignature> rootsig = m_Resources.rootsigCache[hash];

        if (!rootsig)
        {
            // Does not exist - build a new one, take ownership
            rootsig = checked_cast<RootSignature*>(buildRootSignature(pipelineLayouts, allowInputLayout, false).Get());
            rootsig->hash = hash;

            m_Resources.rootsigCache[hash] = rootsig;
        }

        // Pass ownership of the RS to caller
        return rootsig;
    }

    RootSignature::~RootSignature()
    {
        // Remove the root signature from the cache
        const auto it = m_Resources.rootsigCache.find(hash);
        if (it != m_Resources.rootsigCache.end())
            m_Resources.rootsigCache.erase(it);
    }

    bool Device::writeDescriptorTable(IDescriptorTable* _descriptorTable, const BindingSetItem& binding)
    {
        DescriptorTable* descriptorTable = checked_cast<DescriptorTable*>(_descriptorTable);

        if (binding.slot >= descriptorTable->capacity)
            return false;

        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_Resources.shaderResourceViewHeap.getCpuHandle(descriptorTable->firstDescriptor + binding.slot);

        switch (binding.type)
        {
        case ResourceType::None:
            Buffer::createNullSRV(descriptorHandle.ptr, Format::UNKNOWN, m_Context);
            break; 
        case ResourceType::Texture_SRV: {
            Texture* texture = checked_cast<Texture*>(binding.resourceHandle);
            texture->createSRV(descriptorHandle.ptr, binding.format, binding.dimension, binding.subresources);
            break;
        }
        case ResourceType::Texture_UAV: {
            Texture* texture = checked_cast<Texture*>(binding.resourceHandle);
            texture->createUAV(descriptorHandle.ptr, binding.format, binding.dimension, binding.subresources);
            break;
        }
        case ResourceType::SamplerFeedbackTexture_UAV: {
            SamplerFeedbackTexture* texture = checked_cast<SamplerFeedbackTexture*>(binding.resourceHandle);
            texture->createUAV(descriptorHandle.ptr);
            break;
        }
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::RawBuffer_SRV: {
            Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
            buffer->createSRV(descriptorHandle.ptr, binding.format, binding.range, binding.type);
            break;
        }
        case ResourceType::TypedBuffer_UAV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_UAV: {
            Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
            buffer->createUAV(descriptorHandle.ptr, binding.format, binding.range, binding.type);
            break;
        }
        case ResourceType::ConstantBuffer: {
            Buffer* buffer = checked_cast<Buffer*>(binding.resourceHandle);
            buffer->createCBV(descriptorHandle.ptr, binding.range);
            break;
        }
        case ResourceType::RayTracingAccelStruct: {
            AccelStruct* accelStruct = checked_cast<AccelStruct*>(binding.resourceHandle);
            accelStruct->createSRV(descriptorHandle.ptr);
            break;
        }

        case ResourceType::VolatileConstantBuffer:
            m_Context.error("Attempted to bind a volatile constant buffer to a bindless set.");
            return false;

        case ResourceType::Sampler:
        case ResourceType::PushConstants:
        case ResourceType::Count:
        default:
            utils::InvalidEnum();
            return false;
        }

        m_Resources.shaderResourceViewHeap.copyToShaderVisibleHeap(descriptorTable->firstDescriptor + binding.slot, 1);
        return true;
    }

    void Device::resizeDescriptorTable(IDescriptorTable* _descriptorTable, uint32_t newSize, bool keepContents)
    {
        DescriptorTable* descriptorTable = checked_cast<DescriptorTable*>(_descriptorTable);

        if (newSize == descriptorTable->capacity)
            return;

        if (newSize < descriptorTable->capacity)
        {
            m_Resources.shaderResourceViewHeap.releaseDescriptors(descriptorTable->firstDescriptor + newSize, descriptorTable->capacity - newSize);
            descriptorTable->capacity = newSize;
            return;
        }

        uint32_t originalFirst = descriptorTable->firstDescriptor;
        if (!keepContents && descriptorTable->capacity > 0)
        {
            m_Resources.shaderResourceViewHeap.releaseDescriptors(descriptorTable->firstDescriptor, descriptorTable->capacity);
        }

        descriptorTable->firstDescriptor = m_Resources.shaderResourceViewHeap.allocateDescriptors(newSize);

        if (keepContents && descriptorTable->capacity > 0)
        {
            m_Context.device->CopyDescriptorsSimple(descriptorTable->capacity,
                m_Resources.shaderResourceViewHeap.getCpuHandle(descriptorTable->firstDescriptor),
                m_Resources.shaderResourceViewHeap.getCpuHandle(originalFirst),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            m_Context.device->CopyDescriptorsSimple(descriptorTable->capacity,
                m_Resources.shaderResourceViewHeap.getCpuHandleShaderVisible(descriptorTable->firstDescriptor),
                m_Resources.shaderResourceViewHeap.getCpuHandle(originalFirst),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            m_Resources.shaderResourceViewHeap.releaseDescriptors(originalFirst, descriptorTable->capacity);
        }

        descriptorTable->capacity = newSize;
    }

    void CommandList::setComputeBindings(
        const BindingSetVector& bindings, uint32_t bindingUpdateMask,
        IBuffer* indirectParams, bool updateIndirectParams,
        const RootSignature* rootSignature)
    {
        if (bindingUpdateMask)
        {
            static_vector<VolatileConstantBufferBinding, c_MaxVolatileConstantBuffers> newVolatileCBs;

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < uint32_t(bindings.size()); bindingSetIndex++)
            {
                IBindingSet* _bindingSet = bindings[bindingSetIndex];

                if (!_bindingSet)
                    continue;

                const bool updateThisSet = (bindingUpdateMask & (1 << bindingSetIndex)) != 0;

                const std::pair<BindingLayoutHandle, RootParameterIndex>& layoutAndOffset = rootSignature->pipelineLayouts[bindingSetIndex];
                RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                if (_bindingSet->getDesc())
                {
                    assert(layoutAndOffset.first == _bindingSet->getLayout()); // validation layer handles this

                    BindingSet* bindingSet = checked_cast<BindingSet*>(_bindingSet);

                    // Bind the volatile constant buffers
                    for (size_t volatileCbIndex = 0; volatileCbIndex < bindingSet->rootParametersVolatileCB.size(); volatileCbIndex++)
                    {
                        const auto& parameter = bindingSet->rootParametersVolatileCB[volatileCbIndex];
                        RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                        if (parameter.second)
                        {
                            Buffer* buffer = checked_cast<Buffer*>(parameter.second);

                            if (buffer->desc.isVolatile)
                            {
                                D3D12_GPU_VIRTUAL_ADDRESS volatileData = m_VolatileConstantBufferAddresses[buffer];

                                if (!volatileData)
                                {
                                    std::stringstream ss;
                                    ss << "Attempted use of a volatile constant buffer " << utils::DebugNameToString(buffer->desc.debugName)
                                        << " before it was written into";
                                    m_Context.error(ss.str());

                                    continue;
                                }

                                if (updateThisSet || volatileData != m_CurrentComputeVolatileCBs[newVolatileCBs.size()].address)
                                {
                                    m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, volatileData);
                                }

                                newVolatileCBs.push_back(VolatileConstantBufferBinding{ rootParameterIndex, buffer, volatileData });
                            }
                            else if (updateThisSet)
                            {
                                assert(buffer->gpuVA != 0);

                                m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                            }
                        }
                        else if (updateThisSet)
                        {
                            // This can only happen as a result of an improperly built binding set. 
                            // Such binding set should fail to create.
                            m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, 0);
                        }
                    }

                    if (updateThisSet)
                    {
                        if (bindingSet->descriptorTableValidSamplers)
                        {
                            m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSamplers,
                                m_Resources.samplerHeap.getGpuHandle(bindingSet->descriptorTableSamplers));
                        }

                        if (bindingSet->descriptorTableValidSRVetc)
                        {
                            m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSRVetc,
                                m_Resources.shaderResourceViewHeap.getGpuHandle(bindingSet->descriptorTableSRVetc));
                        }

                        if (bindingSet->desc.trackLiveness)
                            m_Instance->referencedResources.push_back(bindingSet);
                    }

                    if (m_EnableAutomaticBarriers && (updateThisSet || bindingSet->hasUavBindings)) // UAV bindings may place UAV barriers on the same binding set
                    {
                        setResourceStatesForBindingSet(bindingSet);
                    }
                }
                else
                {
                    DescriptorTable* descriptorTable = checked_cast<DescriptorTable*>(_bindingSet);

                    m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(rootParameterOffset, m_Resources.shaderResourceViewHeap.getGpuHandle(descriptorTable->firstDescriptor));
                }
            }

            m_CurrentComputeVolatileCBs = newVolatileCBs;
        }

        if (indirectParams && updateIndirectParams)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireBufferState(indirectParams, ResourceStates::IndirectArgument);
            }
            m_Instance->referencedResources.push_back(indirectParams);
        }

        uint32_t bindingMask = (1 << uint32_t(bindings.size())) - 1;
        if ((bindingUpdateMask & bindingMask) == bindingMask)
        {
            // Only reset this flag when this function has gone over all the binging sets
            m_AnyVolatileBufferWrites = false;
        }
    }

    void CommandList::setGraphicsBindings(
        const BindingSetVector& bindings, uint32_t bindingUpdateMask,
        IBuffer* indirectParams, bool updateIndirectParams,
        const RootSignature* rootSignature)
    {
        if (bindingUpdateMask)
        {
            static_vector<VolatileConstantBufferBinding, c_MaxVolatileConstantBuffers> newVolatileCBs;

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < uint32_t(bindings.size()); bindingSetIndex++)
            {
                IBindingSet* _bindingSet = bindings[bindingSetIndex];

                if (!_bindingSet)
                    continue;

                const bool updateThisSet = (bindingUpdateMask & (1 << bindingSetIndex)) != 0;

                const std::pair<BindingLayoutHandle, RootParameterIndex>& layoutAndOffset = rootSignature->pipelineLayouts[bindingSetIndex];
                RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                if (_bindingSet->getDesc())
                {
                    assert(layoutAndOffset.first == _bindingSet->getLayout()); // validation layer handles this

                    BindingSet* bindingSet = checked_cast<BindingSet*>(_bindingSet);

                    // Bind the volatile constant buffers
                    for (size_t volatileCbIndex = 0; volatileCbIndex < bindingSet->rootParametersVolatileCB.size(); volatileCbIndex++)
                    {
                        const auto& parameter = bindingSet->rootParametersVolatileCB[volatileCbIndex];
                        RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                        if (parameter.second)
                        {
                            Buffer* buffer = checked_cast<Buffer*>(parameter.second);

                            if (buffer->desc.isVolatile)
                            {
                                const D3D12_GPU_VIRTUAL_ADDRESS volatileData = m_VolatileConstantBufferAddresses[buffer];

                                if (!volatileData)
                                {
                                    std::stringstream ss;
                                    ss << "Attempted use of a volatile constant buffer " << utils::DebugNameToString(buffer->desc.debugName)
                                        << " before it was written into";
                                    m_Context.error(ss.str());

                                    continue;
                                }

                                if (updateThisSet || volatileData != m_CurrentGraphicsVolatileCBs[newVolatileCBs.size()].address)
                                {
                                    m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, volatileData);
                                }

                                newVolatileCBs.push_back(VolatileConstantBufferBinding{ rootParameterIndex, buffer, volatileData });
                            }
                            else if (updateThisSet)
                            {
                                assert(buffer->gpuVA != 0);

                                m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                            }
                        }
                        else if (updateThisSet)
                        {
                            // This can only happen as a result of an improperly built binding set. 
                            // Such binding set should fail to create.
                            m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, 0);
                        }
                    }

                    if (updateThisSet)
                    {
                        if (bindingSet->descriptorTableValidSamplers)
                        {
                            m_ActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSamplers,
                                m_Resources.samplerHeap.getGpuHandle(bindingSet->descriptorTableSamplers));
                        }

                        if (bindingSet->descriptorTableValidSRVetc)
                        {
                            m_ActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                                rootParameterOffset + bindingSet->rootParameterIndexSRVetc,
                                m_Resources.shaderResourceViewHeap.getGpuHandle(bindingSet->descriptorTableSRVetc));
                        }

                        if (bindingSet->desc.trackLiveness)
                            m_Instance->referencedResources.push_back(bindingSet);
                    }

                    if (m_EnableAutomaticBarriers && (updateThisSet || bindingSet->hasUavBindings)) // UAV bindings may place UAV barriers on the same binding set
                    {
                        setResourceStatesForBindingSet(bindingSet);
                    }
                }
                else if (updateThisSet)
                {
                    DescriptorTable* descriptorTable = checked_cast<DescriptorTable*>(_bindingSet);

                    m_ActiveCommandList->commandList->SetGraphicsRootDescriptorTable(rootParameterOffset, m_Resources.shaderResourceViewHeap.getGpuHandle(descriptorTable->firstDescriptor));
                }
            }

            m_CurrentGraphicsVolatileCBs = newVolatileCBs;
        }

        if (indirectParams && updateIndirectParams)
        {
            if (m_EnableAutomaticBarriers)
            {
                requireBufferState(indirectParams, ResourceStates::IndirectArgument);
            }
            m_Instance->referencedResources.push_back(indirectParams);
        }

        uint32_t bindingMask = (1 << uint32_t(bindings.size())) - 1;
        if ((bindingUpdateMask & bindingMask) == bindingMask)
        {
            // Only reset this flag when this function has gone over all the binging sets
            m_AnyVolatileBufferWrites = false;
        }
    }


} // namespace nvrhi::d3d12
