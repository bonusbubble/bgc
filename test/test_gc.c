#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include "minunit.h"

#include "../src/bgc.h"

#include "../src/libbgc.c"

#define UNUSED(x) (void)(x)

static size_t DTOR_COUNT = 0;

static char* test_primes()
{
    /*
     * Test a few known cases.
     */
    mu_assert(!is_prime(0), "Prime test failure for 0");
    mu_assert(!is_prime(1), "Prime test failure for 1");
    mu_assert(is_prime(2), "Prime test failure for 2");
    mu_assert(is_prime(3), "Prime test failure for 3");
    mu_assert(!is_prime(12742382), "Prime test failure for 12742382");
    mu_assert(is_prime(611953), "Prime test failure for 611953");
    mu_assert(is_prime(479001599), "Prime test failure for 479001599");
    return 0;
}

void dtor(void* ptr)
{
    UNUSED(ptr);
    DTOR_COUNT++;
}

static char* test_gc_allocation_new_delete()
{
    int* ptr = malloc(sizeof(int));
    bgc_Allocation* a = bgc_allocation_new(ptr, sizeof(int), dtor);
    mu_assert(a != NULL, "bgc_Allocation should return non-NULL");
    mu_assert(a->ptr == ptr, "bgc_Allocation should contain original pointer");
    mu_assert(a->size == sizeof(int), "Size of mem pointed to should not change");
    mu_assert(a->tag == BGC_TAG_NONE, "Annotation should initially be untagged");
    mu_assert(a->dtor == dtor, "Destructor pointer should not change");
    mu_assert(a->next == NULL, "Annotation should initilally be unlinked");
    bgc_allocation_delete(a);
    free(ptr);
    return NULL;
}


static char* test_gc_allocation_map_new_delete()
{
    /* Standard invocation */
    bgc_AllocationMap* am = bgc_allocation_map_new(8, 16, 0.5, 0.2, 0.8);
    mu_assert(am->min_capacity == 11, "True min capacity should be next prime");
    mu_assert(am->capacity == 17, "True capacity should be next prime");
    mu_assert(am->size == 0, "bgc_Allocation map should be initialized to empty");
    mu_assert(am->sweep_limit == 8, "Incorrect sweep limit calculation");
    mu_assert(am->downsize_factor == 0.2, "Downsize factor should not change");
    mu_assert(am->upsize_factor == 0.8, "Upsize factor should not change");
    mu_assert(am->allocs != NULL, "bgc_Allocation map must not have a NULL pointer");
    bgc_allocation_map_delete(am);

    /* Enforce min sizes */
    am = bgc_allocation_map_new(8, 4, 0.5, 0.2, 0.8);
    mu_assert(am->min_capacity == 11, "True min capacity should be next prime");
    mu_assert(am->capacity == 11, "True capacity should be next prime");
    mu_assert(am->size == 0, "bgc_Allocation map should be initialized to empty");
    mu_assert(am->sweep_limit == 5, "Incorrect sweep limit calculation");
    mu_assert(am->downsize_factor == 0.2, "Downsize factor should not change");
    mu_assert(am->upsize_factor == 0.8, "Upsize factor should not change");
    mu_assert(am->allocs != NULL, "bgc_Allocation map must not have a NULL pointer");
    bgc_allocation_map_delete(am);

    return NULL;
}


