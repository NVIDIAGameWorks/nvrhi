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

#include <nvrhi/common/shader-blob.h>
#include <sstream>

namespace nvrhi
{

    bool findPermutationInBlob(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, const void** pBinary, size_t* pSize)
    {
        if (!blob || blobSize < g_BlobSignatureSize)
            return false;

        if (!pBinary || !pSize)
            return false;

        if (memcmp(blob, g_BlobSignature, g_BlobSignatureSize) != 0)
        {
            if (numConstants == 0)
            {
                *pBinary = blob;
                *pSize = blobSize;
                return true; // this blob is not a permutation blob, and no permutation is requested
            }
            else
            {
                return false; // this blob is not a permutation blob, but the caller requested a permutation
            }
        }

        blob = static_cast<const char*>(blob) + g_BlobSignatureSize;
        blobSize -= g_BlobSignatureSize;
        
        std::stringstream ss;
        for (uint32_t n = 0; n < numConstants; n++)
        {
            const ShaderConstant& constant = constants[n];

            ss << constant.name << "=" << constant.value << " ";
        }
        std::string permutation = ss.str();

        while (blobSize > sizeof(ShaderBlobEntry))
        {
            const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

            if (header->dataSize == 0)
                return false; // last header in the blob is empty

            if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->permutationSize)
                return false; // insufficient bytes in the blob, cannot continue

            const char* entryPermutation = static_cast<const char*>(blob) + sizeof(ShaderBlobEntry);

            if ((header->permutationSize == permutation.size()) && ((permutation.size() == 0) || (strncmp(entryPermutation, permutation.data(), permutation.size()) == 0)))
            {
                const char* binary = static_cast<const char*>(blob) + sizeof(ShaderBlobEntry) + header->permutationSize;
                
                *pBinary = binary;
                *pSize = header->dataSize;
                return true;
            }

            size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->permutationSize;
            blob = static_cast<const char*>(blob) + offset;
            blobSize -= offset;
        }

        return false; // went through the blob, permutation not found
    }

    void enumeratePermutationsInBlob(const void* blob, size_t blobSize, std::vector<std::string>& permutations)
    {
        if (!blob || blobSize < g_BlobSignatureSize)
            return;

        if (memcmp(blob, g_BlobSignature, g_BlobSignatureSize) != 0)
            return;

        blob = static_cast<const char*>(blob) + g_BlobSignatureSize;
        blobSize -= g_BlobSignatureSize;

        while (blobSize > sizeof(ShaderBlobEntry))
        {
            const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

            if (header->dataSize == 0)
                return;

            if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->permutationSize)
                return;

            if (header->permutationSize > 0)
            {
                std::string permutation;
                permutation.resize(header->permutationSize);

                memcpy(&permutation[0], static_cast<const char*>(blob) + sizeof(ShaderBlobEntry), header->permutationSize);

                permutations.push_back(permutation);
            }
            else
            {
                permutations.push_back("<default>");
            }

            size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->permutationSize;
            blob = static_cast<const char*>(blob) + offset;
            blobSize -= offset;
        }
    }

    std::string formatShaderNotFoundMessage(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants)
    {
        std::stringstream ss;
        ss << "Couldn't find the required shader permutation in the blob, or the blob is corrupted." << std::endl;
        ss << "Required permutation key: " << std::endl;

        if (numConstants)
        {
            for (uint32_t n = 0; n < numConstants; n++)
            {
                const ShaderConstant& constant = constants[n];
                ss << constant.name << '=' << constant.value << ';';
            }
        }
        else
        {
            ss << "<default>";
        }

        ss << std::endl;

        std::vector<std::string> permutations;
        enumeratePermutationsInBlob(blob, blobSize, permutations);

        if (!permutations.empty())
        {
            ss << "Permutations available in the blob:" << std::endl;
            for (const std::string& key : permutations)
                ss << key << std::endl;
        }
        else
        {
            ss << "No permutations found in the blob.";
        }

        return ss.str();
    }

    ShaderHandle createShaderPermutation(IDevice* device, const ShaderDesc& d, const void* blob, size_t blobSize,
        const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound)
    {
        const void* binary = nullptr;
        size_t binarySize = 0;

        if (findPermutationInBlob(blob, blobSize, constants, numConstants, &binary, &binarySize))
        {
            return device->createShader(d, binary, binarySize);
        }

        if (errorIfNotFound)
        {
            std::string message = formatShaderNotFoundMessage(blob, blobSize, constants, numConstants);
            device->getMessageCallback()->message(MessageSeverity::Error, message.c_str());
        }

        return nullptr;
    }

    ShaderLibraryHandle createShaderLibraryPermutation(IDevice* device, const void* blob, size_t blobSize,
        const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound)
    {
        const void* binary = nullptr;
        size_t binarySize = 0;

        if (findPermutationInBlob(blob, blobSize, constants, numConstants, &binary, &binarySize))
        {
            return device->createShaderLibrary(binary, binarySize);
        }

        if (errorIfNotFound)
        {
            std::string message = formatShaderNotFoundMessage(blob, blobSize, constants, numConstants);
            device->getMessageCallback()->message(MessageSeverity::Error, message.c_str());
        }

        return nullptr;
    }


} // namespace nvrhi