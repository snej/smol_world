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


enum class heappos : uintpos { };      ///< A position in a Heap, relative to its base.

static constexpr heappos nullpos {0};

static inline heappos operator+ (heappos p, intpos i) {return heappos(uintpos(p) + i);}
static inline heappos operator- (heappos p, intpos i) {return heappos(uintpos(p) - i);}
static inline std::strong_ordering operator<=> (heappos p, size_t i) {return uintpos(p) <=> i;}
static inline std::strong_ordering operator<=> (heappos p, intpos i) {return int64_t(p) <=> int64_t(i);}


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
