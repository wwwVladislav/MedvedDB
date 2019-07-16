#include "mdv_peer.h"
#include <mdv_messages.h>
#include <mdv_version.h>
#include <mdv_timerfd.h>
#include <mdv_alloc.h>
#include <mdv_log.h>
#include <mdv_socket.h>
#include <stddef.h>
#include <string.h>


/// Client UUID
static mdv_uuid const MDV_CLIENT_UUID = {};


/// Peer context used for storing different type of information about active peer
typedef struct mdv_peer
{
    mdv_tablespace     *tablespace;             ///< tablespace
    mdv_nodes          *nodes;                  ///< nodes storage
    mdv_dispatcher     *dispatcher;             ///< Messages dispatcher
    mdv_descriptor      sock;                   ///< Socket associated with peer
    mdv_uuid            peer_uuid;              ///< peer uuid
    mdv_uuid            current_uuid;           ///< current node uuid
    uint32_t            id;                     ///< Unique peer identifier inside current server
    mdv_string          addr;                   ///< peer address
    size_t              created_time;           ///< time, when peer registered
    char                buff[MDV_ADDR_LEN_MAX]; ///< buffer for address string
} mdv_peer;


static mdv_errno mdv_peer_reply(mdv_peer *peer, mdv_msg const *msg);


static mdv_errno mdv_peer_wave_reply(mdv_peer *peer, uint16_t id, mdv_msg_hello const *msg)
{
    binn hey;

    if (!mdv_binn_hello(msg, &hey))
        return MDV_FAILED;

    mdv_msg message =
    {
        .hdr =
        {
            .id     = mdv_msg_hello_id,
            .number = id,
            .size   = binn_size(&hey)
        },
        .payload = binn_ptr(&hey)
    };

    mdv_errno err = mdv_peer_reply(peer, &message);

    binn_free(&hey);

    return err;
}


static mdv_errno mdv_peer_status_reply(mdv_peer *peer, uint16_t id, mdv_msg_status const *msg)
{
    binn status;

    if (!mdv_binn_status(msg, &status))
        return MDV_FAILED;

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_msg_status_id,
            .number = id,
            .size = binn_size(&status)
        },
        .payload = binn_ptr(&status)
    };

    mdv_errno err = mdv_peer_reply(peer, &message);

    binn_free(&status);

    return err;
}


static mdv_errno mdv_peer_table_info_reply(mdv_peer *peer, uint16_t id, mdv_msg_table_info const *msg)
{
    binn table_info;

    if (!mdv_binn_table_info(msg, &table_info))
        return MDV_FAILED;

    mdv_msg message =
    {
        .hdr =
        {
            .id = mdv_msg_table_info_id,
            .number = id,
            .size = binn_size(&table_info)
        },
        .payload = binn_ptr(&table_info)
    };

    mdv_errno err = mdv_peer_reply(peer, &message);

    binn_free(&table_info);

    return err;
}


static mdv_errno mdv_peer_wave_handler(mdv_msg const *msg, void *arg)
{
    MDV_LOGI("<<<<< '%s'", mdv_msg_name(msg->hdr.id));

    mdv_peer *peer = arg;

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_hello client_hello = {};

    if (!mdv_unbinn_hello(&binn_msg, &client_hello))
    {
        MDV_LOGE("Invalid '%s' message", mdv_msg_name(msg->hdr.id));
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    binn_free(&binn_msg);

    if(client_hello.version != MDV_VERSION)
    {
        MDV_LOGE("Invalid client version");
        return MDV_FAILED;
    }

    peer->peer_uuid = client_hello.uuid;

    mdv_msg_hello hello =
    {
        .version = MDV_VERSION,
        .uuid = peer->current_uuid
    };

    return mdv_peer_wave_reply(peer, msg->hdr.number, &hello);
}


static mdv_errno mdv_peer_create_table_handler(mdv_msg const *msg, void *arg)
{
    MDV_LOGI("<<<<< '%s'", mdv_msg_name(msg->hdr.id));

    mdv_peer *peer = arg;

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        MDV_LOGW("Message '%s' reading failed", mdv_msg_name(msg->hdr.id));
        return MDV_FAILED;
    }

    mdv_msg_create_table_base *create_table = mdv_unbinn_create_table(&binn_msg);

    binn_free(&binn_msg);

    if (!create_table)
    {
        MDV_LOGE("Invalid '%s' message", mdv_msg_name(mdv_msg_create_table_id));
        return MDV_FAILED;
    }

    mdv_errno err = mdv_tablespace_log_create_table(peer->tablespace, 0, (mdv_table_base*)&create_table->table);

    if (err == MDV_OK)
    {
        mdv_msg_table_info const table_info =
        {
            .uuid = create_table->table.uuid
        };

        err = mdv_peer_table_info_reply(peer, msg->hdr.number, &table_info);
    }
    else
    {
        mdv_msg_status const status =
        {
            .err = err,
            .message = { 0 }
        };

        err = mdv_peer_status_reply(peer, msg->hdr.number, &status);
    }

    mdv_free(create_table);

    return err;
}


