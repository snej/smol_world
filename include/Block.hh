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
#include <string_view>

namespace snej::smol {

/// A heap block; always created inside a Heap.
/// The smol pointers in Vals/Values point to Blocks.
/// Contains the metadata that gives its data size in bytes, its Type, and a few flags for GC.
class Block {
public:
    //---- Allocation:

    static heapsize sizeForData(heapsize dataSize) {
        assert(dataSize <= MaxSize);
        heapsize blockSize = std::max(heapsize(sizeof(Block) + dataSize), kMinBlockSize);
        if (dataSize >= LargeSize)
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

    static constexpr heapsize TagBits = 7;                          //   Number of bits of tags
    static constexpr heapsize MaxSize = (1 << (32 - TagBits)) - 1;  ///< Maximum block size in bytes
    static constexpr heapsize LargeSize = 1 << (16 - TagBits);      //   Size that needs more header

    heapsize blockSize() const                  {
        return std::max(((_tags & Large) ? 4 : 2) + dataSize(), kMinBlockSize);
    }

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

    bool containsVals() const pure              {return TypeIs(type(), TypeSet::Container);}
    
    slice<Val> vals() const pure {
        return containsVals() ? slice_cast<Val>(data()) : slice<Val>();
    }

    void fill(slice<Val> contents) {
        assert(containsVals());
        auto vals = slice_cast<Val>(data());
        assert(contents.size() <= vals.size());
        if (contents) {
            Val *dst = vals.begin();
            for (Val &src : contents)
                *dst++ = src;
        }
        ::memset(vals.begin() + contents.size(), 0, (vals.size() - contents.size()) * sizeof(Val));
    }

    //---- Data type:

    Type type() const pure                      {assert(!isForwarded());
                                                 return Type((_tags & TypeMask) >> TypeShift);}

    //---- Stuff used by Heap and GC:

    /// Points to the next Block in the Heap (or else to the Heap's '_cur' pointer.)
    Block* nextBlock() pure {
        auto dat = data();
        return (Block*)( dat.begin() + std::max(dat.size(), heapsize(2)) );
    }

    Block const* nextBlock() const pure         {return const_cast<Block*>(this)->nextBlock();}

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

    const char* validate() const {
        if (isForwarded()) return "a block is forwarded";
        //if (isVisited()) return "a block's temporary 'visited' flag is set";
        auto size = dataSize();
        if (size < LargeSize && (_tags & Large))
            return "a small block's 'large' flag is set unnecessarily";
        switch (Type t = type()) {
            case Type::BigInt:
                if (size < 1 || size > 8) return "a BigInt has an invalid size";
                break;
            case Type::Float:
                if (size != 4 && size != 8) return "A Float has an invalid size";
                break;
            case Type::Array:
                if (size & 0x3) return "An Array has an invalid size";
                break;
            case Type::Vector:
                if ((size & 0x3) || size == 0) return "A Vector has an invalid size";
                break;
            case Type::Dict:
                if (size & 0x7) return "A Dict has an invalid size";
                break;
            case Type::String:
            case Type::Symbol:
            case Type::Blob:
                break;
            default:
                if (TypeIs(t, TypeSet::Valid))
                    return "a block has a non-object type";
                else
                    return "a block has an invalid type";
        }
        return nullptr;
    }

private:
    friend class Heap;
    friend class GarbageCollector;

    static constexpr heapsize kMinBlockSize = sizeof(heappos); // Block must be able to store fwd ptr

    // Tag bits stored in an Block's meta word, alongsize its size.
    enum Tags : uint8_t {
        Fwd          = 0b00000001,    // If set, all 31 remaining bits are the forwarding address
        Large        = 0b00000010,          // If set, size is 32-bit not 16-bit
        Visited      = 0b00000100,          // Marker used by Heap::visit()
        TypeMask     = 0b01111000,          // type tags; encodes Type values 0..15
        TagsMask     = (1 << TagBits) - 1,  // all tags
    };

    static constexpr heapsize TypeShift = 3; // How far over the TypeMask is

    static constexpr Tags typeTag(Type t)       {return Tags(uint8_t(t) << TypeShift);}
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
