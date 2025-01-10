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

#include <array>
#include <assert.h>

#include "vulkan-backend.h"

#define ENABLE_SHORTCUT_CONVERSIONS 1

namespace nvrhi::vulkan
{

    struct FormatMapping
    {
        nvrhi::Format rhiFormat;
        VkFormat vkFormat;
    };

    static const std::array<FormatMapping, size_t(Format::COUNT)> c_FormatMap = { {
        { Format::UNKNOWN,           VK_FORMAT_UNDEFINED                },
        { Format::R8_UINT,           VK_FORMAT_R8_UINT                  },
        { Format::R8_SINT,           VK_FORMAT_R8_SINT                  },
        { Format::R8_UNORM,          VK_FORMAT_R8_UNORM                 },
        { Format::R8_SNORM,          VK_FORMAT_R8_SNORM                 },
        { Format::RG8_UINT,          VK_FORMAT_R8G8_UINT                },
        { Format::RG8_SINT,          VK_FORMAT_R8G8_SINT                },
        { Format::RG8_UNORM,         VK_FORMAT_R8G8_UNORM               },
        { Format::RG8_SNORM,         VK_FORMAT_R8G8_SNORM               },
        { Format::R16_UINT,          VK_FORMAT_R16_UINT                 },
        { Format::R16_SINT,          VK_FORMAT_R16_SINT                 },
        { Format::R16_UNORM,         VK_FORMAT_R16_UNORM                },
        { Format::R16_SNORM,         VK_FORMAT_R16_SNORM                },
        { Format::R16_FLOAT,         VK_FORMAT_R16_SFLOAT               },
        { Format::BGRA4_UNORM,       VK_FORMAT_B4G4R4A4_UNORM_PACK16    },
        { Format::B5G6R5_UNORM,      VK_FORMAT_B5G6R5_UNORM_PACK16      },
        { Format::B5G5R5A1_UNORM,    VK_FORMAT_B5G5R5A1_UNORM_PACK16    },
        { Format::RGBA8_UINT,        VK_FORMAT_R8G8B8A8_UINT            },
        { Format::RGBA8_SINT,        VK_FORMAT_R8G8B8A8_SINT            },
        { Format::RGBA8_UNORM,       VK_FORMAT_R8G8B8A8_UNORM           },
        { Format::RGBA8_SNORM,       VK_FORMAT_R8G8B8A8_SNORM           },
        { Format::BGRA8_UNORM,       VK_FORMAT_B8G8R8A8_UNORM           },
        { Format::SRGBA8_UNORM,      VK_FORMAT_R8G8B8A8_SRGB            },
        { Format::SBGRA8_UNORM,      VK_FORMAT_B8G8R8A8_SRGB            },
        { Format::R10G10B10A2_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32 },
        { Format::R11G11B10_FLOAT,   VK_FORMAT_B10G11R11_UFLOAT_PACK32  },
        { Format::RG16_UINT,         VK_FORMAT_R16G16_UINT              },
        { Format::RG16_SINT,         VK_FORMAT_R16G16_SINT              },
        { Format::RG16_UNORM,        VK_FORMAT_R16G16_UNORM             },
        { Format::RG16_SNORM,        VK_FORMAT_R16G16_SNORM             },
        { Format::RG16_FLOAT,        VK_FORMAT_R16G16_SFLOAT            },
        { Format::R32_UINT,          VK_FORMAT_R32_UINT                 },
        { Format::R32_SINT,          VK_FORMAT_R32_SINT                 },
        { Format::R32_FLOAT,         VK_FORMAT_R32_SFLOAT               },
        { Format::RGBA16_UINT,       VK_FORMAT_R16G16B16A16_UINT        },
        { Format::RGBA16_SINT,       VK_FORMAT_R16G16B16A16_SINT        },
        { Format::RGBA16_FLOAT,      VK_FORMAT_R16G16B16A16_SFLOAT      },
        { Format::RGBA16_UNORM,      VK_FORMAT_R16G16B16A16_UNORM       },
        { Format::RGBA16_SNORM,      VK_FORMAT_R16G16B16A16_SNORM       },
        { Format::RG32_UINT,         VK_FORMAT_R32G32_UINT              },
        { Format::RG32_SINT,         VK_FORMAT_R32G32_SINT              },
        { Format::RG32_FLOAT,        VK_FORMAT_R32G32_SFLOAT            },
        { Format::RGB32_UINT,        VK_FORMAT_R32G32B32_UINT           },
        { Format::RGB32_SINT,        VK_FORMAT_R32G32B32_SINT           },
        { Format::RGB32_FLOAT,       VK_FORMAT_R32G32B32_SFLOAT         },
        { Format::RGBA32_UINT,       VK_FORMAT_R32G32B32A32_UINT        },
        { Format::RGBA32_SINT,       VK_FORMAT_R32G32B32A32_SINT        },
        { Format::RGBA32_FLOAT,      VK_FORMAT_R32G32B32A32_SFLOAT      },
        { Format::D16,               VK_FORMAT_D16_UNORM                },
        { Format::D24S8,             VK_FORMAT_D24_UNORM_S8_UINT        },
        { Format::X24G8_UINT,        VK_FORMAT_D24_UNORM_S8_UINT        },
        { Format::D32,               VK_FORMAT_D32_SFLOAT               },
        { Format::D32S8,             VK_FORMAT_D32_SFLOAT_S8_UINT       },
        { Format::X32G8_UINT,        VK_FORMAT_D32_SFLOAT_S8_UINT       },
        { Format::BC1_UNORM,         VK_FORMAT_BC1_RGBA_UNORM_BLOCK     },
        { Format::BC1_UNORM_SRGB,    VK_FORMAT_BC1_RGBA_SRGB_BLOCK      },
        { Format::BC2_UNORM,         VK_FORMAT_BC2_UNORM_BLOCK          },
        { Format::BC2_UNORM_SRGB,    VK_FORMAT_BC2_SRGB_BLOCK           },
        { Format::BC3_UNORM,         VK_FORMAT_BC3_UNORM_BLOCK          },
        { Format::BC3_UNORM_SRGB,    VK_FORMAT_BC3_SRGB_BLOCK           },
        { Format::BC4_UNORM,         VK_FORMAT_BC4_UNORM_BLOCK          },
        { Format::BC4_SNORM,         VK_FORMAT_BC4_SNORM_BLOCK          },
        { Format::BC5_UNORM,         VK_FORMAT_BC5_UNORM_BLOCK          },
        { Format::BC5_SNORM,         VK_FORMAT_BC5_SNORM_BLOCK          },
        { Format::BC6H_UFLOAT,       VK_FORMAT_BC6H_UFLOAT_BLOCK        },
        { Format::BC6H_SFLOAT,       VK_FORMAT_BC6H_SFLOAT_BLOCK        },
        { Format::BC7_UNORM,         VK_FORMAT_BC7_UNORM_BLOCK          },
        { Format::BC7_UNORM_SRGB,    VK_FORMAT_BC7_SRGB_BLOCK           },

    } };

