#include "mdv_tracker.h"
#include "mdv_limits.h"
#include "mdv_log.h"
#include "mdv_rollbacker.h"
#include <string.h>


/// Node identifier
typedef struct
{
    uint32_t   id;      ///< Unique identifier inside current server
    mdv_node  *node;    ///< Cluster node information
} mdv_node_id;


/// Peer identifier
typedef struct
{
    mdv_uuid   uuid;    ///< Global unique identifier
    mdv_node  *node;    ///< Cluster node information
} mdv_peer_id;


static mdv_errno  mdv_tracker_insert(mdv_tracker *tracker, mdv_node const *node);
static mdv_node * mdv_tracker_find(mdv_tracker *tracker, mdv_uuid const *uuid);
static void       mdv_tracker_erase(mdv_tracker *tracker, mdv_node const *node);
static mdv_errno  mdv_tracker_insert_peer(mdv_tracker *tracker, mdv_node *node);
static void       mdv_tracker_erase_peer(mdv_tracker *tracker, mdv_uuid const *uuid);


static size_t mdv_node_id_hash(int const *id)
{
    return *id;
}

static int mdv_node_id_cmp(int const *id1, int const *id2)
{
    return *id1 - *id2;
}


static mdv_errno mdv_tracker_insert(mdv_tracker *tracker, mdv_node const *node)
{
    mdv_list_entry(mdv_node) *entry = (void*)_mdv_hashmap_insert(&tracker->nodes, node, node->size);

    if (entry)
    {
        mdv_node_id const nid =
        {
            .id = node->id,
            .node = &entry->data
        };

        if (mdv_hashmap_insert(tracker->ids, nid))
        {
            if (mdv_tracker_insert_peer(tracker, &entry->data) == MDV_OK)
                return MDV_OK;

            mdv_hashmap_erase(tracker->ids, node->id);
        }

        mdv_hashmap_erase(tracker->nodes, node->uuid);
        MDV_LOGW("Node '%s' discarded", mdv_uuid_to_str(&node->uuid).ptr);
    }
    else
        MDV_LOGW("Node '%s' discarded", mdv_uuid_to_str(&node->uuid).ptr);

    return MDV_FAILED;
}


static mdv_errno mdv_tracker_insert_peer(mdv_tracker *tracker, mdv_node *node)
{
    if (node->connected)
    {
        mdv_peer_id const peer_id =
        {
            .uuid = node->uuid,
            .node = node
        };

        if (mdv_hashmap_insert(tracker->peers, peer_id))
            return MDV_OK;
    }

    return MDV_OK;
}


static void mdv_tracker_erase(mdv_tracker *tracker, mdv_node const *node)
{
    mdv_hashmap_erase(tracker->peers, node->uuid);
    mdv_hashmap_erase(tracker->ids, node->id);
    mdv_hashmap_erase(tracker->nodes, node->uuid);
}


static void mdv_tracker_erase_peer(mdv_tracker *tracker, mdv_uuid const *uuid)
{
    mdv_hashmap_erase(tracker->peers, uuid);
}


static mdv_node * mdv_tracker_find(mdv_tracker *tracker, mdv_uuid const *uuid)
{
    return mdv_hashmap_find(tracker->nodes, *uuid);
}


static uint32_t mdv_tracker_new_id(mdv_tracker *tracker)
{
    return ++tracker->max_id;
}


mdv_errno mdv_tracker_create(mdv_tracker *tracker)
{
    mdv_rollbacker(6) rollbacker;
    mdv_rollbacker_clear(rollbacker);

    tracker->max_id = 0;


    if (!mdv_hashmap_init(tracker->nodes,
                          mdv_node,
                          uuid,
                          256,
                          &mdv_uuid_hash,
                          &mdv_uuid_cmp))
    {
        MDV_LOGE("There is no memory for nodes");
        return MDV_NO_MEM;
    }

    mdv_rollbacker_push(rollbacker, _mdv_hashmap_free, &tracker->nodes);


    if (mdv_mutex_create(&tracker->nodes_mutex) != MDV_OK)
    {
        MDV_LOGE("Nodes storage mutex not created");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &tracker->nodes_mutex);


    if (!mdv_hashmap_init(tracker->ids,
                          mdv_node_id,
                          id,
                          256,
                          &mdv_node_id_hash,
                          &mdv_node_id_cmp))
    {
        MDV_LOGE("There is no memory for nodes");
        mdv_rollback(rollbacker);
        return MDV_NO_MEM;
    }

    mdv_rollbacker_push(rollbacker, _mdv_hashmap_free, &tracker->ids);


    if (mdv_mutex_create(&tracker->ids_mutex) != MDV_OK)
    {
        MDV_LOGE("Nodes storage mutex not created");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &tracker->ids_mutex);


    if (!mdv_hashmap_init(tracker->peers,
                          mdv_peer_id,
                          uuid,
                          256,
                          &mdv_uuid_hash,
                          &mdv_uuid_cmp))
    {
        MDV_LOGE("There is no memory for peers");
        mdv_rollback(rollbacker);
        return MDV_NO_MEM;
    }

    mdv_rollbacker_push(rollbacker, _mdv_hashmap_free, &tracker->peers);


    if (mdv_mutex_create(&tracker->peers_mutex) != MDV_OK)
    {
        MDV_LOGE("Peers storage mutex not created");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_mutex_free, &tracker->peers_mutex);


    return MDV_OK;
}


