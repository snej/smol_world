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

/*  BLOCK HEADER:
    A block's header occupies 1-3 bytes before the start of the Block.
    The byte immediately before the block contains the flags that indicate the size.

                  0xxxxxxx |            = Small: the x bits are the size in bytes, 0-127
         <1 byte> 100xxxxx |            = Medium: size is xxxxxyyyyyyyy, up to 8192 (2^13)
        <2 bytes> 101xxxxx |            = Large: size is 8200 - 2MB (2^21)
        <3 bytes> 110xxxxx |            = Huge; size up to 128MB (2^29)

    If the flag bits are 111, the block has been forwarded, moved to another heap,
    and the rest of the byte plus the first 3 bytes of the block give the address:

            111xxxxx | <3 bytes>    = Forward -- xxxxx + next 3 bytes are 29-bit address

    This implies every block has to occupy at least 4 bytes (3 bytes data), to leave room.
    This is not reflected in the header! So a 2-byte block has a size 2 in its header, but
    occupies 3 bytes (plus the 1-byte header.)

    NOTE: The Heap class may add a 'hint' byte just before the header, to enable it to iterate
          its blocks.
 */

/// A heap block header; always created inside a Heap.
/// Variable size, from 1 to 4 bytes depending on the size of the value following.
/// For sanity's sake, `this` always points to 4 bytes before the value data.
class BlockHeader {
public:
    //---- Allocation:

    BlockHeader() = default;

    slice<byte> init(heapsize dataSize) {
        assert(dataSize <= MaxSize);
        heapsize blockSize;
        uint8_t meta;
        if (dataSize < MedSize) {
            blockSize = 1;
            meta = uint8_t(dataSize);
        } else {
            blockSize = 2;
            meta = (dataSize & ~SizeMask) | NotSmolMask;
            _ext0 = uint8_t(dataSize >> 5);
            if (dataSize >= LargeSize) {
                blockSize = 3;
                meta += NextSizeTag;
                _ext1 = uint8_t(dataSize >> 13);
                if (dataSize >= HugeSize) {
                    blockSize = 4;
                    meta += NextSizeTag;
                    _ext2 = uint8_t(dataSize >> 21);
                }
            }
        }
        _meta = meta;
        auto start = &_meta + 1 - blockSize;
        return {(byte*)start, blockSize};
    }

    //---- Data & size:

    static constexpr heapsize MedSize   = (1 << 7);      ///< Size requiring 2-byte header
    static constexpr heapsize LargeSize = (1 << 13);     ///< Size requiring 3-byte header
    static constexpr heapsize HugeSize  = (1 << 21);     ///< Size requiring 4-byte header
    static constexpr heapsize MaxSize   = (1 << 29) - 1; ///< Maximum block size in bytes

    static constexpr heappos MaxForwardingAddr { (1 << 29) - 1 };

    static heapsize sizeForDataSize(heapsize dataSize) {
        return 1 + (dataSize >= MedSize) + (dataSize >= LargeSize) + (dataSize >= HugeSize);
    }

    /// The exact size of the block's data.
    heapsize dataSize() const {
        heapsize size = 0;
        switch (_meta >> 5) {
            case 7: // forwarded!
                assert(false);
            case 6: // huge
                size  = (heapsize(_ext2) << 21);
            case 5: // large
                size += (heapsize(_ext1) << 13);
            case 4: // med
                size += (heapsize(_ext0) << 5) + (_meta & ~SizeMask);
                break;
            default: // 0..3 means smol
                size = _meta;
                break;
        }
        return size;
    }

    /// A pointer to the block's data, just past its header.
    void* dataPtr() pure                        {return &_meta + 1;}
    const void* dataPtr() const pure            {return &_meta + 1;}

    /// Recovers the Block object given the data.
    static BlockHeader& fromData(void* data) pure {
        assert(data);
        return *(BlockHeader*)((byte*)data - sizeof(BlockHeader));
    }

