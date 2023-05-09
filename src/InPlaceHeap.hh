//
// InPlaceHeap.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Base.hh"
#include <vector>

namespace snej::smol {


    class Chonk {
    public:
        static constexpr size_t Size = 4096;
        static constexpr size_t Mask = ~(Size - 1);
        static constexpr size_t MinBlockSize = 4;

        static Chonk* chonkContaining(const void* addr) {
            return (Chonk*)(uintptr_t(addr) & Mask);
        }

        static inline heapsize sizeOfBlock(const void* block);

        heapsize blockSize() {return _blockSize;}

        inline void free(void *ptr);
        inline void mark(void *ptr);
        inline bool isMarked(void *ptr) const;
        inline void sweep();
        inline void reset();

    protected:
        using chonkOffset = uint16_t;
        static constexpr chonkOffset NoOffset = 0xFFFF;

        Chonk(heapsize blockSize)
        :_blockSize(blockSize)
        {
            assert(this == chonkContaining(this)); // Chonk must be page-aligned
        }

        bool validOffset(size_t offset) const {return offset < sizeof(_data);}

        chonkOffset offsetOf(const void *addr) const {
            auto off = size_t((byte const*)addr - &_data[0]);
            assert(validOffset(off));
            return chonkOffset(off);
        }

        void* atOffset(size_t offset)   {
            assert(validOffset(offset));
            return &_data[offset];
        }

        byte* begin()   {return &_data[0];}
        byte* end()     {return &_data[sizeof(_data)];}

        struct FreeBlock {
            chonkOffset next;
        };

        FreeBlock* firstFree()  {
            if (_unlikely(_firstFree == NoOffset))
                return nullptr;
            return (FreeBlock*)atOffset(_firstFree);
        }

        // TODO: Should use smaller bitmaps with bigger blockSizes
        static constexpr unsigned kSizeOfMarkBits = Size / 8 / MinBlockSize;

        uint16_t        _blockSize;                     // Size of blocks in this Chonk (0=variable)
        chonkOffset     _firstFree = 0;                 // Byte offset of first free block
        byte            _data[Size - sizeof(_blockSize) - sizeof(_firstFree) - kSizeOfMarkBits];
        uint8_t         _markBits[kSizeOfMarkBits];
    };

    static_assert(sizeof(Chonk) == Chonk::Size);



    /// A Chonk in which all blocks are the same size. No block header is needed.
    class UniformChonk : public Chonk {
    public:
        UniformChonk(heapsize blockSize)
        :Chonk(blockSize)
        {
            assert(blockSize >= 2);
            reset();
        }

        unsigned capacity() const {
            return sizeof(_data) / _blockSize;
        }

        void* alloc() {
            FreeBlock* result = firstFree();
            if (_likely(result != nullptr))
                _firstFree = result->next;
            return result; // uniform blocks have no header; just return direct ptr
        }

        void free(void *ptr) {
            if (_unlikely(ptr == nullptr))
                return;
            auto off = offsetOf(ptr);
            ((FreeBlock*)ptr)->next = _firstFree;
            _firstFree = off;
        }

        void mark(void *ptr) {
            auto off = offsetOf(ptr);
            assert(off % MinBlockSize == 0);
            auto bitNumber = off / MinBlockSize;
            _markBits[bitNumber / 8] |= (1 << (bitNumber % 8));
        }

        bool isMarked(void *ptr) const {
            auto off = offsetOf(ptr);
            assert(off % MinBlockSize == 0);
            auto bitNumber = off / MinBlockSize;
            return (_markBits[bitNumber / 8] & (bitNumber % 8)) != 0;
        }

        void sweep() {
            chonkOffset *last = &_firstFree;
            byte *end = this->end() + 1 - _blockSize;
            for (byte *block = begin(); block < end; block += _blockSize) {
                if (!isMarked(block)) {
                    *last = offsetOf(block);
                    last = &((FreeBlock*)block)->next;
                }
            }
            *last = NoOffset;
            memset(&_markBits, 0, sizeof(_markBits));
        }

        void reset() {
            byte *block = begin();
            _firstFree = offsetOf(block);
            auto lastBlock = block + (capacity() - 1) * _blockSize;
            for( ; block < lastBlock; block += _blockSize)
                ((FreeBlock*)block)->next = offsetOf(block + _blockSize);
            ((FreeBlock*)lastBlock)->next = NoOffset;

            memset(&_markBits, 0, sizeof(_markBits));
        }
    };


    /// A Chonk in which blocks can be different sizes. Each block has a 2-byte header.
    class VariableChonk : public Chonk {
    public:
        VariableChonk(heapsize)
        :Chonk(0) { }

        heapsize sizeOfBlock(const void* ptr) {
            return recoverBlock(ptr)->size;
        }

        void* alloc(heapsize size) {
            // VariableChonk uses _next as a simple bump allocator.
            if(size > 0xFFFF)
                return nullptr;
            heapsize nextFree = _firstFree + sizeof(ActiveBlock) + size;
            if (nextFree > sizeof(_data))
                return nullptr;
            auto block = (ActiveBlock*)atOffset(_firstFree);
            _firstFree = nextFree;
            block->size = size;
            block->marked = false;
            block->free = false;
            return block->payload;
        }

        void free(void */*ptr*/) {
            // TODO!
        }

        void mark(void *ptr) {
            recoverBlock(ptr)->marked = true;
        }

        bool isMarked(void *ptr) const {
            return recoverBlock(ptr)->marked;
        }

        void reset() {
            _firstFree = 0;
        }

        bool empty() const {
            return _firstFree == 0;
        }

    protected:
        struct ActiveBlock {
            uint16_t    size   :14;
            bool        free   : 1;
            bool        marked : 1;
            byte        payload[0];
        };

        ActiveBlock* recoverBlock(void *ptr) {
            return (ActiveBlock*)((byte*)ptr - offsetof(ActiveBlock, payload));
        }

        ActiveBlock const* recoverBlock(const void *ptr) const {
            return const_cast<VariableChonk*>(this)->recoverBlock((void*)ptr);
        }
    };



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



    heapsize Chonk::sizeOfBlock(const void* block) {
        auto chonk = chonkContaining(block);
        heapsize size = chonk->blockSize();
        if (size == 0)
            size = ((VariableChonk&)chonk).sizeOfBlock(block);
        return size;
    }

    void Chonk::free(void *ptr) {
        if (_blockSize != 0) ((UniformChonk*)this)->free(ptr);
        else ((VariableChonk*)this)->free(ptr);
    }
    void Chonk::mark(void *ptr) {
        if (_blockSize != 0) ((UniformChonk*)this)->mark(ptr);
        else ((VariableChonk*)this)->mark(ptr);
    }
    bool Chonk::isMarked(void *ptr) const {
        return (_blockSize != 0) ? ((UniformChonk*)this)->isMarked(ptr)
                                 : ((VariableChonk*)this)->isMarked(ptr);
    }
    void Chonk::sweep() {
        if (_blockSize != 0) ((UniformChonk*)this)->sweep();
        else ((VariableChonk*)this)->sweep();
    }
    void Chonk::reset() {
        if (_blockSize != 0) ((UniformChonk*)this)->reset();
        else ((VariableChonk*)this)->reset();
    }

}
