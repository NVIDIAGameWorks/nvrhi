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

#include <nvrhi/common/misc.h>
#include <nvrhi/utils.h>

#include <sstream>


namespace nvrhi::validation
{

    CommandListWrapper::CommandListWrapper(DeviceWrapper* device, ICommandList* commandList, bool isImmediate, CommandQueue queueType)
        : m_CommandList(commandList)
        , m_Device(device)
        , m_MessageCallback(device->getMessageCallback())
        , m_IsImmediate(isImmediate)
        , m_type(queueType)
    {
    }
    
    void CommandListWrapper::error(const std::string& messageText) const
    {
        m_MessageCallback->message(MessageSeverity::Error, messageText.c_str());
    }

    void CommandListWrapper::warning(const std::string& messageText) const
    {
        m_MessageCallback->message(MessageSeverity::Warning, messageText.c_str());
    }

    static const char* CommandListStateToString(CommandListState state)
    {
        switch (state)
        {
        case CommandListState::INITIAL:
            return "INITIAL";
        case CommandListState::OPEN:
            return "OPEN";
        case CommandListState::CLOSED:
            return "CLOSED";
        default:
            return "<INVALID>";
        }
    }

    static const char* CommandQueueTypeToString(CommandQueue type)
    {
        switch (type)
        {
        case CommandQueue::Graphics:
            return "GRAPHICS";
        case CommandQueue::Compute:
            return "COMPUTE";
        case CommandQueue::Copy:
            return "COPY";
        case CommandQueue::Count:
        default:
            return "<INVALID>";
        }
    }

    bool CommandListWrapper::requireOpenState() const
    {
        if (m_State == CommandListState::OPEN)
            return true;

        std::stringstream ss;
        ss << "A command list must be opened before any rendering commands can be executed. "
            "Actual state: " << CommandListStateToString(m_State);
        error(ss.str());

        return false;
    }

    bool CommandListWrapper::requireExecuteState()
    {
        switch (m_State)
        {
        case CommandListState::INITIAL:
            error("Cannot execute a command list before it is opened and then closed");
            return false;
        case CommandListState::OPEN:
            error("Cannot execute a command list before it is closed");
            return false;
        case CommandListState::CLOSED:
        default:
            break;
        }

        m_State = CommandListState::INITIAL;
        return true;
    }

    bool CommandListWrapper::requireType(CommandQueue queueType, const char* operation) const
    {
        if ((int)m_type > (int)queueType)
        {
            std::stringstream ss;
            ss << "This command list has type " << CommandQueueTypeToString(m_type)
                << ", but the '" << operation  << "' operation requires at least " << CommandQueueTypeToString(queueType);
            error(ss.str());

            return false;
        }

        return true;
    }

    Object CommandListWrapper::getNativeObject(ObjectType objectType)
    {
        return m_CommandList->getNativeObject(objectType);
    }

    void CommandListWrapper::open()
    {
        switch (m_State)
        {
        case CommandListState::OPEN:
            error("Cannot open a command list that is already open");
            return;
        case CommandListState::CLOSED:
            if (m_IsImmediate)
            {
                error("An immediate command list cannot be abandoned and must be executed before it is re-opened");
                return;
            }
            else
            {
                warning("A command list should be executed before it is reopened");
                break;
            }
        case CommandListState::INITIAL:
        default:
            break;
        }

        if (m_IsImmediate)
        {
            if (++m_Device->m_NumOpenImmediateCommandLists > 1)
            {
                error("Two or more immediate command lists cannot be open at the same time");
                --m_Device->m_NumOpenImmediateCommandLists;
                return;
            }
        }

        m_CommandList->open();

        m_State = CommandListState::OPEN;
        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
        m_MeshletStateSet = false;
    }

    void CommandListWrapper::close()
    {
        switch (m_State)
        {
        case CommandListState::INITIAL:
            error("Cannot close a command list before it is opened");
            return;
        case CommandListState::CLOSED:
            error("Cannot close a command list that is already closed");
            return;
        case CommandListState::OPEN:
        default:
            break;
        }

        if (m_IsImmediate)
        {
            --m_Device->m_NumOpenImmediateCommandLists;
        }

        m_CommandList->close();

        m_State = CommandListState::CLOSED;
        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
        m_MeshletStateSet = false;
    }

