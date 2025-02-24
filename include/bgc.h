#if !defined(BGC__BGC_H)
#define BGC__BGC_H

/// @file           bgc.h
/// @brief          A simple mark and sweep garbage collector for C.
/// @copyright      Copyright 2025 bonusbubble
///                 Licensed under GPL-3.0-or-later

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/// @brief A deconstructor to call after freeing managed memory.
typedef void (*bgc_Deconstructor)(void *);

/**
 * The allocation object.
 *
 * The allocation object holds all metadata for a memory location
 * in one place.
 */
typedef struct bgc_Allocation {
    void *ptr;                      // mem pointer
    size_t size;                    // allocated size in bytes
    char tag;                       // the tag for mark-and-sweep
    bgc_Deconstructor dtor;         // destructor
    struct bgc_Allocation *next;    // separate chaining
} bgc_Allocation;

/**
 * The allocation hash map.
 *
 * The core data structure is a hash map that holds the allocation
 * objects and allows O(1) retrieval given the memory location. Collision
 * resolution is implemented using separate chaining.
 */
typedef struct bgc_AllocationMap {
    size_t capacity;
    size_t min_capacity;
    double downsize_factor;
    double upsize_factor;
    double sweep_factor;
    size_t sweep_limit;
    size_t size;
    bgc_Allocation **allocs;
} bgc_AllocationMap;

/// @brief A garbage collector, used to manage memory.
typedef struct bgc_GC {
    /// @brief The allocation map.
    struct bgc_AllocationMap *allocs;

    /// @brief Toggling this variable will (temporarily) switch gc on/off.
    bool disabled;

    /// @brief A pointer to the bottom of managed stack.
    void *stack_bp;

    /// @brief The minimum size of the managed heap.
    size_t min_size;
} bgc_GC;

/// @brief A managed buffer of RAM.
typedef struct bgc_Buffer {
    /// @brief The address where the buffer's data is stored in memory.
    void * const address;

    /// @brief The length of the buffer *(in bytes)*.
    const size_t length;
} bgc_Buffer;

/// @brief A managed array of objects.
typedef struct bgc_Array {
    /// @brief The underlying buffer containing the array's objects.
    bgc_Buffer *buffer;

    /// @brief The number of slots the array has.
    const size_t slot_count;

    /// @brief The size *(in bytes)* of each slot.
    const size_t slot_size;
} bgc_Array;

/// @brief A global instance of the garbage collector for use by single-threaded applications.
extern bgc_GC *BGC_GLOBAL_GC;

#if !defined(bgc__libc_free)
/// @brief The C standard library function `free`. Set this to the underlying `free` function to call when managed memory should be freed.
void (*BGC_LIBC_FREE)(void *block) = free;
#endif

#if !defined(bgc__libc_malloc)
/// @brief The C standard library function `malloc`. Set this to the underlying `malloc` function to call when managed memory should be allocated.
void * (*BGC_LIBC_MALLOC)(size_t size) = malloc;
#endif

/// @brief Run the garbage collector, freeing up any unreachable memory resources that are no longer being used.
/// @return The amount of memory freed (in bytes).
size_t bgc_collect(bgc_GC *gc);

/// @brief Disable garbage collection.
void bgc_disable(bgc_GC *gc);

/// @brief Enable garbage collection.
void bgc_enable(bgc_GC *gc);

/// @brief Start the garbage collector.
/// @param gc The garbage collector to start.
/// @param stack_bp The base pointer of the stack.
void bgc_start(bgc_GC *gc, void *stack_bp);

/// @brief Start the garbage collector.
/// @param gc The garbage collector to start.
/// @param stack_bp The base pointer of the stack.
/// @param initial_size The initial size of the heap.
/// @param min_size The minimum size of the heap.
/// @param downsize_load_factor The down-size load factor.
/// @param upsize_load_factor The up-size load factor.
/// @param sweep_factor The sweep factor.
void bgc_start_ext(bgc_GC *gc, void *stack_bp, size_t initial_size, size_t min_size, double downsize_load_factor, double upsize_load_factor, double sweep_factor);

/// @brief Stop the garbage collector.
/// @param gc The garbage collector to stop.
/// @return The number of bytes freed.
size_t bgc_stop(bgc_GC *gc);

