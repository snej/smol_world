//
// Block.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.hh"
#include "slice.hh"
#include "Val.hh"
#include <algorithm>

namespace snej::smol {

/// A heap block; always created inside a Heap.
/// Contains the metadata that gives its data size in bytes, its Type, and a few flags for GC.
class Block {
public:
    //---- Allocation:

    static heapsize sizeForData(heapsize dataSize) {
        assert(dataSize <= MaxSize);
        heapsize blockSize = sizeof(Block) + dataSize;
        if (blockSize < 4)
            blockSize = 4;  // Block must allocate at least enough space to store forwarding pos
        else if (dataSize >= LargeSize)
            blockSize += 2;      // Add room for 32-bit dataSize
        return blockSize;
    }

    static void* operator new(size_t, void *addr) pure {return addr;} // "placement" operator new

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

    //---- Data & size:

    static constexpr heapsize TagBits = 6;
    static constexpr heapsize MaxSize = (1 << (32 - TagBits)) - 1;
    static constexpr heapsize LargeSize = 1 << (16 - TagBits);

    heapsize blockSize() const                  {return ((_tags & Large) ? 4 : 2) + dataSize();}

    /// Returns both the data pointer and size; slightly more efficient.
    slice<byte> data() const pure {
        assert(!isForwarded());
        if (uint32_t meta = bigMeta(); meta & Large)
            return {(byte*)this + 4, meta >> TagBits};
        else
            return {(byte*)this + 2, (meta & 0xFFFF) >> TagBits};
    }

    /// A pointer to the block's data, just past its header.
    void* dataPtr() pure                        {return (byte*)this + ((_tags & Large) ? 4 : 2);}
    const void* dataPtr() const pure            {return const_cast<Block*>(this)->dataPtr();}

    /// The exact size of the block's data.
    heapsize dataSize() const pure {
        assert(!isForwarded());
        uint32_t meta = bigMeta();
        if (!(meta & Large))
            meta &= 0xFFFF;
        return meta >> TagBits;
    }

    /// Recovers the Block object given the data range it owns.
    static Block* fromData(slice<byte> data) pure {
        return (data.size()<LargeSize) ? fromSmallData(data.begin()) : fromLargeData(data.begin());
    }
    static Block* fromSmallData(void* data) pure {assert(data); return (Block*)((byte*)data - 2);}
    static Block* fromLargeData(void* data) pure {assert(data); return (Block*)((byte*)data - 4);}

    void fill(slice<byte> contents) {
        auto bytes = this->data();
        assert(contents.size() <= bytes.size());
        if (contents)
            ::memcpy(bytes.begin(), contents.begin(), contents.size());
        ::memset(bytes.begin() + contents.size(), 0, bytes.size() - contents.size());
    }

    static constexpr bool typeContainsVals(Type type) {
        return type >= Type::Array && type <= Type::Dict;
    }

    bool containsVals() const pure              {return typeContainsVals(type());}
    
    slice<Val> vals() const pure {
        return containsVals() ? slice_cast<Val>(data()) : slice<Val>();
    }

    //---- Data type:

    Type type() const pure                      {assert(!isForwarded());
                                                 return Type((_tags & TypeMask) >> 1);}

    //---- Stuff used by Heap and GC:

    /// Points to the next Block in the Heap (or else to the Heap's '_cur' pointer.)
    Block* nextBlock() pure {
        auto dat = data();
        return (Block*)( dat.begin() + std::max(dat.size(), heapsize(2)) );
    }

    bool isVisited() const pure                 {return (_tags & Visited) != 0;}
    void setVisited()                           {_tags |= Visited;}
    void clearVisited()                         {_tags &= ~Visited;}

    bool isForwarded() const pure               {return (_tags & Fwd) != 0;}
    heappos forwardingAddress() const pure      {assert(isForwarded());
                                                 return heappos(bigMeta() >> 1);}
    void setForwardingAddress(heappos addr) {
        assert(addr > 0 && !(uintpos(addr) & 0x80000000));
        bigMeta() = (uintpos(addr) << 1) | Fwd;
    }

private:
    friend class Heap;
    friend class GarbageCollector;

    // Tag bits stored in an Block's meta word, alongsize its size.
    enum Tags : uint8_t {
        Fwd          = 0b00000001,    // If set, all 31 remaining bits are the forwarding address
        TypeMask     = 0b00001110,          // type tags; encodes Type values 0..7
        Large        = 0b00010000,          // If set, size is 32-bit not 16-bit
        Visited      = 0b00100000,          // Marker used by Heap::visit()
        TagsMask     = (1 << TagBits) - 1,  // all tags
    };

    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << 1);}
    Tags tags() const pure                      {return Tags(_tags & TagsMask);}

    uint32_t& bigMeta() const pure              {return *(uint32_t*)this;}

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

}
