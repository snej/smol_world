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


/// A fixed-size bitmap, or array of bits.
template <size_t Size>
class bitmap {
public:
    /// The number of bits in each of the integers in the bitmap array (64.)
    static constexpr size_t SizeQuantum = sizeof(uint64_t) * 8;

    /// The number of 64-bit integers in the bitmap array.
    static constexpr size_t N = (Size + SizeQuantum - 1) / SizeQuantum;

    bitmap() = default;

    bitmap(uint64_t const bitmaps[N])   :_bitmap(bitmaps, N) { }

    /// The total number of '1' bits.
    /// @note  This has to be computed; it is not cached.
    size_t count() const {
        size_t c = 0;
        for (size_t i = 0; i < N; i++)
            c += popcount(_bitmap[i]);
        return c;
    }

    /// The number of '1' bits before index `i`.
    size_t countUpTo(size_t i) const {
        size_t bi = bitmapIndex(i);
        size_t itemIndex = popcount( _bitmap[bi] & (bit(i) - 1) );
        while (bi > 0)
            itemIndex += popcount(_bitmap[--bi]);
        return itemIndex;
    }

    /// True if all bits are zero. (Faster than `count() == 0`.)
    bool empty() const {
        uint64_t ored = 0;
        for (size_t i = 0; i < N; i++)
            ored |= _bitmap[i];
        return ored == 0;
    }

    /// The number of items the bucket holds (a constant.)
    static constexpr size_t size()                {return Size;}

    /// The value of the bit at the given index.
    bool contains(size_t i) const                 {return (_bitmap[bitmapIndex(i)] & bit(i)) !=0;}

    void insert(size_t i)                         {_bitmap[bitmapIndex(i)] |= bit(i);}
    void remove(size_t i)                         {_bitmap[bitmapIndex(i)] &= ~bit(i);}

    bool operator[] (size_t i) const              {return contains(i);}

    std::array<uint64_t,N> const& bits() const      {return _bitmap;}

    /// Calls the function `v` with every index whose bit is 1. Stops if the function returns false.
    /// The function's signature should be `(size_t)->bool`.
    template <typename Visitor>
    bool visit(Visitor v) const {
        size_t start = 0;
        for (uint64_t bits : _bitmap) {
            uint64_t mask = 1;
            for (size_t i = 0; i < 64; ++i, mask <<= 1) {
                if ((bits & mask) && !v(start + i))
                    return false;
            }
            start += 64;
        }
        return true;
    }

protected:
    static uint64_t bit(size_t i)                 {return 1ull << (i & 63);}
    static size_t bitmapIndex(size_t i)           {assert(i < Size); return i >> 6;}

private:
    std::array<uint64_t,N> _bitmap = {};
};



/// A fixed-size array, whose items can be empty, that only allocates memory for non-empty items.
/// The array size (`Size`) must be a multiple of 64.
template <typename T, size_t Size_, bool Sparse = true>
class sparse_bucket {
public:
    static constexpr size_t Size = Size_;

    sparse_bucket() {
        if (!Sparse)
            _items = std::make_unique<T[]>(Size);
    }

    sparse_bucket(uint64_t const* bitmaps, T const* items)
    :_bitmap(bitmaps)
    {
        if (Sparse) {
            size_t count = this->count();
            _items = std::make_unique<T[]>(count);
            T const* src = items;
            T* dst = &_items[0];
            _bitmap.visit([&](size_t i) {
                *dst = items[i];
            });
            assert(dst == &_items[count]);
        } else {
            _items = std::make_unique<T[]>(Size);
            copyItems(_items, items, Size);
        }
    }

    size_t capacity() const {return Size;}
    size_t count() const {return _bitmap.count();}
    bool empty() const {return _bitmap.empty();}
    bool contains(size_t i) const {return _bitmap.contains(i);}

    /// Returns a copy of the item at the given index, else a default-constructed item.
    T get(size_t i) const {
        T const* p = find(i);
        return p ? *p : T();
    }

    /// Returns the address of the item at the given index, else nullptr if it's not present.
    /// @warning  Item pointers are invalidated by any modification of the bucket. (When Sparse=true)
    T const* find(size_t i) const {
        return contains(i) ? &_items[_itemIndex(i)] : nullptr;
    }

    T const* operator[] (size_t i) const      {return find(i);}

