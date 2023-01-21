//
// Symbol.cc
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

#include "Symbol.hh"
#include <iostream>

namespace _ {
#include "wyhash.h"
}


/*
 Every Heap has a global symbol-table pointer.
 If not null, it points to an Array.
 The symbol string's hash code is divided by the size of the array, and that's its bucket index.
 Non-null items are buckets, which are also arrays.
 The non-null items in a bucket are Symbols.

 Buckets are created with a single item but have empty spaces for a few more.
 Once those are filled in, the bucket is reallocated (copied) to append new symbols.

 But if a bucket would grow too big, then instead the hash table doubles in size and all the
 symbols are put into new buckets in the new bigger table, which roughly halves their sizes.
*/


static constexpr heapsize kInitialNumberOfBuckets   = 8;
static constexpr heapsize kInitialBucketSize        = 3;
static constexpr heapsize kMaxBucketSize            = 8;


// hash function for Symbol strings
static inline uint32_t hash(const void *data, size_t size) {
    return uint32_t(_::wyhash(data, size, 0, _::_wyp));
}


Array* Symbol::getTable(IN_HEAP) {
    return heap->symbolTable().as<Array>(heap);
}


Symbol* Symbol::find(string_view str, IN_HEAP) {
    Array *table = getTable(heap);
    if (!table)
        return nullptr;
    auto tableSize = table->size();
    assert(tableSize > 0);
    auto hashCode = hash(str.data(), str.size());
    auto i = hashCode % tableSize;
    auto& bucketp = (*table)[uint32_t(i)];
    auto bucket = bucketp.as<Array>(heap);
    if (bucket) {
        for (auto itemv : *bucket) {
            auto item = itemv.as<Symbol>(heap);
            if (item && item->get() == str)
                return item;
        }
    }
    return nullptr;
}


Symbol* Symbol::create(string_view str, IN_MUT_HEAP) {
    // Find or create the heap's hash table array:
    Array *table = getTable(heap);
    if (!table) {
        table = Array::create(kInitialNumberOfBuckets, heap);
        heap->symbolTable() = table->asVal(heap);
    }
    return create(str, nullptr, table, heap);
}


Symbol* Symbol::create(string_view str, Symbol *symbol, Array *table, IN_MUT_HEAP) {
    // Use the string's hash to find the bucket:
    auto tableSize = table->size();
    auto hashCode = hash(str.data(), str.size());
    uint32_t i = hashCode % tableSize;
    Array* bucket = (*table)[i].as<Array>(heap);

    bool rehashing = (symbol != nullptr);
    if (!rehashing) {
        if (bucket) {
            // Look for matching Symbol in bucket:
            for (auto itemv : *bucket) {
                auto item = itemv.as<Symbol>(heap);
                if (!item)
                    break;
                if (item->get() == str)
                    return item;    // Return existing symbol!
            }
        }

        // Have to create a new Symbol:
        symbol = Collection::create(str.data(), str.size(), heap);
        if (!symbol)
            return nullptr;
    }

    Val symVal = symbol->asVal(heap);
    if (bucket) {
        // Add symbol to existing bucket:
        for (Val &itemv : *bucket) {
            if (!itemv.is<Symbol>(heap)) {
                // Store new symbol in the first empty spot in the existing bucket:
                itemv = symVal;
                return symbol;
            }
        }
        // Bucket is full, so allocate a bigger one:
        if (bucket->count() >= kMaxBucketSize && !rehashing) {
            // Bucket is too big; instead, grow the hash table itself.
            if (!growTable(heap))
                return nullptr;
            return create(str, heap);
        }
        auto bucketSize = bucket->size();
        Array *newBucket = Array::create(bucket->items(), bucketSize + 1, heap);
        if (!newBucket) return nullptr;
        (*newBucket)[heapsize(bucketSize)] = symVal;
        (*table)[i] = newBucket->asVal(heap);
        return symbol;
    } else {
        // Create a new bucket:
        Array *newBucket = Array::create(slice(&symVal, 1), kInitialBucketSize, heap);
        (*table)[i] = newBucket->asVal(heap);
        return symbol;
    }
}


bool Symbol::growTable(IN_MUT_HEAP) {
    Array *oldTable = getTable(heap);
    auto oldSize = oldTable->count();
    std::cerr << "=== Growing symbol table from " << oldSize << " to " << 2*oldSize << " buckets ===\n";
    Array *newTable = Array::create(2 * oldSize, heap);
    if (!newTable)
        return false;
    bool ok = visitSymbols(heap, [&](Symbol *symbol, uint32_t bucketNo) {
        return create(symbol->get(), symbol, newTable, heap) != nullptr;
    });
    if (ok)
        heap->symbolTable() = newTable->asVal(heap);
    return ok;
}


bool Symbol::visitSymbols(IN_HEAP, Visitor visitor) {
    Array *table = getTable(heap);
    if (!table)
        return true;
    uint32_t bucketNo = 0;
    for (Val item : *table) {
        if (Array *bucket = item.as<Array>(heap)) {
            for (Val sym : *bucket) {
                if (Symbol *symbol = sym.as<Symbol>(heap)) {
                    if (!visitor(symbol, bucketNo))
                        return false;
                }
            }
        }
        ++bucketNo;
    }
    return true;
}
