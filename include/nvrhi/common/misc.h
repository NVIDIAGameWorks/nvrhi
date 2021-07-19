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

#include <cstdint>
#include <cassert>

namespace nvrhi 
{
    template<typename T> T align(T size, T alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    template<typename T, typename U> [[nodiscard]] bool arraysAreDifferent(const T& a, const U& b)
    {
        if (a.size() != b.size())
            return true;

        for (uint32_t i = 0; i < uint32_t(a.size()); i++)
        {
            if (a[i] != b[i])
                return true;
        }

        return false;
    }

    template<typename T, typename U> [[nodiscard]] uint32_t arrayDifferenceMask(const T& a, const U& b)
    {
        assert(a.size() <= 32);
        assert(b.size() <= 32);

        if (a.size() != b.size())
            return ~0u;

        uint32_t mask = 0;
        for (uint32_t i = 0; i < uint32_t(a.size()); i++)
        {
            if (a[i] != b[i])
                mask |= (1 << i);
        }

        return mask;
    }

    inline uint32_t hash_to_u32(size_t hash)
    {
        return uint32_t(hash) ^ (uint32_t(hash >> 32));
    }

    // A type cast that is safer than static_cast in debug builds, and is a simple static_cast in release builds.
    // Used for downcasting various ISomething* pointers to their implementation classes in the backends.
    template <typename T, typename U>
    T checked_cast(U u)
    {
        static_assert(!std::is_same<T, U>::value, "Redundant checked_cast");
#ifdef _DEBUG
        if (!u) return nullptr;
        T t = dynamic_cast<T>(u);
        if (!t) assert(!"Invalid type cast");  // NOLINT(clang-diagnostic-string-conversion)
        return t;
#else
        return static_cast<T>(u);
#endif
    }
} // namespace nvrhi