    /// Stores a value at an index. Returns the address of the item in the bucket.
    /// @warning  Item pointers are invalidated by any modification of the bucket. (When Sparse=true)
    T* put(size_t index, T&& value) {
        if (contains(index))
            return _replace(index, std::forward<T>(value));
        else
            return _insert(index, std::forward<T>(value));
    }

    /// Stores a value at an index, but only if that index was empty; else just returns nullptr.
    /// @warning  Item pointers are invalidated by any modification of the bucket. (When Sparse=true)
    T* insert(size_t index, T&& value) {
        if (contains(index))
            return nullptr;
        return _insert(index, std::forward<T>(value));
    }

    /// Calls the function `v` for every item. Its signature should be `(size_t,T const&)->bool`.
    template <typename Visitor>
    bool visit(Visitor v) const {
        return _bitmap.visit([&](size_t i) {
            return v(i, *find(i));
        });
    }

    bitmap<Size> const& bits() const            {return _bitmap;}

private:
    size_t _itemIndex(size_t i) const         {return Sparse ? _bitmap.countUpTo(i) : i;}

    T* _insert(size_t i, T&& value) {
        assert(i < Size);
        assert(!contains(i));

        size_t itemIndex = 0;
        if (Sparse) {
            // Allocate a larger items array:
            auto count = this->count();
            auto newItems = std::make_unique<T[]>(count + 1);

            // Copy the existing items before/after the insertion:
            if (count > 0) {
                itemIndex = _itemIndex(i);
                moveItems(&newItems[0],             &_items[0],         itemIndex);
                moveItems(&newItems[itemIndex + 1], &_items[itemIndex], count - itemIndex);
            }
            _items = std::move(newItems);
        } else {
            itemIndex = i;
        }

        // Copy the inserted item:
        auto result = &_items[itemIndex];
        new (result) T(std::forward<T>(value));
        // Finally set the bit in the bitmap:
        _bitmap.insert(i);
        return result;
    }

    T* _replace(size_t i, T value) {
        assert(contains(i));
        T* result = &_items[_itemIndex(i)];
        *result = value;
        return result;
    }

    static void copyItems(T* dst, T const* src, size_t n) {
#if 0
        ::memcpy(dst, src, n * ItemSize);   // Optimization if `T` is trivially moveable
#else
        while (n-- > 0)
            *dst++ = *src++;
#endif
    }

    static void moveItems(T* dst, T const* src, size_t n) {
#if 0
        ::memcpy(dst, src, n * ItemSize);   // Optimization if `T` is trivially moveable
#else
        while (n-- > 0)
            *dst++ = std::move(*src++);
#endif
    }

    bitmap<Size>            _bitmap;
    std::unique_ptr<T[]>    _items;
};



/// A variable-length array whose items can be empty,
/// that only allocates memory for non-empty items.
template <typename T, size_t BucketSize = 128, bool Sparse = true>
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
    bool contains(size_t index)       {return bucketFor(index).contains(bucketOffset(index));}

    /// Returns the address of the item at the given index, else nullptr if it's not present.
    T const* find(size_t index) const         {return bucketFor(index)[bucketOffset(index)];}
    T const* operator[] (size_t i) const      {return find(i);}

    /// Returns a copy of the item at the given index, else a default-constructed item.
    T get(size_t index) const         {return bucketFor(index).get(bucketOffset(index));}

    /// Stores a value at an index. Returns the address of the item.
    T* put(size_t index, T&& value) {
        auto [result, isNew] = bucketFor(index).put(bucketOffset(index), std::forward<T>(value));
        if (isNew)
            ++_count;
        return result;
    }

    /// Stores a value at an index, but only if that index was empty; else just returns nullptr.
    T* insert(size_t index, T&& value) {
        T* result = bucketFor(index).insert(bucketOffset(index), std::forward<T>(value));
        if (result)
            ++_count;
        return result;
    }

    using Bucket = sparse_bucket<T,BucketSize,Sparse>;
    std::vector<Bucket> const& buckets() const  {return _buckets;}

    /// Calls the function `v` for every item. Its signature should be `(size_t,T const&)->bool`.
    template <typename Visitor>
    bool visit(Visitor v) const {
        size_t start = 0;
        for (auto& bucket : _buckets) {
            bool ok = bucket.visit([&](size_t i, T const& item) {
                return v(start + i, item);
            });
            if (!ok) return false;
            start += BucketSize;
        }
        return true;
    }

private:
    static size_t bucketOffset(size_t i)    {return i & (BucketSize - 1);}
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