    VkFormat convertFormat(nvrhi::Format format)
    {
        assert(format < nvrhi::Format::COUNT);
        assert(c_FormatMap[uint32_t(format)].rhiFormat == format);

        return c_FormatMap[uint32_t(format)].vkFormat;
    }
    
    vk::SamplerAddressMode convertSamplerAddressMode(SamplerAddressMode mode)
    {
        switch(mode)
        {
            case SamplerAddressMode::ClampToEdge:
                return vk::SamplerAddressMode::eClampToEdge;

            case SamplerAddressMode::Repeat:
                return vk::SamplerAddressMode::eRepeat;

            case SamplerAddressMode::ClampToBorder:
                return vk::SamplerAddressMode::eClampToBorder;

            case SamplerAddressMode::MirroredRepeat:
                return vk::SamplerAddressMode::eMirroredRepeat;

            case SamplerAddressMode::MirrorClampToEdge:
                return vk::SamplerAddressMode::eMirrorClampToEdge;

            default:
                utils::InvalidEnum();
                return vk::SamplerAddressMode(0);
        }
    }

    vk::PipelineStageFlagBits2 convertShaderTypeToPipelineStageFlagBits(ShaderType shaderType)
    {
        if (shaderType == ShaderType::All)
            return vk::PipelineStageFlagBits2::eAllCommands;

        uint32_t result = 0;

        if ((shaderType & ShaderType::Compute) != 0)        result |= uint32_t(vk::PipelineStageFlagBits2::eComputeShader);
        if ((shaderType & ShaderType::Vertex) != 0)         result |= uint32_t(vk::PipelineStageFlagBits2::eVertexShader);
        if ((shaderType & ShaderType::Hull) != 0)           result |= uint32_t(vk::PipelineStageFlagBits2::eTessellationControlShader);
        if ((shaderType & ShaderType::Domain) != 0)         result |= uint32_t(vk::PipelineStageFlagBits2::eTessellationEvaluationShader);
        if ((shaderType & ShaderType::Geometry) != 0)       result |= uint32_t(vk::PipelineStageFlagBits2::eGeometryShader);
        if ((shaderType & ShaderType::Pixel) != 0)          result |= uint32_t(vk::PipelineStageFlagBits2::eFragmentShader);
        if ((shaderType & ShaderType::Amplification) != 0)  result |= uint32_t(vk::PipelineStageFlagBits2::eTaskShaderNV);
        if ((shaderType & ShaderType::Mesh) != 0)           result |= uint32_t(vk::PipelineStageFlagBits2::eMeshShaderNV);
        if ((shaderType & ShaderType::AllRayTracing) != 0)  result |= uint32_t(vk::PipelineStageFlagBits2::eRayTracingShaderKHR); // or eRayTracingShaderNV, they have the same value

        return vk::PipelineStageFlagBits2(result);
    }

