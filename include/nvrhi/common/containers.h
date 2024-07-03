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

#include <array>
#include <assert.h>
#include <cstddef>
#include <cstdint>

namespace nvrhi {

// a static vector is a vector with a capacity defined at compile-time
template <typename T, uint32_t _max_elements>
struct static_vector : private std::array<T, _max_elements>
{
    typedef std::array<T, _max_elements> base;
    enum { max_elements = _max_elements };

    using typename base::value_type;
    using typename base::size_type;
    using typename base::difference_type;
    using typename base::reference;
    using typename base::const_reference;
    using typename base::pointer;
    using typename base::const_pointer;
    using typename base::iterator;
    using typename base::const_iterator;
    // xxxnsubtil: reverse iterators not implemented

    static_vector()
        : base()
        , current_size(0)
    { }

    static_vector(size_t size)
        : base()
        , current_size(size)
    {
        assert(size <= max_elements);
    }

    static_vector(std::initializer_list<T> il)
        : current_size(0)
    {
        for(auto i : il)
            push_back(i);
    }

    using base::at;

    reference operator[] (size_type pos)
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    const_reference operator[] (size_type pos) const
    {
        assert(pos < current_size);
        return base::operator[](pos);
    }

    using base::front;

    reference back() noexcept                   { auto tmp =  end(); --tmp; return *tmp; }
    const_reference back() const noexcept       { auto tmp = cend(); --tmp; return *tmp; }

    using base::data;
    using base::begin;
    using base::cbegin;

    iterator end() noexcept                     { return iterator(begin()) + current_size; }
    const_iterator end() const noexcept         { return cend(); }
    const_iterator cend() const noexcept        { return const_iterator(cbegin()) + current_size; }

    bool empty() const noexcept                 { return current_size == 0; }
    size_t size() const noexcept                { return current_size; }
    constexpr size_t max_size() const noexcept  { return max_elements; }

    void fill(const T& value) noexcept
    {
        base::fill(value);
        current_size = max_elements;
    }

    void swap(static_vector& other) noexcept
    {
        base::swap(*this);
        std::swap(current_size, other.current_size);
    }

    void push_back(const T& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = value;
        current_size++;
    }

    void push_back(T&& value) noexcept
    {
        assert(current_size < max_elements);
        *(data() + current_size) = std::move(value);
        current_size++;
    }

    void pop_back() noexcept
    {
        assert(current_size > 0);
        current_size--;
    }

    void resize(size_type new_size) noexcept
    {
        assert(new_size <= max_elements);

        if (current_size > new_size)
        {
            for (size_type i = new_size; i < current_size; i++)
                *(data() + i) = T{};
        }
        else
        {
            for (size_type i = current_size; i < new_size; i++)
                *(data() + i) = T{};
        }

        current_size = new_size;
    }

    reference emplace_back() noexcept
    {
        resize(current_size + 1);
        return back();
    }

private:
    size_type current_size = 0;
};

} // namespace nvrhi