    static const BlockHeader& fromData(const void* data) pure {
        assert(data);
        return *(const BlockHeader*)((byte*)data - sizeof(BlockHeader));
    }

    heapsize headerSize() const {
        switch (_meta >> 5) {
            case 7:     return 0; // forwarded!
            case 6:     return 4; // huge
            case 5:     return 3; // large
            case 4:     return 2; // med
            default:    return 1; // 0..3 means smol
        }
    }

    bool isForwarded() const pure               {return (_meta & SizeMask) == Forwarded;}

    heappos forwardingAddress() const pure {
        auto bytes = &_meta;
        heapsize addr = (uintpos(bytes[0] & ~SizeMask) << 24)
                            | uintpos(bytes[1] << 16) | uintpos(bytes[2] << 8) | bytes[3];
        return heappos(addr);
    }

    void setForwardingAddress(heappos addr) {
        assert(addr > 0 && addr < MaxForwardingAddr);
        auto n = uintpos(addr);
        auto bytes = &_meta;
        bytes[0] = uint8_t(Forwarded | (n >> 24));
        bytes[1] = uint8_t(n >> 16);
        bytes[2] = uint8_t(n >> 8);
        bytes[3] = uint8_t(n);
    }

private:
    friend class Heap;
    friend class GarbageCollector;

    static constexpr heapsize kMinBlockSize = sizeof(heappos); // Block must be able to store fwd ptr

    // Tag bits stored in an Block's meta word, alongsize its size.
    enum Tags : uint8_t {
        NotSmolMask  = 0b10000000,    // High bit 0 means this byte is the value size
        SizeMask     = 0b11100000,
        NextSizeTag  = 0b00100000,    // Add this to increment size tag
        Forwarded    = 0b11100000,
    };

    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;

    uint8_t _ext3, _ext2, _ext1, _ext0;    // up to 4 bytes of size extension
    uint8_t _meta;                         // the main size and flags byte
                                           // ...the actual data value follows...
} __attribute__((aligned (1))) __attribute__((packed));

static_assert(alignof(BlockHeader) == 1);



class Block {
public:
    static constexpr heapsize MinSize   = 3;    ///< Minimum size (ensures room for fwd ptr)
    static constexpr heapsize MaxSize   = BlockHeader::MaxSize; ///< Maximum block size in bytes

    heapsize sizeInBytes() const pure           {return header().dataSize();}
    heapsize dataSize() const pure           {return header().dataSize();}// TODO: Remove
    slice<byte> data() const pure               {return {(byte*)this, sizeInBytes()};}

    void fill(slice<byte> contents) {
        auto size = this->sizeInBytes();
        assert(contents.sizeInBytes() <= size);
        if (contents)
            contents.memcpyTo((byte*)this);
        ::memset((byte*)this + contents.size(), 0, size - contents.sizeInBytes());
    }

    bool isForwarded() const pure               {return header().isForwarded();}
    heappos forwardingAddress() const pure      {return header().forwardingAddress();}
    void setForwardingAddress(heappos addr)     {header().setForwardingAddress(addr);}

    BlockHeader& header() pure                  {return BlockHeader::fromData(this);}
    BlockHeader const& header() const pure      {return BlockHeader::fromData(this);}

    std::pair<slice<byte>,heapsize> withHeader() const pure {
        BlockHeader const& hdr = header();
        auto headerSize = hdr.headerSize();
        auto begin = offsetBy(this, -int32_t(headerSize));
        auto end = offsetBy(this, std::max(hdr.dataSize(), MinSize));
        return {slice<byte>{(byte*)begin, (byte*)end}, headerSize};
    }

    heapsize sizeWithHeader() const pure {
        BlockHeader const& hdr = header();
        return hdr.headerSize() + std::max(hdr.dataSize(), MinSize);
    }

    const void* end() const {return offsetBy(this, std::max(sizeInBytes(), MinSize));}

private:
    Block() = delete;
    static void* operator new(size_t size) = delete;
    static void operator delete(void*) = delete;
} __attribute__((aligned (1)));


}
