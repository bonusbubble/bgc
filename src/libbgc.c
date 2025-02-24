#if !defined(BGC__BGC_C)
#define BGC__BGC_C

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bgc.h>

#define LOGLEVEL LOGLEVEL_DEBUG

typedef enum bgc_LogLevel {
    LOGLEVEL_CRITICAL,
    LOGLEVEL_WARNING,
    LOGLEVEL_INFO,
    LOGLEVEL_DEBUG,
    LOGLEVEL_NONE
} bgc_LogLevel;

#if defined(DISABLE_LOGGING)

static const char * log_level_strings [] = { "CRIT", "WARN", "INFO", "DEBG", "NONE" };

#define log(level, fmt, ...) \
    do { if (level <= LOGLEVEL) fprintf(stderr, "[%s] %s:%s:%llu: " fmt "\n", log_level_strings[level], __func__, __FILE__, (long long unsigned int) __LINE__, __VA_ARGS__); } while (0)

#else

static bgc_LogLevel log(bgc_LogLevel log_level, ...)
{
    return log_level;
}

#endif // DEBUG

#define LOG_CRITICAL(fmt, ...) log(LOGLEVEL_CRITICAL, fmt, __VA_ARGS__)
#define LOG_WARNING(fmt, ...) log(LOGLEVEL_WARNING, fmt, __VA_ARGS__)
#define LOG_INFO(fmt, ...) log(LOGLEVEL_INFO, fmt, __VA_ARGS__)
#define LOG_DEBUG(fmt, ...) log(LOGLEVEL_DEBUG, fmt, __VA_ARGS__)

/*
 * Set log level for this compilation unit. If set to LOGLEVEL_DEBUG,
 * the garbage collector will be very chatty.
 */
#undef LOGLEVEL
#define LOGLEVEL LOGLEVEL_INFO

/*
 * The size of a pointer.
 */
#define BGC_PTRSIZE sizeof(void *)

/*
 * Allocations can temporarily be tagged as "marked" an part of the
 * mark-and-sweep implementation or can be tagged as "roots" which are
 * not automatically garbage collected. The latter allows the implementation
 * of global variables.
 */
#define BGC_TAG_NONE 0x0
#define BGC_TAG_ROOT 0x1
#define BGC_TAG_MARK 0x2

/*
 * Support for windows c compiler is added by adding this macro.
 * Tested on: Microsoft (R) C/C++ Optimizing Compiler Version 19.24.28314 for x86
 */
#if defined(_MSC_VER)
#include <intrin.h>

#define __builtin_frame_address(x)  ((void)(x), _AddressOfReturnAddress())
#endif

/*
 * Define a globally available GC object; this allows all code that
 * includes the gc.h header to access a global static garbage collector.
 * Convenient for single-threaded code, insufficient for multi-threaded
 * use cases. Use the BGC_NO_GLOBAL_GC flag to toggle.
 */
#ifndef BGC_NO_GLOBAL_GC
/// @brief A global garbage collector for all single-threaded applications.
bgc_GC *BGC_GLOBAL_GC;
#endif

static void bgc__array_set_buffer(bgc_Array *array, bgc_Buffer * value);

static void bgc__array_set_slot_count(bgc_Array *array, size_t value);

static void bgc__array_set_slot_size(bgc_Array *array, size_t value);

static void bgc__buffer_set_address(bgc_Buffer *buffer, void * value);

static void bgc__buffer_set_length(bgc_Buffer *buffer, size_t value);

static bool is_prime(size_t n) {
    /* https://stackoverflow.com/questions/1538644/c-determine-if-a-number-is-prime */
    if (n <= 3)
        return n > 1;     // as 2 and 3 are prime
    else if (n % 2==0 || n % 3==0)
        return false;     // check if n is divisible by 2 or 3
    else {
        for (size_t i=5; i*i<=n; i+=6) {
            if (n % i == 0 || n%(i + 2) == 0)
                return false;
        }
        return true;
    }
}

static size_t next_prime(size_t n) {
    while (!is_prime(n)) ++n;
    return n;
}

/**
 * Create a new allocation object.
 *
 * Creates a new allocation object using the system `malloc`.
 *
 * @param[in] ptr The pointer to the memory to manage.
 * @param[in] size The size of the memory range pointed to by `ptr`.
 * @param[in] dtor A pointer to a destructor function that should be called
 *                 before freeing the memory pointed to by `ptr`.
 * @returns Pointer to the new allocation instance.
 */