    vk::ShaderStageFlagBits convertShaderTypeToShaderStageFlagBits(ShaderType shaderType)
    {
        if (shaderType == ShaderType::All)
            return vk::ShaderStageFlagBits::eAll;

        if (shaderType == ShaderType::AllGraphics)
            return vk::ShaderStageFlagBits::eAllGraphics;

#if ENABLE_SHORTCUT_CONVERSIONS
        static_assert(uint32_t(ShaderType::Vertex)        == uint32_t(VK_SHADER_STAGE_VERTEX_BIT));
        static_assert(uint32_t(ShaderType::Hull)          == uint32_t(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT));
        static_assert(uint32_t(ShaderType::Domain)        == uint32_t(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT));
        static_assert(uint32_t(ShaderType::Geometry)      == uint32_t(VK_SHADER_STAGE_GEOMETRY_BIT));
        static_assert(uint32_t(ShaderType::Pixel)         == uint32_t(VK_SHADER_STAGE_FRAGMENT_BIT));
        static_assert(uint32_t(ShaderType::Compute)       == uint32_t(VK_SHADER_STAGE_COMPUTE_BIT));
        static_assert(uint32_t(ShaderType::Amplification) == uint32_t(VK_SHADER_STAGE_TASK_BIT_NV));
        static_assert(uint32_t(ShaderType::Mesh)          == uint32_t(VK_SHADER_STAGE_MESH_BIT_NV));
        static_assert(uint32_t(ShaderType::RayGeneration) == uint32_t(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
        static_assert(uint32_t(ShaderType::ClosestHit)    == uint32_t(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
        static_assert(uint32_t(ShaderType::AnyHit)        == uint32_t(VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
        static_assert(uint32_t(ShaderType::Miss)          == uint32_t(VK_SHADER_STAGE_MISS_BIT_KHR));
        static_assert(uint32_t(ShaderType::Intersection)  == uint32_t(VK_SHADER_STAGE_INTERSECTION_BIT_KHR));
        static_assert(uint32_t(ShaderType::Callable)      == uint32_t(VK_SHADER_STAGE_CALLABLE_BIT_KHR));

        return vk::ShaderStageFlagBits(shaderType);
#else
        uint32_t result = 0;

        if ((shaderType & ShaderType::Compute) != 0)        result |= uint32_t(vk::ShaderStageFlagBits::eCompute);
        if ((shaderType & ShaderType::Vertex) != 0)         result |= uint32_t(vk::ShaderStageFlagBits::eVertex);
        if ((shaderType & ShaderType::Hull) != 0)           result |= uint32_t(vk::ShaderStageFlagBits::eTessellationControl);
        if ((shaderType & ShaderType::Domain) != 0)         result |= uint32_t(vk::ShaderStageFlagBits::eTessellationEvaluation);
        if ((shaderType & ShaderType::Geometry) != 0)       result |= uint32_t(vk::ShaderStageFlagBits::eGeometry);
        if ((shaderType & ShaderType::Pixel) != 0)          result |= uint32_t(vk::ShaderStageFlagBits::eFragment);
        if ((shaderType & ShaderType::Amplification) != 0)  result |= uint32_t(vk::ShaderStageFlagBits::eTaskNV);
        if ((shaderType & ShaderType::Mesh) != 0)           result |= uint32_t(vk::ShaderStageFlagBits::eMeshNV);
        if ((shaderType & ShaderType::RayGeneration) != 0)  result |= uint32_t(vk::ShaderStageFlagBits::eRaygenKHR); // or eRaygenNV, they have the same value
        if ((shaderType & ShaderType::Miss) != 0)           result |= uint32_t(vk::ShaderStageFlagBits::eMissKHR);   // same etc...
        if ((shaderType & ShaderType::ClosestHit) != 0)     result |= uint32_t(vk::ShaderStageFlagBits::eClosestHitKHR);
        if ((shaderType & ShaderType::AnyHit) != 0)         result |= uint32_t(vk::ShaderStageFlagBits::eAnyHitKHR);
        if ((shaderType & ShaderType::Intersection) != 0)   result |= uint32_t(vk::ShaderStageFlagBits::eIntersectionKHR);

        return vk::ShaderStageFlagBits(result);
#endif
    }

    struct ResourceStateMappingInternal
    {
        ResourceStates nvrhiState;
        vk::PipelineStageFlags2 stageFlags;
        vk::AccessFlags2 accessMask;
        vk::ImageLayout imageLayout;

        ResourceStateMapping AsResourceStateMapping() const 
        {
            // It's safe to cast vk::AccessFlags2 -> vk::AccessFlags and vk::PipelineStageFlags2 -> vk::PipelineStageFlags (as long as the enum exist in both versions!),
            // synchronization2 spec says: "The new flags are identical to the old values within the 32-bit range, with new stages and bits beyond that."
            // The below stages are exclustive to synchronization2
            assert((stageFlags & vk::PipelineStageFlagBits2::eMicromapBuildEXT) != vk::PipelineStageFlagBits2::eMicromapBuildEXT);
            assert((accessMask & vk::AccessFlagBits2::eMicromapWriteEXT) != vk::AccessFlagBits2::eMicromapWriteEXT);
            return
                ResourceStateMapping(nvrhiState,
                    reinterpret_cast<const vk::PipelineStageFlags&>(stageFlags),
                    reinterpret_cast<const vk::AccessFlags&>(accessMask),
                    imageLayout
                );
        }

        ResourceStateMapping2 AsResourceStateMapping2() const
        {
            return ResourceStateMapping2(nvrhiState, stageFlags, accessMask, imageLayout);
        }
    };

    static const ResourceStateMappingInternal g_ResourceStateMap[] =
    {
        { ResourceStates::Common,
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::AccessFlagBits2(),
            vk::ImageLayout::eUndefined },
        { ResourceStates::ConstantBuffer,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eUniformRead,
            vk::ImageLayout::eUndefined },
        { ResourceStates::VertexBuffer,
            vk::PipelineStageFlagBits2::eVertexInput,
            vk::AccessFlagBits2::eVertexAttributeRead,
            vk::ImageLayout::eUndefined },
        { ResourceStates::IndexBuffer,
            vk::PipelineStageFlagBits2::eVertexInput,
            vk::AccessFlagBits2::eIndexRead,
            vk::ImageLayout::eUndefined },
        { ResourceStates::IndirectArgument,
            vk::PipelineStageFlagBits2::eDrawIndirect,
            vk::AccessFlagBits2::eIndirectCommandRead,
            vk::ImageLayout::eUndefined },
        { ResourceStates::ShaderResource,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eShaderRead,
            vk::ImageLayout::eShaderReadOnlyOptimal },
        { ResourceStates::UnorderedAccess,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
            vk::ImageLayout::eGeneral },
        { ResourceStates::RenderTarget,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::ImageLayout::eColorAttachmentOptimal },
        { ResourceStates::DepthWrite,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::ImageLayout::eDepthStencilAttachmentOptimal },
        { ResourceStates::DepthRead,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal },
        { ResourceStates::StreamOut,
            vk::PipelineStageFlagBits2::eTransformFeedbackEXT,
            vk::AccessFlagBits2::eTransformFeedbackWriteEXT,
            vk::ImageLayout::eUndefined },
        { ResourceStates::CopyDest,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eTransferDstOptimal },
        { ResourceStates::CopySource,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::ImageLayout::eTransferSrcOptimal },
        { ResourceStates::ResolveDest,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eTransferDstOptimal },
        { ResourceStates::ResolveSource,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::ImageLayout::eTransferSrcOptimal },
        { ResourceStates::Present,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead,
            vk::ImageLayout::ePresentSrcKHR },
        { ResourceStates::AccelStructRead,
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            vk::ImageLayout::eUndefined },
        { ResourceStates::AccelStructWrite,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            vk::ImageLayout::eUndefined },
        { ResourceStates::AccelStructBuildInput,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            vk::ImageLayout::eUndefined },
        { ResourceStates::AccelStructBuildBlas,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            vk::ImageLayout::eUndefined },
        { ResourceStates::ShadingRateSurface,
            vk::PipelineStageFlagBits2::eFragmentShadingRateAttachmentKHR,
            vk::AccessFlagBits2::eFragmentShadingRateAttachmentReadKHR,
            vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR },
        { ResourceStates::OpacityMicromapWrite,
            vk::PipelineStageFlagBits2::eMicromapBuildEXT,
            vk::AccessFlagBits2::eMicromapWriteEXT,
            vk::ImageLayout::eUndefined },
        { ResourceStates::OpacityMicromapBuildInput,
            vk::PipelineStageFlagBits2::eMicromapBuildEXT,
            vk::AccessFlagBits2::eShaderRead,
            vk::ImageLayout::eUndefined },
    };

    ResourceStateMappingInternal convertResourceStateInternal(ResourceStates state)
    {
        ResourceStateMappingInternal result = {};

        constexpr uint32_t numStateBits = sizeof(g_ResourceStateMap) / sizeof(g_ResourceStateMap[0]);

        uint32_t stateTmp = uint32_t(state);
        uint32_t bitIndex = 0;

        while (stateTmp != 0 && bitIndex < numStateBits)
        {
            uint32_t bit = (1 << bitIndex);

            if (stateTmp & bit)
            {
                const ResourceStateMappingInternal& mapping = g_ResourceStateMap[bitIndex];

                assert(uint32_t(mapping.nvrhiState) == bit);
                assert(result.imageLayout == vk::ImageLayout::eUndefined || mapping.imageLayout == vk::ImageLayout::eUndefined || result.imageLayout == mapping.imageLayout);

                result.nvrhiState = ResourceStates(result.nvrhiState | mapping.nvrhiState);
                result.accessMask |= mapping.accessMask;
                result.stageFlags |= mapping.stageFlags;
                if (mapping.imageLayout != vk::ImageLayout::eUndefined)
                    result.imageLayout = mapping.imageLayout;

                stateTmp &= ~bit;
            }

            bitIndex++;
        }

        assert(result.nvrhiState == state);

        return result;
    }

    ResourceStateMapping convertResourceState(ResourceStates state)
    {
        const ResourceStateMappingInternal mapping = convertResourceStateInternal(state);
        return mapping.AsResourceStateMapping();
    }

    ResourceStateMapping2 convertResourceState2(ResourceStates state)
    {
        const ResourceStateMappingInternal mapping = convertResourceStateInternal(state);
        return mapping.AsResourceStateMapping2();
    }

    const char* resultToString(VkResult result)
    {
        switch(result)
        {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:
            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_PIPELINE_COMPILE_REQUIRED_EXT:
            return "VK_PIPELINE_COMPILE_REQUIRED_EXT";

        default: {
            // Print the value into a static buffer - this is not thread safe but that shouldn't matter
            static char buf[24];
            snprintf(buf, sizeof(buf), "Unknown (%d)", result);
            return buf;
        }
        }
    }
    
    vk::PrimitiveTopology convertPrimitiveTopology(PrimitiveType topology)
    {
        switch(topology)
        {
            case PrimitiveType::PointList:
                return vk::PrimitiveTopology::ePointList;

            case PrimitiveType::LineList:
                return vk::PrimitiveTopology::eLineList;

            case PrimitiveType::LineStrip:
                return vk::PrimitiveTopology::eLineStrip;

            case PrimitiveType::TriangleList:
                return vk::PrimitiveTopology::eTriangleList;

            case PrimitiveType::TriangleStrip:
                return vk::PrimitiveTopology::eTriangleStrip;

            case PrimitiveType::TriangleFan:
                return vk::PrimitiveTopology::eTriangleFan;

            case PrimitiveType::TriangleListWithAdjacency:
                return vk::PrimitiveTopology::eTriangleListWithAdjacency;

            case PrimitiveType::TriangleStripWithAdjacency:
                return vk::PrimitiveTopology::eTriangleStripWithAdjacency;

            case PrimitiveType::PatchList:
                return vk::PrimitiveTopology::ePatchList;

            default:
                assert(0);
                return vk::PrimitiveTopology::eTriangleList;
        }
    }

    vk::PolygonMode convertFillMode(RasterFillMode mode)
    {
        switch(mode)
        {
            case RasterFillMode::Fill:
                return vk::PolygonMode::eFill;

            case RasterFillMode::Line:
                return vk::PolygonMode::eLine;

            default:
                assert(0);
                return vk::PolygonMode::eFill;
        }
    }

    vk::CullModeFlagBits convertCullMode(RasterCullMode mode)
    {
        switch(mode)
        {
            case RasterCullMode::Back:
                return vk::CullModeFlagBits::eBack;

            case RasterCullMode::Front:
                return vk::CullModeFlagBits::eFront;

            case RasterCullMode::None:
                return vk::CullModeFlagBits::eNone;

            default:
                assert(0);
                return vk::CullModeFlagBits::eNone;
        }
    }

    vk::CompareOp convertCompareOp(ComparisonFunc op)
    {
        switch(op)
        {
            case ComparisonFunc::Never:
                return vk::CompareOp::eNever;

            case ComparisonFunc::Less:
                return vk::CompareOp::eLess;

            case ComparisonFunc::Equal:
                return vk::CompareOp::eEqual;

            case ComparisonFunc::LessOrEqual:
                return vk::CompareOp::eLessOrEqual;

            case ComparisonFunc::Greater:
                return vk::CompareOp::eGreater;

            case ComparisonFunc::NotEqual:
                return vk::CompareOp::eNotEqual;

            case ComparisonFunc::GreaterOrEqual:
                return vk::CompareOp::eGreaterOrEqual;

            case ComparisonFunc::Always:
                return vk::CompareOp::eAlways;

            default:
                utils::InvalidEnum();
                return vk::CompareOp::eAlways;
        }
    }

    vk::StencilOp convertStencilOp(StencilOp op)
    {
        switch(op)
        {
            case StencilOp::Keep:
                return vk::StencilOp::eKeep;

            case StencilOp::Zero:
                return vk::StencilOp::eZero;

            case StencilOp::Replace:
                return vk::StencilOp::eReplace;

            case StencilOp::IncrementAndClamp:
                return vk::StencilOp::eIncrementAndClamp;

            case StencilOp::DecrementAndClamp:
                return vk::StencilOp::eDecrementAndClamp;

            case StencilOp::Invert:
                return vk::StencilOp::eInvert;

            case StencilOp::IncrementAndWrap:
                return vk::StencilOp::eIncrementAndWrap;

            case StencilOp::DecrementAndWrap:
                return vk::StencilOp::eDecrementAndWrap;

            default:
                utils::InvalidEnum();
                return vk::StencilOp::eKeep;
        }
    }

    vk::StencilOpState convertStencilState(const DepthStencilState& depthStencilState, const DepthStencilState::StencilOpDesc& desc)
    {
        return vk::StencilOpState()
                .setFailOp(convertStencilOp(desc.failOp))
                .setPassOp(convertStencilOp(desc.passOp))
                .setDepthFailOp(convertStencilOp(desc.depthFailOp))
                .setCompareOp(convertCompareOp(desc.stencilFunc))
                .setCompareMask(depthStencilState.stencilReadMask)
                .setWriteMask(depthStencilState.stencilWriteMask)
                .setReference(depthStencilState.stencilRefValue);
    }

    vk::BlendFactor convertBlendValue(BlendFactor value)
    {
        switch(value)
        {
            case BlendFactor::Zero:
                return vk::BlendFactor::eZero;

            case BlendFactor::One:
                return vk::BlendFactor::eOne;

            case BlendFactor::SrcColor:
                return vk::BlendFactor::eSrcColor;

            case BlendFactor::OneMinusSrcColor:
                return vk::BlendFactor::eOneMinusSrcColor;

            case BlendFactor::SrcAlpha:
                return vk::BlendFactor::eSrcAlpha;

            case BlendFactor::OneMinusSrcAlpha:
                return vk::BlendFactor::eOneMinusSrcAlpha;

            case BlendFactor::DstAlpha:
                return vk::BlendFactor::eDstAlpha;

            case BlendFactor::OneMinusDstAlpha:
                return vk::BlendFactor::eOneMinusDstAlpha;

            case BlendFactor::DstColor:
                return vk::BlendFactor::eDstColor;

            case BlendFactor::OneMinusDstColor:
                return vk::BlendFactor::eOneMinusDstColor;

            case BlendFactor::SrcAlphaSaturate:
                return vk::BlendFactor::eSrcAlphaSaturate;

            case BlendFactor::ConstantColor:
                return vk::BlendFactor::eConstantColor;

            case BlendFactor::OneMinusConstantColor:
                return vk::BlendFactor::eOneMinusConstantColor;

            case BlendFactor::Src1Color:
                return vk::BlendFactor::eSrc1Color;

            case BlendFactor::OneMinusSrc1Color:
                return vk::BlendFactor::eOneMinusSrc1Color;

            case BlendFactor::Src1Alpha:
                return vk::BlendFactor::eSrc1Alpha;

            case BlendFactor::OneMinusSrc1Alpha:
                return vk::BlendFactor::eOneMinusSrc1Alpha;

            default:
                assert(0);
                return vk::BlendFactor::eZero;
        }
    }

    vk::BlendOp convertBlendOp(BlendOp op)
    {
        switch(op)
        {
            case BlendOp::Add:
                return vk::BlendOp::eAdd;

            case BlendOp::Subrtact:
                return vk::BlendOp::eSubtract;

            case BlendOp::ReverseSubtract:
                return vk::BlendOp::eReverseSubtract;

            case BlendOp::Min:
                return vk::BlendOp::eMin;

            case BlendOp::Max:
                return vk::BlendOp::eMax;

            default:
                assert(0);
                return vk::BlendOp::eAdd;
        }
    }

    vk::ColorComponentFlags convertColorMask(ColorMask mask)
    {
        return vk::ColorComponentFlags(uint8_t(mask));
    }

    vk::PipelineColorBlendAttachmentState convertBlendState(const BlendState::RenderTarget& state)
    {
        return vk::PipelineColorBlendAttachmentState()
                .setBlendEnable(state.blendEnable)
                .setSrcColorBlendFactor(convertBlendValue(state.srcBlend))
                .setDstColorBlendFactor(convertBlendValue(state.destBlend))
                .setColorBlendOp(convertBlendOp(state.blendOp))
                .setSrcAlphaBlendFactor(convertBlendValue(state.srcBlendAlpha))
                .setDstAlphaBlendFactor(convertBlendValue(state.destBlendAlpha))
                .setAlphaBlendOp(convertBlendOp(state.blendOpAlpha))
                .setColorWriteMask(convertColorMask(state.colorWriteMask));
    }

    vk::BuildAccelerationStructureFlagsKHR convertAccelStructBuildFlags(rt::AccelStructBuildFlags buildFlags)
    {
#if ENABLE_SHORTCUT_CONVERSIONS
        static_assert(uint32_t(rt::AccelStructBuildFlags::AllowUpdate) == uint32_t(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR));
        static_assert(uint32_t(rt::AccelStructBuildFlags::AllowCompaction) == uint32_t(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR));
        static_assert(uint32_t(rt::AccelStructBuildFlags::PreferFastTrace) == uint32_t(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR));
        static_assert(uint32_t(rt::AccelStructBuildFlags::PreferFastBuild) == uint32_t(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR));
        static_assert(uint32_t(rt::AccelStructBuildFlags::MinimizeMemory) == uint32_t(VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR));

        return vk::BuildAccelerationStructureFlagsKHR(uint32_t(buildFlags) & 0x1f);
#else
        vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR(0);
        if ((buildFlags & rt::AccelStructBuildFlags::AllowUpdate) != 0)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        if ((buildFlags & rt::AccelStructBuildFlags::AllowCompaction) != 0)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
        if ((buildFlags & rt::AccelStructBuildFlags::PreferFastTrace) != 0)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        if ((buildFlags & rt::AccelStructBuildFlags::PreferFastBuild) != 0)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild;
        if ((buildFlags & rt::AccelStructBuildFlags::MinimizeMemory) != 0)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory;
        return flags;
#endif
    }

    vk::GeometryInstanceFlagsKHR convertInstanceFlags(rt::InstanceFlags instanceFlags)
    {
#if ENABLE_SHORTCUT_CONVERSIONS
        static_assert(uint32_t(rt::InstanceFlags::TriangleCullDisable) == uint32_t(VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR));
        static_assert(uint32_t(rt::InstanceFlags::TriangleFrontCounterclockwise) == uint32_t(VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR));
        static_assert(uint32_t(rt::InstanceFlags::ForceOpaque) == uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR));
        static_assert(uint32_t(rt::InstanceFlags::ForceNonOpaque) == uint32_t(VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR));
        static_assert(uint32_t(rt::InstanceFlags::ForceOMM2State) == uint32_t(VK_GEOMETRY_INSTANCE_FORCE_OPACITY_MICROMAP_2_STATE_EXT));
        static_assert(uint32_t(rt::InstanceFlags::DisableOMMs) == uint32_t(VK_GEOMETRY_INSTANCE_DISABLE_OPACITY_MICROMAPS_EXT));

        return vk::GeometryInstanceFlagsKHR(uint32_t(instanceFlags) );
#else
        vk::GeometryInstanceFlagsKHR flags = vk::GeometryInstanceFlagBitsKHR(0);
        if ((instanceFlags & rt::InstanceFlags::ForceNonOpaque) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eForceNoOpaque;
        if ((instanceFlags & rt::InstanceFlags::ForceOpaque) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eForceOpaque;
        if ((instanceFlags & rt::InstanceFlags::ForceOMM2State) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eForceOpacityMicromap2StateEXT;
        if ((instanceFlags & rt::InstanceFlags::DisableOMMs) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eDisableOpacityMicromapsEXT;
        if ((instanceFlags & rt::InstanceFlags::TriangleCullDisable) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eTriangleCullDisable;
        if ((instanceFlags & rt::InstanceFlags::TriangleFrontCounterclockwise) != 0)
            flags |= vk::GeometryInstanceFlagBitsKHR::eTriangleFrontCounterclockwise;
        return flags;
#endif
    }

    vk::Extent2D convertFragmentShadingRate(VariableShadingRate shadingRate)
    {
        switch (shadingRate)
        {
        case VariableShadingRate::e1x2:
            return vk::Extent2D().setWidth(1).setHeight(2);
        case VariableShadingRate::e2x1:
            return vk::Extent2D().setWidth(2).setHeight(1);
        case VariableShadingRate::e2x2:
            return vk::Extent2D().setWidth(2).setHeight(2);
        case VariableShadingRate::e2x4:
            return vk::Extent2D().setWidth(2).setHeight(4);
        case VariableShadingRate::e4x2:
            return vk::Extent2D().setWidth(4).setHeight(2);
        case VariableShadingRate::e4x4:
            return vk::Extent2D().setWidth(4).setHeight(4);
        case VariableShadingRate::e1x1:
        default:
            return vk::Extent2D().setWidth(1).setHeight(1);
        }
    }

    vk::FragmentShadingRateCombinerOpKHR convertShadingRateCombiner(ShadingRateCombiner combiner)
    {
        switch (combiner)
        {
        case ShadingRateCombiner::Override:
            return vk::FragmentShadingRateCombinerOpKHR::eReplace;
        case ShadingRateCombiner::Min:
            return vk::FragmentShadingRateCombinerOpKHR::eMin;
        case ShadingRateCombiner::Max:
            return vk::FragmentShadingRateCombinerOpKHR::eMax;
        case ShadingRateCombiner::ApplyRelative:
            return vk::FragmentShadingRateCombinerOpKHR::eMul;
        case ShadingRateCombiner::Passthrough:
        default:
            return vk::FragmentShadingRateCombinerOpKHR::eKeep;
        }
    }

} // namespace nvrhi::vulkan