/// @brief Allocate managed memory.
/// @param gc The garbage collector to use.
/// @param size The size of the managed memory *(in bytes)* to allocate.
/// @return A pointer to the allocated managed memory.
void * bgc_malloc(bgc_GC *gc, size_t size);

/// @brief Allocate static managed memory.
/// @param gc The garbage collector to use.
/// @param size The number of bytes to allocate.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed memory.
void * bgc_malloc_static(bgc_GC *gc, size_t size, bgc_Deconstructor dtor);

/// @brief Allocate a block of managed memory.
/// @param gc The garbage collector to use.
/// @param size The size of the block of managed memory *(in bytes)* to allocate.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed memory.
void * bgc_malloc_ext(bgc_GC *gc, size_t size, bgc_Deconstructor dtor);

/// @brief Allocate multiple blocks of managed memory.
/// @param gc The garbage collector to use.
/// @param count The number of blocks to allocate.
/// @param size The number of bytes to allocate *(per block)*.
/// @return A pointer to the allocated blocks of managed memory.
void * bgc_calloc(bgc_GC *gc, size_t count, size_t size);

/// @brief Allocate multiple blocks of managed memory.
/// @param gc The garbage collector to use.
/// @param count The number of blocks to allocate.
/// @param size The number of bytes to allocate *(per block)*.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated blocks of managed memory.
void * bgc_calloc_ext(bgc_GC *gc, size_t count, size_t size, bgc_Deconstructor dtor);

/// @brief Reallocate (resize) a block of managed memory.
/// @param gc The garbage collector to use.
/// @param ptr A pointer to the managed memory.
/// @param size The number of bytes to allocate.
/// @return The reallocated block of managed memory.
void * bgc_realloc(bgc_GC *gc, void *ptr, size_t size);

/// @brief Free a block of managed memory.
/// @param gc The garbage collector to use.
/// @param ptr A pointer to the managed memory.
void bgc_free(bgc_GC *gc, void *ptr);

/// @brief Make a block of managed memory become static.
/// @param gc The garbage collector to use.
/// @param ptr A pointer to the managed memory.
/// @return A pointer to the managed memory.
void *bgc_make_static(bgc_GC *gc, void *ptr);

/// @brief Returns a pointer to a null-terminated byte string, which is a duplicate of the string pointed to by `str1`.
/// @param gc The garbage collector to use.
/// @param str1 The string to duplicate.
/// @return A duplicate of `str1`.
char * bgc_strdup(bgc_GC *gc, const char *str1);

/// @brief Create a managed array.
/// @param gc The garbage collector to use.
/// @param tsize The size of an item contained within the array.
/// @param count The number of items the managed array can hold.
/// @return A pointer to the allocated managed array.
bgc_Array * bgc_create_array(bgc_GC *gc, size_t tsize, size_t count);

/// @brief Create a managed array.
/// @param gc The garbage collector to use.
/// @param tsize The size of an item contained within the array.
/// @param count The number of items the managed array can hold.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed array.
bgc_Array * bgc_create_array_ext(bgc_GC *gc, size_t tsize, size_t count, bgc_Deconstructor dtor);

/// @brief Create a managed buffer.
/// @param gc The garbage collector to use.
/// @param size The size of the buffer *(in bytes)* to allocate.
/// @return A pointer to the allocated managed buffer.
bgc_Buffer * bgc_create_buffer(bgc_GC *gc, size_t size);

/// @brief Create a managed buffer.
/// @param gc The garbage collector to use.
/// @param size The size of the buffer *(in bytes)* to allocate.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed buffer.
bgc_Buffer * bgc_create_buffer_ext(bgc_GC *gc, size_t size, bgc_Deconstructor dtor);

/// @brief Create a managed array.
/// @param tsize The size of an item contained within the array.
/// @param count The number of items the managed array can hold.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed array.
#define bgcx_create_array_ext(T, count, dtor)           bgc_create_array_ext(BGC_GLOBAL_GC, sizeof(T), count, dtor)

/// @brief Create a managed array.
/// @param gc The garbage collector to use.
/// @param T The type of an item contained within the array.
/// @param count The number of items the managed array can hold.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed array.
#define bgcx_create_array_pro(gc, T, count, dtor)       bgc_create_array_ext(gc, sizeof(T), count, dtor)