static bgc_Allocation * bgc_allocation_new(void *ptr, size_t size, bgc_Deconstructor dtor) {
    bgc_Allocation *a = (bgc_Allocation*) malloc(sizeof(bgc_Allocation));
    a->ptr = ptr;
    a->size = size;
    a->tag = BGC_TAG_NONE;
    a->dtor = dtor;
    a->next = NULL;
    return a;
}

/**
 * Delete an allocation object.
 *
 * Deletes the allocation object pointed to by `a`, but does *not*
 * free the memory pointed to by `a->ptr`.
 *
 * @param a The allocation object to delete.
 */
static void bgc_allocation_delete(bgc_Allocation *a) {
    free(a);
}

/**
 * Determine the current load factor of an `AllocationMap`.
 *
 * Calculates the load factor of the hash map as the quotient of the size and
 * the capacity of the hash map.
 *
 * @param am The allocationo map to calculate the load factor for.
 * @returns The load factor of the allocation map `am`.
 */
static double bgc_allocation_map_load_factor(bgc_AllocationMap * am) {
    return (double) am->size / (double) am->capacity;
}

static bgc_AllocationMap * bgc_allocation_map_new(size_t min_capacity,
        size_t capacity,
        double sweep_factor,
        double downsize_factor,
        double upsize_factor) {
    bgc_AllocationMap * am = (bgc_AllocationMap *) malloc(sizeof(bgc_AllocationMap));
    am->min_capacity = next_prime(min_capacity);
    am->capacity = next_prime(capacity);
    if (am->capacity < am->min_capacity) am->capacity = am->min_capacity;
    am->sweep_factor = sweep_factor;
    am->sweep_limit = (int) (sweep_factor * am->capacity);
    am->downsize_factor = downsize_factor;
    am->upsize_factor = upsize_factor;
    am->allocs = (bgc_Allocation**) calloc(am->capacity, sizeof(bgc_Allocation*));
    am->size = 0;
    LOG_DEBUG("Created allocation map (cap=%lld, siz=%lld)", (uint64_t) am->capacity, (uint64_t) am->size);
    return am;
}

static void bgc_allocation_map_delete(bgc_AllocationMap * am) {
    // Iterate over the map
    LOG_DEBUG("Deleting allocation map (cap=%lld, siz=%lld)",
              (uint64_t) am->capacity, (uint64_t) am->size);
    bgc_Allocation *alloc, *tmp;
    for (size_t i = 0; i < am->capacity; ++i) {
        if ((alloc = am->allocs[i])) {
            // Make sure to follow the chain inside a bucket
            while (alloc) {
                tmp = alloc;
                alloc = alloc->next;
                // free the management structure
                bgc_allocation_delete(tmp);
            }
        }
    }
    free(am->allocs);
    free(am);
}

static size_t bgc_hash(void *ptr) {
    return ((uintptr_t)ptr) >> 3;
}

static void bgc_allocation_map_resize(bgc_AllocationMap * am, size_t new_capacity) {
    if (new_capacity <= am->min_capacity) {
        return;
    }
    // Replaces the existing items array in the hash table
    // with a resized one and pushes items into the new, correct buckets
    LOG_DEBUG("Resizing allocation map (cap=%lld, siz=%lld) -> (cap=%lld)",
              (uint64_t) am->capacity, (uint64_t) am->size, (uint64_t) new_capacity);
    bgc_Allocation **resized_allocs = (bgc_Allocation**) calloc(new_capacity, sizeof(bgc_Allocation*));

    for (size_t i = 0; i < am->capacity; ++i) {
        bgc_Allocation *alloc = am->allocs[i];
        while (alloc) {
            bgc_Allocation *next_alloc = alloc->next;
            size_t new_index = bgc_hash(alloc->ptr) % new_capacity;
            alloc->next = resized_allocs[new_index];
            resized_allocs[new_index] = alloc;
            alloc = next_alloc;
        }
    }
    free(am->allocs);
    am->capacity = new_capacity;
    am->allocs = resized_allocs;
    am->sweep_limit = am->size + am->sweep_factor * (am->capacity - am->size);
}

