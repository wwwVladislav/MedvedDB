#include "mdv_syncer.h"
#include "mdv_syncerino.h"
#include "event/mdv_evt_types.h"
#include "event/mdv_evt_topology.h"
#include "event/mdv_evt_trlog.h"
#include <mdv_alloc.h>
#include <mdv_log.h>
#include <mdv_rollbacker.h>
#include <mdv_eventfd.h>
#include <mdv_threads.h>
#include <mdv_safeptr.h>
#include <mdv_router.h>
#include <mdv_mutex.h>
#include <mdv_hashmap.h>
#include <mdv_vector.h>
#include <stdatomic.h>


/// Data synchronizer
struct mdv_syncer
{
    atomic_uint     rc;             ///< References counter
    mdv_ebus       *ebus;           ///< Event bus
    mdv_jobber     *jobber;         ///< Jobs scheduler
    mdv_uuid        uuid;           ///< Current node UUID
    mdv_mutex       mutex;          ///< Mutex for peers synchronizers guard
    mdv_hashmap    *peers;          ///< Current synchronizing peers (hashmap<mdv_syncer_peer>)
};


/// Synchronizing peer
typedef struct
{
    mdv_uuid        uuid;           ///< Peer UUID
    mdv_syncerino  *syncerino;      ///< Transaction logs synchronizer with specific node
} mdv_syncer_peer;


static mdv_trlog * mdv_syncer_trlog(mdv_syncer *syncer, mdv_uuid const *uuid)
{
    mdv_evt_trlog *evt = mdv_evt_trlog_create(uuid);

    if (!evt)
    {
        MDV_LOGE("Transaction log request failed. No memory.");
        return 0;
    }

    if (mdv_ebus_publish(syncer->ebus, &evt->base, MDV_EVT_SYNC) != MDV_OK)
        MDV_LOGE("Transaction log request failed");

    mdv_trlog *trlog = mdv_trlog_retain(evt->trlog);

    mdv_evt_trlog_release(evt);

    return trlog;
}


static void mdv_syncerino_start4all_storages(mdv_syncerino *syncerino, mdv_topology *topology)
{
    mdv_vector *nodes = mdv_topology_nodes(topology);

    mdv_vector_foreach(nodes, mdv_toponode, node)
        mdv_syncerino_start(syncerino, &node->uuid);

    mdv_vector_release(nodes);
}


static mdv_errno mdv_syncer_topology_changed(mdv_syncer *syncer, mdv_topology *topology)
{
    mdv_errno err = MDV_OK;

    mdv_hashmap *routes = mdv_routes_find(topology, &syncer->uuid);

    if (routes)
    {
        err = mdv_mutex_lock(&syncer->mutex);

        if (err == MDV_OK)
        {
            // I. Disconnected peers removal
            if (!mdv_hashmap_empty(syncer->peers))
            {
                mdv_vector *removed_peers = mdv_vector_create(mdv_hashmap_size(syncer->peers),
                                                              sizeof(mdv_uuid),
                                                              &mdv_stallocator);

                if (removed_peers)
                {
                    mdv_hashmap_foreach(syncer->peers, mdv_syncer_peer, peer)
                    {
                        if (!mdv_hashmap_find(routes, &peer->uuid))
                        {
                            mdv_syncerino_cancel(peer->syncerino);
                            mdv_syncerino_release(peer->syncerino);
                            mdv_vector_push_back(removed_peers, &peer->uuid);
                        }
                    }

                    mdv_vector_foreach(removed_peers, mdv_uuid, peer)
                        mdv_hashmap_erase(syncer->peers, peer);

                    mdv_vector_release(removed_peers);
                }
                else
                {
                    err = MDV_NO_MEM;
                    MDV_LOGE("No memory for removed peers");
                }
            }

            // II. New peers adding
            mdv_hashmap_foreach(routes, mdv_route, route)
            {
                mdv_syncer_peer *peer = mdv_hashmap_find(syncer->peers, &route->uuid);

                if (peer)
                    continue;

                mdv_syncer_peer syncer_peer =
                {
                    .uuid = route->uuid,
                    .syncerino = mdv_syncerino_create(&syncer->uuid, &route->uuid, syncer->ebus, syncer->jobber)
                };

                if (syncer_peer.syncerino)
                {
                    if (mdv_hashmap_insert(syncer->peers, &syncer_peer, sizeof syncer_peer))
                        mdv_syncerino_start4all_storages(syncer_peer.syncerino, topology);
                    else
                    {
                        err = MDV_FAILED;
                        mdv_syncerino_release(syncer_peer.syncerino);
                        MDV_LOGE("Synchronizer creation for '%s' peer failed", mdv_uuid_to_str(&route->uuid).ptr);
                    }
                }
                else
                {
                    err = MDV_FAILED;
                    MDV_LOGE("Synchronizer creation for '%s' peer failed", mdv_uuid_to_str(&route->uuid).ptr);
                }
            }


            mdv_mutex_unlock(&syncer->mutex);
        }

        mdv_hashmap_release(routes);
    }
    else
        MDV_LOGE("Routes calculation failed");

    return err;
}


