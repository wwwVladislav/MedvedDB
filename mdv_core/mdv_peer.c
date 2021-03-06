#include "mdv_peer.h"
#include "mdv_p2pmsg.h"
#include "mdv_config.h"
#include "event/mdv_evt_types.h"
#include "event/mdv_evt_link.h"
#include "event/mdv_evt_topology.h"
#include "event/mdv_evt_broadcast.h"
#include "event/mdv_evt_trlog.h"
#include <mdv_alloc.h>
#include <mdv_threads.h>
#include <mdv_log.h>
#include <mdv_dispatcher.h>
#include <mdv_rollbacker.h>
#include <mdv_proto.h>
#include <mdv_version.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdlib.h>


typedef struct
{
    mdv_channel             base;           ///< connection context base type
    atomic_uint             rc;             ///< References counter
    mdv_channel_dir         dir;            ///< Channel direction
    mdv_uuid                uuid;           ///< current node uuid
    mdv_uuid                peer_uuid;      ///< peer global uuid
    char                   *peer_addr;      ///< peer address
    mdv_dispatcher         *dispatcher;     ///< Messages dispatcher
    mdv_ebus               *ebus;           ///< Events bus
} mdv_peer;


/**
 * @brief Send message and wait response.
 *
 * @param peer [in]     peer connection context
 * @param req [in]      request to be sent
 * @param resp [out]    received response
 * @param timeout [in]  timeout for response wait (in milliseconds)
 *
 * @return MDV_OK if message is successfully sent and 'resp' contains response from remote peer
 * @return MDV_BUSY if there is no free slots for request. At this case caller should wait and try again later.
 * @return On error return nonzero error code
 */
static mdv_errno mdv_peer_send(mdv_peer *peer, mdv_msg *req, mdv_msg *resp, size_t timeout);


/**
 * @brief Send message but response isn't required.
 *
 * @param peer [in]     peer connection context
 * @param msg [in]      message to be sent
 *
 * @return On success returns MDV_OK
 * @return On error return nonzero error code.
 */
static mdv_errno mdv_peer_post(mdv_peer *peer, mdv_msg *msg);


/**
 * @brief Frees peer connection context
 *
 * @param peer [in]     peer connection context
 */
static void mdv_peer_free(mdv_peer *peer);


static mdv_channel * mdv_peer_retain_impl(mdv_channel *channel)
{
    if (channel)
    {
        mdv_peer *peer = (mdv_peer*)channel;

        uint32_t rc = atomic_load_explicit(&peer->rc, memory_order_relaxed);

        if (!rc)
            return 0;

        while(!atomic_compare_exchange_weak(&peer->rc, &rc, rc + 1))
        {
            if (!rc)
                return 0;
        }
    }

    return channel;
}


static mdv_peer * mdv_peer_retain(mdv_peer *peer)
{
    return (mdv_peer *)mdv_peer_retain_impl(&peer->base);
}


static uint32_t mdv_peer_release_impl(mdv_channel *channel)
{
    if (channel)
    {
        mdv_peer *peer = (mdv_peer*)channel;

        uint32_t rc = atomic_fetch_sub_explicit(&peer->rc, 1, memory_order_relaxed) - 1;

        if (!rc)
            mdv_peer_free(peer);

        return rc;
    }

    return 0;
}


static uint32_t mdv_peer_release(mdv_peer *peer)
{
    return mdv_peer_release_impl(&peer->base);
}

static mdv_channel_t mdv_peer_type_impl(mdv_channel const *channel)
{
    (void)channel;
    return MDV_PEER_CHANNEL;
}


static mdv_uuid const * mdv_peer_id_impl(mdv_channel const *channel)
{
    mdv_peer *peer = (mdv_peer*)channel;
    return &peer->peer_uuid;
}


static mdv_errno mdv_peer_recv_impl(mdv_channel *channel)
{
    mdv_peer *peer = (mdv_peer*)channel;
    return mdv_dispatcher_read(peer->dispatcher);
}


