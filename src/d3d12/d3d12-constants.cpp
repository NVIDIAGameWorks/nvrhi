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

namespace nvrhi::d3d12
{
    DXGI_FORMAT convertFormat(nvrhi::Format format)
    {
        return getDxgiFormatMapping(format).srvFormat;
    }
    
    D3D12_SHADER_VISIBILITY convertShaderStage(ShaderType s)
    {
        switch (s)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case ShaderType::Vertex:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case ShaderType::Hull:
            return D3D12_SHADER_VISIBILITY_HULL;
        case ShaderType::Domain:
            return D3D12_SHADER_VISIBILITY_DOMAIN;
        case ShaderType::Geometry:
            return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case ShaderType::Pixel:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        case ShaderType::Amplification:
            return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
        case ShaderType::Mesh:
            return D3D12_SHADER_VISIBILITY_MESH;

        default:
            // catch-all case - actually some of the bitfield combinations are unrepresentable in DX12
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    D3D12_BLEND convertBlendValue(BlendFactor value)
    {
        switch (value)
        {
        case BlendFactor::Zero:
            return D3D12_BLEND_ZERO;
        case BlendFactor::One:
            return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor:
            return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:
            return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::DstColor:
            return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::InvDstColor:
            return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::SrcAlphaSaturate:
            return D3D12_BLEND_SRC_ALPHA_SAT;
        case BlendFactor::ConstantColor:
            return D3D12_BLEND_BLEND_FACTOR;
        case BlendFactor::InvConstantColor:
            return D3D12_BLEND_INV_BLEND_FACTOR;
        case BlendFactor::Src1Color:
            return D3D12_BLEND_SRC1_COLOR;
        case BlendFactor::InvSrc1Color:
            return D3D12_BLEND_INV_SRC1_COLOR;
        case BlendFactor::Src1Alpha:
            return D3D12_BLEND_SRC1_ALPHA;
        case BlendFactor::InvSrc1Alpha:
            return D3D12_BLEND_INV_SRC1_ALPHA;
        default:
            utils::InvalidEnum();
            return D3D12_BLEND_ZERO;
        }
    }

    D3D12_BLEND_OP convertBlendOp(BlendOp value)
    {
        switch (value)
        {
        case BlendOp::Add:
            return D3D12_BLEND_OP_ADD;
        case BlendOp::Subrtact:
            return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min:
            return D3D12_BLEND_OP_MIN;
        case BlendOp::Max:
            return D3D12_BLEND_OP_MAX;
        default:
            utils::InvalidEnum();
            return D3D12_BLEND_OP_ADD;
        }
    }

    D3D12_STENCIL_OP convertStencilOp(StencilOp value)
    {
        switch (value)
        {
        case StencilOp::Keep:
            return D3D12_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return D3D12_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return D3D12_STENCIL_OP_REPLACE;
        case StencilOp::IncrementAndClamp:
            return D3D12_STENCIL_OP_INCR_SAT;
        case StencilOp::DecrementAndClamp:
            return D3D12_STENCIL_OP_DECR_SAT;
        case StencilOp::Invert:
            return D3D12_STENCIL_OP_INVERT;
        case StencilOp::IncrementAndWrap:
            return D3D12_STENCIL_OP_INCR;
        case StencilOp::DecrementAndWrap:
            return D3D12_STENCIL_OP_DECR;
        default:
            utils::InvalidEnum();
            return D3D12_STENCIL_OP_KEEP;
        }
    }

    D3D12_COMPARISON_FUNC convertComparisonFunc(ComparisonFunc value)
    {
        switch (value)
        {
        case ComparisonFunc::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case ComparisonFunc::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case ComparisonFunc::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case ComparisonFunc::LessOrEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case ComparisonFunc::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case ComparisonFunc::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case ComparisonFunc::GreaterOrEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case ComparisonFunc::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            utils::InvalidEnum();
            return D3D12_COMPARISON_FUNC_NEVER;
        }
    }
    D3D_PRIMITIVE_TOPOLOGY convertPrimitiveType(PrimitiveType pt, uint32_t controlPoints)
    {
        switch (pt)
        {
        case PrimitiveType::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveType::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveType::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case PrimitiveType::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case PrimitiveType::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case PrimitiveType::TriangleFan:
            utils::NotSupported();
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        case PrimitiveType::TriangleListWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        case PrimitiveType::TriangleStripWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        case PrimitiveType::PatchList:
            if (controlPoints == 0 || controlPoints > 32)
            {
                utils::InvalidEnum();
                return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            }
            return D3D_PRIMITIVE_TOPOLOGY(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + (controlPoints - 1));
        default:
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    D3D12_TEXTURE_ADDRESS_MODE convertSamplerAddressMode(SamplerAddressMode mode)
    {
        switch (mode)
        {
        case SamplerAddressMode::Clamp:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case SamplerAddressMode::Wrap:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case SamplerAddressMode::Border:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case SamplerAddressMode::Mirror:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case SamplerAddressMode::MirrorOnce:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        default:
            utils::InvalidEnum();
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        }
    }
    
    UINT convertSamplerReductionType(SamplerReductionType reductionType)
    {
        switch (reductionType)
        {
        case SamplerReductionType::Standard:
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        case SamplerReductionType::Comparison:
            return D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
        case SamplerReductionType::Minimum:
            return D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
        case SamplerReductionType::Maximum:
            return D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
        default:
            utils::InvalidEnum();
            return D3D12_FILTER_REDUCTION_TYPE_STANDARD;
        }
    }

    D3D12_RESOURCE_STATES convertResourceStates(ResourceStates stateBits)
    {
        if (stateBits == ResourceStates::Common)
            return D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON; // also 0

        if ((stateBits & ResourceStates::ConstantBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if ((stateBits & ResourceStates::VertexBuffer) != 0) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if ((stateBits & ResourceStates::IndexBuffer) != 0) result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
        if ((stateBits & ResourceStates::IndirectArgument) != 0) result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        if ((stateBits & ResourceStates::ShaderResource) != 0) result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & ResourceStates::UnorderedAccess) != 0) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if ((stateBits & ResourceStates::RenderTarget) != 0) result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        if ((stateBits & ResourceStates::DepthWrite) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if ((stateBits & ResourceStates::DepthRead) != 0) result |= D3D12_RESOURCE_STATE_DEPTH_READ;
        if ((stateBits & ResourceStates::StreamOut) != 0) result |= D3D12_RESOURCE_STATE_STREAM_OUT;
        if ((stateBits & ResourceStates::CopyDest) != 0) result |= D3D12_RESOURCE_STATE_COPY_DEST;
        if ((stateBits & ResourceStates::CopySource) != 0) result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        if ((stateBits & ResourceStates::ResolveDest) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
        if ((stateBits & ResourceStates::ResolveSource) != 0) result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        if ((stateBits & ResourceStates::Present) != 0) result |= D3D12_RESOURCE_STATE_PRESENT;
        if ((stateBits & ResourceStates::AccelStructRead) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & ResourceStates::AccelStructWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & ResourceStates::AccelStructBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & ResourceStates::AccelStructBuildBlas) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        if ((stateBits & ResourceStates::ShadingRateSurface) != 0) result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
        if ((stateBits & ResourceStates::OpacityMicromapBuildInput) != 0) result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if ((stateBits & ResourceStates::OpacityMicromapWrite) != 0) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        return result;
    }

    D3D12_SHADING_RATE convertPixelShadingRate(VariableShadingRate shadingRate)
    {
        switch (shadingRate)
        {
        case VariableShadingRate::e1x2:
            return D3D12_SHADING_RATE_1X2;
        case VariableShadingRate::e2x1:
            return D3D12_SHADING_RATE_2X1;
        case VariableShadingRate::e2x2:
            return D3D12_SHADING_RATE_2X2;
        case VariableShadingRate::e2x4:
            return D3D12_SHADING_RATE_2X4;
        case VariableShadingRate::e4x2:
            return D3D12_SHADING_RATE_4X2;
        case VariableShadingRate::e4x4:
            return D3D12_SHADING_RATE_4X4;
        case VariableShadingRate::e1x1:
        default:
            return D3D12_SHADING_RATE_1X1;
        }
    }

    D3D12_SHADING_RATE_COMBINER convertShadingRateCombiner(ShadingRateCombiner combiner)
    {
        switch (combiner)
        {
        case ShadingRateCombiner::Override:
            return D3D12_SHADING_RATE_COMBINER_OVERRIDE;
        case ShadingRateCombiner::Min:
            return D3D12_SHADING_RATE_COMBINER_MIN;
        case ShadingRateCombiner::Max:
            return D3D12_SHADING_RATE_COMBINER_MAX;
        case ShadingRateCombiner::ApplyRelative:
            return D3D12_SHADING_RATE_COMBINER_SUM;
        case ShadingRateCombiner::Passthrough:
        default:
            return D3D12_SHADING_RATE_COMBINER_PASSTHROUGH;
        }
    }

} // namespace nvrhi::d3d12