mdv_peer * mdv_peer_accept(mdv_tablespace *tablespace, mdv_nodes *nodes, mdv_descriptor fd, mdv_string const *addr, mdv_uuid const *uuid)
{
    mdv_peer *peer = mdv_alloc(sizeof(mdv_peer));

    if (!peer)
    {
        MDV_LOGE("No memory to accept peer");
        mdv_socket_shutdown(fd, MDV_SOCK_SHUT_RD | MDV_SOCK_SHUT_WR);
        return 0;
    }

    MDV_LOGD("Peer %p initialize", peer);

    peer->tablespace    = tablespace;
    peer->nodes         = nodes;
    peer->sock          = fd;
    peer->id            = 0;
    peer->addr          = mdv_str_static(peer->buff);
    peer->current_uuid  = *uuid;
    peer->created_time  = mdv_gettime();

    memset(&peer->buff, 0, sizeof peer->buff);

    size_t const addr_len = strlen(addr->ptr) + 1;
    peer->addr.size = peer->addr.size < addr_len
                        ? peer->addr.size
                        : addr_len;
    memcpy(peer->addr.ptr, addr->ptr, addr_len);
    peer->addr.ptr[peer->addr.size - 1] = 0;

    mdv_dispatcher_handler const handlers[] =
    {
        { mdv_msg_hello_id,         &mdv_peer_wave_handler,         peer },
        { mdv_msg_create_table_id,  &mdv_peer_create_table_handler, peer }
    };

    peer->dispatcher = mdv_dispatcher_create(fd, sizeof handlers / sizeof *handlers, handlers);

    if (!peer->dispatcher)
    {
        MDV_LOGE("Messages dispatcher not created");
        mdv_free(peer);
        mdv_socket_shutdown(fd, MDV_SOCK_SHUT_RD | MDV_SOCK_SHUT_WR);
        return 0;
    }

    MDV_LOGD("Peer %p accepted", peer);

    return peer;
}


mdv_errno mdv_peer_recv(mdv_peer *peer)
{
    mdv_errno err = mdv_dispatcher_read(peer->dispatcher);

    switch(err)
    {
        case MDV_OK:
        case MDV_EAGAIN:
            break;

        default:
            mdv_socket_shutdown(peer->sock, MDV_SOCK_SHUT_RD | MDV_SOCK_SHUT_WR);
    }

    return err;
}


mdv_errno mdv_peer_send(mdv_peer *peer, mdv_msg *req, mdv_msg *resp, size_t timeout)
{
    MDV_LOGI(">>>>> %s '%s'", mdv_uuid_to_str(&peer->peer_uuid).ptr, mdv_msg_name(req->hdr.id));
    return mdv_dispatcher_send(peer->dispatcher, req, resp, timeout);
}


mdv_errno mdv_peer_post(mdv_peer *peer, mdv_msg *msg)
{
    MDV_LOGI(">>>>> %s '%s'", mdv_uuid_to_str(&peer->peer_uuid).ptr, mdv_msg_name(msg->hdr.id));
    return mdv_dispatcher_post(peer->dispatcher, msg);
}


static mdv_errno mdv_peer_reply(mdv_peer *peer, mdv_msg const *msg)
{
    MDV_LOGI(">>>>> %s '%s'", mdv_uuid_to_str(&peer->peer_uuid).ptr, mdv_msg_name(msg->hdr.id));
    return mdv_dispatcher_reply(peer->dispatcher, msg);
}


void mdv_peer_free(mdv_peer *peer)
{
    MDV_LOGD("Peer %p freed", peer);
    mdv_dispatcher_free(peer->dispatcher);
    mdv_free(peer);
}

