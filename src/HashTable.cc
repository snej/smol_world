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

/* HashTable ideas:
    List of hashes followed by list of values, optionally followed by list of keys
 */

// Minimum size of table to create (must be a power of 2)
static constexpr uint32_t kMinTableSize = 8;

// Max fraction of entries that can be used; at this point we grow the hash table.
static constexpr float kMaxLoad = 0.9;

static constexpr unsigned kHashSeed = 0xFE152280;
//TODO: choosing different seed for each Heap would protect against some DoS attacks


// hash function for keys: WyHash64.
// `Val` can only store 31-bit signed ints, so reinterpret hash as int32, then shift right 1 bit.
int32_t HashTable::computeHash(string_view str) {
    return int32_t(wy::wyhash32(str.data(), str.size(), kHashSeed)) >> 1 ;
}


unique_ptr<HashTable> HashTable::create(Heap &heap, uint32_t capacity) {
    uint32_t targetSize = uint32_t(capacity / kMaxLoad);
    uint32_t size = kMinTableSize;
    while (size < targetSize)
        size *= 2;
    unless(array, newArray(2 * size, heap)) {return nullptr;}
    return unique_ptr<HashTable>(new HashTable(heap, array, false));
}


HashTable::HashTable(Heap &heap, Array array, bool recount)
:_heap(&heap)
,_array(array, heap)
{
    _sizeMask = uint32_t(_array.size() / 2 - 1);    // number of HashEntrys - 1
    _capacity = uint32_t(round(tableSize() * kMaxLoad));
    uint32_t count = 0;
    if (recount) {
        for (auto i = begin(); i < end(); i++) {
            if (i->symbol != nullval)
                ++count;
        }
    }
    _count = count;
}


std::pair<HashTable::HashEntry*,Value>
HashTable::search(string_view str, int32_t hashCode) const {
    Val hashVal(hashCode);
    HashEntry *entry = begin() + (uint32_t(hashCode) & _sizeMask);
    while (true) {
        if (entry->hash == hashVal) {
            Value sym = entry->symbol;
            if (recoverKey(sym) == str)
                return {entry, sym};
        } else if (entry->hash == nullval) {
            return {entry, {}};
        }
        if (++entry == end())
            entry = begin();
    }
}


bool HashTable::insert(string_view str, Value symbol) {
    int32_t hashCode = computeHash(str);
    auto [entry, existing] = search(str, hashCode);
    return !existing && insert(str, hashCode, entry, symbol);
}

bool HashTable::insert(string_view str, int32_t hashCode, HashEntry* entry, Value newSymbol) {
    if (_count >= _capacity) {
        if (!grow())
            return false;
        entry = search(str, hashCode).first;
    }
    assert(entry->symbol == nullval);
    entry->hash = hashCode;
    entry->symbol = newSymbol;
    ++_count;
    return true;
}


bool HashTable::grow() {
    //    std::cout << "=== Growing symbol table from " << _table.size
    //              << " to " << 2*_table.size << " buckets ===\n";
    unless(array, newArray(4 * tableSize(), *_heap)) { return false; }
    HashTable newTable(*_heap, array, false);
    // Scan the old table, inserting each entry into the new table:
    for (auto e = begin(); e != end(); ++e) {
        if (e->symbol != nullval) {
            auto [newEntry, foundSym] = newTable.search(recoverKey(e->symbol), e->hash.asInt());
            assert(!foundSym);
            *newEntry = *e;
        }
    }
    newTable._count = _count;
    // Switch to the new table:
    swap(*this, newTable);
    return true;
}


bool HashTable::visit(Visitor visitor) const {
    for (auto i = begin(); i != end(); ++i) {
        if(i->symbol != nullval) {
            if (!visitor(i->symbol, i->hash.asInt()))
                return false;
        }
    }
    return true;
}


void HashTable::dump(std::ostream &out) const {
    uint32_t count = 0, probes = 0;
    uint32_t i = 0;
    for (auto e = begin(); e != end(); ++e, ++i) {
        out << setw(3) << i << ": ";
        if (Value symbol = e->symbol; symbol != nullvalue) {
            ++count;
            uint32_t hashCode = uint32_t(e->hash.asInt());
            uint32_t delta = i - (hashCode & _sizeMask);
            if (delta) {
                out << '+' << setw(2) << delta << ' '; // distance from optimal position
            } else {
                out << "    ";
            }
            probes += 1 + delta;
            out << setw(8) << hex << hashCode << dec << ' ' << symbol;
        }
        out << endl;
    }
    auto size = end() - begin();
    out << count << " symbols in " << size << " buckets; " << (count/float(size)*100.0) << "% full. total #probes is " << probes << ", avg is " << (probes/float(count));
}


}