static char* test_gc_allocation_map_basic_get()
{
    bgc_AllocationMap* am = bgc_allocation_map_new(8, 16, 0.5, 0.2, 0.8);

    /* Ask for something that does not exist */
    int* five = malloc(sizeof(int));
    bgc_Allocation* a = bgc_allocation_map_get(am, five);
    mu_assert(a == NULL, "Empty allocation map must not contain any allocations");

    /* Create an entry and query it */
    *five = 5;
    a = bgc_allocation_map_put(am, five, sizeof(int), NULL);
    mu_assert(a != NULL, "Result of PUT on allocation map must be non-NULL");
    mu_assert(am->size == 1, "Expect size of one-element map to be one");
    mu_assert(am->allocs != NULL, "bgc_AllocationMap must hold list of allocations");
    bgc_Allocation* b = bgc_allocation_map_get(am, five);
    mu_assert(a == b, "Get should return the same result as put");
    mu_assert(a->ptr == b->ptr, "Pointers must not change between calls");
    mu_assert(b->ptr == five, "Get result should equal original pointer");

    /* Update the entry  and query */
    a = bgc_allocation_map_put(am, five, sizeof(int), dtor);
    mu_assert(am->size == 1, "Expect size of one-element map to be one");
    mu_assert(a->dtor == dtor, "Setting the dtor should set the dtor");
    b = bgc_allocation_map_get(am, five);
    mu_assert(b->dtor == dtor, "Failed to persist the dtor update");

    /* Delete the entry */
    bgc_allocation_map_remove(am, five, true);
    mu_assert(am->size == 0, "After removing last item, map should be empty");
    bgc_Allocation* c = bgc_allocation_map_get(am, five);
    mu_assert(c == NULL, "Empty allocation map must not contain any allocations");

    bgc_allocation_map_delete(am);
    free(five);
    return NULL;
}


static char* test_gc_allocation_map_put_get_remove()
{
    /* Create a few data pointers */
    int** ints = malloc(64*sizeof(int*));
    for (size_t i=0; i<64; ++i) {
        ints[i] = malloc(sizeof(int));
    }

    /* Enforce separate chaining by disallowing up/downsizing.
     * The pigeonhole principle then states that we need to have at least one
     * entry in the hash map that has a separare chain with len > 1
     */
    bgc_AllocationMap* am = bgc_allocation_map_new(32, 32, DBL_MAX, 0.0, DBL_MAX);
    bgc_Allocation* a;
    for (size_t i=0; i<64; ++i) {
        a = bgc_allocation_map_put(am, ints[i], sizeof(int), NULL);
    }
    mu_assert(am->size == 64, "Maps w/ 64 elements should have size 64");
    /* Now update all of them with a new dtor */
    for (size_t i=0; i<64; ++i) {
        a = bgc_allocation_map_put(am, ints[i], sizeof(int), dtor);
    }
    mu_assert(am->size == 64, "Maps w/ 64 elements should have size 64");
    /* Now delete all of them again */
    for (size_t i=0; i<64; ++i) {
        bgc_allocation_map_remove(am, ints[i], true);
    }
    mu_assert(am->size == 0, "Empty map must have size 0");
    /* And delete the entire map */
    bgc_allocation_map_delete(am);

    /* Clean up the data pointers */
    for (size_t i=0; i<64; ++i) {
        free(ints[i]);
    }
    free(ints);

    return NULL;
}

static char* test_gc_allocation_map_cleanup()
{
    /* Make sure that the entries in the allocation map get reset
     * to NULL when we delete things. This is required for the
     * chunk != NULL checks when iterating over the items in the hash map.
     */
    DTOR_COUNT = 0;
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start_ext(&gc, stack_bp, 32, 32, 0.0, DBL_MAX, DBL_MAX);

    /* run a few alloc/free cycles */
    int** ptrs = bgc_malloc_ext(&gc, 64*sizeof(int*), dtor);
    for (size_t j=0; j<8; ++j) {
        for (size_t i=0; i<64; ++i) {
            ptrs[i] = bgc_malloc(&gc, i*sizeof(int));
        }
        for (size_t i=0; i<64; ++i) {
            bgc_free(&gc, ptrs[i]);
        }
    }
    bgc_free(&gc, ptrs);
    mu_assert(DTOR_COUNT == 1, "Failed to call destructor for array");
    DTOR_COUNT = 0;

    /* now make sure that all allocation entries are NULL */
    for (size_t i = 0; i < gc.allocs->capacity; ++i) {
        mu_assert(gc.allocs->allocs[i] == NULL, "Deleted allocs should be reset to NULL");
    }
    bgc_stop(&gc);
    return NULL;
}