/// @brief Create a managed array.
/// @param T The type of an item contained within the array.
/// @param count The number of items the managed array can hold.
/// @return A pointer to the allocated managed array.
#define bgcx_create_array(T, count)     bgc_create_array(BGC_GLOBAL_GC, sizeof(T), count)

/// @brief Destroy a managed array.
/// @param gc The garbage collector to use.
/// @param array The array to destroy.
void bgc_destroy_array(bgc_GC *gc, bgc_Array *array);

/// @brief Destroy a managed buffer.
/// @param gc The garbage collector to use.
/// @param buffer The buffer to destroy.
void bgc_destroy_buffer(bgc_GC *gc, bgc_Buffer *buffer);

/// @brief Destroy a managed array.
/// @param array The array to destroy.
#define bgcx_destroy_array(array)       bgc_destroy_array(BGC_GLOBAL_GC, array)

/// @brief Destroy a managed buffer.
/// @param buffer The buffer to destroy.
#define bgcx_destroy_buffer(buffer)     bgc_destroy_buffer(BGC_GLOBAL_GC, buffer)

// Core API macros

/// @brief Create a managed object.
/// @param gc The garbage collector to use.
/// @param T The type of the new object.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed object.
#define bgcx_new_ext(gc, T, dtor)       ((T *) bgc_malloc_ext(gc, sizeof(T), dtor))

/// @brief Create a managed object.
/// @param gc The garbage collector to use.
/// @param T The type of the new object.
/// @param dtor The deconstructor to call after freeing the managed memory.
/// @return A pointer to the allocated managed object.
#define bgcx_new(T)     bgcx_new_ext(BGC_GLOBAL_GC, T, NULL)

/// @brief Create a managed object and store it in a variable.
/// @param gc The garbage collector to use.
/// @param T The type of the new object.
/// @param name The name of the new variable.
/// @return A pointer to the allocated managed object.
#define bgcx_var_ext(gc, T, name, dtor)       T *name = bgcx_new_ext(gc, T, dtor)

/// @brief Create a managed object and store it in a variable.
/// @param T The type of the new object.
/// @param name The name of the new variable.
/// @return A pointer to the allocated managed object.
#define bgcx_var(T, name)       bgcx_var_ext(BGC_GLOBAL_GC, T, name, NULL)

// Auxilary API macros

/// @brief Begin the global garbage collector for all single-threaded applications.
#define bgcx_start()                    void *bgc__bp = malloc(sizeof(bgc_GC));\
                                        BGC_GLOBAL_GC = (bgc_GC *) bgc__bp;\
                                        bgc_start(BGC_GLOBAL_GC, &bgc__bp);\
                                        (void) 0
#define BGCX_BEGIN                      bgcx_start()

/// @brief Stop the global garbage collector for all single-threaded applications.
#define bgcx_stop()                     bgc_stop(BGC_GLOBAL_GC)
#define BGCX_END                        bgcx_stop()
#define bgcx_calloc(count, size)        bgc_calloc(BGC_GLOBAL_GC, count, size)
#define bgcx_free(ptr)                  (bgc_free(BGC_GLOBAL_GC, ptr))
#define bgcx_malloc(size)               bgc_malloc(BGC_GLOBAL_GC, size)
#define bgcx_carray(T, count)           bgcx_calloc(sizeof(T), count)
#define bgcx_free_array(T, array)       bgc_free_array(BGC_GLOBAL_GC, array)
#define bgcx_malloc_array(T, count)     bgc_malloc_array(BGC_GLOBAL_GC, sizeof(T), count)
#define bgcx_realloc(ptr, size)         bgc_realloc(BGC_GLOBAL_GC, ptr, size)

#define bgcx_create_stack()             void *_BGCX_STACK_BP = NULL
#define BGCX_CREATE_STACK               bgcx_create_stack()
#define bgcx_get_stack()                (&_BGCX_STACK_BP)
#define BGCX_STACK                      bgcx_get_stack()

// Auxilary API macros (exclusive to C)
#if !defined(__cplusplus)
#if !defined(new)
#define new(T)                  bgcx_new(T)
#endif // new
#if !defined(var)
#define var(T, name)            bgcx_var(T, name)
#endif // var
#endif // __cplusplus

#endif // BGC__BGC_H
