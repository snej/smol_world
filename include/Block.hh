//
// Block.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "slice.hh"
#include "Val.hh"


/// A heap block; always created inside a Heap.
/// Contains the metadata that gives its data size in bytes, its Type, and a few flags for GC.
class Block {
public:

    //---- Allocation:

    static Block* alloc(size_t dataSize, Type type, IN_MUT_HEAP) {
        assert(dataSize <= MaxSize);
        auto blockSize = sizeof(Block) + dataSize;
        if (blockSize < 4)
            blockSize = 4;  // Block must allocate at least enough space to store forwarding pos
        else if (dataSize >= LargeSize)
            blockSize += 2;      // Add room for 32-bit dataSize
        void *addr = heap->rawAlloc(heapsize(blockSize));
        if (!addr)
            return nullptr;
        return new (addr) Block(heapsize(dataSize), type);
    }

    //---- Data & size:

    static constexpr heapsize TagBits = 6;
    static constexpr heapsize MaxSize = (1 << (32 - TagBits)) - 1;
    static constexpr heapsize LargeSize = 1 << (16 - TagBits);

    /// Returns both the data pointer and size; slightly more efficient.
    template <typename T = byte>
    slice<T> data() const {
        assert(!isForwarded());
        if (uint32_t meta = bigMeta(); meta & Large)
            return {(T*)((byte*)this + 4), uint32_t((meta >> TagBits) / sizeof(T))};
        else
            return {(T*)((byte*)this + 2), uint32_t(((meta &= 0xFFFF) >> TagBits) / sizeof(T))};
    }

    /// A pointer to the block's data, just past its header.
    void* dataPtr()                             {return (byte*)this + ((_tags & Large) ? 4 : 2);}
    const void* dataPtr() const                 {return const_cast<Block*>(this)->dataPtr();}

    /// The exact size of the block's data.
    heapsize dataSize() const {
        assert(!isForwarded());
        uint32_t meta = bigMeta();
        if (!(meta & Large))
            meta &= 0xFFFF;
        return meta >> TagBits;
    }

    /// Recovers the Block object given the data range it owns.
    static Block* fromData(slice<byte> data) {
        return (data.size()<LargeSize) ? fromSmallData(data.begin()) : fromLargeData(data.begin());
    }
    static Block* fromSmallData(void* data)     {return (Block*)( (byte*)data - 2);}
    static Block* fromLargeData(void* data)     {return (Block*)( (byte*)data - 4);}

    //---- Data type:

    Type type() const                           {assert(!isForwarded());
                                                 return Type((_tags & TypeMask) >> 1);}

    //---- Stuff used by Heap and GC:

    /// Points to the next Block in the Heap (or to the Heap's 'cur' pointer.)
    Block* nextBlock() {
        auto dat = data();
        return (Block*)( dat.begin() + std::max(dat.size(), heapsize(2)) );
    }

    static constexpr bool typeContainsPointers(Type type) {
        return type >= Type::Array && type <= Type::Dict;
    }

    bool isVisited() const                      {return (_tags & Visited) != 0;}
    void setVisited()                           {_tags |= Visited;}

    void clearVisited()                         {_tags &= ~Visited;}
    bool isForwarded() const                    {return (_tags & Fwd) != 0;}

    heappos getForwardingAddress() const        {return heappos(isForwarded() ? (bigMeta() >> 1) : 0);}

    void setForwardingAddress(heappos addr) {
        assert(addr > 0 && !(uintpos(addr) & Fwd));
        bigMeta() = (uintpos(addr) << 1) | Fwd;
    }

private:
    friend class Heap;
    friend class GarbageCollector;

    // Tag bits stored in an Block's meta word, alongsize its size.
    enum Tags : uint8_t {
        Fwd          = 0b000001,    // If set, all 31 remaining bits are the forwarding address

        Large        = 0b010000,    // If set, size is 32-bit not 16-bit
        Visited      = 0b100000,    // Marker used by Heap::visit()

        // scalars:
        TypeBigNumber= uint8_t(Type::BigNumber) << 1,
        TypeString   = uint8_t(Type::String) << 1,
        TypeSymbol   = uint8_t(Type::Symbol) << 1,
        TypeBlob     = uint8_t(Type::Blob) << 1,
        // contain pointers:
        TypeArray    = uint8_t(Type::Array) << 1,
        TypeDict     = uint8_t(Type::Dict) << 1,
        Type_spare1  = uint8_t(Type::_spare1) << 1,
        Type_spare2  = uint8_t(Type::_spare2) << 1,

        TypeMask     = 0b001110,            // type tags
        TagsMask     = (1 << TagBits) - 1,  // all tags
    };

    static void* operator new(size_t size, void *addr) {return addr;} // "placement" operator new

    Block(heapsize dataSize, Type type) {
        assert(dataSize <= MaxSize);
        uint32_t meta = (dataSize << TagBits) | typeTag(type);
        if (meta <= 0xFFFF) {
            _meta = meta;
        } else {
            meta |= Large;
            bigMeta() = meta;
        }
    }

    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << 1);}
    Tags tags() const                           {return Tags(_tags & TagsMask);}
    uint32_t& bigMeta() const                   {return *(uint32_t*)this;}

    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    // Finally the instance data! (little-endian CPU assumed)
    union {
        uint8_t  _tags;
        uint16_t _meta;
    };
} __attribute__((aligned (1))) __attribute__((packed));

static_assert(sizeof(Block) == 2);
static_assert(alignof(Block) == 1);