static mdv_errno mdv_peer_connected(mdv_peer *peer, char const *addr, mdv_uuid const *uuid);

static void      mdv_peer_disconnected(mdv_peer *peer);


static mdv_errno mdv_peer_reply(mdv_peer *peer, mdv_msg const *msg);


static char * mdv_string_dup(char const *str)
{
    size_t len = strlen(str);
    char *dup_str = mdv_alloc(len + 1);
    if (dup_str)
        memcpy(dup_str, str, len + 1);
    return dup_str;
}


static mdv_errno mdv_peer_link_check(mdv_peer *peer, mdv_uuid const *peer_id, bool *connected)
{
    mdv_errno err = MDV_FAILED;

    mdv_evt_link_check *evt = mdv_evt_link_check_create(&peer->uuid, peer_id);

    if(evt)
    {
        err = mdv_ebus_publish(peer->ebus, &evt->base, MDV_EVT_SYNC);
        *connected = evt->connected;
        mdv_evt_link_check_release(evt);
    }

    return err;
}


static mdv_errno mdv_peer_hello_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_p2p_hello req;

    if (!mdv_unbinn_p2p_hello(&binn_msg, &req))
    {
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    mdv_free(peer->peer_addr);

    peer->peer_addr = mdv_string_dup(req.listen);

    if (!peer->peer_addr)
    {
        MDV_LOGE("No memory for peer address");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    binn_free(&binn_msg);

    mdv_errno err = mdv_peer_connected(peer, peer->peer_addr, &peer->peer_uuid);

    if (err != MDV_OK)
    {
        char err_msg[128];
        MDV_LOGE("Peer registration failed with error '%s' (%d)",
                    mdv_strerror(err, err_msg, sizeof err_msg), err);
    }

    return err;
}


static mdv_errno mdv_peer_toposync_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    mdv_rollbacker *rollbacker = mdv_rollbacker_create(2);

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, binn_free, &binn_msg);

    mdv_msg_p2p_toposync req;

    if (!mdv_unbinn_p2p_toposync(&binn_msg, &req))
    {
        MDV_LOGE("Topology synchronization request processing failed");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_p2p_toposync_free, &req);

    mdv_evt_topology_sync *event = mdv_evt_topology_sync_create(
                                            &peer->peer_uuid,
                                            &peer->uuid,
                                            req.topology);

    if (event)
    {
        mdv_ebus_publish(peer->ebus, &event->base, MDV_EVT_DEFAULT);
        mdv_evt_topology_sync_release(event);
    }

    mdv_rollback(rollbacker);

    return MDV_OK;
}


static mdv_errno mdv_peer_broadcast_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    mdv_rollbacker *rollbacker = mdv_rollbacker_create(2);

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, binn_free, &binn_msg);

    mdv_msg_p2p_broadcast req;

    if (!mdv_unbinn_p2p_broadcast(&binn_msg, &req))
    {
        MDV_LOGE("Topology synchronization request processing failed");
        mdv_rollback(rollbacker);
        return MDV_FAILED;
    }

    mdv_rollbacker_push(rollbacker, mdv_hashmap_release, req.notified);

    mdv_evt_broadcast *event = mdv_evt_broadcast_create(
                                            &peer->peer_uuid,
                                            req.msg_id,
                                            req.size,
                                            req.data,
                                            req.notified);

    if (event)
    {
        mdv_ebus_publish(peer->ebus, &event->base, MDV_EVT_DEFAULT);
        mdv_evt_broadcast_release(event);
    }

    mdv_rollback(rollbacker);

    return MDV_OK;
}


static mdv_errno mdv_peer_trlog_sync_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_p2p_trlog_sync req;

    if (!mdv_unbinn_p2p_trlog_sync(&binn_msg, &req))
    {
        MDV_LOGE("Transaction log synchronization request processing failed");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    binn_free(&binn_msg);

    mdv_evt_trlog_sync *sync = mdv_evt_trlog_sync_create(&req.trlog, &peer->peer_uuid, &peer->uuid);

    if (sync)
    {
        if (mdv_ebus_publish(peer->ebus, &sync->base, MDV_EVT_DEFAULT) != MDV_OK)
            MDV_LOGE("Transaction log synchronization failed");
        mdv_evt_trlog_sync_release(sync);
    }
    else
    {
        MDV_LOGE("Transaction log synchronization failed. No memory.");
        return MDV_NO_MEM;
    }

    return MDV_OK;
}