static bool bgc_allocation_map_resize_to_fit(bgc_AllocationMap * am) {
    double load_factor = bgc_allocation_map_load_factor(am);
    if (load_factor > am->upsize_factor) {
        LOG_DEBUG("Load factor %0.3g > %0.3g. Triggering upsize.",
                  load_factor, am->upsize_factor);
        bgc_allocation_map_resize(am, next_prime(am->capacity * 2));
        return true;
    }
    if (load_factor < am->downsize_factor) {
        LOG_DEBUG("Load factor %0.3g < %0.3g. Triggering downsize.",
                  load_factor, am->downsize_factor);
        bgc_allocation_map_resize(am, next_prime(am->capacity / 2));
        return true;
    }
    return false;
}

static bgc_Allocation * bgc_allocation_map_get(bgc_AllocationMap * am, void *ptr) {
    size_t index = bgc_hash(ptr) % am->capacity;
    bgc_Allocation *cur = am->allocs[index];
    while(cur) {
        if (cur->ptr == ptr) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static bgc_Allocation * bgc_allocation_map_put(bgc_AllocationMap * am,
        void *ptr,
        size_t size,
        bgc_Deconstructor dtor) {
    size_t index = bgc_hash(ptr) % am->capacity;
    LOG_DEBUG("PUT request for allocation ix=%lld", (uint64_t) index);
    bgc_Allocation *alloc = bgc_allocation_new(ptr, size, dtor);
    bgc_Allocation *cur = am->allocs[index];
    bgc_Allocation *prev = NULL;
    /* Upsert if ptr is already known (e.g. dtor update). */
    while(cur != NULL) {
        if (cur->ptr == ptr) {
            // found it
            alloc->next = cur->next;
            if (!prev) {
                // position 0
                am->allocs[index] = alloc;
            } else {
                // in the list
                prev->next = alloc;
            }
            bgc_allocation_delete(cur);
            LOG_DEBUG("AllocationMap Upsert at ix=%lld", (uint64_t) index);
            return alloc;

        }
        prev = cur;
        cur = cur->next;
    }
    /* Insert at the front of the separate chaining list */
    cur = am->allocs[index];
    alloc->next = cur;
    am->allocs[index] = alloc;
    am->size++;
    LOG_DEBUG("AllocationMap insert at ix=%lld", (uint64_t) index);
    void *p = alloc->ptr;
    if (bgc_allocation_map_resize_to_fit(am)) {
        alloc = bgc_allocation_map_get(am, p);
    }
    return alloc;
}

static void bgc_allocation_map_remove(bgc_AllocationMap * am,
                                     void *ptr,
                                     bool allow_resize) {
    // ignores unknown keys
    size_t index = bgc_hash(ptr) % am->capacity;
    bgc_Allocation *cur = am->allocs[index];
    bgc_Allocation *prev = NULL;
    bgc_Allocation *next;
    while(cur != NULL) {
        next = cur->next;
        if (cur->ptr == ptr) {
            // found it
            if (!prev) {
                // first item in list
                am->allocs[index] = cur->next;
            } else {
                // not the first item in the list
                prev->next = cur->next;
            }
            bgc_allocation_delete(cur);
            am->size--;
        } else {
            // move on
            prev = cur;
        }
        cur = next;
    }
    if (allow_resize) {
        bgc_allocation_map_resize_to_fit(am);
    }
}

static void * bgc_mcalloc(size_t count, size_t size) {
    if (!count) return malloc(size);
    return calloc(count, size);
}

static bool bgc_needs_sweep(bgc_GC *gc) {
    return gc->allocs->size > gc->allocs->sweep_limit;
}

static void * bgc_allocate(bgc_GC *gc, size_t count, size_t size, bgc_Deconstructor dtor) {
    /* Allocation logic that generalizes over malloc/calloc. */

    /* Check if we reached the high-water mark and need to clean up */
    if (bgc_needs_sweep(gc) && !gc->disabled) {
        size_t freed_mem = bgc_collect(gc);
        LOG_DEBUG("Garbage collection cleaned up %llu bytes.", freed_mem);
    }
    /* With cleanup out of the way, attempt to allocate memory */
    void *ptr = bgc_mcalloc(count, size);
    size_t alloc_size = count ? count * size : size;
    /* If allocation fails, force an out-of-policy run to free some memory and try again. */
    if (!ptr && !gc->disabled && (errno == EAGAIN || errno == ENOMEM)) {
        bgc_collect(gc);
        ptr = bgc_mcalloc(count, size);
    }
    /* Start managing the memory we received from the system */
    if (ptr) {
        LOG_DEBUG("Allocated %zu bytes at %p", alloc_size, (void *) ptr);
        bgc_Allocation *alloc = bgc_allocation_map_put(gc->allocs, ptr, alloc_size, dtor);
        /* Deal with metadata allocation failure */
        if (alloc) {
            LOG_DEBUG("Managing %zu bytes at %p", alloc_size, (void *) alloc->ptr);
            ptr = alloc->ptr;
        } else {
            /* We failed to allocate the metadata, fail cleanly. */
            free(ptr);
            ptr = NULL;
        }
    }
    return ptr;
}

static void bgc_make_root(bgc_GC *gc, void * const ptr) {
    bgc_Allocation *alloc = bgc_allocation_map_get(gc->allocs, ptr);
    if (alloc) {
        alloc->tag |= BGC_TAG_ROOT;
    }
}

void * bgc_malloc(bgc_GC *gc, size_t const size) {
    return bgc_malloc_ext(gc, size, NULL);
}

bgc_Array * bgc_create_array(bgc_GC *gc, size_t tsize, size_t count) {
    return bgc_create_array_ext(gc, tsize, count, NULL);
}

bgc_Array * bgc_create_array_ext(bgc_GC *gc, size_t tsize, size_t count, bgc_Deconstructor dtor) {
    // Allocate the memory required by the array.
    bgc_Array *array = bgcx_new_ext(gc, bgc_Array, dtor);

    // Allocate an underlying buffer for the array to store its values.
    bgc_Buffer *buffer = bgc_create_buffer(gc, count * tsize);

    // Set the underlying buffer that the array represents.
    bgc__array_set_buffer(array, buffer);

    // Set the number of slots the array contains.
    bgc__array_set_slot_count(array, count);

    // Set the size of a single slot in the array.
    bgc__array_set_slot_size(array, tsize);

    return array;
}

bgc_Buffer * bgc_create_buffer(bgc_GC *gc, size_t size) {
    return bgc_create_buffer_ext(gc, size, NULL);
}

bgc_Buffer * bgc_create_buffer_ext(bgc_GC *gc, size_t size, bgc_Deconstructor dtor) {
    // Create a new buffer.
    bgc_Buffer *buffer = bgcx_new_ext(gc, bgc_Buffer, dtor);

    // If a destructor was provided:
    if (dtor == NULL) {
        // Allocate the buffer's memory.
        bgc__buffer_set_address(buffer, bgc_malloc(gc, size));
        bgc__buffer_set_length(buffer, size);
    }
    // Otherwise:
    else {
        // Allocate the buffer's memory.
        bgc__buffer_set_address(buffer, bgc_malloc_ext(gc, size, dtor));
        bgc__buffer_set_length(buffer, size);
    }

    return buffer;
}

void * bgc_malloc_static(bgc_GC *gc, size_t size, bgc_Deconstructor dtor) {
    void *ptr = bgc_malloc_ext(gc, size, dtor);
    bgc_make_root(gc, ptr);
    return ptr;
}

void * bgc_make_static(bgc_GC *gc, void *ptr) {
    bgc_make_root(gc, ptr);
    return ptr;
}

void * bgc_malloc_ext(bgc_GC *gc, size_t size, bgc_Deconstructor dtor) {
    return bgc_allocate(gc, 0, size, dtor);
}


void * bgc_calloc(bgc_GC *gc, size_t count, size_t size) {
    return bgc_calloc_ext(gc, count, size, NULL);
}


void * bgc_calloc_ext(bgc_GC *gc, size_t count, size_t size,
                    bgc_Deconstructor dtor) {
    return bgc_allocate(gc, count, size, dtor);
}


void * bgc_realloc(bgc_GC *gc, void *p, size_t size) {
    bgc_Allocation *alloc = bgc_allocation_map_get(gc->allocs, p);
    if (p && !alloc) {
        // the user passed an unknown pointer
        errno = EINVAL;
        return NULL;
    }
    void *q = realloc(p, size);
    if (!q) {
        // realloc failed but p is still valid
        return NULL;
    }
    if (!p) {
        // allocation, not reallocation
        bgc_Allocation *alloc = bgc_allocation_map_put(gc->allocs, q, size, NULL);
        return alloc->ptr;
    }
    if (p == q) {
        // successful reallocation w/o copy
        alloc->size = size;
    } else {
        // successful reallocation w/ copy
        bgc_Deconstructor dtor = alloc->dtor;
        bgc_allocation_map_remove(gc->allocs, p, true);
        bgc_allocation_map_put(gc->allocs, q, size, dtor);
    }
    return q;
}

void bgc_free(bgc_GC *gc, void *ptr) {
    bgc_Allocation *alloc = bgc_allocation_map_get(gc->allocs, ptr);
    if (alloc) {
        if (alloc->dtor) {
            alloc->dtor(ptr);
        }
        bgc_allocation_map_remove(gc->allocs, ptr, true);
        free(ptr);
    } else {
        LOG_WARNING("Ignoring request to free unknown pointer %p", (void *) ptr);
    }
}

void bgc_start(bgc_GC *gc, void *stack_bp) {
    bgc_start_ext(gc, stack_bp, 1024, 1024, 0.2, 0.8, 0.5);
}

void bgc_start_ext(bgc_GC *gc,
                  void *stack_bp,
                  size_t initial_capacity,
                  size_t min_capacity,
                  double downsize_load_factor,
                  double upsize_load_factor,
                  double sweep_factor) {
    double downsize_limit = downsize_load_factor > 0.0 ? downsize_load_factor : 0.2;
    double upsize_limit = upsize_load_factor > 0.0 ? upsize_load_factor : 0.8;
    sweep_factor = sweep_factor > 0.0 ? sweep_factor : 0.5;
    gc->disabled = false;
    gc->stack_bp = stack_bp;
    initial_capacity = initial_capacity < min_capacity ? min_capacity : initial_capacity;
    gc->allocs = bgc_allocation_map_new(min_capacity, initial_capacity,
                                       sweep_factor, downsize_limit, upsize_limit);
    LOG_DEBUG("Created new garbage collector (cap=%lld, siz=%lld).", (uint64_t)(gc->allocs->capacity),
              (uint64_t)(gc->allocs->size));
}

void bgc_disable(bgc_GC *gc) {
    gc->disabled = true;
}

void bgc_enable(bgc_GC *gc) {
    gc->disabled = false;
}

void bgc_mark_alloc(bgc_GC *gc, void *ptr) {
    bgc_Allocation *alloc = bgc_allocation_map_get(gc->allocs, ptr);
    /* Mark if alloc exists and is not tagged already, otherwise skip */
    if (alloc && !(alloc->tag & BGC_TAG_MARK)) {
        LOG_DEBUG("Marking allocation (ptr=%p)", ptr);
        alloc->tag |= BGC_TAG_MARK;
        /* Iterate over allocation contents and mark them as well */
        LOG_DEBUG("Checking allocation (ptr=%p, size=%llu) contents", ptr, alloc->size);
        for (char *p = (char*) alloc->ptr;
                p <= (char*) alloc->ptr + alloc->size - BGC_PTRSIZE;
                ++p) {
            LOG_DEBUG("Checking allocation (ptr=%p) @%llu with value %p",
                      ptr, p-((char*) alloc->ptr), *(void **)p);
            bgc_mark_alloc(gc, *(void **)p);
        }
    }
}

void bgc_mark_stack(bgc_GC *gc) {
    LOG_DEBUG("Marking the stack (gc@%p) in increments of %lld", (void *) gc, (uint64_t)(sizeof(char)));
    void *stack_sp = __builtin_frame_address(0);
    void *stack_bp = gc->stack_bp;
    /* The stack grows towards smaller memory addresses, hence we scan stack_sp->stack_bp.
     * Stop scanning once the distance between stack_sp & stack_bp is too small to hold a valid pointer */
    for (char *p = (char*) stack_sp; p <= (char*) stack_bp - BGC_PTRSIZE; ++p) {
        bgc_mark_alloc(gc, *(void **)p);
    }
}

void bgc_mark_roots(bgc_GC *gc) {
    LOG_DEBUG("Marking roots%s", "");
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        bgc_Allocation *chunk = gc->allocs->allocs[i];
        while (chunk) {
            if (chunk->tag & BGC_TAG_ROOT) {
                LOG_DEBUG("Marking root @ %p", chunk->ptr);
                bgc_mark_alloc(gc, chunk->ptr);
            }
            chunk = chunk->next;
        }
    }
}

void bgc_mark(bgc_GC *gc) {
    /* Note: We only look at the stack and the heap, and ignore BSS. */
    LOG_DEBUG("Initiating GC mark (gc@%p)", (void *) gc);
    /* Scan the heap for roots */
    bgc_mark_roots(gc);
    /* Dump registers onto stack and scan the stack */
    void (*volatile _mark_stack)(bgc_GC*) = bgc_mark_stack;
    jmp_buf ctx;
    memset(&ctx, 0, sizeof(jmp_buf));
    setjmp(ctx);
    _mark_stack(gc);
}

size_t bgc_sweep(bgc_GC *gc) {
    LOG_DEBUG("Initiating GC sweep (gc@%p)", (void *) gc);
    size_t total = 0;
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        bgc_Allocation *chunk = gc->allocs->allocs[i];
        bgc_Allocation *next = NULL;
        /* Iterate over separate chaining */
        while (chunk) {
            if (chunk->tag & BGC_TAG_MARK) {
                LOG_DEBUG("Found used allocation %p (ptr=%p)", (void *) chunk, (void *) chunk->ptr);
                /* unmark */
                chunk->tag &= ~BGC_TAG_MARK;
                chunk = chunk->next;
            } else {
                LOG_DEBUG("Found unused allocation %p (%llu bytes @ ptr=%p)", (void *) chunk, chunk->size, (void *) chunk->ptr);
                /* no reference to this chunk, hence delete it */
                total += chunk->size;
                if (chunk->dtor) {
                    chunk->dtor(chunk->ptr);
                }
                free(chunk->ptr);
                /* and remove it from the bookkeeping */
                next = chunk->next;
                bgc_allocation_map_remove(gc->allocs, chunk->ptr, false);
                chunk = next;
            }
        }
    }
    bgc_allocation_map_resize_to_fit(gc->allocs);
    return total;
}

