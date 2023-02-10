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

#pragma once

#include <vector>
#include <cstdint>

namespace nvrhi {

// This is a container for bits that has virtually unlimited capacity, otherwise similar to std::bitset.
// It maintains a sorted vector of elements where each element is a 32-bit word of bits at a given offset.
// It is used in the validation layer to compute, modify and compare sets of binding indices,
// and implements only the operations necessary for that purpose.
class sparse_bitset
{
private:
    friend class sparse_bitset_test;

    struct element
    {
        uint32_t wordIndex;
        uint32_t bits;
    };

    std::vector<element> m_storage;

    // Internal function that finds an element containing the specified bit index.
    // If such element is not present, it is inserted.
    uint32_t& findOrInsertWord(uint32_t wordIndex);

    // Internal function that finds an element containing the specified bit index.
    // If such element is not present, returns zero.
    [[nodiscard]] uint32_t tryGetWord(uint32_t wordIndex) const;

    // Checks if the elements are ordered correctly - for testing.
    [[nodiscard]] bool isOrdered() const;

public:
    // Sets the specified bit to the provided value.
    void set(uint32_t bitIndex, bool value);

    // Returns the value of the specified bit.
    // If the container element is not present, the bit is zero.
    [[nodiscard]] bool get(uint32_t bitIndex) const;

    // Returns a new bitset that contains bits that are set to 1 in both A and B (return A & B)
    static sparse_bitset intersect(const sparse_bitset& a, const sparse_bitset& b);

    // Returns a new bitset that contains bits that are set to 1 in A but not in B (return A & ~B)
    static sparse_bitset difference(const sparse_bitset& a, const sparse_bitset& b);

    // Modify the current bitset by adding all nonzero bits from B (*this |= B)
    void include(const sparse_bitset& b);

    // Returns true if there are any nonzero bits in the set.
    [[nodiscard]] bool any() const;

    // Iterator that returns indices of all nonzero bits in the set.
    struct const_iterator
    {
        const sparse_bitset* bitset;
        uint32_t elemIndex;
        int bit;

        // Returns the index of the current bit.
        uint32_t operator*() const;

        // Advances the iterator to the next nonzero bit.
        const_iterator& operator++();

        bool operator!=(const const_iterator& b) const;
    };

    // Returns an iterator pointing at the first nonzero bit, or the end if the set is empty.
    [[nodiscard]] const_iterator begin() const;

    // Returns an iterator pointing at the end of the set.
    [[nodiscard]] const_iterator end() const;
};

inline sparse_bitset operator&(const sparse_bitset& a, const sparse_bitset& b)
{
    return sparse_bitset::intersect(a, b);
}

inline sparse_bitset operator-(const sparse_bitset& a, const sparse_bitset& b)
{
    return sparse_bitset::difference(a, b);
}

inline void operator|=(sparse_bitset& a, const sparse_bitset& b)
{
    a.include(b);
}

} // namespace nvrhi