void mdv_tracker_free(mdv_tracker *tracker)
{
    if (tracker)
    {
        mdv_hashmap_free(tracker->ids);
        mdv_mutex_free(&tracker->ids_mutex);
        mdv_hashmap_free(tracker->peers);
        mdv_mutex_free(&tracker->peers_mutex);
        mdv_hashmap_free(tracker->nodes);
        mdv_mutex_free(&tracker->nodes_mutex);
        memset(tracker, 0, sizeof *tracker);
    }
}


mdv_errno mdv_tracker_peer_connected(mdv_tracker *tracker, mdv_node *new_node)
{
    mdv_errno err = MDV_FAILED;

    if (mdv_mutex_lock(&tracker->nodes_mutex) == MDV_OK)
    {
        if (mdv_mutex_lock(&tracker->ids_mutex) == MDV_OK)
        {
            if (mdv_mutex_lock(&tracker->peers_mutex) == MDV_OK)
            {
                mdv_node *node = mdv_tracker_find(tracker, &new_node->uuid);

                if (node)
                {
                    if (!node->connected)
                    {
                        new_node->id    = node->id;

                        node->connected = 1;
                        node->active    = 1;
                        node->userdata  = new_node->userdata;

                        err = mdv_tracker_insert_peer(tracker, node);
                    }
                    else
                    {
                        err = MDV_EEXIST;
                        MDV_LOGD("Connection with '%s' is already exist", mdv_uuid_to_str(&node->uuid).ptr);
                    }
                }
                else
                {
                    new_node->id = mdv_tracker_new_id(tracker);
                    new_node->connected = 1;
                    new_node->active = 1;
                    err = mdv_tracker_insert(tracker, new_node);
                }

                mdv_mutex_unlock(&tracker->peers_mutex);
            }
            mdv_mutex_unlock(&tracker->ids_mutex);
        }
        mdv_mutex_unlock(&tracker->nodes_mutex);
    }

    return err;
}


void mdv_tracker_peer_disconnected(mdv_tracker *tracker, mdv_uuid const *uuid)
{
    if (mdv_mutex_lock(&tracker->nodes_mutex) == MDV_OK)
    {
        mdv_node *node = mdv_tracker_find(tracker, uuid);

        if (node)
        {
            if (mdv_mutex_lock(&tracker->peers_mutex) == MDV_OK)
            {
                node->connected = 0;
                node->accepted  = 0;
                node->active    = 0;
                node->userdata  = 0;
                mdv_tracker_erase_peer(tracker, uuid);

                mdv_mutex_unlock(&tracker->peers_mutex);
            }
        }

        mdv_mutex_unlock(&tracker->nodes_mutex);
    }
}


void mdv_tracker_append(mdv_tracker *tracker, mdv_node const *node)
{
    if (mdv_mutex_lock(&tracker->nodes_mutex) == MDV_OK)
    {
        if (!mdv_tracker_find(tracker, &node->uuid))
        {
            if (mdv_mutex_lock(&tracker->ids_mutex) == MDV_OK)
            {
                if (tracker->max_id < node->id)
                    tracker->max_id = node->id;

                mdv_tracker_insert(tracker, node);
                mdv_mutex_unlock(&tracker->ids_mutex);
            }
        }

        mdv_mutex_unlock(&tracker->nodes_mutex);
    }
}


void mdv_tracker_peers_foreach(mdv_tracker *tracker, void *arg, void (*fn)(mdv_node *, void *))
{
    if (mdv_mutex_lock(&tracker->peers_mutex) == MDV_OK)
    {
        mdv_hashmap_foreach(tracker->peers, mdv_peer_id, entry)
        {
            fn(entry->node, arg);
        }
        mdv_mutex_unlock(&tracker->peers_mutex);
    }
}


size_t mdv_tracker_peers_count(mdv_tracker *tracker)
{
    size_t peers_count = 0;

    if (mdv_mutex_lock(&tracker->peers_mutex) == MDV_OK)
    {
        peers_count = mdv_hashmap_size(tracker->peers);
        mdv_mutex_unlock(&tracker->peers_mutex);
    }

    return peers_count;
}


mdv_errno mdv_tracker_peers_call(mdv_tracker *tracker, uint32_t id, void *arg, mdv_errno (*fn)(mdv_node *, void *))
{
    mdv_errno err = MDV_FAILED;

    if (mdv_mutex_lock(&tracker->peers_mutex) == MDV_OK)
    {
        mdv_node_id *node_id = 0;

        if (mdv_mutex_lock(&tracker->ids_mutex) == MDV_OK)
        {
            node_id = mdv_hashmap_find(tracker->ids, id);
            mdv_mutex_unlock(&tracker->ids_mutex);
        }

        if (node_id)
        {
            mdv_node *node = node_id->node;

            if (node->connected)
                err = fn(node, arg);
        }

        mdv_mutex_unlock(&tracker->peers_mutex);
    }

    return err;
}
