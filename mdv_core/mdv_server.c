#include "mdv_server.h"
#include "mdv_config.h"
#include <mdv_log.h>
#include <mdv_alloc.h>
#include <mdv_chaman.h>
#include <mdv_def.h>
#include <mdv_rollbacker.h>
#include <mdv_messages.h>
#include "mdv_user.h"
#include "mdv_peer.h"
#include "mdv_conctx.h"


/// @cond Doxygen_Suppress

struct mdv_server
{
    mdv_uuid            uuid;
    mdv_chaman         *chaman;
    mdv_nodes          *nodes;
    mdv_tablespace     *tablespace;
};

/// @endcond


static mdv_errno mdv_channel_select(mdv_descriptor fd, uint32_t *type)
{
    mdv_msg_tag tag;
    size_t len = sizeof tag;

    mdv_errno err = mdv_read(fd, &tag, &len);

    if (err == MDV_OK)
        *type = tag.tag;

    return err;
}


static void * mdv_channel_create(mdv_descriptor fd, mdv_string const *addr, void *userdata, uint32_t type, mdv_channel_dir dir)
{
    mdv_server *server = (mdv_server *)userdata;
    (void)addr;

    switch(type)
    {
        case MDV_CLI_USER:
            return mdv_user_accept(server->tablespace, fd, &server->uuid);

        case MDV_CLI_PEER:
            return dir == MDV_CHIN
                        ? mdv_peer_accept(server->tablespace, server->nodes, fd, &server->uuid)
                        : mdv_peer_connect(server->tablespace, server->nodes, fd, &server->uuid);

        default:
            MDV_LOGE("Undefined client type: %u", type);
    }

    return 0;
}


static mdv_errno mdv_channel_recv(void *channel)
{
    mdv_conctx *conctx = channel;

    switch(conctx->type)
    {
        case MDV_CLI_USER:
            return mdv_user_recv(channel);

        case MDV_CLI_PEER:
            return mdv_peer_recv(channel);

        default:
            MDV_LOGE("Undefined client type: %u", conctx->type);
    }

    return MDV_FAILED;
}


static void mdv_channel_close(void *channel)
{
    mdv_conctx *conctx = channel;

    switch(conctx->type)
    {
        case MDV_CLI_USER:
            return mdv_user_free(channel);

        case MDV_CLI_PEER:
            return mdv_peer_free(channel);

        default:
            MDV_LOGE("Undefined client type: %u", conctx->type);
    }
}


mdv_server * mdv_server_create(mdv_tablespace *tablespace, mdv_nodes *nodes, mdv_uuid const *uuid)
{
    mdv_rollbacker(2) rollbacker;
    mdv_rollbacker_clear(rollbacker);

    mdv_server *server = mdv_alloc(sizeof(mdv_server));

    if(!server)
    {
        MDV_LOGE("Memory allocation for server failed");
        return 0;
    }

    server->uuid = *uuid;
    server->nodes = nodes;
    server->tablespace = tablespace;

    mdv_rollbacker_push(rollbacker, mdv_free, server);

    mdv_chaman_config const config =
    {
        .peer =
        {
            .keepidle          = MDV_CONFIG.connection.keep_idle,
            .keepcnt           = MDV_CONFIG.connection.keep_count,
            .keepintvl         = MDV_CONFIG.connection.keep_interval
        },
        .threadpool =
        {
            .size = MDV_CONFIG.server.workers,
            .thread_attrs =
            {
                .stack_size = MDV_THREAD_STACK_SIZE
            }
        },
        .userdata = server,
        .channel =
        {
            .select = mdv_channel_select,
            .create = mdv_channel_create,
            .recv = mdv_channel_recv,
            .close = mdv_channel_close
        }
    };

    server->chaman = mdv_chaman_create(&config);

    if (!server->chaman)
    {
        MDV_LOGE("Chaman not created");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_chaman_free, server->chaman);

    mdv_errno err = mdv_chaman_listen(server->chaman, MDV_CONFIG.server.listen);

    if (err != MDV_OK)
    {
        MDV_LOGE("Listener failed with error: %s (%d)", mdv_strerror(err), err);
        mdv_rollback(rollbacker);
        return 0;
    }

    return server;
}


bool mdv_server_start(mdv_server *srvr)
{
    return true;
}


void mdv_server_free(mdv_server *srvr)
{
    if (srvr)
    {
        mdv_chaman_free(srvr->chaman);
        mdv_free(srvr);
    }
}

