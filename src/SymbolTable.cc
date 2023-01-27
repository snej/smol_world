//
// SymbolTable.cc
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

#include "SymbolTable.hh"
#include <iomanip>
#include <iostream>
#include <cmath>

namespace wy {
#include "wyhash32.h"
}

using namespace std;


/*
 Every Heap has a global symbol-table pointer. If not null, it points to an Array.
 The array is an open hash table; it's interpreted as containing {hash, Symbol} pairs (`HashEntry`)
 where the `hash` field is either a hash code (int) or empty (null), and the `symbol field points
 to the Symbol object with that hash.

 The table algorithm is pretty generic; it uses linear probing. The table size is always a power
 of two since it speeds up the modulo computation.

 The `hash` field is redundant, but makes it faster to skip collision chains. It also speeds up
 growing the table since the Symbol strings don't have to be rehashed. It will also be needed if I
 implement Robin Hood hashing.
*/

// Initial size of hash table (number of entries.)
static constexpr heapsize kInitialNumberOfEntries   = 128;

// Max fraction of entries that can be used; at this point we grow the hash table.
static constexpr float kMaxLoad = 0.9;

static constexpr unsigned kHashSeed = 0xFE152280;
//TODO: choosing different seed for each Heap would protect against some DoS attacks


// hash function for Symbol strings: WyHash64.
// `Val` can only store 31-bit signed ints, so reinterpret hash as int32, then shift right 1 bit.
static inline int32_t computeHash(string_view str) {
    return int32_t(wy::wyhash32(str.data(), str.size(), kHashSeed)) >> 1 ;
}


SymbolTable::HashTable::HashTable(Array a)
:array(a)
{
    begin = (SymbolTable::HashEntry*)array.begin();
    end = (SymbolTable::HashEntry*)array.end();
    sizeMask = uint32_t(array.size() / 2 - 1);
    assert((sizeMask & (sizeMask + 1)) == 0); // must be power of 2 - 1
    capacity = uint32_t(round(size() * kMaxLoad));
}


uint32_t SymbolTable::HashTable::count() const {
    uint32_t count = 0;
    for (auto i = begin; i < end; i++) {
        if (i->symbol != nullval)
            ++count;
    }
    return count;
}


std::pair<SymbolTable::HashEntry*,Maybe<Symbol>>
SymbolTable::HashTable::search(Heap *heap, string_view str, int32_t hashCode) const {
    Val hashVal(hashCode);
    HashEntry *entry = begin + (uint32_t(hashCode) & sizeMask);
    while (true) {
        if (entry->hash == hashVal) {
            Symbol sym = entry->symbol.as<Symbol>(heap);
            if (sym.str() == str)
                return {entry, sym};
        } else if (entry->hash == nullval) {
            return {entry, {}};
        }
        if (++entry == end)
            entry = begin;
    }
}


void SymbolTable::HashTable::dump(std::ostream &out, Heap const* heap) const {
    uint32_t count = 0, probes = 0;
    uint32_t i = 0;
    for (auto e = begin; e != end; ++e, ++i) {
        out << setw(3) << i << ": ";
        if (auto symbol = e->symbol.maybeAs<Symbol>(heap)) {
            ++count;
            uint32_t hashCode = uint32_t(e->hash.asInt());
            uint32_t delta = i - (hashCode & sizeMask);
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
    auto size = end - begin;
    out << count << " symbols in " << size << " buckets; " << (count/float(size)*100.0) << "% full. total #probes is " << probes << ", avg is " << (probes/float(count));
}


std::unique_ptr<SymbolTable> SymbolTable::create(IN_MUT_HEAP) {
    if_let(table, Array::create(2 * kInitialNumberOfEntries, heap))
        return std::make_unique<SymbolTable>(heap, table);
    else
        return nullptr;
}


void SymbolTable::setTable(Val tableVal) {
    Array array = tableVal.as<Array>(_heap);
    if (array != _table.array) {  // if called from my own setTable(Array) method, don't do this
        _table = HashTable(array);
        _count = _table.count();
    }
}


Maybe<Symbol> SymbolTable::find(string_view str) const {
    if (!_table)
        return nullptr;
    auto [entry, symbol] = _table.search(_heap, str, computeHash(str));
    return symbol;
}


Maybe<Symbol> SymbolTable::create(string_view str) {
    if (_table) {
        if (_count >= _table.capacity) {
            if (!grow()) return nullptr;
        }
    } else {
        setTable(Array::create(2 * kInitialNumberOfEntries, _heap));
    }
    int32_t hashCode = computeHash(str);
    auto [entry, symbol] = _table.search(_heap, str, hashCode);
    if (!symbol) {
        symbol = Symbol::create(str, _heap);
        if (!symbol)
            return nullptr;
        entry->hash = hashCode;
        entry->symbol = symbol;
        ++_count;
        if (_count == 1)
            _heap->setSymbolTableVal(_table.array); // make sure it's registered with the Heap
    }
    return symbol;
}


bool SymbolTable::grow() {
//    std::cout << "=== Growing symbol table from " << _table.size
//              << " to " << 2*_table.size << " buckets ===\n";
    unless(newArray, Array::create(4 * _table.size(), _heap)) { return false; }
    HashTable newTable(newArray);
    // Scan the old table, inserting each Symbol into the new table:
    for (auto e = _table.begin; e != _table.end; ++e) {
        if_let (symbol, e->symbol.maybeAs<Symbol>(_heap)) {
            auto [newEntry, foundSym] = newTable.search(_heap, symbol.str(), e->hash.asInt());
            assert(!foundSym);
            *newEntry = *e;
        }
    }
    // Switch to the new table:
    _table = newTable;
    _heap->setSymbolTableVal(newArray);
    return true;
}


bool SymbolTable::visit(Visitor visitor) const {
    if (!_table)
        return true;
    for (auto i = _table.begin; i != _table.end; ++i) {
        if_let (symbol, i->symbol.maybeAs<Symbol>(_heap)) {
            if (!visitor(symbol, i->hash.asInt()))
                return false;
        }
    }
    return true;
}
