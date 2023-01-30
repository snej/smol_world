//
// slice.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include <cassert>
#include <cstdint>

namespace snej::smol {

/// Generic pointer+length pair, a range of values.
template <typename T>
struct slice {
    slice()                 :_begin(nullptr), _end(nullptr) { }
    slice(T* b, T* e)       :_begin(b), _end(e) {assert(e >= b && (b != nullptr || b == e));}
    slice(T* b, uint32_t s) :slice(b, b + s) { }

    using iterator = T*;
    T* begin() const    {return _begin;}
    T* end() const      {return _end;}

    T& front() const    {assert(!empty()); return _begin[0];}
    T& back() const     {assert(!empty()); return _end[-1];}

    uint32_t size() const {return uint32_t(_end - _begin);}
    bool empty() const  {return _end == _begin;}
    bool null() const   {return _begin == nullptr;}

    T& operator[] (uint32_t i) const  {auto ptr = &_begin[i]; assert(ptr < _end); return *ptr;}

    template <typename TO>
    friend inline slice<TO> slice_cast(slice<T> from) {
        static_assert(sizeof(TO) % sizeof(T) == 0 || sizeof(T) % sizeof(TO) == 0);
        return {(TO*)from._begin, (TO*)from._end};
    }

    void moveTo(T* addr)        {_end = addr + size(); _begin = addr;}

private:
    T* _begin;
    T* _end;
};

}