// Value of BucketSize used by sparse_hash_table
static constexpr size_t HashBucketSize = 2 * bitmap<64>::SizeQuantum;
static_assert((HashBucketSize & (HashBucketSize - 1)) == 0);


/// A hash table using a sparse_array (in sparse or dense mode) as its backing store.
/// `Key` is the key type used to look up items, e.g. `std::string_view`.
/// `Item` is the type stored in the table. Must be constructible from `Key`. E.g. `std::string`.
/// `Hash` is the hash function: must have an `operator()` that takes a `Key` and returns uint32_t.
template <typename Key, typename Item, typename Hash, bool Sparse = true>
class hash_table {
public:
    hash_table(size_t capacity)
    :_array(arraySizeForCapacity(capacity))
    ,_capacity(size_t(floor(_array.size() * kMaxLoad)))
    { }

    template <bool S>
    hash_table(size_t capacity, hash_table<Key,Item,Hash,S> const& other)
    :hash_table(capacity)
    {
        other.visit([&](size_t, Item const& item) {
            insert(Key(item));
            return true;
        });
    }

    template <bool S>
    hash_table(hash_table<Key,Item,Hash,S> const& other)
    :hash_table(other.capacity(), other)
    { }

    size_t table_size() const pure              {return _array.size();}

    /// The maximum number of items the hash table can store.
    size_t capacity() const pure                {return _capacity;}

    /// The current number of items.
    size_t count() const pure                   {return _array.count();}

    /// Looks up an Item by its Key; returns a pointer to it if found, else nullptr.
    /// (The item cannot be modified: that might alter its hash.)
    Item const* get(Key const& key) const       {return search(key, Hash{}(key)).first;}
    Item const* operator[] (Key const& key) const {return get(key);}

    /// Adds a new item with the given key and returns a pointer;
    /// if one already exists, returns nullptr.
    Item* insert(Key const& key) {
        if (count() >= capacity())
            grow();
        auto [item, i] = search(key, Hash{}(key));
        if (item)
            return nullptr;
        return _array.insert(i, Item(key));
    }

    /// Returns the existing item with the given key; if there is none, it inserts a new one.
    Item* put(Key const& key) {
        if (count() >= capacity())
            grow();
        auto [item, i] = search(key, Hash{}(key));
        if (item)
            return const_cast<Item*>(item);
        return _array.insert(i, Item(key));
    }

    void grow() {
        hash_table bigger(capacity() * 2, *this);
        std::swap(*this, bigger);
    }

    /// Calls the function `v` for every item. Its signature should be `(unsigned,T const*)->bool`.
    template <typename Visitor>
    bool visit(Visitor &&v) const               {return _array.visit(std::forward<Visitor>(v));}

    using ArrayClass = sparse_array<Item,HashBucketSize,Sparse>;
    std::vector<typename ArrayClass::Bucket> const& buckets() const  {return _array.buckets();}

    // just for inspecting/tuning
    size_t probe_count(Key const& key) const {
        size_t mask = _array.size() - 1;
        size_t i = Hash{}(key) & mask;
        size_t probe = 0;
        while (true) {
            if (auto item = _array[i]; !item || *item == key)
                break;
            i = (i + (++probe)) & mask;     // quadratic (triangle-number) probing
            assert(probe < mask);
        }
        return probe + 1;
    }

private:
    static constexpr float kMaxLoad = 0.5;

    size_t arraySizeForCapacity(size_t capacity) {
        auto targetSize = size_t(ceil(capacity / kMaxLoad));
        size_t size = HashBucketSize;
        while (size < targetSize)
            size *= 2;
        return size;
    }

    std::pair<Item const*,size_t> search(Key const& key, uint32_t hashCode) const {
        size_t mask = _array.size() - 1;
        size_t i = hashCode & mask;
        size_t probe = 0;
        while (true) {
            if (auto item = _array[i]; !item || *item == key)
                return {item, i};
            i = (i + (++probe)) & mask;     // quadratic (triangle-number) probing
            assert(probe < mask);
        }
    }

    ArrayClass  _array;
    size_t      _count;
    size_t      _capacity;
};


template <typename Key, typename Item, typename Hash>
using sparse_hash_table = hash_table<Key,Item,Hash,true>;

template <typename Key, typename Item, typename Hash>
using dense_hash_table = hash_table<Key,Item,Hash,false>;

}
