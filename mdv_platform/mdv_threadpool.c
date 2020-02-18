#include "mdv_threadpool.h"
#include "mdv_eventfd.h"
#include "mdv_threads.h"
#include "mdv_mutex.h"
#include "mdv_alloc.h"
#include "mdv_log.h"
#include "mdv_rollbacker.h"
#include "mdv_hashmap.h"
#include <string.h>


/// @cond Doxygen_Suppress

enum
{
    MDV_TP_QUEUEFD_SIZE = 1024      /// Events queue size per thread in thread pool.
};

/// @endcond


/// Thread pool descriptor
struct mdv_threadpool
{
    mdv_threadpool_config   config;         ///< Thread pool configuration
    mdv_descriptor          stopfd;         ///< eventfd for thread pool stop notification
    mdv_descriptor          epollfd;        ///< epoll file descriptor
    mdv_thread             *threads;        ///< threads array
    mdv_mutex               tasks_mtx;      ///< mutex for tasks protection
    mdv_hashmap            *tasks;          ///< file descriptors and handlers
    uint8_t                 data_space[1];  ///< data space
};


static void * mdv_threadpool_worker(void *arg)
{
    mdv_threadpool *threadpool = (mdv_threadpool *)arg;

    int work = 1;

    while(work)
    {
        uint32_t size = 64;
        mdv_epoll_event events[size];

        mdv_errno err = mdv_epoll_wait(threadpool->epollfd, events, &size, -1);

        if (err != MDV_OK)
            continue;

        for(uint32_t i = 0; i < size; ++i)
        {
            mdv_threadpool_task_base *task = (mdv_threadpool_task_base *)events[i].data;

            if(task->fd == threadpool->stopfd)
            {
                work = 0;
                break;
            }

            if (task->fn)
                task->fn(events[i].events, task);
            else
                MDV_LOGE("Thread pool task without handler skipped");
        }
    }

    return 0;
}


static mdv_errno mdv_threadpool_add_default_tasks(mdv_threadpool *threadpool)
{
    mdv_threadpool_task_base task =
    {
        .fd = threadpool->stopfd,
        .fn = 0,
        .context_size = 0
    };

    return mdv_threadpool_add(threadpool, MDV_EPOLLIN | MDV_EPOLLERR, &task) != 0
            ? MDV_OK
            : MDV_FAILED;
}


mdv_threadpool * mdv_threadpool_create(mdv_threadpool_config const *config)
{
    mdv_rollbacker *rollbacker = mdv_rollbacker_create(4);

    size_t const mem_size = offsetof(mdv_threadpool, data_space) + config->size * sizeof(mdv_thread);

    mdv_threadpool *tp = (mdv_threadpool *)mdv_alloc(mem_size, "threadpool");

    if(!tp)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    memset(tp->data_space, 0, mem_size - offsetof(mdv_threadpool, data_space));

    tp->config = *config;

    mdv_rollbacker_push(rollbacker, mdv_free, tp, "threadpool");


    tp->epollfd = mdv_epoll_create();

    if (tp->epollfd == MDV_INVALID_DESCRIPTOR)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_epoll_close, tp->epollfd);


    tp->stopfd = mdv_eventfd(true);

    if (tp->stopfd == MDV_INVALID_DESCRIPTOR)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_eventfd_close, tp->stopfd);


    if (mdv_mutex_create(&tp->tasks_mtx) != MDV_OK)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &tp->tasks_mtx);


    tp->tasks = mdv_hashmap_create(mdv_threadpool_task_base,
                                   fd,
                                   256,
                                   mdv_descriptor_hash,
                                   mdv_descriptor_cmp);

    if (!tp->tasks)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollback(rollbacker);
        return 0;
    }


    if (mdv_threadpool_add_default_tasks(tp) != MDV_OK)
    {
        MDV_LOGE("threadpool_create failed");
        mdv_rollbacker_free(rollbacker);
        mdv_threadpool_free(tp);
        return 0;
    }


    tp->threads = (mdv_thread*)tp->data_space;

    for(size_t i = 0; i < config->size; ++i)
    {
        mdv_errno err = mdv_thread_create(tp->threads + i, &config->thread_attrs, mdv_threadpool_worker, tp);
        if (err != MDV_OK)
        {
            MDV_LOGE("threadpool_create failed");
            mdv_rollbacker_free(rollbacker);
            mdv_threadpool_stop(tp);
            mdv_threadpool_free(tp);
            return 0;
        }
    }

    mdv_rollbacker_free(rollbacker);

    return tp;
}


