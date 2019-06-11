#include "mdv_alloc.h"
#include "mdv_log.h"
#include <rpmalloc.h>


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


void *mdv_alloc(size_t size)
{
    void *ptr = rpmalloc(size);
    if (!ptr)
        MDV_LOGE("malloc(%zu) failed", size);
    MDV_LOGD("malloc: %p:%zu", ptr, size);
    return ptr;
}


void *mdv_aligned_alloc(size_t alignment, size_t size)
{
    void *ptr = rpaligned_alloc(alignment, size);
    if (!ptr)
        MDV_LOGE("aligned_alloc(%zu) failed", size);
    MDV_LOGD("aligned_alloc: %p:%zu", ptr, size);
    return ptr;
}


void mdv_free(void *ptr)
{
    MDV_LOGD("free: %p", ptr);
    rpfree(ptr);
}
