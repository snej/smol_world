# it’s a smol world

    It's a world of laughter
    a world of song
    it's a world where pointers
    are four bytes long
    
    There's only so much cache
    we must clean up our trash
    It's a smol world after all!
    
    It's a world of strings and
    arrays for you
    dictionaries and ints
    can be found here too
    
    Everything's quite compact
    with nothing to be lacked
    it's a smol, smol world!
    
    while (true) {
        It's a smol world after all...
        It's a smol world after all...
        It's a smol world after all...
        It's a smol, smol world
    }

## What?

**smol world** is an experimental memory manager and object model written in C++. It provides:

* A memory space called a “heap”, which internally uses 32-bit pointers
* A super fast “bump” or “arena” memory allocator
* Allocated blocks with only two bytes of overhead; a 3-bit type field, some GC-related flags, and the block's _exact_ size [Disclaimer: blocks 1KB and larger have 4 bytes overhead.] Blocks are **not** aligned. No space is wasted for alignment.
* A simple Cheney-style garbage collector that copies the live blocks to another heap
* Basic JSON-ish object types: strings, arrays and dictionaries/maps. Plus binary blobs.
* Symbols, i.e. unique de-duplicated strings for use as dictionary keys
* A polymorphic 32-bit value type that represents either an object pointer, a 31-bit integer, or null
* Friendly C++ wrapper classes for type-safe operations on heap values.

The idea is that you can easily plug this into an interpreter of some kind and have a complete virtual machine.

## Goals

1. Have fun hacking on a small codebase with no external dependencies
2. Build some cool, useful & reuseable data structures
3. Keep everything as compact as possible to improve cache coherence (q.v.)

# Manifesto: 32-bit is small now

## smol pointers!

A change in computing that I've been getting through my head lately is that **CPU cycles are basically free. Cache misses are slow.** (Thread synchronization is slow too, but I'm not bothering with threads here.)