    void CommandListWrapper::clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "clearTextureFloat"))
            return;

        const TextureDesc& textureDesc = t->getDesc();

        const FormatInfo& formatInfo = getFormatInfo(textureDesc.format);
        if (formatInfo.hasDepth || formatInfo.hasStencil)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureFloat because it's a depth-stencil texture. Use clearDepthStencilTexture instead.";
            error(ss.str());
            return;
        }

        if (formatInfo.kind == FormatKind::Integer)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureFloat because it's an integer texture. Use clearTextureUInt instead.";
            error(ss.str());
            return;
        }

        if (!textureDesc.isRenderTarget && !textureDesc.isUAV)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureFloat because it was created with both isRenderTarget = false and isUAV = false.";
            error(ss.str());
            return;
        }
        
        m_CommandList->clearTextureFloat(t, subresources, clearColor);
    }

    void CommandListWrapper::clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "clearDepthStencilTexture"))
            return;

        const FormatInfo& formatInfo = getFormatInfo(t->getDesc().format);
        if (!formatInfo.hasDepth && !formatInfo.hasStencil)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(t->getDesc().debugName) << " cannot be cleared with "
                "clearDepthStencilTexture because it's not a depth-stencil texture. Use clearTextureFloat or clearTextureUInt instead.";
            error(ss.str());
            return;
        }

        if (!t->getDesc().isRenderTarget)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(t->getDesc().debugName) << " cannot be cleared with "
                "clearDepthStencilTexture because it was created with isRenderTarget = false.";
            error(ss.str());
            return;
        }

        m_CommandList->clearDepthStencilTexture(t, subresources, clearDepth, depth, clearStencil, stencil);
    }

    void CommandListWrapper::clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "clearTextureUInt"))
            return;

        const TextureDesc& textureDesc = t->getDesc();

        const FormatInfo& formatInfo = getFormatInfo(textureDesc.format);
        if (formatInfo.hasDepth || formatInfo.hasStencil)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureUInt because it's a depth-stencil texture. Use clearDepthStencilTexture instead.";
            error(ss.str());
            return;
        }

        if (formatInfo.kind != FormatKind::Integer)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureUInt because it's not an integer texture. Use clearTextureFloat instead.";
            error(ss.str());
            return;
        }

        if (!textureDesc.isRenderTarget && !textureDesc.isUAV)
        {
            std::stringstream ss;
            ss << "Texture " << utils::DebugNameToString(textureDesc.debugName) << " cannot be cleared with "
                "clearTextureUInt because it was created with both isRenderTarget = false and isUAV = false.";
            error(ss.str());
            return;
        }

        m_CommandList->clearTextureUInt(t, subresources, clearColor);
    }

    void CommandListWrapper::copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;
        
        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        if (!requireOpenState())
            return;

        if (dest->getDesc().height > 1 && rowPitch == 0)
        {
            error("writeTexture: rowPitch is 0 but dest has multiple rows");
        }

        m_CommandList->writeTexture(dest, arraySlice, mipLevel, data, rowPitch, depthPitch);
    }

    void CommandListWrapper::resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "resolveTexture"))
            return;

        bool anyErrors = false;

        if (!dest)
        {
            error("resolveTexture: dest is NULL");
            anyErrors = true;
        }

        if (!src)
        {
            error("resolveTexture: src is NULL");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        const TextureDesc& dstDesc = dest->getDesc();
        const TextureDesc& srcDesc = src->getDesc();

        TextureSubresourceSet dstSR = dstSubresources.resolve(dstDesc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(srcDesc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
        {
            error("resolveTexture: source and destination subresource sets must resolve to sets of the same size");
            anyErrors = true;
        }

        const uint32_t srcMipWidth = std::max(srcDesc.width >> srcSR.baseMipLevel, 1u);
        const uint32_t srcMipHeight = std::max(srcDesc.height >> srcSR.baseMipLevel, 1u);
        const uint32_t dstMipWidth = std::max(dstDesc.width >> dstSR.baseMipLevel, 1u);
        const uint32_t dstMipHeight = std::max(dstDesc.height >> dstSR.baseMipLevel, 1u);
        if (srcMipWidth != dstMipWidth || srcMipHeight != dstMipHeight)
        {
            error("resolveTexture: referenced mip levels of source and destination textures must have the same dimensions");
            anyErrors = true;
        }

        if (dstDesc.sampleCount != 1)
        {
            error("resolveTexture: destination texture must not be multi-sampled");
            anyErrors = true;
        }

        if (srcDesc.sampleCount <= 1)
        {
            error("resolveTexture: source texture must be multi-sampled");
            anyErrors = true;
        }

        if (srcDesc.format != dstDesc.format)
        {
            error("resolveTexture: source and destination textures must have the same format");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        m_CommandList->resolveTexture(dest, dstSubresources, src, srcSubresources);
    }

    void CommandListWrapper::writeBuffer(IBuffer* b, const void* data, size_t dataSize, uint64_t destOffsetBytes)
    {
        if (!requireOpenState())
            return;

        if (dataSize + destOffsetBytes > b->getDesc().byteSize)
        {
            error("writeBuffer: dataSize + destOffsetBytes is greater than the buffer size");
            return;
        }

        if (destOffsetBytes > 0 && b->getDesc().isVolatile)
        {
            error("writeBuffer: cannot write into volatile buffers with an offset");
            return;
        }

        if (dataSize > 0x10000 && b->getDesc().isVolatile)
        {
            error("writeBuffer: cannot write more than 65535 bytes into volatile buffers");
            return;
        }

        m_CommandList->writeBuffer(b, data, dataSize, destOffsetBytes);
    }

    void CommandListWrapper::clearBufferUInt(IBuffer* b, uint32_t clearValue)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "clearBufferUInt"))
            return;

        m_CommandList->clearBufferUInt(b, clearValue);
    }

    void CommandListWrapper::copyBuffer(IBuffer* dest, uint64_t destOffsetBytes, IBuffer* src, uint64_t srcOffsetBytes, uint64_t dataSizeBytes)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyBuffer(dest, destOffsetBytes, src, srcOffsetBytes, dataSizeBytes);
    }

    bool CommandListWrapper::validateBindingSetsAgainstLayouts(const static_vector<BindingLayoutHandle, c_MaxBindingLayouts>& layouts, const static_vector<IBindingSet*, c_MaxBindingLayouts>& sets) const
    {
        if (layouts.size() != sets.size())
        {
            std::stringstream ss;
            ss << "Number of binding sets provided (" << sets.size() << ") does not match "
                "the number of binding layouts in the pipeline (" << layouts.size() << ")";
            error(ss.str());
            return false;
        }

        bool anyErrors = false;

        for (int index = 0; index < int(layouts.size()); index++)
        {
            if (sets[index] == nullptr)
            {
                std::stringstream ss;
                ss << "Binding set in slot " << index << " is NULL";
                error(ss.str());
                anyErrors = true;
                continue;
            }

            IBindingLayout* setLayout = sets[index]->getLayout();
            IBindingLayout* expectedLayout = layouts[index];
            bool setIsBindless = (sets[index]->getDesc() == nullptr);
            bool expectedBindless = expectedLayout->getBindlessDesc();

            if (!expectedBindless && setLayout != expectedLayout)
            {
                std::stringstream ss;
                ss << "Binding set in slot " << index << " does not match the layout in pipeline slot " << index;
                error(ss.str());
                anyErrors = true;
            }

            if (expectedBindless && !setIsBindless)
            {
                std::stringstream ss;
                ss << "Binding set in slot " << index << " is regular while the layout expects a descriptor table";
                error(ss.str());
                anyErrors = true;
            }
        }

        return !anyErrors;
    }

    void CommandListWrapper::setPushConstants(const void* data, size_t byteSize)
    {
        if (!requireOpenState())
            return;

        if (!m_GraphicsStateSet && !m_ComputeStateSet && !m_MeshletStateSet && !m_RayTracingStateSet)
        {
            error("setPushConstants is only valid when a graphics, compute, meshlet, or ray tracing state is set");
            return;
        }

        if (byteSize > c_MaxPushConstantSize)
        {
            std::stringstream ss;
            ss << "Push constant size (" << byteSize << ") cannot exceed " << c_MaxPushConstantSize << " bytes";
            error(ss.str());
            return;
        }

        if (byteSize != m_PipelinePushConstantSize)
        {
            std::stringstream ss;

            if (m_PipelinePushConstantSize == 0)
                ss << "The current pipeline does not expect any push constants, so the setPushConstants call is invalid.";
            else
                ss << "Push constant size (" << byteSize << " bytes) doesn't match the size expected by the pipeline (" << m_PipelinePushConstantSize << " bytes)";

            error(ss.str());
            return;
        }

        m_PushConstantsSet = true;

        m_CommandList->setPushConstants(data, byteSize);
    }

    void CommandListWrapper::setGraphicsState(const GraphicsState& state)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "setGraphicsState"))
            return;

        bool anyErrors = false;
        std::stringstream ss;
        ss << "setGraphicsState: " << std::endl;

        if (!state.pipeline)
        {
            ss << "pipeline is NULL." << std::endl;
            anyErrors = true;
        }

        if (!state.framebuffer)
        {
            ss << "framebuffer is NULL." << std::endl;
            anyErrors = true;
        }

        if (state.indexBuffer.buffer && !state.indexBuffer.buffer->getDesc().isIndexBuffer)
        {
            ss << "Cannot use buffer '" << utils::DebugNameToString(state.indexBuffer.buffer->getDesc().debugName) << "' as an index buffer because it does not have the isIndexBuffer flag set." << std::endl;
            anyErrors = true;
        }

        for (size_t index = 0; index < state.vertexBuffers.size(); index++)
        {
            const VertexBufferBinding& vb = state.vertexBuffers[index];

            if (!vb.buffer)
            {
                ss << "Vertex buffer at index " << index << " is NULL." << std::endl;
                anyErrors = true;
            }
            else if (!vb.buffer->getDesc().isVertexBuffer)
            {
                ss << "Buffer '" << utils::DebugNameToString(vb.buffer->getDesc().debugName) << "' bound to vertex buffer slot " << index << " cannot be used as a vertex buffer because it does not have the isVertexBuffer flag set." << std::endl;
                anyErrors = true;
            }

            if (vb.slot >= c_MaxVertexAttributes)
            {
                ss << "Vertex buffer binding at index " << index << " uses an invalid slot " << vb.slot << "." << std::endl;
                anyErrors = true;
            }
        }

        if (state.indirectParams && !state.indirectParams->getDesc().isDrawIndirectArgs)
        {
            ss << "Cannot use buffer '" << utils::DebugNameToString(state.indirectParams->getDesc().debugName) << "' as a DrawIndirect argument buffer because it does not have the isDrawIndirectArgs flag set." << std::endl;
            anyErrors = true;
        }

        if (anyErrors)
        {
            error(ss.str());
            return;
        }

        if (!validateBindingSetsAgainstLayouts(state.pipeline->getDesc().bindingLayouts, state.bindings))
            anyErrors = true;

        if (state.framebuffer->getFramebufferInfo() != state.pipeline->getFramebufferInfo())
        {
            ss << "The framebuffer used in the draw call does not match the framebuffer used to create the pipeline." << std::endl <<
                "Formats and sample counts of the framebuffers must match." << std::endl;
            anyErrors = true;
        }

        if (anyErrors)
        {
            error(ss.str());
            return;
        }

        evaluatePushConstantSize(state.pipeline->getDesc().bindingLayouts);

        m_CommandList->setGraphicsState(state);

        m_GraphicsStateSet = true;
        m_ComputeStateSet = false;
        m_MeshletStateSet = false;
        m_RayTracingStateSet = false;
        m_PushConstantsSet = false;
        m_CurrentGraphicsState = state;
    }

    void CommandListWrapper::draw(const DrawArguments& args)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "draw"))
            return;

        if (!m_GraphicsStateSet)
        {
            error("Graphics state is not set before a draw call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (!validatePushConstants("graphics", "setGraphicsState"))
            return;

        m_CommandList->draw(args);
    }

    void CommandListWrapper::drawIndexed(const DrawArguments& args)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "drawIndexed"))
            return;

        if (!m_GraphicsStateSet)
        {
            error("Graphics state is not set before a drawIndexed call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (m_CurrentGraphicsState.indexBuffer.buffer == nullptr)
        {
            error("Index buffer is not set before a drawIndexed call");
            return;
        }

        if (!validatePushConstants("graphics", "setGraphicsState"))
            return;

        m_CommandList->drawIndexed(args);
    }

    void CommandListWrapper::drawIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "drawIndirect"))
            return;

        if (!m_GraphicsStateSet)
        {
            error("Graphics state is not set before a drawIndirect call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (!m_CurrentGraphicsState.indirectParams)
        {
            error("Indirect params buffer is not set before a drawIndirect call.");
            return;
        }

        if (!validatePushConstants("graphics", "setGraphicsState"))
            return;

        m_CommandList->drawIndirect(offsetBytes, drawCount);
    }

    void CommandListWrapper::drawIndexedIndirect(uint32_t offsetBytes, uint32_t drawCount)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "drawIndexedIndirect"))
            return;

        if (!m_GraphicsStateSet)
        {
            error("Graphics state is not set before a drawIndexedIndirect call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (!m_CurrentGraphicsState.indirectParams)
        {
            error("Indirect params buffer is not set before a drawIndexedIndirect call.");
            return;
        }

        if (!validatePushConstants("graphics", "setGraphicsState"))
            return;

        m_CommandList->drawIndexedIndirect(offsetBytes, drawCount);
    }

    void CommandListWrapper::setComputeState(const ComputeState& state)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "setComputeState"))
            return;

        bool anyErrors = false;
        std::stringstream ss;
        ss << "setComputeState: " << std::endl;

        if (!state.pipeline)
        {
            ss << "pipeline is NULL." << std::endl;
            anyErrors = true;
        }

        if (state.indirectParams && !state.indirectParams->getDesc().isDrawIndirectArgs)
        {
            ss << "Cannot use buffer '" << utils::DebugNameToString(state.indirectParams->getDesc().debugName) << "' as a DispatchIndirect argument buffer because it does not have the isDrawIndirectArgs flag set." << std::endl;
            anyErrors = true;
        }

        if (anyErrors)
        {
            error(ss.str());
            return;
        }

        if (anyErrors)
            return;

        if (!validateBindingSetsAgainstLayouts(state.pipeline->getDesc().bindingLayouts, state.bindings))
            anyErrors = true;

        if (anyErrors)
            return;

        evaluatePushConstantSize(state.pipeline->getDesc().bindingLayouts);

        m_CommandList->setComputeState(state);

        m_GraphicsStateSet = false;
        m_ComputeStateSet = true;
        m_MeshletStateSet = false;
        m_RayTracingStateSet = false;
        m_PushConstantsSet = false;
        m_CurrentComputeState = state;
    }

    void CommandListWrapper::dispatch(uint32_t groupsX, uint32_t groupsY /*= 1*/, uint32_t groupsZ /*= 1*/)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "dispatch"))
            return;

        if (!m_ComputeStateSet)
        {
            error("Compute state is not set before a dispatch call.\n"
                "Note that setting graphics state invalidates the compute state.");
            return;
        }

        if (!validatePushConstants("compute", "setComputeState"))
            return;

        m_CommandList->dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandListWrapper::dispatchIndirect(uint32_t offsetBytes)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "dispatchIndirect"))
            return;

        if (!m_ComputeStateSet)
        {
            error("Compute state is not set before a dispatchIndirect call.\n"
                "Note that setting graphics state invalidates the compute state.");
            return;
        }

        if (!m_CurrentComputeState.indirectParams)
        {
            error("Indirect params buffer is not set before a dispatchIndirect call.");
            return;
        }

        if (!validatePushConstants("compute", "setComputeState"))
            return;

        m_CommandList->dispatchIndirect(offsetBytes);
    }

    void CommandListWrapper::setMeshletState(const MeshletState& state)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "setMeshletState"))
            return;

        bool anyErrors = false;
        if (!state.pipeline)
        {
            error("MeshletState::pipeline is NULL");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        if (!validateBindingSetsAgainstLayouts(state.pipeline->getDesc().bindingLayouts, state.bindings))
            anyErrors = true;

        if (anyErrors)
            return;

        evaluatePushConstantSize(state.pipeline->getDesc().bindingLayouts);

        m_CommandList->setMeshletState(state);

        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
        m_MeshletStateSet = true;
        m_RayTracingStateSet = false;
        m_PushConstantsSet = false;
        m_CurrentMeshletState = state;
    }

    void CommandListWrapper::dispatchMesh(uint32_t groupsX, uint32_t groupsY /*= 1*/, uint32_t groupsZ /*= 1*/)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Graphics, "dispatchMesh"))
            return;

        if (!m_MeshletStateSet)
        {
            error("Meshlet state is not set before a dispatchMesh call.\n"
                "Note that setting graphics or compute state invalidates the meshlet state.");
            return;
        }

        if (!validatePushConstants("meshlet", "setMeshletState"))
            return;

        m_CommandList->dispatchMesh(groupsX, groupsY, groupsZ);
    }

    void CommandListWrapper::beginTimerQuery(ITimerQuery* query)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTimerQuery(query);
    }

    void CommandListWrapper::endTimerQuery(ITimerQuery* query)
    {
        if (!requireOpenState())
            return;

        m_CommandList->endTimerQuery(query);
    }

    void CommandListWrapper::beginMarker(const char *name)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginMarker(name);
    }

    void CommandListWrapper::endMarker()
    {
        if (!requireOpenState())
            return;

        m_CommandList->endMarker();
    }

    void CommandListWrapper::setEnableAutomaticBarriers(bool enable)
    {
        if (!requireOpenState())
            return;
        
        m_CommandList->setEnableAutomaticBarriers(enable);
    }

    void CommandListWrapper::setResourceStatesForBindingSet(IBindingSet* bindingSet)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setResourceStatesForBindingSet(bindingSet);
    }

    void CommandListWrapper::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "setEnableUavBarriersForTexture"))
            return;

        m_CommandList->setEnableUavBarriersForTexture(texture, enableBarriers);
    }

    void CommandListWrapper::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "setEnableUavBarriersForBuffer"))
            return;

        m_CommandList->setEnableUavBarriersForBuffer(buffer, enableBarriers);
    }

    void CommandListWrapper::beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTrackingTextureState(texture, subresources, stateBits);
    }

    void CommandListWrapper::beginTrackingBufferState(IBuffer* buffer, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTrackingBufferState(buffer, stateBits);
    }

    void CommandListWrapper::setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setTextureState(texture, subresources, stateBits);
    }

    void CommandListWrapper::setBufferState(IBuffer* buffer, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setBufferState(buffer, stateBits);
    }

    void CommandListWrapper::setAccelStructState(rt::IAccelStruct* as, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setAccelStructState(checked_cast<rt::IAccelStruct*>(unwrapResource(as)), stateBits);
    }

    void CommandListWrapper::setPermanentTextureState(ITexture* texture, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setPermanentTextureState(texture, stateBits);
    }

    void CommandListWrapper::setPermanentBufferState(IBuffer* buffer, ResourceStates stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setPermanentBufferState(buffer, stateBits);
    }

    void CommandListWrapper::commitBarriers()
    {
        if (!requireOpenState())
            return;

        m_CommandList->commitBarriers();
    }

    ResourceStates CommandListWrapper::getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        if (!requireOpenState())
            return ResourceStates::Common;

        return m_CommandList->getTextureSubresourceState(texture, arraySlice, mipLevel);
    }

    ResourceStates CommandListWrapper::getBufferState(IBuffer* buffer)
    {
        if (!requireOpenState())
            return ResourceStates::Common;

        return m_CommandList->getBufferState(buffer);
    }

    void CommandListWrapper::clearState()
    {
        if (!requireOpenState())
            return;

        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
        m_MeshletStateSet = false;
        m_RayTracingStateSet = false;
        m_PushConstantsSet = false;

        m_CommandList->clearState();
    }

    IDevice* CommandListWrapper::getDevice()
    {
        return m_Device;
    }

    const CommandListParameters& CommandListWrapper::getDesc()
    {
        return m_CommandList->getDesc();
    }

    void CommandListWrapper::setRayTracingState(const rt::State& state)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "setRayTracingState"))
            return;

        evaluatePushConstantSize(state.shaderTable->getPipeline()->getDesc().globalBindingLayouts);

        m_CommandList->setRayTracingState(state);

        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
        m_MeshletStateSet = true;
        m_RayTracingStateSet = true;
        m_PushConstantsSet = false;
        m_CurrentRayTracingState = state;
    }

    void CommandListWrapper::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "dispatchRays"))
            return;

        if (!m_RayTracingStateSet)
        {
            error("Ray tracing state is not set before a dispatchRays call.\n"
                "Note that setting graphics or compute state invalidates the ray tracing state.");
            return;
        }

        if (!validatePushConstants("ray tracing", "setRayTracingState"))
            return;

        m_CommandList->dispatchRays(args);
    }

    void CommandListWrapper::compactBottomLevelAccelStructs()
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "compactBottomLevelAccelStructs"))
            return;

        m_CommandList->compactBottomLevelAccelStructs();
    }

    void CommandListWrapper::buildOpacityMicromap(rt::IOpacityMicromap* omm, const rt::OpacityMicromapDesc& desc) 
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "buildOpacityMicromap"))
            return;

        m_CommandList->buildOpacityMicromap(omm, desc);
    }

    void CommandListWrapper::buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::GeometryDesc* pGeometries, size_t numGeometries, rt::AccelStructBuildFlags buildFlags)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "buildBottomLevelAccelStruct"))
            return;

        rt::IAccelStruct* underlyingAS = as;

        AccelStructWrapper* wrapper = dynamic_cast<AccelStructWrapper*>(as);
        if (wrapper)
        {
            underlyingAS = wrapper->getUnderlyingObject();

            if (wrapper->isTopLevel)
            {
                error("Cannot perform buildBottomLevelAccelStruct on a top-level AS");
                return;
            }
            
            for (size_t i = 0; i < numGeometries; i++)
            {
                const auto& geom = pGeometries[i];

                if (geom.geometryType == rt::GeometryType::Triangles)
                {
                    const auto& triangles = geom.geometryData.triangles;

                    if (triangles.indexFormat != Format::UNKNOWN)
                    {
                        switch (triangles.indexFormat)  // NOLINT(clang-diagnostic-switch-enum)
                        {
                        case Format::R8_UINT:
                            if (m_Device->getGraphicsAPI() != GraphicsAPI::VULKAN)
                            {
                                std::stringstream ss;
                                ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                    << " has index format R8_UINT which is only supported on Vulkan";
                                error(ss.str());
                                return;
                            }
                            break;
                        case Format::R16_UINT:
                        case Format::R32_UINT:
                            break;
                        default: {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has unsupported index format: " << utils::FormatToString(triangles.indexFormat);
                            error(ss.str());
                            return;
                        }
                        }

                        if (triangles.indexBuffer == nullptr)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has a NULL index buffer but indexFormat is " << utils::FormatToString(triangles.indexFormat);
                            error(ss.str());
                            return;
                        }

                        const BufferDesc& indexBufferDesc = triangles.indexBuffer->getDesc();
                        if (!indexBufferDesc.isAccelStructBuildInput)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has index buffer = " << utils::DebugNameToString(indexBufferDesc.debugName)
                                << " which does not have the isAccelStructBuildInput flag set";
                            error(ss.str());
                            return;
                        }

                        const size_t indexSize = triangles.indexCount * getFormatInfo(triangles.indexFormat).bytesPerBlock;
                        if (triangles.indexOffset + indexSize > indexBufferDesc.byteSize)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " points at " << indexSize << " bytes of index data at offset " << triangles.indexOffset
                                << " in buffer " << utils::DebugNameToString(indexBufferDesc.debugName) << " whose size is " << indexBufferDesc.byteSize
                                << ", which will result in a buffer overrun";
                            error(ss.str());
                            return;
                        }

                        if ((triangles.indexCount % 3) != 0)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has indexCount = " << triangles.indexCount
                                << ", which is not a multiple of 3";
                            error(ss.str());
                            return;
                        }
                    }
                    else
                    {
                        if (triangles.indexCount != 0 || triangles.indexBuffer != nullptr)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has indexFormat = UNKNOWN but nonzero indexCount = " << triangles.indexCount;
                            error(ss.str());
                            return;
                        }

                        if (triangles.indexBuffer != nullptr)
                        {
                            std::stringstream ss;
                            ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                                << " has indexFormat = UNKNOWN but non-NULL indexBuffer = "
                                << utils::DebugNameToString(triangles.indexBuffer->getDesc().debugName);
                            error(ss.str());
                            return;
                        }
                    }

                    switch (triangles.vertexFormat)  // NOLINT(clang-diagnostic-switch-enum)
                    {
                    case Format::RG32_FLOAT:
                    case Format::RGB32_FLOAT:
                    case Format::RGBA32_FLOAT:
                    case Format::RG16_FLOAT:
                    case Format::RGBA16_FLOAT:
                    case Format::RG16_SNORM:
                    case Format::RGBA16_SNORM:
                    case Format::RGBA16_UNORM:
                    case Format::RG16_UNORM:
                    case Format::R10G10B10A2_UNORM:
                    case Format::RGBA8_UNORM:
                    case Format::RG8_UNORM:
                    case Format::RGBA8_SNORM:
                    case Format::RG8_SNORM:
                        break;
                    default: {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has unsupported vertex format: " << utils::FormatToString(triangles.vertexFormat);
                        error(ss.str());
                        return;
                    }
                    }

                    if (triangles.vertexBuffer == nullptr)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has NULL vertex buffer";
                        error(ss.str());
                        return;
                    }

                    if (triangles.vertexStride == 0)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has vertexStride = 0";
                        error(ss.str());
                        return;
                    }

                    if ((triangles.indexFormat == Format::UNKNOWN) && (triangles.vertexCount % 3) != 0)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has indexFormat = UNKNOWN and vertexCount = " << triangles.vertexCount
                            << ", which is not a multiple of 3";
                        error(ss.str());
                        return;
                    }

                    const BufferDesc& vertexBufferDesc = triangles.vertexBuffer->getDesc();
                    if (!vertexBufferDesc.isAccelStructBuildInput)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has vertex buffer = " << utils::DebugNameToString(vertexBufferDesc.debugName)
                            << " which does not have the isAccelStructBuildInput flag set";
                        error(ss.str());
                        return;
                    }

                    const size_t vertexDataSize = triangles.vertexCount * triangles.vertexStride;
                    if (triangles.vertexOffset + vertexDataSize > vertexBufferDesc.byteSize)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " points at " << vertexDataSize << " bytes of vertex data at offset " << triangles.vertexOffset
                            << " in buffer " << utils::DebugNameToString(vertexBufferDesc.debugName) << " whose size is " << vertexBufferDesc.byteSize
                            << ", which will result in a buffer overrun";
                        error(ss.str());
                        return;
                    }
                }
                else if (geom.geometryType == rt::GeometryType::AABBs)
                {
                    const auto& aabbs = geom.geometryData.aabbs;

                    if (aabbs.buffer== nullptr)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has NULL AABB data buffer";
                        error(ss.str());
                        return;
                    }

                    const BufferDesc& aabbBufferDesc = aabbs.buffer->getDesc();
                    if (!aabbBufferDesc.isAccelStructBuildInput)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has AABB data buffer = " << utils::DebugNameToString(aabbBufferDesc.debugName)
                            << " which does not have the isAccelStructBuildInput flag set";
                        error(ss.str());
                        return;
                    }

                    if (aabbs.count > 1 && aabbs.stride < sizeof(rt::GeometryAABB))
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has AABB stride = " << aabbs.stride
                            << " which is less than the size of one AABB (" << sizeof(rt::GeometryAABB) << " bytes)";
                        error(ss.str());
                        return;
                    }

                    const size_t aabbDataSize = aabbs.count * aabbs.stride;
                    if (aabbs.offset + aabbDataSize > aabbBufferDesc.byteSize)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " points at " << aabbDataSize << " bytes of AABB data at offset " << aabbs.offset
                            << " in buffer " << utils::DebugNameToString(aabbBufferDesc.debugName) << " whose size is " << aabbBufferDesc.byteSize
                            << ", which will result in a buffer overrun";
                        error(ss.str());
                        return;
                    }

                    if (geom.useTransform)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " is of type AABB but has useTransform = true, "
                            "which is unsupported, and the transform will be ignored";
                        m_MessageCallback->message(MessageSeverity::Warning, ss.str().c_str());
                    }
                }
                else if (geom.geometryType == rt::GeometryType::Spheres)
                {
                    const auto& spheres = geom.geometryData.spheres;

                    if (spheres.vertexBuffer == nullptr)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has NULL vertex buffer";
                        error(ss.str());
                        return;
                    }

                    // TODO: Add more validation
                }
                else if (geom.geometryType == rt::GeometryType::Lss)
                {
                    const auto& lss = geom.geometryData.lss;

                    if (lss.vertexBuffer == nullptr)
                    {
                        std::stringstream ss;
                        ss << "BLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build geometry " << i
                            << " has NULL vertex buffer";
                        error(ss.str());
                        return;
                    }

                    // TODO: Add more validation
                }
            }

            if ((buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0)
            {
                if (!wrapper->allowUpdate)
                {
                    std::stringstream ss;
                    ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                        << " that was not created with the AllowUpdate flag";
                    error(ss.str());
                    return;
                }

                if (!wrapper->wasBuilt)
                {
                    std::stringstream ss;
                    ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                        << " before the same BLAS was initially built";
                    error(ss.str());
                    return;
                }

                if (numGeometries != wrapper->buildGeometries.size())
                {
                    std::stringstream ss;
                    ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                        << " with " << numGeometries << " geometries "
                        "when this BLAS was built with " << wrapper->buildGeometries.size() << " geometries";
                    error(ss.str());
                    return;
                }
                
                for (size_t i = 0; i < numGeometries; i++)
                {
                    const auto& before = wrapper->buildGeometries[i];
                    const auto& after = pGeometries[i];

                    if (before.geometryType != after.geometryType)
                    {
                        std::stringstream ss;
                        ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                            << " with mismatching geometry types in slot " << i;
                        error(ss.str());
                        return;
                    }

                    if (before.geometryType == rt::GeometryType::Triangles)
                    {
                        uint32_t primitivesBefore = (before.geometryData.triangles.vertexFormat == Format::UNKNOWN)
                            ? before.geometryData.triangles.vertexCount
                            : before.geometryData.triangles.indexCount;

                        uint32_t primitivesAfter = (after.geometryData.triangles.vertexFormat == Format::UNKNOWN)
                            ? after.geometryData.triangles.vertexCount
                            : after.geometryData.triangles.indexCount;

                        primitivesBefore /= 3;
                        primitivesAfter /= 3;

                        if (primitivesBefore != primitivesAfter)
                        {
                            std::stringstream ss;
                            ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                                << " with mismatching triangle counts in geometry slot " << i << ": "
                                "built with " << primitivesBefore << " triangles, updating with " << primitivesAfter << " triangles";
                            error(ss.str());
                            return;
                        }
                    }
                    else // AABBs
                    {
                        uint32_t aabbsBefore = before.geometryData.aabbs.count;
                        uint32_t aabbsAfter = after.geometryData.aabbs.count;

                        if (aabbsBefore != aabbsAfter)
                        {
                            std::stringstream ss;
                            ss << "Cannot perform an update on BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                                << " with mismatching AABB counts in geometry slot " << i << ": "
                                "built with " << aabbsBefore << " AABBs, updating with " << aabbsAfter << " AABBs";
                            error(ss.str());
                            return;
                        }
                    }
                }
            }

            if (wrapper->allowCompaction && wrapper->wasBuilt)
            {
                std::stringstream ss;
                ss << "Cannot rebuild BLAS " << utils::DebugNameToString(as->getDesc().debugName)
                    << " that has the AllowCompaction flag set";
                error(ss.str());
                return;
            }

            wrapper->wasBuilt = true;
            wrapper->buildGeometries.assign(pGeometries, pGeometries + numGeometries);
        }

        m_CommandList->buildBottomLevelAccelStruct(underlyingAS, pGeometries, numGeometries, buildFlags);
    }

    bool CommandListWrapper::validateBuildTopLevelAccelStruct(AccelStructWrapper* wrapper, size_t numInstances, rt::AccelStructBuildFlags buildFlags) const
    {
        if (!wrapper->isTopLevel)
        {
            std::stringstream ss;
            ss << "Cannot perform buildTopLevelAccelStruct on a bottom-level AS "
                << utils::DebugNameToString(wrapper->getDesc().debugName);
            error(ss.str());
            return false;
        }

        if (numInstances > wrapper->maxInstances)
        {
            std::stringstream ss;
            ss << "Cannot build TLAS " << utils::DebugNameToString(wrapper->getDesc().debugName)
                << " with " << numInstances << " instances which is greater than topLevelMaxInstances "
                   " specified at creation (" << wrapper->maxInstances << ")";
            error(ss.str());
            return false;
        }

        if ((buildFlags & rt::AccelStructBuildFlags::PerformUpdate) != 0)
        {
            if (!wrapper->allowUpdate)
            {
                std::stringstream ss;
                ss << "Cannot perform an update on TLAS " << utils::DebugNameToString(wrapper->getDesc().debugName)
                    << " that was not created with the ALLOW_UPDATE flag";
                error(ss.str());
                return false;
            }

            if (!wrapper->wasBuilt)
            {
                std::stringstream ss;
                ss << "Cannot perform an update on TLAS " << utils::DebugNameToString(wrapper->getDesc().debugName)
                    << " before the same TLAS was initially built";
                error(ss.str());
                return false;
            }

            if (wrapper->buildInstances != numInstances)
            {
                std::stringstream ss;
                ss << "Cannot perform an update on TLAS " << utils::DebugNameToString(wrapper->getDesc().debugName)
                    << " with " << numInstances << " instances when this TLAS was built with "
                    << wrapper->buildInstances << " instances";
                error(ss.str());
                return false;
            }
        }

        return true;
    }


    void CommandListWrapper::buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::InstanceDesc* pInstances, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "buildTopLevelAccelStruct"))
            return;

        if (!as)
        {
            error("buildTopLevelAccelStruct: 'as' is NULL");
            return;
        }

        std::vector<rt::InstanceDesc> patchedInstances;
        patchedInstances.assign(pInstances, pInstances + numInstances);

        for (auto& instance : patchedInstances)
        {
            instance.bottomLevelAS = checked_cast<rt::IAccelStruct*>(unwrapResource(instance.bottomLevelAS));
        }

        rt::IAccelStruct* underlyingAS = as;

        AccelStructWrapper* wrapper = dynamic_cast<AccelStructWrapper*>(as);
        if (wrapper)
        {
            underlyingAS = wrapper->getUnderlyingObject();

            if (!validateBuildTopLevelAccelStruct(wrapper, numInstances, buildFlags))
                return;

            const bool allowEmptyInstances = (buildFlags & rt::AccelStructBuildFlags::AllowEmptyInstances) != 0;
            
            for (size_t i = 0; i < numInstances; i++)
            {
                const auto& instance = pInstances[i];

                if (instance.bottomLevelAS == nullptr)
                {
                    if (allowEmptyInstances)
                    {
                        continue;
                    }
                    else
                    {
                        std::stringstream ss;
                        ss << "TLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build instance " << i
                            << " has a NULL bottomLevelAS";
                        error(ss.str());
                        return;
                    }
                }

                AccelStructWrapper* blasWrapper = dynamic_cast<AccelStructWrapper*>(instance.bottomLevelAS);
                if (blasWrapper)
                {
                    if (blasWrapper->isTopLevel)
                    {
                        std::stringstream ss;
                        ss << "TLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build instance " << i
                            << " refers to another TLAS, which is unsupported";
                        error(ss.str());
                        return;
                    }

                    if (!blasWrapper->wasBuilt)
                    {
                        std::stringstream ss;
                        ss << "TLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build instance " << i
                            << " refers to a BLAS which was never built";
                        error(ss.str());
                        return;
                    }
                }

                if (instance.instanceMask == 0 && !allowEmptyInstances)
                {
                    std::stringstream ss;
                    ss << "TLAS " << utils::DebugNameToString(as->getDesc().debugName) << " build instance " << i
                        << " has instanceMask = 0, which means the instance "
                        "will never be included in any ray intersections";
                    m_MessageCallback->message(MessageSeverity::Warning, ss.str().c_str());
                }
            }
            
            wrapper->wasBuilt = true;
            wrapper->buildInstances = numInstances;
        }
        m_CommandList->buildTopLevelAccelStruct(underlyingAS, patchedInstances.data(), uint32_t(patchedInstances.size()), buildFlags);
    }

    void CommandListWrapper::buildTopLevelAccelStructFromBuffer(rt::IAccelStruct* as, nvrhi::IBuffer* instanceBuffer, uint64_t instanceBufferOffset, size_t numInstances, rt::AccelStructBuildFlags buildFlags)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "buildTopLevelAccelStruct"))
            return;

        if (!as)
        {
            error("buildTopLevelAccelStructFromBuffer: 'as' is NULL");
            return;
        }

        if (!instanceBuffer)
        {
            error("buildTopLevelAccelStructFromBuffer: 'instanceBuffer' is NULL");
            return;
        }

        rt::IAccelStruct* underlyingAS = as;

        AccelStructWrapper* wrapper = dynamic_cast<AccelStructWrapper*>(as);
        if (wrapper)
        {
            underlyingAS = wrapper->getUnderlyingObject();

            if (!validateBuildTopLevelAccelStruct(wrapper, numInstances, buildFlags))
                return;
        }

        auto bufferDesc = instanceBuffer->getDesc();
        if (!bufferDesc.isAccelStructBuildInput)
        {
            std::stringstream ss;
            ss << "Buffer " << utils::DebugNameToString(bufferDesc.debugName) << " used in buildTopLevelAccelStructFromBuffer "
                "doesn't have the 'isAccelStructBuildInput' flag set";
            error(ss.str());
            return;
        }

        uint64_t sizeOfData = numInstances * sizeof(rt::InstanceDesc);
        if (bufferDesc.byteSize < instanceBufferOffset + sizeOfData)
        {
            std::stringstream ss;
            ss << "Buffer " << utils::DebugNameToString(bufferDesc.debugName) << " used in buildTopLevelAccelStructFromBuffer "
                "is smaller than the referenced instance data: " << sizeOfData << " bytes used at offset " << instanceBufferOffset
                << ", buffer size is " << bufferDesc.byteSize << " bytes";
            error(ss.str());
            return;
        }

        m_CommandList->buildTopLevelAccelStructFromBuffer(underlyingAS, instanceBuffer, instanceBufferOffset, numInstances, buildFlags);
    }

    void CommandListWrapper::executeMultiIndirectClusterOperation(const rt::cluster::OperationDesc& desc)
    {
        if (!requireOpenState())
            return;

        if (!requireType(CommandQueue::Compute, "executeMultiIndirectClusterOperation"))
            return;

        if (!m_Device->validateClusterOperationParams(desc.params))
            return;

        if (desc.inIndirectArgCountBuffer == nullptr && desc.params.maxArgCount == 0)
        {
            error("executeMultiIndirectClusterOperation: 'inIndirectArgCountBuffer' is NULL and maxArgCount is 0");
            return;
        }

        if (desc.inIndirectArgsBuffer == nullptr)
        {
            error("executeMultiIndirectClusterOperation: 'inIndirectArgsBuffer' is NULL");
            return;
        }

        if (desc.scratchSizeInBytes == 0)
        {
            error("executeMultiIndirectClusterOperation: 'scratchSizeInBytes' is 0");
            return;
        }

        if (desc.params.mode == rt::cluster::OperationMode::ImplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr)
            {
                error("executeMultiIndirectClusterOperation (cluster::OperationMode::ImplicitDestinations): 'inOutAddressesBuffer' is NULL");
                return;
            }
            if (desc.outAccelerationStructuresBuffer == nullptr)
            {
                error("executeMultiIndirectClusterOperation (cluster::OperationMode::ImplicitDestinations): 'outAccelerationStructuresBuffer' is NULL");
                return;
            }
        }
        else if (desc.params.mode == rt::cluster::OperationMode::ExplicitDestinations)
        {
            if (desc.inOutAddressesBuffer == nullptr)
            {
                error("executeMultiIndirectClusterOperation (cluster::OperationMode::ExplicitDestinations): 'inOutAddressesBuffer' is NULL");
                return;
            }
        }
        else if (desc.params.mode == rt::cluster::OperationMode::GetSizes)
        {
            if (desc.outSizesBuffer == nullptr)
            {
                error("executeMultiIndirectClusterOperation (cluster::OperationMode::GetSizes): 'outSizesBuffer' is NULL");
                return;
            }
        }

        m_CommandList->executeMultiIndirectClusterOperation(desc);
    }

    void CommandListWrapper::evaluatePushConstantSize(const nvrhi::BindingLayoutVector& bindingLayouts)
    {
        m_PipelinePushConstantSize = 0;

        // Find the first PushConstants entry.
        // Assumes that the binding layout vector has been validated for duplicated push constants entries.

        for (const auto& layout : bindingLayouts)
        {
            const BindingLayoutDesc* layoutDesc = layout->getDesc();

            if (!layoutDesc) // bindless layouts have null desc
                continue;

            for (const auto& item : layoutDesc->bindings)
            {
                if (item.type == ResourceType::PushConstants)
                {
                    m_PipelinePushConstantSize = item.size;
                    return;
                }
            }
        }
    }

    bool CommandListWrapper::validatePushConstants(const char* pipelineType, const char* stateFunctionName) const
    {
        if (m_PipelinePushConstantSize != 0 && !m_PushConstantsSet)
        {
            std::stringstream ss;
            ss << "The " << pipelineType << " pipeline expects push constants (" << m_PipelinePushConstantSize << " bytes) that were not set." << std::endl;
            ss << "Push constants must be set after each call to " << stateFunctionName << ".";

            error(ss.str());

            return false;
        }

        return true;
    }
    
} // namespace nvrhi::validation
