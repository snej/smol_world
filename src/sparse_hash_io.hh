//
// sparse_hash_io.hh
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

#pragma once
#include "sparse_hash.hh"
#include <iostream>
#include <iomanip>

namespace snej::smol {


template <size_t Size>
void dump(std::ostream &out, bitmap<Size> const& bitm) {
    auto fill = out.fill();
    out << std::hex << std::setfill('0');
    for (uint64_t b : bitm.bits())
        out << std::setw(16) << b << ' ';
    out << std::dec << std::setfill(fill);
}

template <typename T, size_t Size, bool Sparse>
void dump(std::ostream &out, sparse_bucket<T,Size,Sparse> const& bucket) {
    dump(out, bucket.bits());
    out << '|';
    bucket.visit([&](size_t, T const& item) {
        out << ' ' << item;
        return true;
    });
    out << std::endl;
}


template <typename Key, typename Item, typename Hash, bool Sparse>
void dump(std::ostream &out, hash_table<Key,Item,Hash,Sparse> const& hash) {
    using ArrayClass = sparse_array<Item,HashBucketSize,Sparse>;
    out << (Sparse ? "Sparse" : "Dense") << " Hash Table: count=" << hash.count() << ", capacity=" << hash.capacity() << ", size=" << hash.table_size() << "; " << hash.buckets().size() << " buckets of " << ArrayClass::Bucket::Size << " bits (" << sizeof(typename ArrayClass::Bucket) << " bytes) each\n";
    for (auto const& bucket : hash.buckets())
        dump(out, bucket);
    size_t space = hash.buckets().size() * sizeof(typename ArrayClass::Bucket);
    out << "Overhead is " << space << " bytes for " << hash.count() << " items: " << (8*float(space)/hash.count()) << " bits per item. ";
    out << "At full capacity, " << (8*float(space)/hash.capacity()) << " bits per item.\n";
}


}
