/**
 * @file
 * @brief Channels manager
 * @details Channels manager (chaman) is used for incoming and outgoing peers connections handling.
 */
#pragma once
#include "mdv_errno.h"
#include "mdv_string.h"
#include "mdv_threadpool.h"
#include "mdv_def.h"


/// Channels manager descriptor
typedef struct mdv_chaman mdv_chaman;


/// channel initialization handler type
typedef void (*mdv_channel_init_fn)(void *userdata, void *context, mdv_descriptor fd);


/// data receiving handler type
typedef mdv_errno (*mdv_channel_recv_fn)(void *userdata, void *context, mdv_descriptor fd);


/// channel closing handler type
typedef void (*mdv_channel_close_fn)(void *userdata, void *context);


/// Channels manager configuration. All options are mandatory.
typedef struct
{
    struct
    {
        uint32_t    keepidle;           ///< Start keeplives after this period (in seconds)
        uint32_t    keepcnt;            ///< Number of keepalives before death
        uint32_t    keepintvl;          ///< Interval between keepalives (in seconds)
    } peer;                             ///< peer configuration

    mdv_threadpool_config   threadpool; ///< thread pool options

    void                   *userdata;   ///< userdata passed to channel hadlers

    struct
    {
        struct
        {
            size_t      size;           ///< channel context size
            size_t      guardsize;      ///< context guard size (small buffer at the end of context)
        } context;                      ///< context configuration

        mdv_channel_init_fn  init;      ///< channel initialization handler
        mdv_channel_recv_fn  recv;      ///< data receiving handler
        mdv_channel_close_fn close;     ///< channel closing handler
    } channel;                          ///< channel configuration
} mdv_chaman_config;


/**
 * @brief Create new channels manager
 *
 * @param config [in] channels manager configuration
 *
 * @return On success, return pointer to a new created channels manager
 * @return On error, return NULL
 */
mdv_chaman * mdv_chaman_create(mdv_chaman_config const *config);


/**
 * @brief Stop and free channels manager
 *
 * @param chaman [in] channels manager
 */
void mdv_chaman_free(mdv_chaman *chaman);


/**
 * @brief Register new listener
 *
 * @param chaman [in] channels manager
 * @param addr [in] address for listening in following format protocol://host:port.
 *
 * @return On success, return MDV_OK
 * @return On error, return non zero value
 */
mdv_errno mdv_chaman_listen(mdv_chaman *chaman, mdv_string const addr);


/**
 * @brief Register new peer
 *
 * @details Channels manager periodically tries to connect to this peer.
 *
 * @param chaman [in] channels manager
 * @param addr [in] peer address
 *
 * @return On success, return MDV_OK
 * @return On error, return non zero value
 */
mdv_errno mdv_chaman_connect(mdv_chaman *chaman, mdv_string const addr);