/**
 * Unset the ROOT tag on all roots on the heap.
 *
 * @param gc A pointer to a garbage collector instance.
 */
void bgc_unroot_roots(bgc_GC *gc) {
    LOG_DEBUG("Unmarking roots%s", "");
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        bgc_Allocation *chunk = gc->allocs->allocs[i];
        while (chunk) {
            if (chunk->tag & BGC_TAG_ROOT) {
                chunk->tag &= ~BGC_TAG_ROOT;
            }
            chunk = chunk->next;
        }
    }
}

size_t bgc_stop(bgc_GC *gc) {
    bgc_unroot_roots(gc);
    size_t collected = bgc_sweep(gc);
    bgc_allocation_map_delete(gc->allocs);
    return collected;
}

size_t bgc_collect(bgc_GC *gc) {
    LOG_DEBUG("Initiating GC run (gc@%p)", (void *) gc);
    bgc_mark(gc);
    return bgc_sweep(gc);
}

char * bgc_strdup (bgc_GC *gc, const char *str1) {
    size_t len = strlen(str1) + 1;
    void *instance = bgc_malloc(gc, len);

    if (instance == NULL) {
        return NULL;
    }
    return (char*) memcpy(instance, str1, len);
}

/// @brief Destroy a managed array.
/// @param array The array to destroy.
void bgc_destroy_array(bgc_GC *gc, bgc_Array *array)
{
    bgc_destroy_buffer(gc, array->buffer);
}

