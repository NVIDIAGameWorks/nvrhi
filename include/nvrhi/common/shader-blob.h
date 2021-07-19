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

#include <nvrhi/nvrhi.h>
#include <vector>
#include <string>

namespace nvrhi
{
    struct ShaderConstant
    {
        const char* name;
        const char* value;
    };

    static const char* g_BlobSignature = "NVSP";
    static size_t g_BlobSignatureSize = 4;

    struct ShaderBlobEntry
    {
        uint32_t permutationSize;
        uint32_t dataSize;
    };

    NVRHI_API bool findPermutationInBlob(
        const void* blob,
        size_t blobSize,
        const ShaderConstant* constants,
        uint32_t numConstants,
        const void** pBinary,
        size_t* pSize);

    NVRHI_API void enumeratePermutationsInBlob(
        const void* blob,
        size_t blobSize,
        std::vector<std::string>& permutations);

    NVRHI_API std::string formatShaderNotFoundMessage(
        const void* blob,
        size_t blobSize,
        const ShaderConstant* constants,
        uint32_t numConstants);

    NVRHI_API ShaderHandle createShaderPermutation(
        IDevice* device,
        const ShaderDesc& d,
        const void* blob,
        size_t blobSize,
        const ShaderConstant* constants,
        uint32_t numConstants,
        bool errorIfNotFound = true);

    NVRHI_API ShaderLibraryHandle createShaderLibraryPermutation(
        IDevice* device,
        const void* blob,
        size_t blobSize,
        const ShaderConstant* constants,
        uint32_t numConstants,
        bool errorIfNotFound = true);

} // namespace nvrhi
