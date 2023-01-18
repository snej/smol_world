# it’s a smol world

**smol world** is a little experiment/hack, a simple memory manager and object model for a virtual machine. It provides

* A 32-bit address space called a “heap”, with 32-bit tagged pointers
* A “bump” or “arena” memory allocator
* A way to traverse the blocks in the heap
* A few bits of metadata per block (one bit used by GC, the others unassigned)
* A simple garbage collector that copies the live blocks to another heap
* Basic JSON-ish object types: strings, arrays and dictionaries/maps
* A polymorphic 32-bit value type that represents either an object pointer, a 31-bit integer, or null

The idea is that you can easily plug this into an interpreter of some kind and have a complete virtual machine.

## 32 bits is smol now

Keeping it 32-bit makes data more compact on a 64-bit system, since pointers are only half the size. As [Donald Knuth wrote](https://www-cs-faculty.stanford.edu/~knuth/news08.html) in 2008:

> It is absolutely idiotic to have 64-bit pointers when I compile a program that uses less than 4 gigabytes of RAM. When such pointer values appear inside a struct, they not only waste half the memory, they effectively throw away half of the cache.

I’m not the only one to think of this in terms of virtual machines: the V8 JavaScript engine also uses 32-bit pointers.

(I did *not* consider going as far as 16-bit. 64KB, or even 256KB, is too small for me! I’m one of those old geezers who started out on an 8-bit home computer and does not want to re-explore that era.)

## Heaps

A Heap has a (native) pointer to a range of up to 2^31^ bytes. It can malloc the memory for you, or you can point it to memory you got some other way, like with mmap.

### Pointers

Pointers within a heap are 32-bit unsigned integers, relative to the heap’s base address. The basic type is `heappos`. `Ptr` is a type-safe wrapper around that.

To dereference a pointer, you have to know what heap it refers to. Right now, you have to pass a Heap parameter to any method that uses a pointer, but the intention is to have a thread-local “current heap” state and use that implicitly.

### The allocator

A heap keeps a pointer called `_cur` that points to the first unallocated byte. Memory is allocated by simply moving `_cur` forward and returning its starting value. No attempt is made to align allocations: it turns out that modern CPUs don't penalize misaligned reads and only barely penalize misaligned writes.

If `_cur` would pass the end of the heap, the Heap calls a user-supplied callback that can attempt to free memory somehow (probably by invoking the garbage collector), then retries. If theres’ no callback or the callback returns false, it throws an exception.

When a new heap is created, the first 8 bytes are allocated for a header. The header contains a 32-bit magic number, followed by a pointer to the root object.

### Heap blocks

The class `Object` represents a basic heap block. It reserves 4 bytes, of which 28 bits store the size of the block in bytes (minus the header) and 4 bits are used for tags.

The size field means the heap is structured as a linked list of blocks, which is easily traversed. This is useful for clearing tag bits prior to a garbage collection.

## Vals

`Val` is a more useful type of pointer. It uses 2 bits as tags to identify what type of object is pointed to, and another tag bit to represent 31-bit signed integers.

The idea is that Val will be the primitive data type of a dynamic language interpreter.

## Objects

The three built-in object types are String, Array and Dict, each of which subclass Object. All of these are basically arrays: of `char`, `Val` and `Entry`, respectively (where `Entry` is a key-value pair of `Val`s.) 

No separate size field is needed since the size is easily determined from the `Object`'s' block size. This does mean that you can’t resize, even shrink, since altering the block size would break the linked list of blocks used by the heap. However, you can zero/null pad.

`Dict` always keeps its `{key, value}` pairs sorted by key. It’s literally just a descending sort of the 32-bit key; descending is because we want the zero values representing empty pairs to collect at the end. This means that keys are compared by pointer equality, so if you use String objects as keys you need to de-duplicate them.

## The Garbage Collector

smol implements a typical copying garbage collector. You have to give the collector a second Heap to copy into.

The algorithm is:

1. Iterate the source heap by block and clear each block’s `Fwd` flag.
2. Clear the destination heap, i.e. reset its `_cur` pointer back to the beginning.
3. **Process** (q.v) the `root` pointer in the source heap header and write it to the destination heap’s root pointer.
4. Process any other `Val`s given by the application; these are additional roots or existing pointers from the stack or external memory.
5. Swap the pointers of the source and destination heaps: now the source heap contains only live objects.

Processing a `Val` involves:

1. If the `Val` isn’t a pointer, do nothing, just return it as-is.
2. Dereference the `Val` in the source heap. If the Object’s `Fwd` flag is set, interpret the rest of the Object header as a pointer in the _destination_ heap, replace the given `Val`'s address with that pointer, and return it.
3. Else allocate a copy of the object in the destination heap.
4. Then scan the source object for embedded `Val`s (unless it’s a String), process each one and write the result to the corresponding location in the destination object.
5. Finally return a `Val` pointing to the destination object.



# IDEA: 24-Bit

- Unaligned reads are not slow anymore (unaligned writes _sometimes_ are)
- 24-bit addresses, 3 bytes. 16MB address space.
- Array elements are 3 bytes, so address = base + (index x 2) + index
- Heap blocks are byte-aligned
- Block prefixed with size. Use a varint?

What about tags?
If we put type tags in block headers, then value tags only need to identify scalars  (ints)
    That means 8MB address space
    And ints ±4 million
    Null is pointer to 0
    True/false could be pointers to 1, 2
Block header needs to contain size, object tags, GC flag(s)

Object types:
    - String
    - Array
    - Dict
Optional:
    - Symbol [uniqued string]
    - Big int
    - Float
    - Blob
    - Code
Other flags:
    - GC mark

That's 4 bits. The remaining 4 bits for what?
    - Small size field; could only go from 0-15
    - size can be expressed in _items_ not bytes
    - size=15 means bigger size follows
        - or for strings, can mean "nul-terminated" [but this slows down heap scanning]

# IDEA: Separate heap for strings?

* Strings cannot contain pointers, so no need to scan them during GC
* Strings are the most likely thing to get large
    - Handles (indexes) can stay smaller than direct pointers
* Strings are often used as unique IDs, i.e. keys, without being read
* Often useful to intern/uniquify strings: use a hash table or tree
    - Store hash of string next to it, or next to its handle
* Can handle be the index in the hash table? If so, rehashing requires updating string handles
  in the main heap

Symbol heap starts with hash table: array of bucket pointers
    Each bucket is an array of {string, ID} tuples
But this means it's slow to assign a new symbol ID; finding the first unused one is slow.
    ...but creating new symbols is not common.

String heap's root is an array of strings. A string's ID is its index in the array.
Unused items in the array contain integers, forming a linked list of free indexes.
To mark a string ID in the main heap, mark its array item in the string heap as used (how?)
String heap should avoid compaction
