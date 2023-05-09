//
// Base.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace snej::smol {

using byte = std::byte;

using  intpos =  int32_t;
using uintpos = uint32_t;

using heapsize = uintpos;      ///< Like `size_t` for Heaps.

static inline heapsize shorten(size_t size) {
    assert(size < UINT32_MAX);
    return heapsize(size);
}

enum class heappos : uintpos { };      ///< A position in a Heap, relative to its base.

static constexpr heappos nullpos {0};

static inline heappos operator+ (heappos p, intpos i) {return heappos(uintpos(p) + i);}
static inline heappos operator- (heappos p, intpos i) {return heappos(uintpos(p) - i);}
static inline std::strong_ordering operator<=> (heappos p, size_t i) {return uintpos(p) <=> i;}
static inline std::strong_ordering operator<=> (heappos p, intpos i) {return int64_t(p) <=> int64_t(i);}


template <typename NUM>
    concept Numeric = requires { std::integral<NUM> || std::floating_point<NUM>; };

/// Converts between two numeric types, pinning out-of-range values to the nearest limit.
template <Numeric TO, Numeric FROM>
    constexpr TO pinning_cast(FROM n) {
        if constexpr (std::numeric_limits<FROM>::is_signed && !std::numeric_limits<TO>::is_signed) {
            if (n < 0)
                return 0;
        }
        if (n < std::numeric_limits<TO>::lowest())
            return std::numeric_limits<TO>::lowest();
        else if (n > std::numeric_limits<TO>::max())
            return std::numeric_limits<TO>::max();
        else
            return static_cast<TO>(n);
    }

template <Numeric FROM>
    constexpr FROM pinning_cast(FROM n) {return n;}


template <typename T>
T* offsetBy(T *ptr, intptr_t offset) {
    return (T*)(intptr_t(ptr) + offset);
}

template <typename T>
T const* offsetBy(T const *ptr, intptr_t offset) {
    return (T const*)(intptr_t(ptr) + offset);
}

class Block;
class Heap;
class Object;
class SymbolTable;
class Val;
class Value;


template <typename T> concept ValueClass = requires{ T::HasType; };
template <typename T> concept ObjectClass = std::derived_from<T, Object>;
template <ObjectClass T> class Maybe;

}
