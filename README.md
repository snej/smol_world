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
    there's nothing that we lack
    it's a smol, smol world!
    
    do {
        It's a smol world after all...
        It's a smol world after all...
        It's a smol world after all...
        It's a smol, smol world
    } while(!insane);

## What?

**smol world** is an experimental memory manager and object model, which tries to optimize for small data size. It provides:

* A memory space called a “heap”, which internally uses 32-bit pointers
* A super fast “bump” or “arena” memory allocator
* Allocated blocks with only two bytes of overhead, including a type tag and object size. For further space savings, blocks are byte-aligned.
* A simple Cheney-style garbage collector that copies the live blocks to a new heap
* Basic JSON-ish object types: strings, arrays and dictionaries/maps (plus binary blobs.) There’s also a JSON parser and generator.
* Symbols, i.e. unique de-duplicated strings used as dictionary keys
* A polymorphic 32-bit `Val` type that represents either an object pointer, a 31-bit integer, or null
* Friendly C++ wrapper classes for type-safe and null-safe operations on values

The idea is that you can easily plug this into an interpreter of some kind and have a complete virtual machine.

## Goals

1. Have fun hacking on a small C++20 codebase with no external dependencies
2. Build some cool, useful & reuseable data structures
3. Keep everything as compact as possible to improve cache coherence (q.v.)

## Status

☠️ **Under construction: currently highly experimental!** ☠️

There are only some limited unit tests. This hasn’t been used in any serious code yet. I haven't benchmarked anything. I’m changing stuff around and refactoring a lot. Whee!

# Manifesto: 32-bit is small now

## smol pointers!

A change in computing that I've been getting through my head lately is that **CPU cycles are basically free. Cache misses are slow.** (Thread synchronization is slow too, but I'm not bothering with threads here.)

