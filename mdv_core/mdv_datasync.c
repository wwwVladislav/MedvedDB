#include "mdv_datasync.h"
#include <mdv_alloc.h>
#include <mdv_errno.h>
#include <mdv_log.h>
#include <mdv_rollbacker.h>


mdv_errno mdv_datasync_create(mdv_datasync *datasync, mdv_datasync_config const *config)
{
    mdv_rollbacker(2) rollbacker;
    mdv_rollbacker_clear(rollbacker);

    datasync->tablespace = config->tablespace;

    mdv_errno err = mdv_mutex_create(&datasync->mutex);

    if (err != MDV_OK)
    {
        MDV_LOGE("Mutex creation failed with error %d", err);
        return err;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &datasync->mutex);

    datasync->threadpool = mdv_threadpool_create(&config->threadpool);

    if (!datasync->threadpool)
    {
        MDV_LOGE("Threadpool creation failed");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_threadpool_free, datasync->threadpool);

    if (!mdv_vector_create(datasync->routes, 64, mdv_default_allocator))
    {
        MDV_LOGE("No memorty for routes");
        mdv_rollback(rollbacker);
        return MDV_NO_MEM;
    }

    return MDV_OK;
}


void mdv_datasync_free(mdv_datasync *datasync)
{
    // Stop thread pool
    mdv_threadpool_stop(datasync->threadpool);

    // Free thread pool
    mdv_threadpool_free(datasync->threadpool);

    mdv_vector_free(datasync->routes);

    mdv_mutex_free(&datasync->mutex);
}


mdv_errno mdv_datasync_update_routes(mdv_datasync *datasync, mdv_tracker *tracker)
{
    mdv_routes routes;

    if (!mdv_vector_create(routes, 64, mdv_default_allocator))
    {
        MDV_LOGE("No memorty for routes");
        return MDV_NO_MEM;
    }

    mdv_errno err = mdv_routes_find(&routes, tracker);

    if (err != MDV_OK)
    {
        mdv_vector_free(routes);
        return err;
    }

    if (mdv_mutex_lock(&datasync->mutex) == MDV_OK)
    {
        mdv_vector_free(datasync->routes);
        datasync->routes = routes;

        MDV_LOGD("New routes count=%zu", mdv_vector_size(routes));

        for(size_t i = 0; i < mdv_vector_size(routes); ++i)
        {
            MDV_LOGD("Route[%zu]: %u", i, routes.data[i]);
        }

        mdv_mutex_unlock(&datasync->mutex);
        return MDV_OK;
    }

    return MDV_FAILED;
}


bool mdv_datasync_do(mdv_datasync *datasync)
{
    // TODO:
    return false;
}