static char* test_gc_mark_stack()
{
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start_ext(&gc, stack_bp, 32, 32, 0.0, DBL_MAX, DBL_MAX);
    bgc_disable(&gc);

    /* Part 1: Create an object on the heap, reference from the stack,
     * and validate that it gets marked. */
    int** five_ptr = bgc_calloc(&gc, 2, sizeof(int*));
    bgc_mark_stack(&gc);
    bgc_Allocation* a = bgc_allocation_map_get(gc.allocs, five_ptr);
    mu_assert(a->tag & BGC_TAG_MARK, "Heap allocation referenced from stack should be tagged");

    /* manually reset the tags */
    a->tag = BGC_TAG_NONE;

    /* Part 2: Add dependent allocations and check if these allocations
     * get marked properly*/
    five_ptr[0] = bgc_malloc(&gc, sizeof(int));
    *five_ptr[0] = 5;
    five_ptr[1] = bgc_malloc(&gc, sizeof(int));
    *five_ptr[1] = 5;
    bgc_mark_stack(&gc);
    a = bgc_allocation_map_get(gc.allocs, five_ptr);
    mu_assert(a->tag & BGC_TAG_MARK, "Referenced heap allocation should be tagged");
    for (size_t i=0; i<2; ++i) {
        a = bgc_allocation_map_get(gc.allocs, five_ptr[i]);
        mu_assert(a->tag & BGC_TAG_MARK, "Dependent heap allocs should be tagged");
    }

    /* Clean up the tags manually */
    a = bgc_allocation_map_get(gc.allocs, five_ptr);
    a->tag = BGC_TAG_NONE;
    for (size_t i=0; i<2; ++i) {
        a = bgc_allocation_map_get(gc.allocs, five_ptr[i]);
        a->tag = BGC_TAG_NONE;
    }

    /* Part3: Now delete the pointer to five_ptr[1] which should
     * leave the allocation for five_ptr[1] unmarked. */
    bgc_Allocation* unmarked_alloc = bgc_allocation_map_get(gc.allocs, five_ptr[1]);
    five_ptr[1] = NULL;
    bgc_mark_stack(&gc);
    a = bgc_allocation_map_get(gc.allocs, five_ptr);
    mu_assert(a->tag & BGC_TAG_MARK, "Referenced heap allocation should be tagged");
    a = bgc_allocation_map_get(gc.allocs, five_ptr[0]);
    mu_assert(a->tag & BGC_TAG_MARK, "Referenced alloc should be tagged");
    mu_assert(unmarked_alloc->tag == BGC_TAG_NONE, "Unreferenced alloc should not be tagged");

    /* Clean up the tags manually, again */
    a = bgc_allocation_map_get(gc.allocs, five_ptr[0]);
    a->tag = BGC_TAG_NONE;
    a = bgc_allocation_map_get(gc.allocs, five_ptr);
    a->tag = BGC_TAG_NONE;

    bgc_stop(&gc);
    return NULL;
}


