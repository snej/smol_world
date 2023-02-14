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


template <typename T, unsigned Size>
void sparse_bucket<T,Size>::dump(std::ostream &out) const {
    out << std::hex;
    for (uint64_t b : _bitmap)
        out << std::setw(16) << std::setfill('0') << b << ' ';
    out << std::dec;
    out << '|';
    for (unsigned i = 0, n = count(); i < n; ++i)
        out << ' ' << _items[i];
    out << std::endl;
}

template <typename Key, typename Item, typename Hash>
void sparse_hash_table<Key,Item,Hash>::dump(std::ostream &out) const {
    out << "Sparse Hash Table, capacity=" << capacity() << ", " << _array.buckets().size() << " buckets of " << Bucket::Capacity << " bits (" << sizeof(Bucket) << " bytes) each\n";
    for (auto const& bucket : _array.buckets())
        bucket.dump(out);
    size_t space = _array.buckets().size() * sizeof(Bucket);
    out << "Using " << space << " bytes for " << count() << " items: " << (8*float(space)/count()) << " bits per item. ";
    out << "At full capacity, " << (8*float(space)/capacity()) << " bits per item.\n";
}


}
