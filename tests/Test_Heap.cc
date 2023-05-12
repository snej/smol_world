//
// HeapTests.cc
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

#include "smol_world.hh"
#include "catch.hpp"
#include <iomanip>
#include <iostream>

using namespace std;
using namespace snej::smol;


TEST_CASE("BlockHeader", "[heap]") {
    static constexpr heapsize kSizes[] = {0, 1, 2, 3, 4, 5,
        123, 9855,
        BlockHeader::MedSize - 2, BlockHeader::MedSize - 1, BlockHeader::MedSize,
        BlockHeader::MedSize + 1, BlockHeader::MedSize + 2,
        BlockHeader::LargeSize - 2, BlockHeader::LargeSize - 1, BlockHeader::LargeSize,
        BlockHeader::LargeSize + 1, BlockHeader::LargeSize + 2,
        BlockHeader::HugeSize - 2, BlockHeader::HugeSize - 1, BlockHeader::HugeSize,
        BlockHeader::HugeSize + 1, BlockHeader::HugeSize + 2,
        BlockHeader::MaxSize - 2, BlockHeader::MaxSize - 1, BlockHeader::MaxSize
    };
    for (heapsize blockSize : kSizes) {
        BlockHeader header;
        slice<byte> hdr = header.init(blockSize);
        CHECK(hdr.end() == (void*)(&header + 1));
        cout << blockSize << ":";
        for (byte b : hdr)
            printf(" %02X", uint8_t(b));
        cout << endl;
        CHECK(hdr.size() > 0);
        CHECK(header.dataSize() == blockSize);
        CHECK(header.headerSize() == hdr.size());
        CHECK(!header.isForwarded());
    }
}


TEST_CASE("Empty Heap", "[heap]") {
    bool iterable = GENERATE(true, false);
    Heap heap(10000);
    if (iterable)
        heap.makeIterable();
    // attributes:
    CHECK(!heap.invalid());
    CHECK(heap.validate());
    CHECK(!heap.invalid());
    CHECK(heap.base() != nullptr);
    CHECK(heap.capacity() == 10000);
    CHECK(heap.used() == Heap::Overhead);
    CHECK(heap.available() == 10000 - Heap::Overhead);

    CHECK(!heap.contains(nullptr));

    // roots:
    CHECK(heap.root() == nullvalue);

    // current heap:
    CHECK(Heap::maybeCurrent() == nullptr);
    {
        UsingHeap u(heap);
        CHECK(Heap::current() == &heap);
        CHECK(Heap::maybeCurrent() == &heap);
    }
    CHECK(Heap::maybeCurrent() == nullptr);

    // visit:
    if (iterable)
        heap.visitBlocks([&](const Block&, Type) { FAIL("Visitor should not be called"); return false; });
}


TEST_CASE("Alloc", "[heap]") {
    bool iterable = GENERATE(true, false);
    Heap heap(10000);
    if (iterable)
        heap.makeIterable();
    static constexpr size_t size1 = 123;
    auto ptr = (byte*)heap.alloc(size1);
    cout << "ptr = " << (void*)ptr << endl;
    REQUIRE(ptr != nullptr);
    CHECK(heap.contains(ptr));
    CHECK(heap.contains(ptr + size1 - 1));
    CHECK(!heap.contains(ptr + size1));

    CHECK(heap.used() == Heap::Overhead + 1 + iterable + size1);
    CHECK(heap.available() == 10000 - heap.used());
    CHECK(heap.validate());

    if (iterable) {
        int i = 0;
        heap.visitAll([&](const Block &block, Type) {
            CHECK(heap.contains(&block));
            switch (i++) {
                case 0: CHECK(block.sizeInBytes() == 123); return true;
                default: FAIL("Invalid object visited"); return false;
            }
        });
        CHECK(i == 1);
    }

    const heapsize size2 = 9857 - 2*iterable;
    auto ptr2 = (byte*)heap.alloc(size2); // exactly fills the heap
    cout << "ptr2= " << (void*)ptr2 << endl;
    REQUIRE(ptr2 != nullptr);
    CHECK(heap.contains(ptr2));
    CHECK(heap.contains(ptr2 + size2 - 1));
    CHECK(!heap.contains(ptr2 + size2));

    CHECK(heap.used() == 10000);
    CHECK(heap.available() == 0);
    CHECK(heap.validate());

    if (iterable) {
        int i = 0;
        heap.visitAll([&](const Block &block, Type type) {
            CHECK(heap.contains(&block));
            CHECK(type == Type::Blob);
            switch (i++) {
                case 0: CHECK(block.sizeInBytes() == size1); return true;
                case 1: CHECK(block.sizeInBytes() == size2); return true;
                default: FAIL("Invalid object visited"); return false;
            }
        });
        CHECK(i == 2);
    }

    CHECK(heap.alloc(1) == nullptr);
}