static char* test_gc_basic_alloc_free()
{
    /* Create an array of pointers to an int. Then delete the pointer to
     * the containing array and check if all the contained allocs are garbage
     * collected.
     */
    DTOR_COUNT = 0;
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start_ext(&gc, stack_bp, 32, 32, 0.0, DBL_MAX, DBL_MAX);

    int** ints = bgc_calloc(&gc, 16, sizeof(int*));
    bgc_Allocation* a = bgc_allocation_map_get(gc.allocs, ints);
    mu_assert(a->size == 16*sizeof(int*), "Wrong allocation size");

    for (size_t i=0; i<16; ++i) {
        ints[i] = bgc_malloc_ext(&gc, sizeof(int), dtor);
        *ints[i] = 42;
    }
    mu_assert(gc.allocs->size == 17, "Wrong allocation map size");

    /* Test that all managed allocations get tagged if the root is present */
    bgc_mark(&gc);
    for (size_t i=0; i < gc.allocs->capacity; ++i) {
        bgc_Allocation* chunk = gc.allocs->allocs[i];
        while (chunk) {
            mu_assert(chunk->tag & BGC_TAG_MARK, "Referenced allocs should be marked");
            // reset for next test
            chunk->tag = BGC_TAG_NONE;
            chunk = chunk->next;
        }
    }

    /* Now drop the root allocation */
    ints = NULL;
    bgc_mark(&gc);

    /* Check that none of the allocations get tagged */
    size_t total = 0;
    for (size_t i=0; i < gc.allocs->capacity; ++i) {
        bgc_Allocation* chunk = gc.allocs->allocs[i];
        while (chunk) {
            mu_assert(!(chunk->tag & BGC_TAG_MARK), "Unreferenced allocs should not be marked");
            total += chunk->size;
            chunk = chunk->next;
        }
    }
    mu_assert(total == 16 * sizeof(int) + 16 * sizeof(int*),
              "Expected number of managed bytes is off");

    size_t n = bgc_sweep(&gc);
    mu_assert(n == total, "Wrong number of collected bytes");
    mu_assert(DTOR_COUNT == 16, "Failed to call destructor");
    DTOR_COUNT = 0;
    bgc_stop(&gc);
    return NULL;
}

static void _create_static_allocs(bgc_GC* gc,
                                  size_t count,
                                  size_t size)
{
    for (size_t i=0; i<count; ++i) {
        void* p = bgc_malloc_static(gc, size, dtor);
        memset(p, 0, size);
    }
}

static char* test_gc_static_allocation()
{
    DTOR_COUNT = 0;
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start(&gc, stack_bp);
    /* allocate a bunch of static vars in a deeper stack frame */
    size_t N = 256;
    _create_static_allocs(&gc, N, 512);
    /* make sure they are not garbage collected */
    size_t collected = bgc_collect(&gc);
    mu_assert(collected == 0, "Static objects should not be collected");
    /* remove the root tag from the roots on the heap */
    bgc_unroot_roots(&gc);
    /* run the mark phase */
    bgc_mark_roots(&gc);
    /* Check that none of the allocations were tagged. */
    size_t total = 0;
    size_t n = 0;
    for (size_t i=0; i < gc.allocs->capacity; ++i) {
        bgc_Allocation* chunk = gc.allocs->allocs[i];
        while (chunk) {
            mu_assert(!(chunk->tag & BGC_TAG_MARK), "Marked an unused alloc");
            mu_assert(!(chunk->tag & BGC_TAG_ROOT), "Unrooting failed");
            total += chunk->size;
            n++;
            chunk = chunk->next;
        }
    }
    mu_assert(n == N, "Expected number of allocations is off");
    mu_assert(total == N*512, "Expected number of managed bytes is off");
    /* make sure we collect everything */
    collected = bgc_sweep(&gc);
    mu_assert(collected == N*512, "Unexpected number of bytes");
    mu_assert(DTOR_COUNT == N, "Failed to call destructor");
    DTOR_COUNT = 0;
    bgc_stop(&gc);
    return NULL;
}

