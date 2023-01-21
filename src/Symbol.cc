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

namespace _ {
#include "wyhash.h"
}


static constexpr heapsize kInitialHashTableSize = 8;
static constexpr heapsize kInitialBucketSize = 3;
//static constexpr heapsize kMaxBucketSize = 8;


static inline uint32_t hash(const void *data, size_t size) {
    return uint32_t(_::wyhash(data, size, 0, _::_wyp));
}


Array* Symbol::getTable(IN_HEAP) {
    return heap->symbolTable().as<Array>(heap);
}


Symbol* Symbol::create(string_view str, IN_MUT_HEAP) {
    // Find or create the hash table array:
    Array *table = getTable(heap);
    if (!table) {
        table = Array::create(kInitialHashTableSize, heap);
        heap->symbolTable() = table->asVal(heap);
    }

    // Use the string's hash to find the bucket:
    auto tableSize = table->size();
    auto hashCode = hash(str.data(), str.size());
    uint32_t i = hashCode % tableSize;
    auto& bucketp = (*table)[i];
    Array* bucket = bucketp.as<Array>(heap);

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
    Symbol *symbol = Collection::create(str.data(), str.size(), heap);
    if (!symbol)
        return nullptr;
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
        // TODO: If bucketSize == kMaxBucketSize, grow the entire hash table
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


void Symbol::visitSymbols(IN_HEAP, Visitor visitor) {
    Array *table = getTable(heap);
    if (!table)
        return;
    uint32_t bucketNo = 0;
    for (Val item : *table) {
        if (Array *bucket = item.as<Array>(heap)) {
            for (Val sym : *bucket) {
                if (Symbol *symbol = sym.as<Symbol>(heap)) {
                    if (!visitor(symbol, bucketNo))
                        return;
                }
            }
        }
        ++bucketNo;
    }
}
