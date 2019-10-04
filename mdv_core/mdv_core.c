#include "mdv_core.h"
#include "mdv_user.h"
#include "mdv_peer.h"
#include "mdv_config.h"
#include "mdv_p2pmsg.h"
#include "mdv_gossip.h"
#include "storage/async/mdv_nodes.h"
#include <mdv_log.h>
#include <mdv_messages.h>
#include <mdv_ctypes.h>
#include <mdv_rollbacker.h>


static bool mdv_core_cluster_create(mdv_core *core)
{
    mdv_conctx_config const conctx_configs[] =
    {
        { MDV_CLI_USER, sizeof(mdv_user), &mdv_user_init, &mdv_user_free },
        { MDV_CLI_PEER, sizeof(mdv_peer), &mdv_peer_init, &mdv_peer_free }
    };

    mdv_cluster_config const cluster_config =
    {
        .uuid = core->metainf.uuid.value,
        .channel =
        {
            .keepidle  = MDV_CONFIG.connection.keep_idle,
            .keepcnt   = MDV_CONFIG.connection.keep_count,
            .keepintvl = MDV_CONFIG.connection.keep_interval
        },
        .threadpool =
        {
            .size = MDV_CONFIG.server.workers,
            .thread_attrs =
            {
                .stack_size = MDV_THREAD_STACK_SIZE
            }
        },
        .conctx =
        {
            .userdata = core,
            .size     = sizeof conctx_configs / sizeof *conctx_configs,
            .configs  = conctx_configs
        }
    };

    if (mdv_cluster_create(&core->cluster, &cluster_config) != MDV_OK)
    {
        MDV_LOGE("Cluster manager creation failed");
        return false;
    }

    // Load cluster nodes
    if (mdv_nodes_load(core->storage.metainf, core->tracker) != MDV_OK)
    {
        MDV_LOGE("Nodes loading failed");
        mdv_cluster_free(&core->cluster);
        return false;
    }

    return true;
}


bool mdv_core_create(mdv_core *core)
{
    mdv_rollbacker *rollbacker = mdv_rollbacker_create(7);

    // DB meta information storage
    core->storage.metainf = mdv_metainf_storage_open(MDV_CONFIG.storage.path.ptr);

    if (!core->storage.metainf)
    {
        MDV_LOGE("Service initialization failed. Can't create metainf storage '%s'", MDV_CONFIG.storage.path.ptr);
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_storage_release, core->storage.metainf);


    if (!mdv_metainf_load(&core->metainf, core->storage.metainf))
    {
        MDV_LOGE("DB meta information loading failed. Path: '%s'", MDV_CONFIG.storage.path.ptr);
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_metainf_validate(&core->metainf);

    mdv_metainf_flush(&core->metainf, core->storage.metainf);


    // Tablespace
    if (mdv_tablespace_open(&core->storage.tablespace, &core->metainf.uuid.value) != MDV_OK)
    {
        MDV_LOGE("DB tables space creation failed. Path: '%s'", MDV_CONFIG.storage.path.ptr);
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_tablespace_close, &core->storage.tablespace);


    // Jobs scheduler
    mdv_jobber_config const jr_config =
    {
        .threadpool =
        {
            .size = MDV_CONFIG.storage.workers,
            .thread_attrs =
            {
                .stack_size = MDV_THREAD_STACK_SIZE
            }
        },
        .queue =
        {
            .count = MDV_CONFIG.storage.worker_queues
        }
    };

    core->jobber = mdv_jobber_create(&jr_config);

    if (!core->jobber)
    {
        MDV_LOGE("Jobs scheduler creation failed");
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_jobber_release, core->jobber);


    // Topology tracker
    core->tracker = mdv_tracker_create(&core->metainf.uuid.value);

    if (!core->tracker)
    {
        MDV_LOGE("Topology tracker creation failed");
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_tracker_release, core->tracker);


    // Data synchronizer
    if (mdv_datasync_create(&core->datasync,
                            &core->storage.tablespace,
                            core->tracker,
                            core->jobber) != MDV_OK)
    {
        MDV_LOGE("Data synchronizer creation failed");
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_datasync_free, &core->datasync);


    // Data committer
    if (mdv_committer_create(&core->committer,
                             &core->storage.tablespace,
                             core->tracker,
                             core->jobber) != MDV_OK)
    {
        MDV_LOGE("Data committer creation failed");
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_committer_free, &core->committer);


    // Cluster manager
    if (!mdv_core_cluster_create(core))
    {
        mdv_rollback(rollbacker);
        return false;
    }

    mdv_rollbacker_push(rollbacker, mdv_cluster_free, &core->cluster);


    MDV_LOGI("Storage version: %u", core->metainf.version.value);

    MDV_LOGI("Node UUID: %s", mdv_uuid_to_str(&core->metainf.uuid.value).ptr);

    mdv_rollbacker_free(rollbacker);

    return true;
}


void mdv_core_free(mdv_core *core)
{
    mdv_datasync_stop(&core->datasync);
    mdv_committer_stop(&core->committer);
    mdv_cluster_free(&core->cluster);
    mdv_jobber_release(core->jobber);
    mdv_datasync_free(&core->datasync);
    mdv_committer_free(&core->committer);
    mdv_tracker_release(core->tracker);
    mdv_storage_release(core->storage.metainf);
    mdv_tablespace_close(&core->storage.tablespace);
}


bool mdv_core_listen(mdv_core *core)
{
    return mdv_cluster_bind(&core->cluster, MDV_CONFIG.server.listen) == MDV_OK;
}


void mdv_core_connect(mdv_core *core)
{
    for(uint32_t i = 0; i < MDV_CONFIG.cluster.size; ++i)
    {
        mdv_cluster_connect(&core->cluster, mdv_str((char*)MDV_CONFIG.cluster.nodes[i]), MDV_CLI_PEER);
    }
}