/// @brief Destroy a managed array.
/// @param array The array to destroy.
void bgc_destroy_buffer(bgc_GC *gc, bgc_Buffer *buffer)
{
    bgc_free(gc, buffer->address);
}

static void bgc__array_set_buffer(bgc_Array *array, bgc_Buffer * value) {
    void *struct_bp = (void *) array;
    * (bgc_Buffer * *)((size_t) struct_bp + BGC_PTRSIZE * 0) = value;
}

static void bgc__array_set_slot_count(bgc_Array *array, size_t value) {
    void *struct_bp = (void *) array;
    * (size_t *)((size_t) struct_bp + BGC_PTRSIZE * 1) = value;
}

static void bgc__array_set_slot_size(bgc_Array *array, size_t value) {
    void *struct_bp = (void *) array;
    * (size_t *)((size_t) struct_bp + BGC_PTRSIZE * 2) = value;
}

static void bgc__buffer_set_address(bgc_Buffer *buffer, void * value) {
    void *struct_bp = (void *) buffer;
    * (void * *)((size_t) struct_bp + BGC_PTRSIZE * 0) = value;
}

static void bgc__buffer_set_length(bgc_Buffer *buffer, size_t value) {
    void *struct_bp = (void *) buffer;
    * (size_t *)((size_t) struct_bp + BGC_PTRSIZE * 1) = value;
}

#endif // BGC__BGC_C