void mdv_threadpool_stop(mdv_threadpool *threadpool)
{
    uint64_t data = 0xDEADFA11;
    size_t len = sizeof data;

    while(mdv_write(threadpool->stopfd, &data, &len) == MDV_EAGAIN)
    {
        len = sizeof data;
    }

    for(size_t i = 0; i < threadpool->config.size; ++i)
    {
        if (threadpool->threads[i])
        {
            mdv_errno err = mdv_thread_join(threadpool->threads[i]);
            if (err != MDV_OK)
            {
                char err_msg[128];
                MDV_LOGE("Thread join failed with error '%s' (%d)", mdv_strerror(err, err_msg, sizeof err_msg), err);
            }
        }
    }
}


void mdv_threadpool_free(mdv_threadpool *threadpool)
{
    if (threadpool)
    {
        mdv_epoll_close(threadpool->epollfd);
        mdv_eventfd_close(threadpool->stopfd);
        mdv_hashmap_release(threadpool->tasks);
        mdv_mutex_free(&threadpool->tasks_mtx);
        mdv_free(threadpool, "threadpool");
    }
}


mdv_threadpool_task_base * mdv_threadpool_add(mdv_threadpool *threadpool, uint32_t events, mdv_threadpool_task_base const *task)
{
    mdv_threadpool_task_base *ret = 0;

    if(mdv_mutex_lock(&threadpool->tasks_mtx) == MDV_OK)
    {
        size_t const size = offsetof(mdv_threadpool_task_base, context)
                            + task->context_size;

        mdv_threadpool_task_base *entry = mdv_hashmap_insert(threadpool->tasks, task, size);

        if (entry)
        {
            mdv_epoll_event evt = { events, entry };

            mdv_errno err = mdv_epoll_add(threadpool->epollfd, task->fd, evt);

            if (err == MDV_OK)
                ret = entry;
            else
            {
                mdv_hashmap_erase(threadpool->tasks, &task->fd);

                char err_msg[128];
                MDV_LOGE("Threadpool task registration failed with error '%s' (%d)",
                                        mdv_strerror(err, err_msg, sizeof err_msg), err);
            }
        }
        else
            MDV_LOGE("Threadpool task registration failed");

        mdv_mutex_unlock(&threadpool->tasks_mtx);
    }
    else
        MDV_LOGE("Threadpool task registration failed");

    return ret;
}


mdv_errno mdv_threadpool_rearm(mdv_threadpool *threadpool, uint32_t events, mdv_threadpool_task_base *task)
{
    mdv_epoll_event evt = { events, task };

    mdv_errno err = mdv_epoll_mod(threadpool->epollfd, task->fd, evt);

    if (err != MDV_OK)
    {
        char err_msg[128];
        MDV_LOGE("Threadpool task rearming failed with error '%s' (%d)",
                            mdv_strerror(err, err_msg, sizeof err_msg), err);
    }

    return err;
}


mdv_errno mdv_threadpool_remove(mdv_threadpool *threadpool, mdv_descriptor fd)
{
    mdv_errno err = mdv_mutex_lock(&threadpool->tasks_mtx);

    if(err == MDV_OK)
    {
        err = mdv_epoll_del(threadpool->epollfd, fd);

        if (err == MDV_OK)
        {
            mdv_hashmap_erase(threadpool->tasks, &fd);
        }
        else
        {
            char err_msg[128];
            MDV_LOGE("Threadpool task removing failed with error '%s' (%d)",
                                mdv_strerror(err, err_msg, sizeof err_msg), err);
        }

        mdv_mutex_unlock(&threadpool->tasks_mtx);
    }
    else
        MDV_LOGE("Threadpool task removing failed");

    return err;
}


mdv_errno mdv_threadpool_foreach(mdv_threadpool *threadpool, void (*fn)(mdv_descriptor fd, void *context))
{
    mdv_errno err = mdv_mutex_lock(&threadpool->tasks_mtx);

    if(err == MDV_OK)
    {
        mdv_hashmap_foreach(threadpool->tasks, mdv_threadpool_task_base, entry)
        {
            if (entry->fd != threadpool->stopfd)
                fn(entry->fd, entry->context_size ? entry->context : 0);
        }

        mdv_mutex_unlock(&threadpool->tasks_mtx);
    }

    return err;
}

