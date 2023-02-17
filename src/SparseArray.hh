//
// SparseArray.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Collections.hh"
#include "Heap.hh"

namespace snej::smol {

    /// An object that acts like an Array but only allocates spaces for its non-null items.
    class SparseArray {
    public:
        /// Constructs a new SparseArray.
        explicit SparseArray(unsigned size, Heap&);

        /// Constructs an existing SparseArray.
        explicit SparseArray(Array, Heap&);

        Array array() const                     {return _array;}

        /// The logical size of the array (maximum index + 1.)
        unsigned size() const                   {return _bitmap.size() * 8;}

        /// The number of non-null items.
        unsigned nonNullCount() const;

        /// True if all items are null.
        bool allNull() const;

        /// True if the item at that index is non-null. Slightly faster than `get`.
        bool contains(unsigned i) const;

        Value get(unsigned i) const;
        [[nodiscard]] bool put(unsigned i, Value);

        Value operator[] (unsigned i) const     {return get(i);}

        friend std::ostream& operator<< (std::ostream &out, SparseArray const& array);

        /// Calls the function `v` for every item. Its signature should be `(unsigned,Value)->bool`.
        template <typename Visitor>
        bool visit(Visitor v) const {
            unsigned start = 0;
            unsigned nWords = _bitmap.size() / 8;
            for (unsigned word = 0; word < nWords; ++word) {
                uint64_t bits = bitmap()[word], mask = 1;
                for (unsigned i = 0; i < 64; ++i, mask <<= 1) {
                    if ((bits & mask) && !v(start + i, get(start + i)))
                        return false;
                }
                start += 64;
            }
            return true;
        }

        void setHeap(Heap &heap)   {_heap = &heap; _array.setHeap(heap); _bitmap.setHeap(heap);}

    private:
        static constexpr unsigned kItemsPerBucket = 2 * 64;
        static constexpr unsigned kBytesPerBucket = kItemsPerBucket / 8;

        static Array makeArray(unsigned size, Heap &);
        slice<uint64_t> bitmap() const {return slice_cast<uint64_t>(_bitmap.bytes());}
        Maybe<Array> bucketFor(unsigned i) const  {return _array[1 + (i / kItemsPerBucket)].maybeAs<Array>();}
        uint64_t maskFor(unsigned i) const         {return 1ull << (i & 63);}
        unsigned indexInBucket(unsigned i) const;
        Maybe<Array> insertIntoBucket(Maybe<Array> bucket, unsigned index);
        Value deleteFromBucket(Array bucket, unsigned index);

        Heap*           _heap;      // The heap; needed for reallocating bucket arrays
        Handle<Array>   _array;     // The root array
        Handle<Blob>    _bitmap;    // The Blob object in _array[0]
    };

}