static mdv_errno mdv_peer_trlog_state_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_p2p_trlog_state req;

    if (!mdv_unbinn_p2p_trlog_state(&binn_msg, &req))
    {
        MDV_LOGE("Transaction log state processing failed");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    MDV_LOGI("RECV %s '%s': TR log top: %" PRId64,
             mdv_uuid_to_str(&peer->peer_uuid, uuid_str),
             mdv_p2p_msg_name(msg->hdr.id),
             req.trlog_top);

    binn_free(&binn_msg);

    mdv_evt_trlog_state *state = mdv_evt_trlog_state_create(&req.trlog, &peer->peer_uuid, &peer->uuid, req.trlog_top);

    if (state)
    {
        if (mdv_ebus_publish(peer->ebus, &state->base, MDV_EVT_DEFAULT) != MDV_OK)
            MDV_LOGE("Transaction log state notification failed");
        mdv_evt_trlog_state_release(state);
    }
    else
    {
        MDV_LOGE("Transaction log state notification failed. No memory.");
        return MDV_NO_MEM;
    }

    return MDV_OK;
}


static mdv_errno mdv_peer_trlog_data_handler(mdv_msg const *msg, void *arg)
{
    mdv_peer *peer = arg;

    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI("<<<<< %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_p2p_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_p2p_trlog_data req = {};

    if (!mdv_unbinn_p2p_trlog_data(&binn_msg, &req))
    {
        MDV_LOGE("Transaction log data processing failed");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    MDV_LOGI("RECV %s '%s': count: %u",
             mdv_uuid_to_str(&peer->peer_uuid, uuid_str),
             mdv_p2p_msg_name(msg->hdr.id),
             req.count);

    binn_free(&binn_msg);

    mdv_evt_trlog_data *data = mdv_evt_trlog_data_create(&req.trlog, &peer->peer_uuid, &peer->uuid, &req.rows, req.count);

    if (data)
    {
        if (mdv_ebus_publish(peer->ebus, &data->base, MDV_EVT_DEFAULT) != MDV_OK)
            MDV_LOGE("Transaction log data processing failed");
        mdv_evt_trlog_data_release(data);
    }
    else
    {
        MDV_LOGE("Transaction log data processing failed. No memory.");
        return MDV_NO_MEM;
    }

    return MDV_OK;
}


/**
 * @brief Post hello message
 */
static mdv_errno mdv_peer_hello(mdv_peer *peer)
{
    mdv_msg_p2p_hello hello =
    {
        .listen  = MDV_CONFIG.server.listen
    };

    binn hey;

    if (!mdv_binn_p2p_hello(&hello, &hey))
        return MDV_FAILED;

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_hello),
            .size = binn_size(&hey)
        },
        .payload = binn_ptr(&hey)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&hey);

    return err;
}


/**
 * @brief Post topology synchronization message
 */
static mdv_errno mdv_peer_toposync(mdv_peer *peer, mdv_topology *topology)
{
    mdv_msg_p2p_toposync const toposync =
    {
        .topology = topology
    };

    binn obj;

    if (!mdv_binn_p2p_toposync(&toposync, &obj))
    {
        MDV_LOGE("Topology synchronization request failed");
        return MDV_FAILED;
    }

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_toposync),
            .size = binn_size(&obj)
        },
        .payload = binn_ptr(&obj)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&obj);

    return err;
}


/**
 * @brief Post trlog synchronization message
 */
