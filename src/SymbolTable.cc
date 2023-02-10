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
#include "Heap.hh"
#include <iomanip>
#include <iostream>
#include <cmath>

namespace snej::smol {

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


std::unique_ptr<SymbolTable> SymbolTable::create(Heap *heap, heapsize capacity) {
    unless(table, HashSet::createArray(*heap, capacity)) {return nullptr;}
    return std::make_unique<SymbolTable>(heap, table);
}


std::unique_ptr<SymbolTable> SymbolTable::rebuild(Heap *heap) {
    // Pre-count the number of symbols:
    unsigned count = 0;
    heap->visitAll([&](Block const& block) -> bool {
        if (block.type() == Type::Symbol)
            ++count;
        return true;
    });

    // Create a big-enough table. We don't want the table to have to grow during the following
    // `visitAll`, because that might trigger GC and relocate the heap during iteration...
    auto table = SymbolTable::create(heap, std::max(count + 1, kInitialCapacity));
    if (!table) return table;

    // Now add all Symbols to the table:
    bool ok = heap->visitAll([&](Block const& block) -> bool {
        if (block.type() == Type::Symbol) {
            Symbol symbol = Value(&block).as<Symbol>();
            return table->_table.insert(symbol);
        }
        return true;
    });
    if (!ok)
        return nullptr;
    return table;
}


Maybe<Symbol> SymbolTable::create(string_view str) {
    bool inserted = false;
    auto sym = _table.findOrInsert(str, [&](Heap &heap) {
        inserted = true;
        return Maybe<Symbol>(Symbol::create(str, heap));
    });
    if (inserted)
        _table.heap().setSymbolTableArray(_table.array());
    return Maybe<Symbol>(sym);
}


bool SymbolTable::visit(Visitor visitor) const {
    return _table.visit([&](Value key, Value) {return visitor(key.as<Symbol>());});
}

}
