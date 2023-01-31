//
// slice.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <cassert>
#include <cstdint>

namespace snej::smol {

/// Generic pointer+length pair, denoting a range of values in memory.
template <typename T>
struct slice {
    slice()                             :_begin(nullptr), _size(0) { }
    slice(T* b, uint32_t s)             :_begin(b), _size(s) {assert(!(!b && s));}
    slice(T* b, size_t s)               :slice(b, uint32_t(s)) {assert(s < UINT32_MAX);}
    slice(T* b, T* e)                   :slice(b, size_t(e - b)) { }

    uint32_t size() const pure          {return uint32_t(_size);}
    bool empty() const pure             {return _size == 0;}
    bool isNull() const pure            {return _begin == nullptr;}
    explicit operator bool() const pure {return _begin != nullptr;}

    using iterator = T*;
    iterator begin() const pure         {return _begin;}
    iterator end() const pure           {return _begin + _size;}

    T& front() const pure               {assert(!empty()); return _begin[0];}
    T& back() const pure                {assert(!empty()); return end()[-1];}

    T& operator[] (uint32_t i) const pure  {assert(i < _size); return _begin[i];}

    void moveTo(T* addr)                {assert(addr); _begin = addr;}

    template <typename TO>
    friend inline slice<TO> slice_cast(slice<T> from) pure {
        static_assert(sizeof(TO) % sizeof(T) == 0 || sizeof(T) % sizeof(TO) == 0);
        return {(TO*)from._begin, (TO*)from.end()};
    }

private:
    T*          _begin;
    uint32_t    _size;
};

}
