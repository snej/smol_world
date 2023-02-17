//
// SparseArray.cc
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

#include "SparseArray.hh"
#include <iostream>

/*  A SparseArray is represented as an Array.
     - Item 0 is a Blob containing a bitmap, one bit per slot in the table.
     - The rest of its items are Arrays called buckets.
     - Each bucket contains the Values for kBitsPerBucket slots, with nulls omitted.
 */

namespace snej::smol {


// Number of items a bucket grows by at once. Larger values reduce the number of bucket reallocs
// and copies; smaller values have less space overhead.
constexpr unsigned kBucketGrowsBy = 4;


template <typename INT> unsigned popcount(INT);
#if defined(__clang__) || defined(__GNUC__)
inline unsigned popcount(uint64_t value)  {return __builtin_popcountll(value);}
#else
#   error "Haven't added popcount support for this platform yet"
#endif


Array SparseArray::makeArray(unsigned size, Heap &heap) {
    auto nBuckets = (size + kItemsPerBucket - 1) / kItemsPerBucket;
    unless(array, newArray(1 + nBuckets, heap)) {throw std::bad_alloc();}
    auto nInts = (size + 64 - 1) / 64;
    unless(bitmap, newBlob(nInts * sizeof(uint64_t), heap)) {throw std::bad_alloc();}
    array[0] = bitmap;
    return array;
}


SparseArray::SparseArray(unsigned size, Heap &heap)
:SparseArray(makeArray(size, heap), heap)
{ }


SparseArray::SparseArray(Array array, Heap& heap)
:_heap(&heap)
,_array(array, heap)
,_bitmap(array[0].as<Blob>(), heap)
{ }


unsigned SparseArray::nonNullCount() const {
    unsigned total = 0;
    for (uint64_t bits : bitmap())
        total += popcount(bits);
    return total;
}


bool SparseArray::allNull() const {
    uint64_t ored = 0;
    for (uint64_t bits : bitmap())
        ored |= bits;
    return ored == 0;
}


bool SparseArray::contains(unsigned i) const {
    assert(i < size());
    return (bitmap()[i / 64] & maskFor(i)) != 0;
}


Value SparseArray::get(unsigned i) const {
    if (!contains(i))
        return nullvalue;
    return bucketFor(i).value()[indexInBucket(i)];
}


bool SparseArray::put(unsigned i, Value value) {
    assert(i < size());
    uint64_t mask = maskFor(i);
    bool exists = (bitmap()[i / 64] & mask) != 0;
    unsigned indexInBucket = this->indexInBucket(i);
    Maybe<Array> bucket = bucketFor(i);
    if (exists) {
        if (value != nullvalue) {
            // Replacement:
            bucket.value()[indexInBucket] = value;
        } else {
            // Removal:
            Value newBucketVal = deleteFromBucket(bucket.value(), indexInBucket);
            _array[1 + (i / kItemsPerBucket)] = newBucketVal;
            bitmap()[i / 64] &= ~mask;
        }
    } else {
        // Insertion:
        if (value != nullvalue) {
            Handle hv(&value, *_heap);
            unless(newBucket, insertIntoBucket(bucket, indexInBucket)) {return false;}
            newBucket[indexInBucket] = value;
            _array[1 + (i / kItemsPerBucket)] = newBucket;
            bitmap()[i / 64] |= mask;
        }
    }
    return true;
}


Maybe<Array> SparseArray::insertIntoBucket(Maybe<Array> bucket_, unsigned index) {
    if_let(bucket, bucket_) {
        auto bucketSize = bucket.size();
        assert(index <= bucketSize);
        if (index == bucketSize || bucket[bucketSize-1] != nullval) {
            // Bucket is full; allocate a new one and copy values:
            Handle hb(&bucket_, *_heap);
            unless(newBucket, newArray(bucket.size() + kBucketGrowsBy, *_heap)) {
                return nullvalue;
            }
            for (int i = bucketSize - 1; i >= 0; --i)
                newBucket[i + (i >= index)] = bucket[i];
            return newBucket;
       } else {
           // There's room to append to this bucket:
           for (int i = bucketSize - 1; i > index; --i)
               bucket[i] = bucket[i - 1];
           return bucket;
        }
    } else {
        // Bucket was nil -- allocate a small one
        assert(index == 0);
        return newArray(bucket.size() + kBucketGrowsBy, *_heap);
    }
}


Value SparseArray::deleteFromBucket(Array bucket, unsigned index) {
    if (unsigned bucketSize = bucket.size(); bucketSize > 1) {
        Handle hb(&bucket, *_heap);
        Maybe<Array> newBucket = newArray(bucketSize - 1, *_heap);
        if (!newBucket)
            newBucket = bucket; // If allocation fails, reuse the bucket leaving space at the end
        for (unsigned i = 0; i < bucketSize; ++i)
            newBucket.value()[i - (i > index)] = bucket[i];
        if (newBucket == bucket)
            bucket[bucket.size() - 1] = nullvalue;
        return newBucket;
    } else {
        return nullvalue;
    }
}


unsigned SparseArray::indexInBucket(unsigned i) const {
    auto bitmap = this->bitmap();
    uint64_t* bits = &bitmap[i / 64];
    unsigned index = popcount(*bits & (maskFor(i) - 1));
    // Add in the earlier bits in the same bucket:
    uint64_t* b = &bitmap[((i / kItemsPerBucket) * kItemsPerBucket) / 64];
    while (b < bits)
        index += popcount(*b++);
    return index;
}


std::ostream& operator<< (std::ostream &out, SparseArray const& array) {
    heapsize total = array._array.block()->blockSize();
    for (Value val : array._array)
        total += val.block()->blockSize();
    auto count = array.nonNullCount();

    out << "SparseArray(" << count << " / " << array.size() << "), " << total << " bytes (" << (total / float(count)) << "/item):\n";
    for (Value val : array._array)
        out << '\t' << val << std::endl;
    return out;
}


}
