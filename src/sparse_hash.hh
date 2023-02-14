//
// sparse_hash.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.hh"
#include <array>
#include <cmath>
#include <iosfwd>
#include <vector>

namespace snej::smol {

namespace {
    template <typename INT> unsigned popcount(INT);
#if defined(__clang__) || defined(__GNUC__)
    template<> inline unsigned popcount<uint64_t>(uint64_t value)  {return __builtin_popcountll(value);}
    template<> inline unsigned popcount<uint32_t>(uint32_t value)  {return __builtin_popcount(value);}
#else
#   error "Haven't added popcount support for this platform yet"
#endif
}


/// A fixed-size array, whose items can be empty, that only allocates memory for non-empty items.
/// The array size (`Size`) must be a multiple of 64.
template <typename T, unsigned Size = 64>
class sparse_bucket {
public:
    static constexpr unsigned SizeQuantum = sizeof(uint64_t) * 8;
    static_assert(Size % SizeQuantum == 0);
    static constexpr unsigned Capacity = Size;
    static constexpr unsigned N = Size / SizeQuantum;

    sparse_bucket(unsigned capacity = 0)        {if (capacity > 0) grow(0, capacity);}
    sparse_bucket(sparse_bucket &&old)          {*this = std::move(old);}
    ~sparse_bucket()                            {::free(_items);}

    sparse_bucket& operator=(sparse_bucket &&old) {
        _bitmap = old._bitmap;
        _items = old._items;
        old._items = nullptr;
        return *this;
    }

    /// True if all items are empty. (Faster than `count() == 0`.)
    bool empty() const                          {return count() == 0;}

    /// The number of non-empty items.
    /// @note  This has to be computed; it is not cached.
    unsigned count() const {
        unsigned c = 0;
        for (unsigned i = 0; i < N; i++)
            c += popcount(_bitmap[i]);
        return c;
    }

    /// True if there is an item at the given index. (Faster than `find(i) != nullptr`.)
    bool contains(unsigned i) const {
        assert(i < Size);
        return (_bitmap[bitmapIndex(i)] & bit(i)) != 0;
    }

    /// Returns a copy of the item at the given index, else a default-constructed item.
    T get(unsigned i) const {
        T const* p = find(i);
        return p ? *p : T();
    }

    /// Returns the address of the item at the given index, else nullptr if it's not present.
    /// @warning  Item pointers are invalidated by any modification of the bucket.
    T const* find(unsigned i) const {
        if (!contains(i))
            return nullptr;
        size_t bi = bitmapIndex(i);
        unsigned itemIndex = popcount( _bitmap[bi] & (bit(i) - 1) );
        while (bi > 0)
            itemIndex += popcount(_bitmap[--bi]);
        return &_items[itemIndex];
    }

    /// Stores a value at an index. Returns the address of the item in the bucket.
    /// @warning  Item pointers are invalidated by any modification of the bucket.
    T* put(unsigned index, T&& value) {
        if (contains(index)) {
            return _replace(index, std::forward<T>(value));
        } else {
            return _insert(index, std::forward<T>(value));
        }
    }

    /// Stores a value at an index, but only if that index was empty; else just returns nullptr.
    /// @warning  Item pointers are invalidated by any modification of the bucket.
    T* insert(unsigned index, T&& value) {
        if (contains(index))
            return nullptr;
        return _insert(index, std::forward<T>(value));
    }

    void dump(std::ostream &out) const;

private:
    // The size of one element of `_items`, taking into account alignment.
    static constexpr size_t ItemSize = ((sizeof(T) + alignof(T) - 1) / alignof(T)) * alignof(T);

    static uint64_t bit(unsigned i)             {return 1ull << (i & 63);}
    static size_t bitmapIndex(unsigned i)       {return i >> 6;}

    size_t _itemIndex(unsigned i) const {
        size_t bi = bitmapIndex(i);
        unsigned itemIndex = popcount( _bitmap[bi] & (bit(i) - 1) );
        while (bi > 0)
            itemIndex += popcount(_bitmap[--bi]);
        return itemIndex;
    }

    T* _insert(unsigned i, T&& value) {
        assert(!contains(i));
        auto totalCount = count();
        grow(totalCount, totalCount + 1);
        size_t bi = bitmapIndex(i);
        auto itemIndex = _itemIndex(i);
        assert((_bitmap[bi] & bit(i)) == 0);
        _bitmap[bi] |= bit(i);
        if (itemIndex < totalCount) // Note: Assumes T is moveable!
            ::memmove(&_items[itemIndex + 1], &_items[itemIndex],
                      (totalCount - itemIndex) * ItemSize);
        T* result = &_items[itemIndex];
        *result = std::forward<T>(value);
        return result;
    }

    T* _replace(unsigned i, T value) {
        assert(contains(i));
        T* result = &_items[_itemIndex(i)];
        *result = value;
        return result;
    }

    void grow(unsigned oldCapacity, unsigned newCapacity) {
        // Always reallocs to the exact needed capacity, leaving no room to grow.
        // This minimizes memory usage, at the expense of performance.
        assert(newCapacity <= Size);
        auto newItems = (T*)::realloc(_items, newCapacity * ItemSize);
        if (!newItems)
            throw std::bad_alloc();
        for (auto i = oldCapacity; i < newCapacity; ++i)
            new (&newItems[i]) T();  // default-construct new empty items
        _items = newItems;
    }

