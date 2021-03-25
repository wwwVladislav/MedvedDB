#include "mdv_alloc.h"
#include <mdv_log.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>


#define MDV_COUNT_ALLOCS_STAT
//#define MDV_USE_STD_ALLOC


#ifdef MDV_USE_STD_ALLOC

#define rpmalloc_initialize() 0
#define rpmalloc_finalize()
#define rpmalloc_thread_initialize()
#define rpmalloc_thread_finalize()
#define rpmalloc(size)                      malloc(size)
#define rpaligned_alloc(alignment, size)    aligned_alloc(alignment, size)
#define rprealloc(ptr, size)                realloc(ptr, size)
#define rpfree(ptr)                         free(ptr)

#else

#include <rpmalloc.h>

#endif


#ifdef MDV_COUNT_ALLOCS_STAT

    static atomic_int MDV_ALLOCS = 0;

    #define MDV_ALLOCATION(fn, ptr, size, name)                                                         \
        {                                                                                               \
            int allocs_counter = atomic_fetch_add_explicit(&MDV_ALLOCS, 1, memory_order_relaxed);       \
            MDV_LOGD("%s(%p:%zu) '%s' (allocations: %d)", #fn, ptr, size, name, allocs_counter + 1);    \
        }

    #define MDV_REALLOCATION(fn, ptr, size, name)                                                       \
        {                                                                                               \
            int allocs_counter = atomic_load_explicit(&MDV_ALLOCS, memory_order_relaxed);               \
            MDV_LOGD("%s(%p:%zu) '%s' (allocations: %d)", #fn, ptr, size, name, allocs_counter);        \
        }

    #define MDV_DEALLOCATION(fn, ptr, name)                                                             \
        {                                                                                               \
            int allocs_counter = atomic_fetch_sub_explicit(&MDV_ALLOCS, 1, memory_order_relaxed);       \
            MDV_LOGD("%s(%p) '%s' (allocations: %d)", #fn, ptr, name, allocs_counter - 1);              \
        }

#else

    #define MDV_ALLOCATION(fn, ptr, size, name)             \
        MDV_LOGD("%s(%p:%zu) '%s'", #fn, ptr, size, name);

    #define MDV_REALLOCATION(fn, ptr, size, name)           \
        MDV_LOGD("%s(%p:%zu) '%s'", #fn, ptr, size, name);

    #define MDV_DEALLOCATION(fn, ptr, name)                 \
            MDV_LOGD("%s(%p) '%s'", #fn, ptr, name);

#endif


int mdv_alloc_initialize()
{
    return rpmalloc_initialize();
}


void mdv_alloc_finalize()
{
    rpmalloc_finalize();
}


void mdv_alloc_thread_initialize()
{
    rpmalloc_thread_initialize();
}


void mdv_alloc_thread_finalize()
{
    rpmalloc_thread_finalize();
}


void * mdv_alloc(size_t size, char const *name)
{
    void *ptr = rpmalloc(size);
    if (!ptr)
        MDV_LOGE("malloc(%zu) failed", size);
    else
        MDV_ALLOCATION(malloc, ptr, size, name);
    return ptr;
}


void * mdv_aligned_alloc(size_t alignment, size_t size, char const *name)
{
    void *ptr = rpaligned_alloc(alignment, size);
    if (!ptr)
        MDV_LOGE("aligned_alloc(%zu) failed", size);
    else
        MDV_ALLOCATION(aligned_alloc, ptr, size, name);
    return ptr;
}


void * mdv_realloc(void *ptr, size_t size, char const *name)
{
    void *new_ptr = rprealloc(ptr, size);
    if (!new_ptr)
        MDV_LOGE("realloc(%p, %zu) failed", ptr, size);
    else
        MDV_REALLOCATION(realloc, new_ptr, size, name);
    return new_ptr;
}


void mdv_free(void *ptr, char const *name)
{
    if (ptr)
    {
        rpfree(ptr);
        MDV_DEALLOCATION(free, ptr, name);
    }
}


#ifdef MDV_COUNT_ALLOCS_STAT
int mdv_allocations()
{
    return atomic_load_explicit(&MDV_ALLOCS, memory_order_relaxed);
}
#else
int mdv_allocations()
{
    return 0;
}
#endif


mdv_allocator const mdv_default_allocator =
{
    .alloc      = &mdv_alloc,
    .realloc    = &mdv_realloc,
    .free       = &mdv_free
};