At the same time, our programs use 64-bit pointers. That’s eight bytes! For one pointer! As [Donald Knuth wrote](https://www-cs-faculty.stanford.edu/~knuth/news08.html) in 2008:

> It is absolutely idiotic to have 64-bit pointers when I compile a program that uses less than 4 gigabytes of RAM. When such pointer values appear inside a struct, they not only waste half the memory, they effectively throw away half of the cache.

There’s no law that says pointers have to be the size of your CPU address bus; if you use relative offsets instead of absolute addresses they can be as small as you want, it just constrains their reach. 32-bit offsets let you access 4GB of memory, which ought to be enough for ~~anyone~~ many purposes, such as virtual machines for interpreters. Even if you whittle away a bit or two for tags, that leaves a gigabyte or more.

I know I’m not the only one to think of this: the V8 JavaScript engine also uses 32-bit pointers.

(I didn't consider going as far as 16-bit. I know retro-computing is hot, but 64KB, or even 256KB, is too small for me. I’m one of those old geezers who started out on an 8-bit home computer and does not want to re-explore that era.)

## smol alignment!

I learned a few weeks ago that the things we were taught about memory alignment are [no longer true](https://lemire.me/blog/2012/05/31/data-alignment-for-speed-myth-or-reality/). Unaligned reads & writes do not cause a crash (nor make you go blind!) They’re not even slow anymore!

Meanwhile, malloc is aligning all blocks to 16-byte boundaries, at least on my Mac. If I’m allocating a string or other byte-indexed data, that’s an average of 7 bytes wasted. Why not give up alignment and pack everything in tightly?

> Disclaimer: Unaligned writes _can_ be a little bit slower on some CPUs, particularly across page boundaries. Probably not applicable to vector operations. Does not apply at *all* to embedded-scale CPUs. Consult your doctor first to see if unaligned memory access is right for you.

## smol collections!

A lot of the objects we allocate are variable-size collections: strings, arrays, hash tables. That means the object has to keep track of its current size. Typically there’s a `size` field in it. That’s at least 4 bytes, probably 8.

 But the memory manager already knows how big the heap block is, it’s just not telling us … probably because in 1972 when Dennis Ritchie(?) created `malloc` and `free` he didn’t think to add a function to recover the size. So the result is the object is carrying around redundant information. Another 4 or 8 bytes of bloat.

With unaligned memory blocks, the heap metadata contains the exact block size. The smol Heap makes the block size available in the API so the higher-level string and collection classes can use it instead of storing their own.

# Architecture

## Heaps

A `Heap` has a (native) pointer to a range of up to 2^31^ bytes. It can `malloc` the memory for you, or you can point it to memory you got some other way, like with `mmap`.

### Pointers

Pointers within a heap are 32-bit integers, wrapped in an opaque type (an `enum class`) called `heappos`.

There are two ways to implement smol pointers, based on what address they’re relative to:

- They can be unsigned offsets from a known base address
- or they can be signed offsets relative to the location of *the pointer itself* – in other words, a pointer at address `A` with value `n` resolves to address `A+n`.

I went with the first type, even though it has the awkward requirement that you can’t dereference (or create) such a pointer without knowing the base address, i.e. the Heap. Right now, you have to pass a Heap parameter to any method that uses a pointer, but the intention is to have a thread-local “current heap” state and use that implicitly.

> Why not the second type? I looked at it, but realized it’s tricky to code for. I thought of creating a `RelativePtr` smart-pointer class to do the arithmetic, but such a class would have some weird behaviors: it can’t be treated as a value type at all, only a reference, because it breaks if copied. I may still try implementing this in the future.

### The allocator

When a new heap is created, the first 8 bytes are allocated for a header. The header contains a 32-bit magic number, followed by a pointer to the root object.

Memory allocations start from low memory (the heap’s `_base`) and go up. The heap keeps a pointer `_cur` to the first unallocated byte. Memory is allocated by simply moving `_cur` forward and returning its starting value.

If `_cur` would pass the end of the heap, the Heap instead calls a user-supplied callback that can free up space, then retries. If theres’ no callback, or the callback returns false, it returns `nullptr`. The obvious things for the callback to do are run a garbage collector (q.v.) or grow the heap by moving its `_end` pointer upward.

### Heap blocks

The class `Block` represents a heap block. After the allocator reserves some memory, it constructs a `Block` instance at that address. 

A Block usually occupies two bytes, or 16 bits. Six of those bits are flags: Three indicate the type of object stored in the block, one is a “mark” flag for the garbage collector, another is a flag for heap iteration, and the last indicates whether it’s a large block. That leaves 10 bits for the size, up to 1023 bytes.

A block of 1024 or more bytes sets the “large” flag bit, and stores 16 more bits of size in the next two bytes. That makes the maximum size of a block 64MB.

Since each block starts with its size, it effectively points to the next block, meaning that it’s easy to traverse the heap as a linked list.

## Values

The root data type is `Value`. There are currently seven subtypes:

- `Null` (a singleton)
- `Integer` (31 bits signed, range ±1,073,741,824)
- `String` (UTF-8 encoding)
- `Symbol` (like a String but de-duplicated so each instance is unique)
- `Blob` (arbitrary binary data)
- `Array` (or values)
- `Dict` (mapping Symbols to Values)

`Null` and `Integer` are tagged types stored in the `Value` itself; the others are allocated on the heap, and the flag bits in the heap `Block` give its type.

There are actually two forms of value. `Value`, and the others listed above, are used as local variables or function parameters; they contain real pointers so they’re faster and easier to work with. But when a value is stored in the heap, in an `Array` or `Dict`, it’s stored in a 32-bit form called `Val`.

`Val` has one tag bit. If that tag is set, the other 31 bits hold an integer; if not, a pointer (the small form of pointer described earlier.) If that pointer is 0, the value is `Null`.

## Objects

All the heap-based object types are basically arrays: of `char`, `Val` or `Entry`  (where `Entry`, used by `Dict`,is a key-value pair of `Val`s.) 

No separate size field is needed since the size is easily determined from the `Block`’s size. This does mean that you can’t resize an object, since altering the block size would break the linked list of blocks used by the heap. However, you can zero/null pad.

`Dict` always keeps its `{key, value}` pairs sorted by key. It’s literally just a descending sort of the 32-bit key; descending is because we want the null values representing empty pairs to collect at the end. This means that keys are compared by pointer equality, so if you use strings as keys you need to de-duplicate them – fortunately, Symbol objects do exactly that.

Symbols are managed by a SymbolTable, which owns an Array that it uses as a set of Symbol objects. The set is implemented as an open hash table. A 4-byte slot in the heap header points to this array.

## The Garbage Collector

smol_world implements a simple copying garbage collector that uses the venerable Cheney algorithm. You have to give the collector a second Heap to copy into.

The algorithm is:

1. Iterate the source heap by block and clear each block’s `Fwd` flag.
2. Clear the destination heap, i.e. reset its `_cur` pointer back to the beginning.
3. **Move** (q.v) the `root` pointer in the source heap header and write it to the destination heap’s root pointer.
4. **Move** the heap’s symbol table, if it has one.
5. **Move** any other `Val`s given by the application; these are additional roots or existing pointers from the stack or external memory.
6. Scan through the objects in the destination heap until you reach the end; for each object:
   1. If the object is an Array or Dict, iterate through it and **Move** each Val.

7. Swap the pointers of the source and destination heaps: now the source heap contains only live objects.

To **Move** a Val:

1. If the `Val` isn’t a pointer, do nothing, just return it as-is.
2. Dereference the `Val` in the source heap. If the Object’s `Fwd` flag is set, interpret the rest of the Object header as a pointer in the _destination_ heap, and return the Val with its pointer replaced by the new one.
3. Else allocate an exact copy of the object in the destination heap, and return a `Val` pointing to the destination object.
