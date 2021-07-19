/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

namespace nvrhi
{
    /*
    Version words are used to track the usage of upload buffers, scratch buffers,
    and volatile constant buffers across multiple command lists and their instances.

    Versioned objects are initially allocated in the "pending" state, meaing they have
    the submitted flag set to zero, but the instance is nonzero. When the command list
    instance using the object is executed, the objects with a matching version are
    transitioned into the "submitted" state. Later, when the command list instance has
    finished executing, the objects are transitioned into the "available" state, i.e. 0.
     */

    constexpr uint64_t c_VersionSubmittedFlag = 0x8000000000000000;
    constexpr uint32_t c_VersionQueueShift = 60;
    constexpr uint32_t c_VersionQueueMask = 0x7;
    constexpr uint64_t c_VersionIDMask = 0x0FFFFFFFFFFFFFFF;

    constexpr uint64_t MakeVersion(uint64_t id, CommandQueue queue, bool submitted)
    {
        uint64_t result = (id & c_VersionIDMask) | (uint64_t(queue) << c_VersionQueueShift);
        if (submitted) result |= c_VersionSubmittedFlag;
        return result;
    }

    constexpr uint64_t VersionGetInstance(uint64_t version)
    {
        return version & c_VersionIDMask;
    }

    constexpr CommandQueue VersionGetQueue(uint64_t version)
    {
        return CommandQueue((version >> c_VersionQueueShift) & c_VersionQueueMask);
    }

    constexpr bool VersionGetSubmitted(uint64_t version)
    {
        return (version & c_VersionSubmittedFlag) != 0;
    }
}