static char* test_gc_realloc()
{
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start(&gc, stack_bp);

    /* manually allocate some memory */
    {
        void *unmarked = malloc(sizeof(char));
        void *re_unmarked = bgc_realloc(&gc, unmarked, sizeof(char) * 2);
        mu_assert(!re_unmarked, "GC should not realloc pointers unknown to it");
        free(unmarked);
    }

    /* reallocing NULL pointer */
    {
        void *unmarked = NULL;
        void *re_marked = bgc_realloc(&gc, unmarked, sizeof(char) * 42);
        mu_assert(re_marked, "GC should not realloc NULL pointers");
        bgc_Allocation* a = bgc_allocation_map_get(gc.allocs, re_marked);
        mu_assert(a->size == 42, "Wrong allocation size");
    }

    /* realloc a valid pointer with same size to enforce same pointer is used*/
    {
        int** ints = bgc_calloc(&gc, 16, sizeof(int*));
        ints = bgc_realloc(&gc, ints, 16*sizeof(int*));
        bgc_Allocation* a = bgc_allocation_map_get(gc.allocs, ints);
        mu_assert(a->size == 16*sizeof(int*), "Wrong allocation size");
    }

    /* realloc with size greater than before */
    {
        int** ints = bgc_calloc(&gc, 16, sizeof(int*));
        ints = bgc_realloc(&gc, ints, 42*sizeof(int*));
        bgc_Allocation* a = bgc_allocation_map_get(gc.allocs, ints);
        mu_assert(a->size == 42*sizeof(int*), "Wrong allocation size");
    }

    bgc_stop(&gc);
    return NULL;
}

static void _create_allocs(bgc_GC* gc,
                           size_t count,
                           size_t size)
{
    for (size_t i=0; i<count; ++i) {
        bgc_malloc(gc, size);
    }
}
#include <stdio.h>
static char* test_gc_disable_enable()
{
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start(&gc, stack_bp);
    /* allocate a bunch of vars in a deeper stack frame */
    size_t N = 32;
    _create_allocs(&gc, N, 8);
    /* make sure they are garbage collected after a  disable->enable cycle */
    bgc_disable(&gc);
    mu_assert(gc.disabled, "GC should be disabled after pausing");
    bgc_enable(&gc);

    /* Avoid dumping the registers on the stack to make test less flaky */
    bgc_mark_roots(&gc);
    bgc_mark_stack(&gc);
    size_t collected = bgc_sweep(&gc);

    bool success = collected == N*8;
    // bool success = collected == N*8 || N*8 - collected == 8;

    mu_assert(success, "Unexpected number of collected bytes in disable/enable");
    bgc_stop(&gc);
    return NULL;
}

static char* duplicate_string(bgc_GC* gc, char* str)
{
    char* copy = (char*) bgc_strdup(gc, str);
    mu_assert(strncmp(str, copy, 16) == 0, "Strings should be equal");
    return NULL;
}

char* test_gc_strdup()
{
    bgc_GC gc;
    void *stack_bp = __builtin_frame_address(0);
    bgc_start(&gc, stack_bp);
    char* str = "This is a string";
    char* error = duplicate_string(&gc, str);
    mu_assert(error == NULL, "Duplication failed"); // cascade minunit tests
    size_t collected = bgc_collect(&gc);
    mu_assert(collected == 17, "Unexpected number of collected bytes in strdup");
    bgc_stop(&gc);
    return NULL;
}

/*
 * Test runner
 */

int tests_run = 0;

static char* test_suite()
{
    printf("---=[ GC tests\n");
    mu_run_test(test_gc_allocation_new_delete);
    mu_run_test(test_gc_allocation_map_new_delete);
    mu_run_test(test_gc_allocation_map_basic_get);
    mu_run_test(test_gc_allocation_map_put_get_remove);
    mu_run_test(test_gc_mark_stack);
    mu_run_test(test_gc_basic_alloc_free);
    mu_run_test(test_gc_allocation_map_cleanup);
    mu_run_test(test_gc_static_allocation);
    mu_run_test(test_primes);
    mu_run_test(test_gc_realloc);
    mu_run_test(test_gc_disable_enable);
    mu_run_test(test_gc_strdup);
    return 0;
}

int main()
{
    char *result = test_suite();
    if (result) {
        printf("%s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    printf("Tests run: %d\n", tests_run);
    return result != 0;
}
