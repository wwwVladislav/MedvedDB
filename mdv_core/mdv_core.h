/**
 * @file mdv_core.h
 * @author Vladislav Volkov (wwwvladislav@gmail.com)
 * @brief Core component for cluster nodes management and storage accessing.
 * @version 0.1
 * @date 2019-07-31
 *
 * @copyright Copyright (c) 2019, Vladislav Volkov
 *
 */
#pragma once
#include <mdv_cluster.h>
#include <mdv_jobber.h>
#include "storage/mdv_metainf.h"
#include "storage/mdv_tablespace.h"


/// Core component for cluster nodes management and storage accessing.
typedef struct mdv_core
{
    mdv_jobber     *jobber;             ///< Jobs scheduler
    mdv_cluster     cluster;            ///< Cluster manager
    mdv_metainf     metainf;            ///< Metainformation (DB version, node UUID etc.)

    struct
    {
        mdv_storage    *metainf;        ///< Metainformation storage
        mdv_tablespace  tablespace;     ///< Tables strorage
    } storage;                          ///< Storages
} mdv_core;


/**
 * @brief Create new core.
 *
 * @param core [out] Pointer to a core to be initialized.
 *
 * @return true if core successfully created
 * @return false if error is occurred
 */
bool mdv_core_create(mdv_core *core);


/**
 * @brief Frees resources used by core.
 *
 * @param core [in] core
 */
void mdv_core_free(mdv_core *core);


/**
 * @brief Listen incomming connections
 *
 * @param core [in] core
 *
 * @return true if listener successfully started
 * @return false if error is occurred
 */
bool mdv_core_listen(mdv_core *core);


/**
 * @brief Connect to other peers
 *
 * @param core [in] core
 */
void mdv_core_connect(mdv_core *core);


/**
 * @brief Register peer connection
 *
 * @param core [in]             core
 * @param new_node [in] [out]   node information
 *
 * @return On success, return MDV_OK and node->id is initialized by local unique numeric identifier
 * @return On error, return nonzero error code
 */
mdv_errno mdv_core_peer_connected(mdv_core *core, mdv_node *new_node);


/**
 * @brief Mark peer node as disconnected
 * @details Node is not deleted but only is marked as disconnected.
 *
 * @param core [in]     core
 * @param uuid [in]     node UUID
 */
void mdv_core_peer_disconnected(mdv_core *core, mdv_uuid const *uuid);
