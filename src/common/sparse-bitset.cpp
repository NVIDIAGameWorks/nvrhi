/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "sparse-bitset.h"

#include <cstddef>

using namespace nvrhi;

// Set this macro to 1 to run the unit test at initialization time - see below
#define SPARSE_BITSET_UNIT_TEST 0


uint32_t& sparse_bitset::findOrInsertWord(uint32_t wordIndex)
{
    // Use binary search to locate an existing element first.

    int left = 0;
    int right = int(m_storage.size()) - 1;
    while (left <= right)
    {
        int middle = (left + right) / 2;
        element& elem = m_storage[middle];
        if (elem.wordIndex < wordIndex)
            left = middle + 1;
        else if (elem.wordIndex > wordIndex)
            right = middle - 1;
        else
            // Found it!
            return elem.bits;
    }

    // No existing element found - insert a new one at the 'left' location.

    if (left < 0)
        left = 0;

    element elem{};
    elem.wordIndex = wordIndex;
    elem.bits = 0;
    m_storage.insert(m_storage.begin() + left, elem);

    return m_storage[left].bits;
}

uint32_t sparse_bitset::tryGetWord(uint32_t wordIndex) const
{
    // Use binary search to locate an existing element first.

    int left = 0;
    int right = int(m_storage.size()) - 1;
    while (left <= right)
    {
        int middle = (left + right) / 2;
        const element& elem = m_storage[middle];
        if (elem.wordIndex < wordIndex)
            left = middle + 1;
        else if (elem.wordIndex > wordIndex)
            right = middle - 1;
        else
            // Found it!
            return elem.bits;
    }

    // Not found
    return 0;
}

void sparse_bitset::set(uint32_t bitIndex, bool value)
{
    const uint32_t wordIndex = bitIndex >> 5;
    uint32_t& bits = findOrInsertWord(wordIndex);
    const uint32_t mask = 1 << (bitIndex & 0x1f);

    if (value)
        bits |= mask;
    else
        bits &= ~mask;
}

bool sparse_bitset::get(uint32_t bitIndex) const
{
    const uint32_t wordIndex = bitIndex >> 5;
    const uint32_t bits = tryGetWord(wordIndex);
    const uint32_t mask = 1 << (bitIndex & 0x1f);

    return (bits & mask) != 0;
}

sparse_bitset sparse_bitset::intersect(const sparse_bitset& a, const sparse_bitset& b)
{
    sparse_bitset r;
    std::vector<element>::const_iterator pa = a.m_storage.begin();
    std::vector<element>::const_iterator pb = b.m_storage.begin();

    // Iterate while there are elements in both sets - if one set runs out of elements,
    // all remaining elements of the other one are AND'ed with 0 and therefore can be discarded.
    while (pa != a.m_storage.end() && pb != b.m_storage.end())
    {
        if (pa->wordIndex < pb->wordIndex)
        {
            // Next element in A is missing from B - skip A
            ++pa;
        }
        else if (pb->wordIndex < pa->wordIndex)
        {
            // Next element in B is missing from A - skip B
            ++pb;
        }
        else
        {
            // Element present in both A and B - compute the intersection and insert a new
            // element if the result is non-empty.
            uint32_t rbits = pa->bits & pb->bits;
            if (rbits)
            {
                element elem{};
                elem.wordIndex = pa->wordIndex;
                elem.bits = rbits;
                r.m_storage.push_back(elem);
            }

            ++pa;
            ++pb;
        }
    }

    return r;
}

sparse_bitset sparse_bitset::difference(const sparse_bitset& a, const sparse_bitset& b)
{
    sparse_bitset r;
    std::vector<element>::const_iterator pa = a.m_storage.begin();
    std::vector<element>::const_iterator pb = b.m_storage.begin();

    // Iterate while there are elements in A, because we don't care about
    // the contents of B past the end of A.
    while (pa != a.m_storage.end())
    {
        if (pb == b.m_storage.end() || pa->wordIndex < pb->wordIndex)
        {
            // Next element in A is missing from B - copy the element from A.
            // This includes the situation when we reached the end of B.
            r.m_storage.push_back(*pa);
            ++pa;
        }
        else if (pb->wordIndex < pa->wordIndex)
        {
            // Next element in B is missing from A - skip B
            ++pb;
        }
        else
        {
            // Element present in both A and B - compute the difference and insert a new
            // element if the result is non-empty.
            uint32_t rbits = pa->bits & ~pb->bits;
            if (rbits)
            {
                element elem{};
                elem.wordIndex = pa->wordIndex;
                elem.bits = rbits;
                r.m_storage.push_back(elem);
            }

            ++pa;
            ++pb;
        }
    }

    return r;
}

void sparse_bitset::include(const sparse_bitset& b)
{
    std::vector<element>::iterator pr = m_storage.begin();
    std::vector<element>::const_iterator pb = b.m_storage.begin();

    // Iterate while there are elements in B.
    // Once B runs out of elements, this bitset stays the same.
    while (pb != b.m_storage.end())
    {
        if (pr == m_storage.end() || pb->wordIndex < pr->wordIndex)
        {
            // Next element in B is missing from this - insert it.
            const size_t ir = pr - m_storage.begin();
            m_storage.insert(pr, *pb);
            // Recalculate the iterator and make it point at the next element.
            pr = m_storage.begin() + ir + 1;
            ++pb;
        }
        else if (pr->wordIndex < pb->wordIndex)
        {
            // Next element in this is missing from B - skip this.
            ++pr;
        }
        else
        {
            // Element present in both this and B - compute the union.
            pr->bits |= pb->bits;
            ++pr;
            ++pb;
        }
    }
}

