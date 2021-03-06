/**
 * @file mdv_jobber.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief This file contains functionality for jobs scheduling and deferred running.
 * @details Job scheduler work on thread pool.
 * @version 0.1
 * @date 2019-08-01
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 */
#pragma once
#include "mdv_threadpool.h"


/// Jobs scheduler configuration
typedef struct mdv_jobber_config
{
    mdv_threadpool_config   threadpool;     ///< thread pool options
    struct
    {
        size_t  count;                      ///< Job queues count
    } queue;                                ///< Job queue settings
} mdv_jobber_config;


/// Jobs scheduler
typedef struct mdv_jobber mdv_jobber;


/// Basic type for job
typedef struct mdv_job_base mdv_job_base;


/// Job function type
typedef void(*mdv_job_fn)(mdv_job_base *);


/// Job finalization function type
typedef void(*mdv_job_finalize_fn)(mdv_job_base *);


/// Basic type for job
struct mdv_job_base
{
    mdv_job_fn          fn;                     ///< Job function
    mdv_job_finalize_fn finalize;               ///< Job finalization function
    char *              data[1];                ///< Job data
};


/// Job definition macro
#define mdv_job(type)                       \
    struct                                  \
    {                                       \
        mdv_job_fn          fn;             \
        mdv_job_finalize_fn finalize;       \
        type                data;           \
    }


/**
 * @brief Create and start new job scheduler
 *
 * @param config [in] job scheduler configuration
 *
 * @return new job scheduler or NULL pointer if error occured
 */
mdv_jobber * mdv_jobber_create(mdv_jobber_config const *config);


/**
 * @brief Retains job scheduler.
 * @details Reference counter is increased by one.
 */
mdv_jobber * mdv_jobber_retain(mdv_jobber *jobber);


/**
 * @brief Releases job scheduler.
 * @details Reference counter is decreased by one.
 *          When the reference counter reaches zero, the job scheduler is stopped and freed.
 */
void mdv_jobber_release(mdv_jobber *jobber);


/**
 * @brief Push job for deferred asynchronous run.
 *
 * @param jobber [in]   job scheduler
 * @param job [in]      job for deferred asynchronous run.
 *
 * @return On success, return MDV_OK
 * @return On error, return nonzero error code
 */
mdv_errno mdv_jobber_push(mdv_jobber *jobber, mdv_job_base *job);