static mdv_errno mdv_syncer_evt_topology(void *arg, mdv_event *event)
{
    mdv_syncer *syncer = arg;
    mdv_evt_topology *topo = (mdv_evt_topology *)event;
    return mdv_syncer_topology_changed(syncer, topo->topology);
}


static mdv_errno mdv_syncer_evt_trlog_sync(void *arg, mdv_event *event)
{
    mdv_syncer *syncer = arg;
    mdv_evt_trlog_sync *sync = (mdv_evt_trlog_sync *)event;

    if(mdv_uuid_cmp(&syncer->uuid, &sync->to) != 0)
        return MDV_OK;

    mdv_errno err = MDV_FAILED;

    mdv_trlog *trlog = mdv_syncer_trlog(syncer, &sync->trlog);

    mdv_evt_trlog_state *evt = mdv_evt_trlog_state_create(&sync->trlog, &sync->to, &sync->from, trlog ? mdv_trlog_top(trlog) : 0);

    if (evt)
    {
        err = mdv_ebus_publish(syncer->ebus, &evt->base, MDV_EVT_DEFAULT);
        mdv_evt_trlog_state_release(evt);
    }

    mdv_trlog_release(trlog);

    return err;
}


static const mdv_event_handler_type mdv_syncer_handlers[] =
{
    { MDV_EVT_TOPOLOGY,         mdv_syncer_evt_topology },
    { MDV_EVT_TRLOG_SYNC,       mdv_syncer_evt_trlog_sync },
};


mdv_syncer * mdv_syncer_create(mdv_uuid const *uuid,
                               mdv_ebus *ebus,
                               mdv_jobber_config const *jconfig,
                               mdv_topology *topology)
{
    mdv_rollbacker *rollbacker = mdv_rollbacker_create(7);

    mdv_syncer *syncer = mdv_alloc(sizeof(mdv_syncer), "syncer");

    if (!syncer)
    {
        MDV_LOGE("No memory for new syncer");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_free, syncer, "syncer");

    atomic_init(&syncer->rc, 1);

    syncer->uuid = *uuid;

    syncer->ebus = mdv_ebus_retain(ebus);

    mdv_rollbacker_push(rollbacker, mdv_ebus_release, syncer->ebus);

    if (mdv_mutex_create(&syncer->mutex) != MDV_OK)
    {
        MDV_LOGE("Mutex for TR log synchronizers not created");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &syncer->mutex);

    syncer->peers = mdv_hashmap_create(mdv_syncer_peer,
                                       uuid,
                                       4,
                                       mdv_uuid_hash,
                                       mdv_uuid_cmp);
    if (!syncer->peers)
    {
        MDV_LOGE("There is no memory for peers hashmap");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_hashmap_release, syncer->peers);

    if (mdv_syncer_topology_changed(syncer, topology) != MDV_OK)
    {
        MDV_LOGE("Peers synchronizeers creation failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    syncer->jobber = mdv_jobber_create(jconfig);

    if (!syncer->jobber)
    {
        MDV_LOGE("Jobs scheduler creation failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_jobber_release, syncer->jobber);

    mdv_rollbacker_push(rollbacker, mdv_syncer_cancel, syncer);

    if (mdv_ebus_subscribe_all(syncer->ebus,
                               syncer,
                               mdv_syncer_handlers,
                               sizeof mdv_syncer_handlers / sizeof *mdv_syncer_handlers) != MDV_OK)
    {
        MDV_LOGE("Ebus subscription failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_free(rollbacker);

    return syncer;
}


void mdv_syncer_cancel(mdv_syncer *syncer)
{
    mdv_hashmap_foreach(syncer->peers, mdv_syncer_peer, peer)
        mdv_syncerino_cancel(peer->syncerino);
}


static void mdv_syncer_free(mdv_syncer *syncer)
{
    mdv_syncer_cancel(syncer);

    mdv_ebus_unsubscribe_all(syncer->ebus,
                             syncer,
                             mdv_syncer_handlers,
                             sizeof mdv_syncer_handlers / sizeof *mdv_syncer_handlers);

    mdv_jobber_release(syncer->jobber);
    mdv_ebus_release(syncer->ebus);

    mdv_hashmap_foreach(syncer->peers, mdv_syncer_peer, peer)
        mdv_syncerino_release(peer->syncerino);
    mdv_hashmap_release(syncer->peers);

    mdv_mutex_free(&syncer->mutex);

    memset(syncer, 0, sizeof(*syncer));
    mdv_free(syncer, "syncer");
}


mdv_syncer * mdv_syncer_retain(mdv_syncer *syncer)
{
    atomic_fetch_add_explicit(&syncer->rc, 1, memory_order_acquire);
    return syncer;
}


uint32_t mdv_syncer_release(mdv_syncer *syncer)
{
    if (!syncer)
        return 0;

    uint32_t rc = atomic_fetch_sub_explicit(&syncer->rc, 1, memory_order_release) - 1;

    if (!rc)
        mdv_syncer_free(syncer);

    return rc;
}