    std::array<uint64_t,N>  _bitmap = {};
    T*                      _items = nullptr;
};



/// A variable-length array whose items can be empty,
/// that only allocates memory for non-empty items.
template <typename T, size_t BucketSize = 128>
class sparse_array {
public:
    sparse_array(size_t size = BucketSize)  :_buckets(bucketCount(size)) { }

    /// The number of items in the array
    size_t size() const                 {return _buckets.size() * BucketSize;}
    /// Grows/shrinks the array.
    void resize(size_t newSize)         {_buckets.resize(bucketCount(newSize));}

    /// The number of non-empty items.
    size_t count() const                {return _count;}

    /// True if all items are empty.
    bool empty() const                  {return _count == 0;}

    /// True if there is an item at the given index.
    bool contains(unsigned index)       {return bucketFor(index).contains(bucketOffset(index));}

    /// Returns the address of the item at the given index, else nullptr if it's not present.
    T const* find(unsigned index) const {return bucketFor(index).find(bucketOffset(index));}

    /// Returns a copy of the item at the given index, else a default-constructed item.
    T get(unsigned index) const         {return bucketFor(index).get(bucketOffset(index));}

    /// Stores a value at an index. Returns the address of the item.
    T* put(unsigned index, T&& value) {
        auto [result, isNew] = bucketFor(index).put(bucketOffset(index), std::forward<T>(value));
        if (isNew)
            ++_count;
        return result;
    }

    /// Stores a value at an index, but only if that index was empty; else just returns nullptr.
    T* insert(unsigned index, T&& value) {
        T* result = bucketFor(index).insert(bucketOffset(index), std::forward<T>(value));
        if (result)
            ++_count;
        return result;
    }

    using Bucket = sparse_bucket<T,BucketSize>;
    std::vector<Bucket> const& buckets() const  {return _buckets;}

private:
    static unsigned bucketOffset(size_t i)  {return unsigned(i & (BucketSize - 1));}
    static size_t bucketIndex(size_t i)     {return i / BucketSize;}
    static size_t bucketCount(size_t i)     {return bucketIndex(i + BucketSize - 1);}

    Bucket& bucketFor(size_t i) {
        i = bucketIndex(i);
        assert(i < _buckets.size());
        return _buckets[i];
    }

    Bucket const& bucketFor(size_t i) const  {
        return const_cast<sparse_array*>(this)->bucketFor(i);
    }

    std::vector<Bucket> _buckets;
    size_t              _count = 0;
};



/// A hash table using a sparse_array as its backing store. Optimized for low memory usage.
/// `Key` is the key type used to look up items, e.g. `std::string_view`.
/// `Item` is the type stored in the table. Must be constructible from `Key`. E.g. `std::string`.
/// `Hash` is the hash function: must have an `operator()` that takes a `Key` and returns uint32_t.
template <typename Key, typename Item, typename Hash>
class sparse_hash_table {
public:
    sparse_hash_table(size_t capacity)          :_array(arraySizeForCapacity(capacity)) { }

    /// The maximum number of items the hash table can store.
    size_t capacity() const pure                {return size_t(floor(_array.size() * kMaxLoad));}

    /// The current number of items.
    size_t count() const pure                   {return _array.count();}

    /// Looks up an Item by its Key; returns a pointer to it if found, else nullptr.
    Item* get(Key const& key)                   {return const_cast<Item*>(search(key, Hash{}(key)).first);}
    Item const* get(Key const& key) const       {return search(key, Hash{}(key)).first;}

    /// Adds a new item with the given key and returns a pointer;
    /// if one already exists, returns nullptr.
    Item* insert(Key const& key) {
        auto [item, i] = search(key, Hash{}(key));
        if (item)
            return nullptr;
        return _array.insert(i, Item(key));
    }

    /// Returns the existing item with the given key; if there is none, it inserts a new one.
    Item* put(Key const& key) {
        auto [item, i] = search(key, Hash{}(key));
        if (item)
            return const_cast<Item*>(item);
        return _array.insert(i, Item(key));
    }

    static constexpr unsigned BucketSize = 2 * sparse_bucket<Item>::SizeQuantum;
    static_assert((BucketSize & (BucketSize - 1)) == 0);
    using Bucket = typename sparse_array<Item,BucketSize>::Bucket;

    void dump(std::ostream &out) const;

private:
    static constexpr float kMaxLoad = 0.5;

    size_t arraySizeForCapacity(size_t capacity) {
        auto targetSize = size_t(ceil(capacity / kMaxLoad));
        size_t size = BucketSize;
        while (size < targetSize)
            size *= 2;
        return size;
    }

    std::pair<Item const*,unsigned> search(Key const& key, uint32_t hashCode) const {
        unsigned mask = unsigned(_array.size() - 1);
        unsigned i = uint32_t(hashCode) & mask;
        unsigned probe = 0;
        while (true) {
            auto item = _array.find(i);
            if (!item || *item == key)
                return {item, i};
            i = (i + (++probe)) & mask;     // quadratic (triangle-number) probing
            assert(probe < mask);
        }
    }

    sparse_array<Item,BucketSize> _array;
};


}