At the same time, our programs use 64-bit pointers. That’s eight bytes! For one pointer! As [Donald Knuth wrote](https://www-cs-faculty.stanford.edu/~knuth/news08.html) in 2008:

> “It is absolutely idiotic to have 64-bit pointers when I compile a program that uses less than 4 gigabytes of RAM. When such pointer values appear inside a struct, they not only waste half the memory, they effectively throw away half of the cache.”

There’s no law that says pointers have to be the size of your CPU address bus. If you use relative offsets instead of absolute addresses they can be as small as you want, it just constrains their reach. 32-bit offsets let you access 4GB of memory, which ought to be enough for ~~anyone~~ many purposes, such as virtual machines for interpreters. Even if you whittle away a bit or two for tags, that leaves a gigabyte or more.

I know I’m not the only one to think of this: [the V8 JavaScript engine also uses 32-bit pointers](https://v8.dev/blog/pointer-compression).

(Pointers could get even smaller! A few years ago I wrote a B-tree storage engine whose 4KB pages have little heaps inside them; the heaps use *12-bit* pointers. That’s getting too smol for general use ... but 24-bit pointers might be cool; you can do a lot in 16MB.)

## smol alignment!

I learned recently that the things we were taught about memory alignment are [no longer true](https://lemire.me/blog/2012/05/31/data-alignment-for-speed-myth-or-reality/). Unaligned reads & writes do not cause a crash (nor make you go blind!) They’re not even slow anymore!

Meanwhile, malloc is aligning all blocks to 16-byte boundaries, at least on my Mac. If I’m allocating a string or other byte-indexed data, that’s an average of 7 bytes wasted. Why not give up alignment and pack everything tightly?

> Disclaimer: Unaligned writes _can_ be a little bit slower on some CPUs, particularly across page boundaries. Probably not applicable to vector operations. Not applicable to little embedded CPUs. Consult your doctor to see if unaligned memory access is right for you.

## smol collections!

A lot of the objects we allocate are variable-size collections: strings, blobs, arrays, hash tables. That means the size is a runtime value that has to be kept around. Often there’s a `size` field in the object. That’s at least 4 bytes, probably 8.

 But the memory manager already knows how big the heap block is, it’s just not telling us … probably because in 1972 when Dennis Ritchie(?) created `malloc` and `free` he didn’t think to add a function to recover the size. So the result is that the object is carrying around redundant information.

With unaligned memory blocks, the heap metadata contains the *exact* block size. The smol Heap makes the block size available in the API so the higher-level string and collection classes can use it instead of storing their own.

# Architecture

## Heaps

A `Heap` has a (native) pointer to an area of up to 2^30^ bytes (~1GB.) It can `malloc` the memory for you, or you can point it to memory you got some other way, like with `mmap`.

You can persist or transmit a heap if you want, by writing the memory range given by its `contents` property to a file or socket. It can be reconstituted by calling `Heap::existing()`. This works because a heap contains no absolute pointers, only relative offsets (q.v.)

> **Warning:** It’s not yet safe to reconstitute a Heap from untrusted (or corrupted) data. Making that safe would require scanning the heap blocks and internal pointers for validity. Reading or writing an invalid heap can cause crashes or memory corruption and other Bad Stuff; don’t do it.

### Pointers

Pointers within a heap, *smol pointers*, are 32-bit integers. They’re hidden inside a class called `Val` which is described later; `Val` is a tagged value that represents either a pointer or an integer.

> Note: Any time I use the word “pointer” from now on it means a smol pointer unless I say otherwise.

Smol pointers are byte offsets, not absolute addresses. There are basically two ways to do this:

- They could be unsigned offsets from a known base address — in other words, if the heap starts at base address `B`, then a pointer with value `n` resolves to address `B+n`.
- or they could be signed offsets relative to the location of *the pointer itself* — in other words, a pointer at address `A` with value `n` resolves to address `A+n`.

I originally used the first type because they’re simpler. But since dereferencing a pointer required knowing the heap’s base address, it meant either making that a global variable (nope), or passing a reference to the heap all through the API (yuck.)

So I switched to relative pointers. This was trickier to implement, but it made almost everything else a lot cleaner, especially the API. 

> (The trick to getting this to work was to delete `Val`’s copy constructor. You can’t let Vals be copyable or they’re likely to end up on the stack, and there’s no guarantee the stack is within ±2GB of a pointer’s target. Of course this change broke a ton of my code and I had to adapt to passing Vals by reference. Also there were some really, really annoying things I had to do in the Dict implementation and the garbage collector.)

### The allocator

When a new heap is created, the first 12 bytes at its `_base` are reserved for a header. The header contains a 32-bit magic number, then the offset of the root object, then the offset of the symbol table.

Memory allocations start from low memory (the end of the header) and go up. The heap keeps a pointer `_cur` to the first unallocated byte. Memory is allocated by simply moving `_cur` forward and returning its starting value.

If `_cur` would pass the heap’s `_end`, the Heap instead calls a user-supplied callback that can free up space, then reattempts the allocation. If theres’ no callback, or the callback couldn’t make enough space available, it gives up and returns `nullptr`. The obvious things for the callback to do are run a garbage collector (q.v.) or grow the heap by moving its `_end` pointer upward.

(I originally had allocation failure throw a C++ `bad_alloc` exception, but then I decided I didn’t want to make this library rely on exceptions. Of course an application’s callback can throw an exception when it fails.)

### Heap blocks

The class `Block` represents a heap block. After the allocator reserves some memory, it constructs a `Block` instance at that address. 

The `Block` class usually uses the first two bytes, or 16 bits. Six of those bits are flags: Three indicate the type of object stored in the block, one is a “mark” flag for the garbage collector, another is a flag for heap iteration, and the last indicates whether it’s a large block. That leaves 10 bits for the size, up to 1023 bytes.

A block of 1024 or more bytes sets the “large” flag bit, and stores 16 more bits of size in the next two bytes. That makes the maximum size of a block 64MB.

Since each block starts with its size, it effectively points to the next block, meaning that it’s easy to traverse the heap as a linked list.

## Values

The root data type is `Value`. There are currently seven subtypes:

- `Null` (two members: `nullvalue` and `nullishvalue`, the latter of which is used for JSON’s `null`.)
- `Bool` (true or false)
- `Integer` (31 bits, signed, range ±1,073,741,824)
- `String` (UTF-8 encoding)
- `Symbol` (like a String, but de-duplicated so each instance is unique)
- `Blob` (arbitrary binary data)
- `Array` (of values)
- `Dict` (maps Symbols to Values)

`Null`, `Bool` and `Integer` are tagged types stored in the `Value` itself; the others are references to objects allocated on the heap, and the flag bits in the heap `Block` give its type.

### Val

There are actually two forms of value. `Value` is the main type used in the API and application code; it contains a real (64-bit) pointer so it’s faster and easier to work with. 

But when a value is stored in the heap, in an `Array` or `Dict`, it’s stored in a 32-bit form called `Val`.

`Val` has one tag bit. If the LSB is 1, the other 31 bits hold an integer; if not, a smol pointer, except for special values for null, false and true:

```
00000000000000000000000000000000 = null (aka JS `undefined`)
00000000000000000000000000000010 = nullish (aka JSON `null`)
00000000000000000000000000000100 = false
00000000000000000000000000000110 = true
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0 = Pointer (except the values above)
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx1 = Integer
```

## Objects

All the heap-based object types are arrays of some kind: of `char`, `byte`, `Val` or `DictEntry`  (a key-value pair of `Val`s.)

No separate size field is needed since the size is easily computed by right-shifting the `Block`’s size by 0–2 bytes.

`Dict` always keeps its `{key, value}` entries sorted by key. It’s literally just a descending sort of the 32-bit raw key; descending because we want the null (0x00) values representing empty pairs to collect at the end. This means that keys are compared by pointer equality, so if you use strings as keys you need to de-duplicate them – fortunately, `Symbol` objects do exactly that.

Symbols are managed by a `SymbolTable`, which owns a global-per-Heap `Array` that it treats as a hash-set of `Symbol` objects (using open addressing.) An offset field in the heap header points to this array.

### count vs. capacity

None of these collections have a separate `count` field to distinguish how much of the available capacity (block size) is used. That’s slightly awkward, but I didn’t want to add more bytes to the header. What I’m doing so far in Dict and Array is leaving `null` values at the end. This works well with Dict because its key sort puts the nulls last, so operations are still O(log n). It’s a bit awkward for Array, though; the `count` and `append` methods have to scan backwards to find a non-`null` item. But `insert` isn’t slowed down; it just pushes items ahead to the next slot until it hits a `null`.

I’ve also tweaked the garbage collector so that when it copies an `Array` or `Dict`, it truncates the empty space. This seems like a good idea in general, but I suspect it will have some awkward edge cases where you allocate an instance with extra space to fill in, but allocating the objects to put in it triggers a GC, which gets rid of the empty space… Perfect solution TBD.

## The Garbage Collector

smol_world has a simple copying garbage collector that uses the venerable [Cheney](https://en.wikipedia.org/wiki/Cheney's_algorithm) algorithm. It takes a second Heap as the destination, and copies all the live objects from your Heap into it, then swaps the two Heaps’ pointers so your Heap now contains the newly-copied objects. 

In a traditional “semispace” setup you’d keep both Heaps around and let the collector alternate between them, but it’s not required: you can just malloc the second heap on the fly when it’s time to collect and free it afterwards.

### Roots & Handles

Any garbage collector needs to be given root pointers to start scanning from. 

A `Heap` has a `root` property you can set to point to its root/global object.

For temporary stack-based references you should use `Handle` objects. A `Handle<T>` is a reference just like a `T`, but it’s also known to the Heap as a root. (Its constructor registers it with the current Heap; the destructor unregisters it.) This means that any object pointed to by a `Handle` won’t be discarded when the GC runs; and also, the GC will update the pointer stored in the `Handle` with the object’s new address after the collection.

Conversely, this means that if you have a non-`Handle` object reference, like just a  `String` or `Array` variable (or a `Value` referencing an object), and then the GC runs (explicitly or due to the heap filling up), you’re in trouble. That object might be destroyed as garbage, and even if it isn’t, it’s been moved to a different address and your reference is now bogus. Don’t do this!

The guideline is:

- `Handle`s are slightly more expensive than regular references, so you may not want to always use them.
- If the code you’re writing doesn’t create any objects, nor calls any code that does, it’s safe to skip using Handles.
- If you create an object, or call a function that does, then any references that traverse that call (were created before *and* used afterwards) must be `Handle`s.

> (Old-school classic MacOS programmers like myself will recognize the similarity to the use of handles in the memory manager, and for basically the same reason.)
