//
// InPlaceHeap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Bitmap.hh"
#include <vector>

namespace snej::smol {

/*
 - If a block's address is 4-byte-aligned, it's part of a Chonk.
 - This implies that non-chonk allocations have to skip a byte to avoid 4-byte alignment.
 - To find the Chonk header, clear the last XX bits of the block address.
 - This implies Chonks are 2^XX-byte aligned.

 Chonk header:
    - Block size
    - Mark bits
    - Forwarded bits
 */


class Chonk {
public:
    static constexpr size_t Size = 256;
    static constexpr size_t Mask = ~(Size - 1);
    static constexpr heapsize MinBlockSize = 4;

    static bool isChonkAligned(const void* addr) {
        return (intptr_t(addr) & (MinBlockSize-1)) == 0;
    }

    static Chonk* chonkContaining(const void* addr) {
        return (Chonk*)(uintptr_t(addr) & Mask);
    }

    static inline heapsize sizeOfBlock(const void* block) {
        return chonkContaining(block)->blockSize();
    }

    Chonk(heapsize blockSize)
    :_blockSize(blockSize)
    {
        assert(blockSize >= MinBlockSize && blockSize % MinBlockSize == 0);
        assert(this == chonkContaining(this)); // Chonk must be page-aligned
        reset();
    }

    void reset() {
        if (_blockSize > MinBlockSize) {
            _markBits.setAll(true);
            // Clear only the bits corresponding to the starts of blocks:
            unsigned mult = _blockSize / MinBlockSize;
            for (heapsize i = 0; i < MaxBlockCount; i += mult) {
                _markBits.remove(i);
            }
        } else {
            _markBits.clear();
        }
    }

    heapsize blockSize() const pure         {return _blockSize;}

    unsigned capacity() const pure          {return sizeof(_data) / _blockSize;}

    void* alloc() {
        auto i = blockIndex(_markBits.first<false>());
        if (_unlikely(i >= MaxBlockCount))
            return nullptr;
        _markBits.insert(i);
        return atIndex(i);
    }

    void beginMarkPhase()               {reset();}
    bool isMarked(void *block) const    {return _markBits[indexOf(block)];}
    void mark(void *block)              {_markBits.insert(indexOf(block));}
    void endMarkPhase()                 { }

protected:
    using blockIndex = unsigned;
    static constexpr blockIndex NoOffset = 0xFFFF;

    bool validIndex(auto i) const pure {return i >= 0 && i < sizeof(_data) / _blockSize;}

    blockIndex indexOf(const void *addr) const pure {
        auto off = intptr_t(addr) - intptr_t(&_data[0]);
        assert(off % _blockSize == 0);
        auto i = off / _blockSize;
        assert(validIndex(i));
        return blockIndex(i);
    }

    void* atIndex(blockIndex i) {
        assert(validIndex(i));
        return &_data[i * _blockSize];
    }

    byte* begin()   {return &_data[0];}
    byte* end()     {return &_data[sizeof(_data)];}

    uint16_t        _blockSize;                     // Size of blocks in this Chonk (0=variable)
    uint16_t     _filler;
    heappos         _nextChonk;                     // Heap offset of next Chonk of this size

    static constexpr heapsize HeaderSize = sizeof(_nextChonk) + sizeof(_filler) + sizeof(_blockSize);
    static constexpr heapsize MaxBlockCount = (Size - HeaderSize) / MinBlockSize;

    bitmap<MaxBlockCount> _markBits;
    byte                  _data[Size - HeaderSize - sizeof(_markBits)];
};

static_assert(sizeof(Chonk) == Chonk::Size);



#if 0
    class InPlaceHeap {
    public:
        InPlaceHeap(void* addr, size_t size) {
            assert( (uintptr_t(addr) & (Chonk::Size - 1)) == 0);
            size &= (Chonk::Size - 1);
            _begin = (Chonk*)addr;
            _next = _begin;
            _end = (Chonk*)((byte*)addr + size);
        }

        void* alloc(heapsize size) {
            if (size >= kMinSize && size < kVariableSizeThreshold) {
                auto sizeClass = (size - kMinSize) / kSizeClassIncrement;
                UniformChonk* chonk = _chonksBySize[sizeClass];

                void *result = nullptr;
                if (chonk)
                    result = chonk->alloc();
                if (!result) {
                    if (auto next = allocChonk<UniformChonk>(sizeOfClass(sizeClass)))
                        chonk = _chonksBySize[sizeClass] = next;
                    else
                        return nullptr;
                    result = chonk->alloc();
                }
                return result;

            } else {
                assert(size < 0x1000);
                void *result = nullptr;
                if (_variableChonk)
                    result = _variableChonk->alloc(size);
                if (!result) {
                    if (auto next = allocChonk<VariableChonk>(0))
                        _variableChonk = next;
                    else
                        return nullptr;
                    result = _variableChonk->alloc(size);
                }
                return result;
            }
        }

        Chonk* chonkContaining(void *ptr) {
            assert(ptr > _begin && ptr < _next);
            return Chonk::chonkContaining(ptr);
        }

        void free(void *ptr) {
            if (_unlikely(ptr == nullptr))
                return;
            chonkContaining(ptr)->free(ptr);
        }

        void mark(void *ptr) {
            chonkContaining(ptr)->mark(ptr);
        }

        void sweep() {
            for (auto chonk = _begin; chonk < _end; ++chonk)
                chonk->sweep();
        }

    private:
        static constexpr heapsize kMinSize = 4;
        static constexpr unsigned kNumSizeClasses = 20;
        static constexpr heapsize kSizeClassIncrement = 4;

        static constexpr heapsize sizeOfClass(unsigned c) {return kMinSize + c * kSizeClassIncrement;}

        static constexpr size_t kVariableSizeThreshold = kMinSize + kNumSizeClasses * kSizeClassIncrement;


        template <class CHONK>
        CHONK* allocChonk(heapsize blockSize) {
            if (_unlikely(_next >= _end))
                return nullptr;
            return new (_next++) CHONK(blockSize);
        }

        Chonk*  _begin;     // First Chonk
        Chonk*  _next;      // Next available Chonk
        Chonk*  _end;       // End of Chonks (one past the last)
        UniformChonk*  _chonksBySize[kNumSizeClasses] = {};
        VariableChonk*  _variableChonk = nullptr;
    };
#endif


}