static mdv_errno mdv_peer_trlog_sync(mdv_peer *peer, mdv_uuid const *trlog)
{
    mdv_msg_p2p_trlog_sync const trlog_sync =
    {
        .trlog = *trlog
    };

    binn obj;

    if (!mdv_binn_p2p_trlog_sync(&trlog_sync, &obj))
    {
        MDV_LOGE("Transaction log synchronization request failed");
        return MDV_FAILED;
    }

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_trlog_sync),
            .size = binn_size(&obj)
        },
        .payload = binn_ptr(&obj)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&obj);

    return err;
}


/**
 * @brief Post trlog state message
 */
static mdv_errno mdv_peer_trlog_state(mdv_peer *peer, mdv_uuid const *trlog, uint64_t top)
{
    mdv_msg_p2p_trlog_state const trlog_state =
    {
        .trlog = *trlog,
        .trlog_top = top
    };

    binn obj;

    if (!mdv_binn_p2p_trlog_state(&trlog_state, &obj))
    {
        MDV_LOGE("Transaction log state notification failed");
        return MDV_FAILED;
    }

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_trlog_state),
            .size = binn_size(&obj)
        },
        .payload = binn_ptr(&obj)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&obj);

    return err;
}


/**
 * @brief Post trlog data message
 */
static mdv_errno mdv_peer_trlog_data(mdv_peer *peer, mdv_uuid const *trlog, mdv_list const *rows, uint32_t count)
{
    mdv_msg_p2p_trlog_data const trlog_data =
    {
        .trlog = *trlog,
        .count = count,
        .rows = *rows
    };

    binn obj;

    if (!mdv_binn_p2p_trlog_data(&trlog_data, &obj))
    {
        MDV_LOGE("Transaction log data posting failed");
        return MDV_FAILED;
    }

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_trlog_data),
            .size = binn_size(&obj)
        },
        .payload = binn_ptr(&obj)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&obj);

    return err;
}


/**
 * @brief Broadcasts synchronization message
 */
static mdv_errno mdv_peer_broadcast(mdv_peer *peer, mdv_msg_p2p_broadcast const *msg)
{
    binn obj;

    if (!mdv_binn_p2p_broadcast(msg, &obj))
        return MDV_FAILED;

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_message_id(p2p_broadcast),
            .size = binn_size(&obj)
        },
        .payload = binn_ptr(&obj)
    };

    mdv_errno err = mdv_peer_post(peer, &message);

    binn_free(&obj);

    return err;
}


static mdv_errno mdv_peer_evt_broadcast_post(void *arg, mdv_event *event)
{
    mdv_peer *peer = arg;
    mdv_evt_broadcast_post *evt = (mdv_evt_broadcast_post *)event;

    // Ignore peer which is not involved in broadcasting
    if (!mdv_hashmap_find(evt->peers, &peer->peer_uuid))
        return MDV_OK;

    mdv_msg_p2p_broadcast const msg =
    {
        .msg_id   = evt->msg_id,
        .size     = evt->size,
        .data     = evt->data,
        .notified = evt->notified
    };

    return mdv_peer_broadcast(peer, &msg);
}


static mdv_errno mdv_peer_evt_topology_sync(void *arg, mdv_event *event)
{
    mdv_peer *peer = arg;
    mdv_evt_topology_sync *toposync = (mdv_evt_topology_sync *)event;

    if(mdv_uuid_cmp(&peer->uuid, &toposync->from) == 0
       && mdv_uuid_cmp(&peer->peer_uuid, &toposync->to) == 0)
        return mdv_peer_toposync(peer, toposync->topology);

    return MDV_OK;
}


static mdv_errno mdv_peer_evt_trlog_sync(void *arg, mdv_event *event)
{
    mdv_peer *peer = arg;
    mdv_evt_trlog_sync *sync = (mdv_evt_trlog_sync *)event;

    if(mdv_uuid_cmp(&peer->peer_uuid, &sync->to) == 0)
        return mdv_peer_trlog_sync(peer, &sync->trlog);

    return MDV_OK;
}


static mdv_errno mdv_peer_evt_trlog_state(void *arg, mdv_event *event)
{
    mdv_peer *peer = arg;
    mdv_evt_trlog_state *state = (mdv_evt_trlog_state *)event;

    if(mdv_uuid_cmp(&peer->peer_uuid, &state->to) == 0)
        return mdv_peer_trlog_state(peer, &state->trlog, state->top);

    return MDV_OK;
}


