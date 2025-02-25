![Build Status](https://github.com/voidvoxel/bgc/workflows/C/C++%20CI/badge.svg)
[![Coverage Status](https://coveralls.io/repos/github/voidvoxel/bgc/badge.svg)](https://coveralls.io/github/voidvoxel/bgc)

# BGC (Bubbly Garbage Collector): mark & sweep garbage collection for C/C++

`bgc` is an implementation of a conservative, thread-local, mark-and-sweep
garbage collector. The implementation provides a fully functional replacement
for the standard POSIX `malloc()`, `calloc()`, `realloc()`, and `free()` calls.

The focus of `bgc` is to provide a conceptually clean implementation of
a mark-and-sweep GC, without delving into the depths of architecture-specific
optimization (see e.g. the [Boehm GC][boehm] for such an undertaking). It
should be particularly suitable for learning purposes and is open for all kinds
of optimization (PRs welcome!).

The original motivation for `gc` *(the parent fork)* was the original author's desire to write [their own LISP implementation in C](https://github.com/mkirchner/stutter), entirely from scratch - and that required garbage collection.

Ironically enough, my original motivation for `bgc` *(this fork)* is my desire to write [my own programming language](https://github.com/valiant-lang)
in C, entirely from scratch - and that also required garbage collection.

### Acknowledgements

This work would not have been possible without the ability to read the work of others,
most notably the [Boehm GC](https://www.hboehm.info/gc/),
orangeduck's [tgc](https://github.com/orangeduck/tgc) *(which also follows the ideals of being tiny and simple)*,
[The Garbage Collection Handbook](https://amzn.to/2VdEvjC),
[mkirchner](https://github.com/mkirchner), and
[the many other contributors who worked on the original `gc`](https://github.com/mkirchner/gc/graphs/contributors).


## Table of contents

* [Table of contents](#table-of-contents)
* [Documentation Overview](#documentation-overview)
* [Quickstart](#quickstart)
  * [Download and test](#download-and-test)
  * [Basic usage](#basic-usage)
* [Core API](#core-api)
  * [Starting, stopping, pausing, resuming and running GC](#starting-stopping-pausing-resuming-and-running-gc)
  * [Memory allocation and deallocation](#memory-allocation-and-deallocation)
  * [Helper functions](#helper-functions)
* [Basic Concepts](#basic-concepts)
  * [Data Structures](#data-structures)
  * [Garbage collection](#garbage-collection)
  * [Reachability](#reachability)
  * [The Mark-and-Sweep Algorithm](#the-mark-and-sweep-algorithm)
  * [Finding roots](#finding-roots)
  * [Depth-first recursive marking](#depth-first-recursive-marking)
  * [Dumping registers on the stack](#dumping-registers-on-the-stack)
  * [Sweeping](#sweeping)

## Documentation Overview

* Read the [quickstart](#quickstart) below to see how to get started quickly
* The [concepts](#concepts) section describes the basic concepts and design
  decisions that went into the implementation of `bgc`.
* Interleaved with the concepts, there are implementation sections that detail
  the implementation of the core components, see [hash map
  implementation](#data-structures), [dumping registers on the
  stack](#dumping-registers-on-the-stack), [finding roots](#finding-roots), and
  [depth-first, recursive marking](#depth-first-recursive-marking).


## Quickstart

### Download, compile and test

    $ git clone git@github.com:voidvoxel/gc.git
    $ cd gc/src/voidvoxel/garbage_collection

To compile using the `clang` compiler:

    $ make test

To use the GNU Compiler Collection *(GCC)*:

    $ make test CC=gcc

The tests should complete successfully. To create the current coverage report:

    $ make coverage


### Basic usage

```c
struct Vector3 {
    float x;
    float y;
    float z;
};
typedef struct Vector3 Vector3;

struct String {
    size_t length;
    char *data;
};
typedef struct String String;

struct Entity {
    String *name;
    Vector3 position;
};
typedef struct Entity Entity;

void do_something()
{
    bgcx_var(Entity, x);

    x->name = bgcx_new(String);
}

void do_lots_of_things()
{
    int total_iterations = 1000000;

    for (int i = 0; i < total_iterations; i++)
    {
        do_something();
    }
}

int main(int argc, char **argv) {
    bgcx_start();

    do_lots_of_things();

    bgcx_stop();
}
```

## Core API

This describes the core API, see `gc.h` for more details and the low-level API.

### Starting, stopping, pausing, resuming and running GC

In order to initialize and start garbage collection, use the `bgc_start()`
function and pass a *bottom-of-stack* address:

```c
void bgc_start(bgc_GC* gc, void* stack_bp);
```

The bottom-of-stack parameter `stack_bp` needs to point to a stack-allocated
variable and marks the low end of the stack from where [root
finding](#root-finding) *(scanning)* starts.

Garbage collection can be stopped, disabled and resumed with

```c
void bgc_stop(bgc_GC* gc);
void bgc_pause(bgc_GC* gc);
void bgc_resume(bgc_GC* gc);
```

and manual garbage collection can be triggered with

```c
size_t bgc_collect(bgc_GC* gc);
```

### Memory allocation and deallocation

`bgc` supports `malloc()`, `calloc()`and `realloc()`-style memory allocation.
The respective function signatures mimick the POSIX functions *(with the
exception that we need to pass the garbage collector along as the first
argument)*:

```c
void* bgc_malloc(bgc_GC* gc, size_t size);
void* bgc_calloc(bgc_GC* gc, size_t count, size_t size);
void* bgc_realloc(bgc_GC* gc, void* ptr, size_t size);
```

It is possible to pass a pointer to a destructor function through the
extended interface:

```c
void* dtor(void* obj) {
   // do some cleanup work
   obj->parent->deregister();
   obj->db->disconnect()
   ...
   // no need to free obj
}
...
SomeObject* obj = bgc_malloc_ext(gc, sizeof(SomeObject), dtor);
...
```

`bgc` supports static allocations that are garbage collected only when the
GC shuts down via `bgc_stop()`. Just use the appropriate helper function:

```c
void* bgc_malloc_static(bgc_GC* gc, size_t size, void (*dtor)(void*));
```

Static allocation expects a pointer to a finalization function; just set to
`NULL` if finalization is not required.

Note that `bgc` currently does not guarantee a specific ordering when it
collects static variables, If static vars need to be deallocated in a
particular order, the user should call `bgc_free()` on them in the desired
sequence prior to calling `bgc_stop()`, see below.

It is also possible to trigger explicit memory deallocation using

```c
void bgc_free(bgc_GC* gc, void* ptr);
```

Calling `bgc_free()` is guaranteed to *(a)* finalize/destruct on the object
pointed to by `ptr` if applicable and *(b)* to free the memory that `ptr` points to
irrespective of the current scheduling for garbage collection and will also
work if GC has been disabled using `bgc_pause()` above.


### Helper functions

`bgc` also offers a `strdup()` implementation that returns a garbage-collected
copy:

```c
char* bgc_strdup (bgc_GC* gc, const char* s);
```


## Basic Concepts

The fundamental idea behind garbage collection is to automate the memory
allocation/deallocation cycle. This is accomplished by keeping track of all
allocated memory and periodically triggering deallocation for memory that is
still allocated but [unreachable](#reachability).

Many advanced garbage collectors also implement their own approach to memory
allocation *(i.e. replace `malloc()`)*. This often enables them to layout memory
in a more space-efficient manner or for faster access but comes at the price of
architecture-specific implementations and increased complexity. `bgc` sidesteps
these issues by falling back on the POSIX `*alloc()` implementations and keeping
memory management and garbage collection metadata separate. This makes `bgc`
much simpler to understand but, of course, also less space- and time-efficient
than more optimized approaches.

### Data Structures

The core data structure inside `bgc` is a hash map that maps the address of
allocated memory to the garbage collection metadata of that memory:

The items in the hash map are allocations, modeled with the `Allocation`
`struct`:

```c
typedef struct Allocation {
    void* ptr;                // mem pointer
    size_t size;              // allocated size in bytes
    char tag;                 // the tag for mark-and-sweep
    void (*dtor)(void*);      // destructor
    struct Allocation* next;  // separate chaining
} Allocation;
```

Each `Allocation` instance holds a pointer to the allocated memory, the size of
the allocated memory at that location, a tag for mark-and-sweep (see below), an
optional pointer to the destructor function and a pointer to the next
`Allocation` instance (for separate chaining, see below).

The allocations are collected in an `AllocationMap`

```c
typedef struct AllocationMap {
    size_t capacity;
    size_t min_capacity;
    double downsize_factor;
    double upsize_factor;
    double sweep_factor;
    size_t sweep_limit;
    size_t size;
    Allocation** allocs;
} AllocationMap;
```

that, together with a set of `static` functions inside `gc.c`, provides hash
map semantics for the implementation of the public API.

The `AllocationMap` is the central data structure in the `bgc_GC`
struct which is part of the public API:

```c
typedef struct bgc_GC {
    struct AllocationMap* allocs;
    bool disabled;
    void *stack_bp;
    size_t min_size;
} bgc_GC;
```

With the basic data structures in place, any `bgc_*alloc()` memory allocation
request is a two-step procedure: first, allocate the memory through system *(i.e.
standard `malloc()`)* functionality and second, add or update the associated
metadata to the hash map.

For `bgc_free()`, use the pointer to locate the metadata in the hash map,
determine if the deallocation requires a destructor call, call if required,
free the managed memory and delete the metadata entry from the hash map.

These data structures and the associated interfaces enable the
management of the metadata required to build a garbage collector.


### Garbage collection

`bgc` triggers collection under two circumstances: *(a)* when any of the calls to
the system allocation fail (in the hope to deallocate sufficient memory to
fulfill the current request); and *(b)* when the number of entries in the hash
map passes a dynamically adjusted high water mark.

If either of these cases occurs, `bgc` stops the world and starts a
mark-and-sweep garbage collection run over all current allocations. This
functionality is implemented in the `bgc_collect()` function which is part of the
public API and delegates all work to the `bgc_mark()` and `bgc_sweep()` functions
that are part of the private API.

`bgc_mark()` has the task of [finding roots](#finding-roots) and tagging all
known allocations that are referenced from a root *(or from an allocation that
is referenced from a root, i.e. transitively)* as "used". Once the marking of
is completed, `bgc_sweep()` iterates over all known allocations and
deallocates all unused *(i.e. unmarked)* allocations, returns to `bgc_collect()` and
the world continues to run.


### Reachability

`bgc` will keep memory allocations that are *reachable* and collect everything
else. An allocation is considered reachable if any of the following is true:

1. There is a pointer on the stack that points to the allocation content.
   The pointer must reside in a stack frame that is at least as deep in the call
   stack as the bottom-of-stack variable passed to `bgc_start()` (i.e. `stack_bp` is
   the smallest stack address considered during the mark phase).
2. There is a pointer inside `bgc_*alloc()`-allocated content that points to the
   allocation content.
3. The allocation is tagged with `BGC_TAG_ROOT`.


### The Mark-and-Sweep Algorithm

The naïve mark-and-sweep algorithm runs in two stages. First, in a *mark*
stage, the algorithm finds and marks all *root* allocations and all allocations
that are reachable from the roots.  Second, in the *sweep* stage, the algorithm
passes over all known allocations, collecting all allocations that were not
marked and are therefore deemed unreachable.

### Finding roots

At the beginning of the *mark* stage, we first sweep across all known
allocations and find explicit roots with the `BGC_TAG_ROOT` tag set.
Each of these roots is a starting point for [depth-first recursive
marking](#depth-first-recursive-marking).

`bgc` subsequently detects all roots in the stack *(starting from the bottom-of-stack
pointer `stack_bp` that is passed to `bgc_start()`)* and the registers (by [dumping them
on the stack](#dumping-registers-on-the-stack) prior to the mark phase) and
uses these as starting points for marking as well.

### Depth-first recursive marking

Given a root allocation, marking consists of *(1)* setting the `tag` field in an
`Allocation` object to `BGC_TAG_MARK` and *(2)* scanning the allocated memory for
pointers to known allocations, recursively repeating the process.

The underlying implementation is a simple, recursive depth-first search that
scans over all memory content to find potential references:

```c
void bgc_mark_alloc(bgc_GC* gc, void* ptr)
{
    Allocation* alloc = bgc_allocation_map_get(gc->allocs, ptr);
    if (alloc && !(alloc->tag & BGC_TAG_MARK)) {
        alloc->tag |= BGC_TAG_MARK;
        for (char* p = (char*) alloc->ptr;
             p < (char*) alloc->ptr + alloc->size;
             ++p) {
            bgc_mark_alloc(gc, *(void**)p);
        }
    }
}
```

In `gc.c`, `bgc_mark()` starts the marking process by marking the
known roots on the stack via a call to `bgc_mark_roots()`. To mark the roots we
do one full pass through all known allocations. We then proceed to dump the
registers on the stack.


### Dumping registers on the stack

In order to make the CPU register contents available for root finding, `bgc`
dumps them on the stack. This is implemented in a somewhat portable way using
`setjmp()`, which stores them in a `jmp_buf` variable right before we mark the
stack:

```c
...
/* Dump registers onto stack and scan the stack */
void (*volatile _mark_stack)(bgc_GC*) = bgc_mark_stack;
jmp_buf ctx;
memset(&ctx, 0, sizeof(jmp_buf));
setjmp(ctx);
_mark_stack(gc);
...
```

The detour using the `volatile` function pointer `_mark_stack` to the
`bgc_mark_stack()` function is necessary to avoid the inlining of the call to
`bgc_mark_stack()`.


### Sweeping

After marking all memory that is reachable and therefore potentially still in
use, collecting the unreachable allocations is trivial. Here is the
implementation from `bgc_sweep()`:

```c
size_t bgc_sweep(bgc_GC* gc)
{
    size_t total = 0;
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        Allocation* chunk = gc->allocs->allocs[i];
        Allocation* next = NULL;
        while (chunk) {
            if (chunk->tag & BGC_TAG_MARK) {
                /* unmark */
                chunk->tag &= ~BGC_TAG_MARK;
                chunk = chunk->next;
            } else {
                total += chunk->size;
                if (chunk->dtor) {
                    chunk->dtor(chunk->ptr);
                }
                free(chunk->ptr);
                next = chunk->next;
                bgc_allocation_map_remove(gc->allocs, chunk->ptr, false);
                chunk = next;
            }
        }
    }
    bgc_allocation_map_resize_to_fit(gc->allocs);
    return total;
}
```

We iterate over all allocations in the hash map *(the `for` loop)*, following every
chain *(the `while` loop with the `chunk = chunk->next` update)* and either *(1)*
unmark the chunk if it was marked; or *(2)* call the destructor on the chunk and
free the memory if it was not marked, keeping a running total of the amount of
memory we free.

That concludes the mark & sweep run. The stopped world is resumed and we're
ready for the next run!


[valiant]: https://github.com/valiant-lang
[naive_mas]: https://en.wikipedia.org/wiki/Tracing_garbage_collection#Naïve_mark-and-sweep
[boehm]: https://www.hboehm.info/gc/
[stutter]: https://github.com/mkirchner/stutter
[tgc]: https://github.com/orangeduck/tgc
[garbage_collection_handbook]: https://amzn.to/2VdEvjC