static void testAllocRangeOfSizes(heapsize BaseSize, int NumBlocks) {
    bool iterable = GENERATE(true, false);
    Heap heap(Heap::Overhead + NumBlocks * (5 + BaseSize) + (NumBlocks * (NumBlocks - 1)) / 2);
    if (iterable)
        heap.makeIterable();
    cerr << "Allocating block sizes " << BaseSize << "..." << (BaseSize + NumBlocks - 1) << "; Heap size is " << heap.capacity() << endl;
    auto usedBefore = heap.used();

    vector<Block*> blocks(NumBlocks);
    size_t dataSize = 0;
    for (int i = 0; i < NumBlocks; ++i) {
        heapsize size = BaseSize + i;
        INFO("Block size " << size);
        Block *b = blocks[i] = heap.allocBlock(size, Type::Blob);
        //cerr << "Block " << size << " = " << (void*)blob << endl;
        REQUIRE(b != nullptr);
        CHECK(heap.contains(b));
        CHECK(b->dataSize() == size);
        memset(b->data().begin(), uint8_t(i), size);
        CHECK(b->dataSize() == size);
        if (i > 0) {
            auto prev = blocks[i - 1];
            CHECK(prev->dataSize() == size - 1);
        }
        dataSize += size;
        CHECK(heap.validate());
    }
    cerr << "Allocated " << heap.used() << " bytes in " << NumBlocks << " blocks; overhead of "
         << (double(heap.used() - usedBefore - dataSize) / NumBlocks) << " bytes/block\n";

    for (int i = 0; i < NumBlocks; ++i) {
        size_t size = BaseSize + i;
        INFO("Block #" << i);
        Block* b = blocks[i];
        auto data = (uint8_t*)b->data().begin();
        REQUIRE(heap.contains(data));
        for (int j = 0; j < size; j++) {
            if (data[j] != uint8_t(i)) {
                FAIL(" byte " << j << " is " << data[j] << ", expected " << uint8_t(i));
            }
        }
    }

    if (iterable) {
        int i = 0;
        heap.visitAll([&](Block const& b, Type type) {
            INFO("Block #" << i);
            CHECK(type == Type::Blob);
            REQUIRE(i < NumBlocks);
            CHECK(&b == blocks[i]);
            ++i;
            return true;
        });
        CHECK(i == NumBlocks);
    }
}


TEST_CASE("Alloc Small Objects", "[heap]")      {testAllocRangeOfSizes(0,     500);}
TEST_CASE("Alloc Bigger Objects", "[heap]")     {testAllocRangeOfSizes(900,   500);}
TEST_CASE("Alloc Big Objects", "[heap]")        {testAllocRangeOfSizes(BlockHeader::LargeSize - 50, 100);}
TEST_CASE("Alloc Real Big Objects", "[heap]")   {testAllocRangeOfSizes(99990,  20);}
TEST_CASE("Alloc Huge Objects", "[heap]")       {testAllocRangeOfSizes(Block::MaxSize - 2,  2);}
