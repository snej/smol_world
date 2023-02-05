//
// HashTable.cc
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

/*  A hash table is represented as an Array of size 2*n or 3*n, where n is the number of table
    entries. n must be a power of 2.
    - The first n items are hash codes (Ints). An empty entry has null.
    - The next n items are the keys associated with those hash codes (Symbols or Strings.)
    - (HashMap only) The next n items are the corresponding values.
*/

// Minimum size of table to create (must be a power of 2)
static constexpr uint32_t kMinTableSize = 8;

// Max fraction of entries that can be used; at this point we grow the hash table.
static constexpr float kMaxLoad = 0.9;

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
int32_t HashTable::computeHash(string_view str) {
    return int32_t(wy::wyhash32(str.data(), str.size(), kHashSeed)) >> 1 ;
}

int32_t HashTable::computeHash(Value key) {
    return HashTable::computeHash(keyString(key));
}

bool HashTable::keysMatch(Value key1, Value key2) {
    return key1 == key2 || keyString(key1) == keyString(key2);
}

bool HashTable::keysMatch(string_view key1, Value key2) {
    return key1 == keyString(key2);
}



Maybe<Array> HashTable::createArray(Heap &heap, uint32_t capacity, bool withValues) {
    uint32_t targetSize = uint32_t(capacity / kMaxLoad);
    uint32_t size = kMinTableSize;
    while (size < targetSize)
        size *= 2;
    size *= (withValues ? 3 : 2);
    auto maybeArray = newArray(size, heap);
    if_let(array, maybeArray) {
        array[size - 1] = nullishvalue;  // keep array from being truncated on GC
    }
    return maybeArray;
}


HashTable::HashTable(Heap &heap, Array array, bool hasValues, bool recount)
:_heap(&heap)
,_array(array, heap)
,_hasValues(hasValues)
{
    int arraySize = _array.size();
    int itemsPerEntry = _hasValues ? 3 : 2;
    assert(arraySize % itemsPerEntry == 0);
    _size = uint32_t(_array.size() / itemsPerEntry);    // each entry has 2 or 3 array items
    assert((_size & (_size - 1)) == 0);                 // size must be a power of 2
    _capacity = uint32_t(round(_size * kMaxLoad));

    uint32_t count = 0;
    if (recount) {
        for (auto &key : keys()) {
            if (key != nullval && key != nullishval)
                ++count;
        }
    }
    _count = count;
}


template <typename KEY>
std::pair<Val*,bool>
HashTable::search(KEY key, int32_t hashCode) const {
    Val hashVal(hashCode);
    slice<Val> hashes = this->hashes();
    Val *entry = &hashes[uint32_t(hashCode) & (_size - 1)];
    while (true) {
        if (entry[0] == hashVal) {
            if (keysMatch(key, entry[_size]))
                return {entry, true};
        } else if (entry[0] == nullval) {
            return {entry, false};
        }
        if (++entry == hashes.end())
            entry = hashes.begin();
    }
}


bool HashTable::insert(Value key, int32_t hashCode, Value value) {
    auto [entry, found] = search(key, hashCode);
    return !found && insert(key, hashCode, entry, value);
}


bool HashTable::insert(Value key, int32_t hashCode, Val* entry, Value value) {
    if (_count >= _capacity) {
        if (!grow())
            return false;
        entry = search(key, hashCode).first;
    }
    assert(entry[0] == nullval);
    entry[0] = hashCode;
    entry[_size] = key;
    if (_hasValues)
        entry[2 * _size] = value;
    ++_count;
    return true;
}


bool HashTable::grow() {
//    std::cout << "=== Growing HashTable to " << 2*_size << " buckets ===\n";
    uint32_t newSize = 2 * _array.size();
    unless(array, newArray(newSize, *_heap)) { return false; }
    array[newSize - 1] = nullishvalue;  // keep array from being truncated on GC
    HashTable newTable(*_heap, array, _hasValues, false);
    // Scan the old table, inserting each entry into the new table:
    for (Val &hashVal : hashes()) {
        if (hashVal != nullval) {
            int32_t hash = (&hashVal)[0].asInt();
            Value key = (&hashVal)[_size];
            Value value;
            if (_hasValues)
                value = (&hashVal)[2*_size];
            newTable.insert(key, hash, value);
        }
    }
    newTable._count = _count;
    // Switch to the new table:
    swap(*this, newTable);
    return true;
}


bool HashTable::visit(Visitor visitor) const {
    Value value;
    for (Val &key : keys()) {
        if (key != nullval && key != nullishval) {
            if (_hasValues)
                value = (&key)[_size];
            if (!visitor(key, value))
                return false;
        }
    }
    return true;
}


void HashTable::dump(std::ostream &out, bool includeEmpty) const {
    uint32_t count = 0, probes = 0;
    uint32_t i = 0;
    for (Val const* entry = hashes().begin(); entry != hashes().end(); ++entry) {
        Value hash = entry[0];
        if (hash != nullvalue) {
            out << setw(3) << i << ": ";
            ++count;
            uint32_t hashCode = hash.asInt();
            uint32_t delta = i - (hashCode & (_size - 1));
            if (delta) {
                out << '+' << setw(2) << delta << ' '; // distance from optimal position
            } else {
                out << "    ";
            }
            probes += 1 + delta;
            Value key = entry[_size];
            out << setw(8) << hex << hashCode << dec << ' ' << key;
            if (_hasValues)
                out << " --> " << entry[2*_size];
            out << endl;
        } else if (includeEmpty) {
            out << setw(3) << i << ": \n";
        }
        ++i;
    }
    out << count << " symbols in " << _size << " buckets; " << (count/float(_size)*100.0) << "% full. total #probes is " << probes << ", avg is " << (probes/float(count));
}


#pragma mark - HASHMAP:


Value HashMap::get(Value key) const {
    if (auto [entry, found] = search(key, computeHash(key)); found)
        return entry[2*_size];
    else
        return nullvalue;
}

bool HashMap::set(Value key, Value value, bool replace) {
    int32_t hashCode = computeHash(key);
    if (auto [entry, found] = search(key, hashCode); found) {
        if (!replace)
            return false;
        entry[2*_size] = value;
        return true;
    } else {
        return HashTable::insert(key, hashCode, entry, value);
    }
}



#pragma mark - HASHSET:


Value HashSet::find(string_view str) const {
    if (auto [entry, found] = search(str, computeHash(str)); found)
        return entry[_size];
    else
        return nullvalue;
}


}
