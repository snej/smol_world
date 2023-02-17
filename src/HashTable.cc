//
// HashSet.cc
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "HashTable.hh"
#include "Heap.hh"
#include <cmath>
#include <iomanip>
#include <iostream>

namespace snej::smol {

namespace wy {
#include "wyhash32.h"
}

using namespace std;

// Minimum size of table to create (must be a power of 2)
static constexpr uint32_t kMinTableSize = 8;

// Max fraction of entries that can be used; at this point we grow the hash table.
static constexpr float kMaxLoad = 0.5;

static constexpr unsigned kHashSeed = 0xFE152280;
//TODO: choosing different seed for each Heap would protect against some DoS attacks


static string_view keyString(Value v) {
    switch (v.type()) {
        case Type::String:  return v.as<String>().str();
        case Type::Symbol:  return v.as<Symbol>().str();
        default:            assert(false); abort();
    }
}


// hash function for keys: WyHash64.
// `Val` can only store 31-bit signed ints, so reinterpret hash as int32, then shift right 1 bit.
int32_t HashSet::computeHash(string_view str) {
    return int32_t(wy::wyhash32(str.data(), str.size(), kHashSeed)) >> 1 ;
}

int32_t HashSet::computeHash(Value key) {
    return HashSet::computeHash(keyString(key));
}

bool HashSet::keysMatch(Value key1, Value key2) {
    return key1 == key2 || keyString(key1) == keyString(key2);
}

bool HashSet::keysMatch(string_view key1, Value key2) {
    return key1 == keyString(key2);
}



SparseArray HashSet::_createArray(Heap &heap, uint32_t capacity) {
    uint32_t targetSize = uint32_t(capacity / kMaxLoad);
    uint32_t size = kMinTableSize;
    while (size < targetSize)
        size *= 2;
    return SparseArray(size, heap);
}

Array HashSet::createArray(Heap &heap, uint32_t capacity) {
    return _createArray(heap, capacity).array();
}


HashSet::HashSet(Heap &heap, SparseArray &&array, bool recount)
:_heap(&heap)
,_array(std::move(array))
,_size(uint32_t(_array.size()))
,_count(recount ? _array.nonNullCount() : 0)
,_capacity(uint32_t(round(_size * kMaxLoad)))
{
    assert((_size & (_size - 1)) == 0);                 // size must be a power of 2
}


HashSet::HashSet(Heap &heap, Array array)
:HashSet(heap, SparseArray(array, heap), true)
{ }

HashSet::HashSet(Heap &heap, unsigned capacity)
:HashSet(heap, _createArray(heap, capacity), false)
{ }


template <typename KEY>
std::pair<unsigned,bool>
HashSet::search(KEY key, int32_t hashCode) const {
    auto i = uint32_t(hashCode) & (_size - 1);
    while (true) {
        if (Value val = _array[i]) {
            if (keysMatch(key, val))
                return {i, true};
        } else {
            return {i, false};
        }
        i = (i + 1) & (_size - 1);
    }
}


Value HashSet::find(string_view str) const {
    if (auto [i, found] = search(str, computeHash(str)); found)
        return _array[i];
    else
        return nullvalue;
}


bool HashSet::insert(Value key, int32_t hashCode) {
    auto [i, found] = search(key, hashCode);
    return !found && insert(key, hashCode, i);
}


bool HashSet::insert(Value key, int32_t hashCode, unsigned i) {
    if (_count >= _capacity) {
        Handle hKey(&key, *_heap);
        if (!grow())
            return false;
        i = search(key, hashCode).first;
    }
    assert(!_array.contains(i));
    if (!_array.put(i, key))
        return false;
    ++_count;
    return true;
}


bool HashSet::grow() {
//    std::cout << "=== Growing HashSet to " << 2*_size << " buckets ===\n";
    HashSet newTable(*_heap, SparseArray(2 * _size, *_heap), false);
    // Scan the old table, inserting each entry into the new table:
    bool ok = _array.visit([&](unsigned, Value val) {
        return newTable.insert(val);
    });
    if (!ok)
        return false;
    // Switch to the new table:
    swap(*this, newTable);
    return true;
}


bool HashSet::visit(Visitor visitor) const {
    return _array.visit([&](unsigned, Value val) {
        return visitor(val);
    });
}


void HashSet::dump(std::ostream &out, bool longForm) const {
    if (longForm) {
        unsigned probes = 0;
        unsigned lasti = 0;
        _array.visit([&](unsigned i, Value val) {
            out << setw(3) << i << ": ";
            if (++lasti < i) {
                do {
                    out << '.';
                } while (++lasti < i);
                out << std::endl;
            }
            uint32_t hashCode = computeHash(val);
            int32_t delta = i - (hashCode & (_size - 1));
            if (delta) {
                if (delta < 0) delta += _size;
                out << '+' << setw(2) << delta << ' '; // distance from optimal position
            } else {
                out << "    ";
            }
            probes += 1 + delta;
            out << setw(8) << hex << hashCode << dec << ' ' << val << endl;
            lasti = i;
            return true;
        });
        out << _count << " symbols in " << _size << " buckets; " << (_count/float(_size)*100.0) << "% full. total #probes is " << probes << ", avg is " << (probes/float(_count));
    } else {
        out << "HashSet: " << _array;
    }
}


}