static mdv_errno mdv_peer_evt_trlog_data(void *arg, mdv_event *event)
{
    mdv_peer *peer = arg;
    mdv_evt_trlog_data *data = (mdv_evt_trlog_data *)event;

    if(mdv_uuid_cmp(&peer->peer_uuid, &data->to) == 0)
        return mdv_peer_trlog_data(peer, &data->trlog, &data->rows, data->count);

    return MDV_OK;
}


static const mdv_event_handler_type mdv_peer_handlers[] =
{
    { MDV_EVT_TOPOLOGY_SYNC,    mdv_peer_evt_topology_sync },
    { MDV_EVT_BROADCAST_POST,   mdv_peer_evt_broadcast_post },
    { MDV_EVT_TRLOG_SYNC,       mdv_peer_evt_trlog_sync },
    { MDV_EVT_TRLOG_STATE,      mdv_peer_evt_trlog_state },
    { MDV_EVT_TRLOG_DATA,       mdv_peer_evt_trlog_data },
};


mdv_channel * mdv_peer_create(mdv_descriptor  fd,
                              mdv_uuid const *uuid,
                              mdv_uuid const *peer_uuid,
                              mdv_channel_dir dir,
                              mdv_ebus       *ebus)
{
    mdv_rollbacker *rollbacker = mdv_rollbacker_create(3);

    mdv_peer *peer = mdv_alloc(sizeof(mdv_peer));

    if (!peer)
    {
        MDV_LOGE("No memory for peer connection context");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_free, peer);

    MDV_LOGD("Peer %p initialization", peer);

    static const mdv_ichannel vtbl =
    {
        .retain = mdv_peer_retain_impl,
        .release = mdv_peer_release_impl,
        .type = mdv_peer_type_impl,
        .id = mdv_peer_id_impl,
        .recv = mdv_peer_recv_impl,
    };

    // memset(peer, 0, sizeof *peer);

    peer->base.vptr = &vtbl;

    atomic_init(&peer->rc, 1);

    peer->dir = dir;
    peer->uuid = *uuid;
    peer->peer_uuid = *peer_uuid;
    peer->peer_addr = 0;

    peer->ebus = mdv_ebus_retain(ebus);

    mdv_rollbacker_push(rollbacker, mdv_ebus_release, peer->ebus);

    peer->dispatcher = mdv_dispatcher_create(fd);

    if (!peer->dispatcher)
    {
        MDV_LOGE("Peer connection context creation failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_dispatcher_free, peer->dispatcher);

    mdv_dispatcher_handler const handlers[] =
    {
        { mdv_message_id(p2p_hello),        &mdv_peer_hello_handler,        peer },
        { mdv_message_id(p2p_toposync),     &mdv_peer_toposync_handler,     peer },
        { mdv_message_id(p2p_broadcast),    &mdv_peer_broadcast_handler,    peer },
        { mdv_message_id(p2p_trlog_sync),   &mdv_peer_trlog_sync_handler,   peer },
        { mdv_message_id(p2p_trlog_state),  &mdv_peer_trlog_state_handler,  peer },
        { mdv_message_id(p2p_trlog_data),   &mdv_peer_trlog_data_handler,   peer },
    };

    for(size_t i = 0; i < sizeof handlers / sizeof *handlers; ++i)
    {
        if (mdv_dispatcher_reg(peer->dispatcher, handlers + i) != MDV_OK)
        {
            MDV_LOGE("Messages dispatcher handler not registered");
            mdv_rollback(rollbacker);
            return 0;
        }
    }

    if (mdv_ebus_subscribe_all(peer->ebus,
                               peer,
                               mdv_peer_handlers,
                               sizeof mdv_peer_handlers / sizeof *mdv_peer_handlers) != MDV_OK)
    {
        MDV_LOGE("Ebus subscription failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    if (mdv_peer_hello(peer) != MDV_OK)
    {
        MDV_LOGD("Peer handshake message failed");
        mdv_ebus_unsubscribe_all(peer->ebus,
                                    peer,
                                    mdv_peer_handlers,
                                    sizeof mdv_peer_handlers / sizeof *mdv_peer_handlers);
        mdv_rollback(rollbacker);
        return 0;
    }

    MDV_LOGD("Peer %p initialized", peer);

    mdv_rollbacker_free(rollbacker);

    return &peer->base;
}


static void mdv_peer_free(mdv_peer *peer)
{
    if(peer)
    {
        mdv_ebus_unsubscribe_all(peer->ebus,
                                 peer,
                                 mdv_peer_handlers,
                                 sizeof mdv_peer_handlers / sizeof *mdv_peer_handlers);
        mdv_peer_disconnected(peer);
        mdv_dispatcher_free(peer->dispatcher);
        mdv_ebus_release(peer->ebus);
        mdv_free(peer->peer_addr);
        mdv_free(peer);
        MDV_LOGD("Peer %p freed", peer);
    }
}


static mdv_errno mdv_peer_send(mdv_peer *peer, mdv_msg *req, mdv_msg *resp, size_t timeout)
{
    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI(">>>>> %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(req->hdr.id));

    mdv_errno err = MDV_CLOSED;

    peer = mdv_peer_retain(peer);

    if (peer)
    {
        err = mdv_dispatcher_send(peer->dispatcher, req, resp, timeout);
        mdv_peer_release(peer);
    }

    return err;
}


static mdv_errno mdv_peer_post(mdv_peer *peer, mdv_msg *msg)
{
    char uuid_str[MDV_UUID_STR_LEN];

    MDV_LOGI(">>>>> %s '%s'", mdv_uuid_to_str(&peer->peer_uuid, uuid_str), mdv_p2p_msg_name(msg->hdr.id));

    mdv_errno err = MDV_CLOSED;

    peer = mdv_peer_retain(peer);

    if (peer)
    {
        err = mdv_dispatcher_post(peer->dispatcher, msg);
        mdv_peer_release(peer);
    }

    return err;
}


static mdv_errno mdv_peer_reply(mdv_peer *peer, mdv_msg const *msg)
{
    char uuid_str[MDV_UUID_STR_LEN];
    MDV_LOGI(">>>>> %s '%s'",
             mdv_uuid_to_str(&peer->peer_uuid, uuid_str),
             mdv_p2p_msg_name(msg->hdr.id));
    return mdv_dispatcher_reply(peer->dispatcher, msg);
}


static mdv_errno mdv_peer_connected(mdv_peer *peer,
                                    char const *addr,
                                    mdv_uuid const *uuid)
{
    mdv_toponode const remote =
    {
        .uuid = *uuid,
        .addr = addr
    };

    mdv_toponode const current =
    {
        .uuid = peer->uuid,
        .addr = MDV_CONFIG.server.listen
    };

    mdv_evt_link_state *event = peer->dir == MDV_CHIN
            ? mdv_evt_link_state_create(&peer->uuid, &remote, &current, true)
            : mdv_evt_link_state_create(&peer->uuid, &current, &remote, true);

    if (event)
    {
        mdv_ebus_publish(peer->ebus, &event->base, MDV_EVT_DEFAULT);
        mdv_evt_link_state_release(event);
    }

    return MDV_OK;
}


static void mdv_peer_disconnected(mdv_peer *peer)
{
    if (!peer->peer_addr)
        return;

    mdv_toponode const remote =
    {
        .uuid = peer->peer_uuid,
        .addr = peer->peer_addr
    };

    mdv_toponode const current =
    {
        .uuid = peer->uuid,
        .addr = MDV_CONFIG.server.listen
    };

    mdv_evt_link_state *event = mdv_evt_link_state_create(&peer->uuid, &current, &remote, false);

    if (event)
    {
        mdv_ebus_publish(peer->ebus, &event->base, MDV_EVT_DEFAULT);
        mdv_evt_link_state_release(event);
    }
}
