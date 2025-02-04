/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#ifndef NVRHI_HLSL_H
#define NVRHI_HLSL_H

// bit field defines
#if defined(__cplusplus) || __HLSL_VERSION >= 2021 || __SLANG__
namespace nvrhi
{
    typedef uint64_t GpuVirtualAddress;
    struct GpuVirtualAddressAndStride
    {
        GpuVirtualAddress startAddress;
        uint64_t strideInBytes;
    };

    namespace rt
    {
        //////////////////////////////////////////////////////////////////////////
        // Indirect Arg Structs that are shader friendly
        //////////////////////////////////////////////////////////////////////////
        
        // Shader friendly equivalent of nvrhi::rt::InstanceDesc
        struct IndirectInstanceDesc
        {
#ifdef __cplusplus
            float transform[12];
#else
            float4 transform[3];
#endif
            uint32_t instanceID : 24;
            uint32_t instanceMask : 8;
            uint32_t instanceContributionToHitGroupIndex : 24;
            uint32_t flags : 8;
            GpuVirtualAddress blasDeviceAddress;
        };

        namespace cluster
        {
            static const uint32_t kClasByteAlignment = 128;
            static const uint32_t kClasMaxTriangles = 256; // Defined by spec
            static const uint32_t kClasMaxVertices = 256; // Defined by spec
            static const uint32_t kMaxGeometryIndex = 16777215; // Defined by spec

            // Clone of NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS
            struct IndirectTriangleClasArgs
            {
                uint32_t          clusterId;                         // The user specified cluster Id to encode in the CLAS
                uint32_t          clusterFlags;                      // Values of NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_CLUSTER_FLAGS to use as Cluster Flags
                uint32_t          triangleCount : 9;                 // The number of triangles used by the CLAS (max 256)
                uint32_t          vertexCount : 9;                   // The number of vertices used by the CLAS (max 256)
                uint32_t          positionTruncateBitCount : 6;      // The number of bits to truncate from the position values
                uint32_t          indexFormat : 4;                   // The index format to use for the indexBuffer, see NVAPI_3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT for possible values
                uint32_t          opacityMicromapIndexFormat : 4;    // The index format to use for the opacityMicromapIndexBuffer, see NVAPI_3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT for possible values
                uint32_t          baseGeometryIndexAndFlags;         // The base geometry index (lower 24 bit) and base geometry flags (NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAGS), see geometryIndexBuffer
                uint16_t          indexBufferStride;                 // The stride of the elements of indexBuffer, in bytes
                uint16_t          vertexBufferStride;                // The stride of the elements of vertexBuffer, in bytes
                uint16_t          geometryIndexAndFlagsBufferStride; // The stride of the elements of geometryIndexBuffer, in bytes
                uint16_t          opacityMicromapIndexBufferStride;  // The stride of the elements of opacityMicromapIndexBuffer, in bytes
                GpuVirtualAddress indexBuffer;                       // The index buffer to construct the CLAS
                GpuVirtualAddress vertexBuffer;                      // The vertex buffer to construct the CLAS
                GpuVirtualAddress geometryIndexAndFlagsBuffer;       // (optional) Address of an array of 32bit geometry indices and geometry flags with size equal to the triangle count.
                GpuVirtualAddress opacityMicromapArray;              // (optional) Address of a valid OMM array, if used NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM must be set on this and all other cluster operation calls interacting with the object(s) constructed
                GpuVirtualAddress opacityMicromapIndexBuffer;        // (optional) Address of an array of indices into the OMM array
            };

            // Clone of NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS
            struct IndirectTriangleTemplateArgs
            {
                uint32_t          clusterId;                         // The user specified cluster Id to encode in the cluster template
                uint32_t          clusterFlags;                      // Values of NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_CLUSTER_FLAGS to use as Cluster Flags
                uint32_t          triangleCount : 9;                 // The number of triangles used by the cluster template (max 256)
                uint32_t          vertexCount : 9;                   // The number of vertices used by the cluster template (max 256)
                uint32_t          positionTruncateBitCount : 6;      // The number of bits to truncate from the position values
                uint32_t          indexFormat : 4;                   // The index format to use for the indexBuffer, must be one of nvrhi::rt::ClusteOperationIndexFormat
                uint32_t          opacityMicromapIndexFormat : 4;    // The index format to use for the opacityMicromapIndexBuffer, see nvrhi::rt::ClusteOperationIndexFormat for possible values
                uint32_t          baseGeometryIndexAndFlags;         // The base geometry index (lower 24 bit) and base geometry flags (NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAGS), see geometryIndexBuffer
                uint16_t          indexBufferStride;                 // The stride of the elements of indexBuffer, in bytes
                uint16_t          vertexBufferStride;                // The stride of the elements of vertexBuffer, in bytes
                uint16_t          geometryIndexAndFlagsBufferStride; // The stride of the elements of geometryIndexBuffer, in bytes
                uint16_t          opacityMicromapIndexBufferStride;  // The stride of the elements of opacityMicromapIndexBuffer, in bytes
                GpuVirtualAddress indexBuffer;                       // The index buffer to construct the cluster template
                GpuVirtualAddress vertexBuffer;                      // (optional) The vertex buffer to optimize the cluster template, the vertices will not be stored in the cluster template
                GpuVirtualAddress geometryIndexAndFlagsBuffer;       // (optional) Address of an array of 32bit geometry indices and geometry flags (each 32 bit value organized the same as baseGeometryIndex) with size equal to the triangle count, if non-zero the geometry indices of the CLAS triangles will be equal to the lower 24 bit of geometryIndexBuffer[triangleIndex] + baseGeometryIndex, the geometry flags for each triangle will be the bitwise OR of the flags in the upper 8 bits of baseGeometryIndex and geometryIndexBuffer[triangleIndex] otherwise all triangles will have a geometry index equal to baseGeometryIndex
                GpuVirtualAddress opacityMicromapArray;              // (optional) Address of a valid OMM array, if used NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM must be set on this and all other cluster operation calls interacting with the object(s) constructed
                GpuVirtualAddress opacityMicromapIndexBuffer;        // (optional) Address of an array of indices into the OMM array
                GpuVirtualAddress instantiationBoundingBoxLimit;     // (optional) Pointer to 6 floats with alignment NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BOUNDS_BYTE_ALIGNMENT representing the limits of the positions of any vertices the template will ever be instantiated with
            };

            // Clone of NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS
            struct IndirectInstantiateTemplateArgs
            {
                uint32_t                   clusterIdOffset;      // The offset added to the clusterId stored in the Cluster template to calculate the final clusterId that will be written to the instantiated CLAS
                uint32_t                   geometryIndexOffset;  // The offset added to the geometry index stored for each triangle in the Cluster template to calculate the final geometry index that will be written to the triangles of the instantiated CLAS, the resulting value may not exceed maxGeometryIndexValue both of this call, and the call used to construct the original cluster template referenced
                GpuVirtualAddress          clusterTemplate;      // Address of a previously built cluster template to be instantiated
                GpuVirtualAddressAndStride vertexBuffer;         // Vertex buffer with stride to use to fetch the vertex positions used for instantiation
            };

            // Clone of NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS
            struct IndirectArgs
            {
                uint32_t                  clusterCount;     // The size of the array referenced by clusterVAs
                uint32_t                  reserved;         // Reserved, must be 0
                GpuVirtualAddress         clusterAddresses; // Address of an array of D3D12_GPU_VIRTUAL_ADDRESS holding valid addresses of CLAS previously constructed
            };
        } // namespace cluster
    } // namespace rt
} // namespace nvrhi

#endif // __HLSL_VERSION 2021
#endif