bool sparse_bitset::any() const
{
    for (const element& elem : m_storage)
    {
        if (elem.bits)
            return true;
    }

    return false;
}

uint32_t sparse_bitset::const_iterator::operator*() const
{
    const element& elem = bitset->m_storage[elemIndex];
    return (elem.wordIndex << 5) + bit;
}

sparse_bitset::const_iterator& sparse_bitset::const_iterator::operator++()
{
    while (elemIndex < bitset->m_storage.size())
    {
        const element& elem = bitset->m_storage[elemIndex];

        // Mask out the bits that we already processed
        [[maybe_unused]] const uint32_t nextBits = elem.bits & ~((1 << (bit + 1)) - 1);

#if defined(_MSC_VER)
        // Find the index of the lowest unprocessed bit with the MSVC intrinsic
        unsigned long nextBitIndex;
        if (_BitScanForward(&nextBitIndex, nextBits))
        {
            bit = int(nextBitIndex);
            return *this;
        }
#elif defined(__GNUC__) || defined(__clang__)
        // Find the index of the lowest unprocessed bit with the GCC/Clang intrinsic
        int nextBitIndexPlusOne = __builtin_ffs(nextBits);
        if (nextBitIndexPlusOne > 0)
        {
            bit = nextBitIndexPlusOne - 1;
            return *this;
        }
#else
        // Linear search through bits - fallback for unsupported compilers
        while (++bit < 32)
        {
            if (elem.bits & (1 << bit))
                return *this;
        }
#endif
        
        // Didn't find any more nonzero bits - advance to the next element.
        bit = -1;
        ++elemIndex;
    }

    // Reached the end of the bitset - reset the bit index to 0
    // to make this iterator equal to the end() iterator.
    bit = 0;
    return *this;
}

bool sparse_bitset::const_iterator::operator!=(const const_iterator& b) const
{
    return (bitset != b.bitset)
        || (elemIndex != b.elemIndex)
        || (bit != b.bit);
}

sparse_bitset::const_iterator sparse_bitset::begin() const
{
    // Note the ++ to search for the first nonzero bit
    return ++const_iterator{this, 0, -1};
}

sparse_bitset::const_iterator sparse_bitset::end() const
{
    return const_iterator{this, uint32_t(m_storage.size()), 0};
}

bool sparse_bitset::isOrdered() const
{
    for (size_t i = 1; i < m_storage.size(); ++i)
    {
        if (m_storage[i].wordIndex <= m_storage[i-1].wordIndex)
            return false;
    }

    return true;
}

#if SPARSE_BITSET_UNIT_TEST

#include <cassert>

namespace nvrhi
{

class sparse_bitset_test
{
public:
    static bool run()
    {
        sparse_bitset a;

        assert(!a.any());

        a.set(0, true);
        a.set(13, true);
        a.set(342, true);
        a.set(1234, true);

        assert(a.get(0));
        assert(a.get(13));
        assert(a.get(342));
        assert(a.get(1234));
        assert(!a.get(1));
        assert(!a.get(32));
        assert(!a.get(1235));

        a.set(342, false);
        assert(!a.get(342));

        assert(a.any());
        assert(a.isOrdered());

        sparse_bitset b;

        b.set(1234, true);
        b.set(43, true);
        b.set(343, true);
        b.set(1, true);
        assert(b.isOrdered());

        // Test the intersect function
        sparse_bitset c = a & b;

        assert( c.any());
        assert(!c.get(0));
        assert(!c.get(1));
        assert(!c.get(13));
        assert(!c.get(43));
        assert(!c.get(342));
        assert(!c.get(343));
        assert( c.get(1234));
        assert(c.isOrdered());

        // Test the include function
        c = a;
        c |= b;

        assert(c.any());
        assert(c.get(0));
        assert(c.get(1));
        assert(c.get(13));
        assert(c.get(43));
        assert(c.get(343));
        assert(c.get(1234));
        assert(c.isOrdered());

        // Test the iterator
        std::vector<uint32_t> bits;
        for (uint32_t bit : c)
            bits.push_back(bit);

        std::vector<uint32_t> expectedBits = { 0, 1, 13, 43, 343, 1234 };
        assert(bits == expectedBits);

        c = sparse_bitset();
        bits.clear();
        for (uint32_t bit : c)
            bits.push_back(bit);

        assert(bits.empty());

        // Test the difference function
        c = a - b;

        assert(c.any());
        assert(c.get(0));
        assert(c.get(13));
        assert(!c.get(342));
        assert(!c.get(343));
        assert(!c.get(1234));
        assert(c.isOrdered());

        return true;
    }
};

static bool g_SparseBitSetUnitTest = sparse_bitset_test::run();

} // namespace nvrhi
#endif