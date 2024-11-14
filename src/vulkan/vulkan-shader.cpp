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
#include <nvrhi/common/misc.h>

namespace nvrhi::vulkan
{

    ShaderHandle Device::createShader(const ShaderDesc& desc, const void *binary, const size_t binarySize)
    {
        Shader *shader = new Shader(m_Context);

        shader->desc = desc;
        shader->stageFlagBits = convertShaderTypeToShaderStageFlagBits(desc.shaderType);

        auto shaderInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(binarySize)
            .setPCode((const uint32_t *)binary);

        const vk::Result res = m_Context.device.createShaderModule(&shaderInfo, m_Context.allocationCallbacks, &shader->shaderModule);
        CHECK_VK_FAIL(res)

        const std::string debugName = desc.debugName + ":" + desc.entryName;
        m_Context.nameVKObject(VkShaderModule(shader->shaderModule), vk::ObjectType::eShaderModule, vk::DebugReportObjectTypeEXT::eShaderModule, debugName.c_str());

        return ShaderHandle::Create(shader);
    }

    ShaderLibraryHandle Device::createShaderLibrary(const void* binary, const size_t binarySize)
    {
        ShaderLibrary* library = new ShaderLibrary(m_Context);
        
        auto shaderInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(binarySize)
            .setPCode((const uint32_t*)binary);

        const vk::Result res = m_Context.device.createShaderModule(&shaderInfo, m_Context.allocationCallbacks, &library->shaderModule);
        CHECK_VK_FAIL(res)

        return ShaderLibraryHandle::Create(library);
    }

    ShaderHandle Device::createShaderSpecialization(IShader* _baseShader, const ShaderSpecialization* constants, const uint32_t numConstants)
    {
        Shader* baseShader = checked_cast<Shader*>(_baseShader);
        assert(constants);
        assert(numConstants != 0);

        Shader* newShader = new Shader(m_Context);

        // Hold a strong reference to the parent object
        newShader->baseShader = (baseShader->baseShader) ? baseShader->baseShader : baseShader;
        newShader->desc = baseShader->desc;
        newShader->shaderModule = baseShader->shaderModule;
        newShader->stageFlagBits = baseShader->stageFlagBits;
        newShader->specializationConstants.assign(constants, constants + numConstants);

        return ShaderHandle::Create(newShader);
    }


    Shader::~Shader()
    {
        if (shaderModule && !baseShader) // do not destroy the module if this is a derived specialization shader or a library entry
        {
            m_Context.device.destroyShaderModule(shaderModule, m_Context.allocationCallbacks);
            shaderModule = vk::ShaderModule();
        }
    }

    void Shader::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        // we don't save these for vulkan
        if (ppBytecode) *ppBytecode = nullptr;
        if (pSize) *pSize = 0;
    }

    Object Shader::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::VK_ShaderModule:
            return Object(shaderModule);
        default:
            return nullptr;
        }
    }

    ShaderLibrary::~ShaderLibrary()
    {
        if (shaderModule)
        {
            m_Context.device.destroyShaderModule(shaderModule, m_Context.allocationCallbacks);
            shaderModule = vk::ShaderModule();
        }
    }

    void ShaderLibrary::getBytecode(const void** ppBytecode, size_t* pSize) const
    {
        if (ppBytecode) *ppBytecode = nullptr;
        if (pSize) *pSize = 0;
    }

    ShaderHandle ShaderLibrary::getShader(const char* entryName, ShaderType shaderType)
    {
        Shader* newShader = new Shader(m_Context);
        newShader->desc.entryName = entryName;
        newShader->desc.shaderType = shaderType;
        newShader->shaderModule = shaderModule;
        newShader->baseShader = this;
        newShader->stageFlagBits = convertShaderTypeToShaderStageFlagBits(shaderType);

        return ShaderHandle::Create(newShader);
    }

    InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* attributeDesc, uint32_t attributeCount, IShader* vertexShader)
    {
        (void)vertexShader;

        InputLayout *layout = new InputLayout();

        int total_attribute_array_size = 0;

        // collect all buffer bindings
        std::unordered_map<uint32_t, vk::VertexInputBindingDescription> bindingMap;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            const VertexAttributeDesc& desc = attributeDesc[i];

            assert(desc.arraySize > 0);

            total_attribute_array_size += desc.arraySize;

            if (bindingMap.find(desc.bufferIndex) == bindingMap.end())
            {
                bindingMap[desc.bufferIndex] = vk::VertexInputBindingDescription()
                    .setBinding(desc.bufferIndex)
                    .setStride(desc.elementStride)
                    .setInputRate(desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex);
            }
            else {
                assert(bindingMap[desc.bufferIndex].stride == desc.elementStride);
                assert(bindingMap[desc.bufferIndex].inputRate == (desc.isInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex));
            }
        }

        for (const auto& b : bindingMap)
        {
            layout->bindingDesc.push_back(b.second);
        }

        // build attribute descriptions
        layout->inputDesc.resize(attributeCount);
        layout->attributeDesc.resize(total_attribute_array_size);

        uint32_t attributeLocation = 0;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            const VertexAttributeDesc& in = attributeDesc[i];
            layout->inputDesc[i] = in;

            uint32_t element_size_bytes = getFormatInfo(in.format).bytesPerBlock;

            uint32_t bufferOffset = 0;

            for (uint32_t slot = 0; slot < in.arraySize; ++slot)
            {
                auto& outAttrib = layout->attributeDesc[attributeLocation];

                outAttrib.location = attributeLocation;
                outAttrib.binding = in.bufferIndex;
                outAttrib.format = vk::Format(vulkan::convertFormat(in.format));
                outAttrib.offset = bufferOffset + in.offset;
                bufferOffset += element_size_bytes;

                ++attributeLocation;
            }
        }

        return InputLayoutHandle::Create(layout);
    }

    uint32_t InputLayout::getNumAttributes() const 
    { 
        return uint32_t(inputDesc.size());
    }

    const VertexAttributeDesc* InputLayout::getAttributeDesc(uint32_t index) const 
    {
        if (index < uint32_t(inputDesc.size())) 
            return &inputDesc[index]; 
        else 
            return nullptr;
    }

} // namespace nvrhi::vulkan
