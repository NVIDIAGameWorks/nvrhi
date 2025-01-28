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

#include "d3d11-backend.h"
#include <nvrhi/utils.h>

namespace nvrhi::d3d11
{
    DXGI_FORMAT convertFormat(nvrhi::Format format)
    {
        return getDxgiFormatMapping(format).srvFormat;
    }
    
    D3D11_BLEND convertBlendValue(BlendFactor value)
    {
        switch (value)
        {
        case BlendFactor::Zero:
            return D3D11_BLEND_ZERO;
        case BlendFactor::One:
            return D3D11_BLEND_ONE;
        case BlendFactor::SrcColor:
            return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:
            return D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:
            return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha:
            return D3D11_BLEND_INV_DEST_ALPHA;
        case BlendFactor::DstColor:
            return D3D11_BLEND_DEST_COLOR;
        case BlendFactor::InvDstColor:
            return D3D11_BLEND_INV_DEST_COLOR;
        case BlendFactor::SrcAlphaSaturate:
            return D3D11_BLEND_SRC_ALPHA_SAT;
        case BlendFactor::ConstantColor:
            return D3D11_BLEND_BLEND_FACTOR;
        case BlendFactor::InvConstantColor:
            return D3D11_BLEND_INV_BLEND_FACTOR;
        case BlendFactor::Src1Color:
            return D3D11_BLEND_SRC1_COLOR;
        case BlendFactor::InvSrc1Color:
            return D3D11_BLEND_INV_SRC1_COLOR;
        case BlendFactor::Src1Alpha:
            return D3D11_BLEND_SRC1_ALPHA;
        case BlendFactor::InvSrc1Alpha:
            return D3D11_BLEND_INV_SRC1_ALPHA;
        default:
            utils::InvalidEnum();
            return D3D11_BLEND_ZERO;
        }
    }

    D3D11_BLEND_OP convertBlendOp(BlendOp value)
    {
        switch (value)
        {
        case BlendOp::Add:
            return D3D11_BLEND_OP_ADD;
        case BlendOp::Subrtact:
            return D3D11_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return D3D11_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min:
            return D3D11_BLEND_OP_MIN;
        case BlendOp::Max:
            return D3D11_BLEND_OP_MAX;
        default:
            utils::InvalidEnum();
            return D3D11_BLEND_OP_ADD;
        }
    }

    D3D11_STENCIL_OP convertStencilOp(StencilOp value)
    {
        switch (value)
        {
        case StencilOp::Keep:
            return D3D11_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return D3D11_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return D3D11_STENCIL_OP_REPLACE;
        case StencilOp::IncrementAndClamp:
            return D3D11_STENCIL_OP_INCR_SAT;
        case StencilOp::DecrementAndClamp:
            return D3D11_STENCIL_OP_DECR_SAT;
        case StencilOp::Invert:
            return D3D11_STENCIL_OP_INVERT;
        case StencilOp::IncrementAndWrap:
            return D3D11_STENCIL_OP_INCR;
        case StencilOp::DecrementAndWrap:
            return D3D11_STENCIL_OP_DECR;
        default:
            utils::InvalidEnum();
            return D3D11_STENCIL_OP_KEEP;
        }
    }

    D3D11_COMPARISON_FUNC convertComparisonFunc(ComparisonFunc value)
    {
        switch (value)
        {
        case ComparisonFunc::Never:
            return D3D11_COMPARISON_NEVER;
        case ComparisonFunc::Less:
            return D3D11_COMPARISON_LESS;
        case ComparisonFunc::Equal:
            return D3D11_COMPARISON_EQUAL;
        case ComparisonFunc::LessOrEqual:
            return D3D11_COMPARISON_LESS_EQUAL;
        case ComparisonFunc::Greater:
            return D3D11_COMPARISON_GREATER;
        case ComparisonFunc::NotEqual:
            return D3D11_COMPARISON_NOT_EQUAL;
        case ComparisonFunc::GreaterOrEqual:
            return D3D11_COMPARISON_GREATER_EQUAL;
        case ComparisonFunc::Always:
            return D3D11_COMPARISON_ALWAYS;
        default:
            utils::InvalidEnum();
            return D3D11_COMPARISON_NEVER;
        }
    }

    D3D_PRIMITIVE_TOPOLOGY convertPrimType(PrimitiveType pt, uint32_t controlPoints)
    {
        //setup the primitive type
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
            utils::InvalidEnum();
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    D3D11_TEXTURE_ADDRESS_MODE convertSamplerAddressMode(SamplerAddressMode mode)
    {
        switch (mode)
        {
        case SamplerAddressMode::Clamp:
            return D3D11_TEXTURE_ADDRESS_CLAMP;
        case SamplerAddressMode::Wrap:
            return D3D11_TEXTURE_ADDRESS_WRAP;
        case SamplerAddressMode::Border:
            return D3D11_TEXTURE_ADDRESS_BORDER;
        case SamplerAddressMode::Mirror:
            return D3D11_TEXTURE_ADDRESS_MIRROR;
        case SamplerAddressMode::MirrorOnce:
            return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        default:
            utils::InvalidEnum();
            return D3D11_TEXTURE_ADDRESS_CLAMP;
        }
    }

    UINT convertSamplerReductionType(SamplerReductionType reductionType)
    {
        switch (reductionType)
        {
        case SamplerReductionType::Standard:
            return D3D11_FILTER_REDUCTION_TYPE_STANDARD;
        case SamplerReductionType::Comparison:
            return D3D11_FILTER_REDUCTION_TYPE_COMPARISON;
        case SamplerReductionType::Minimum:
            return D3D11_FILTER_REDUCTION_TYPE_MINIMUM;
        case SamplerReductionType::Maximum:
            return D3D11_FILTER_REDUCTION_TYPE_MAXIMUM;
        default:
            utils::InvalidEnum();
            return D3D11_FILTER_REDUCTION_TYPE_STANDARD;
        }
    }


} // namespace nvrhi::d3d11
