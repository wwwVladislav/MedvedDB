#include "mdv_client.h"
#include "mdv_messages.h"
#include "mdv_connection.h"
#include "mdv_proto.h"
#include <mdv_alloc.h>
#include <mdv_binn.h>
#include <mdv_log.h>
#include <mdv_msg.h>
#include <mdv_rollbacker.h>
#include <mdv_chaman.h>
#include <mdv_socket.h>
#include <mdv_mutex.h>
#include <mdv_threads.h>
#include <mdv_serialization.h>
#include <mdv_router.h>
#include <mdv_safeptr.h>
#include <signal.h>


enum { MDV_FETCH_SIZE = 64 };       // limit for rows fetching


bool mdv_initialize()
{
    // mdv_logf_set_level(ZF_LOG_VERBOSE);
    signal(SIGPIPE, SIG_IGN);
    mdv_binn_set_allocator();
    return !mdv_alloc_initialize();
}


void mdv_finalize()
{
    mdv_alloc_finalize();
}


void mdv_thread_initialize()
{
    mdv_alloc_thread_initialize();
}

void mdv_thread_finalize()
{
    mdv_alloc_thread_finalize();
}


/// @cond Doxygen_Suppress


/// Client descriptor
struct mdv_client
{
    mdv_chaman         *chaman;             ///< Channels manager
    mdv_safeptr        *connection;         ///< Connection context
    uint32_t            response_timeout;   ///< Temeout for responses (in milliseconds)
};


/// @endcond


static mdv_connection * mdv_client_channel_retain(mdv_client *client)
{
    return mdv_safeptr_get(client->connection);
}


static bool mdv_client_channel_set(mdv_client *client, mdv_connection *con)
{
    return mdv_safeptr_set(client->connection, con) == MDV_OK;
}


static mdv_errno mdv_client_send(mdv_client *client, mdv_msg *req, mdv_msg *resp, size_t timeout)
{
    mdv_connection *con = mdv_client_channel_retain(client);

    if (!con)
        return MDV_FAILED;

    mdv_errno err = mdv_connection_send(con, req, resp, timeout);

    mdv_connection_release(con);

    return err;
}


/// Connection handshake
static mdv_errno mdv_client_handshake_impl(mdv_descriptor fd, void *userdata)
{
    (void)userdata;
    static const mdv_uuid uuid = {};
    return mdv_handshake_write(fd, MDV_USER_CHANNEL, &uuid);
}


/// Connection accept handler
static mdv_errno mdv_client_accept_impl(mdv_descriptor  fd,
                                        void           *userdata,
                                        mdv_channel_t  *channel_type,
                                        mdv_uuid       *uuid)
{
    (void)userdata;
    return mdv_handshake_read(fd, channel_type, uuid);
}


/// Channel creation
static mdv_channel * mdv_client_channel_create_impl(mdv_descriptor    fd,
                                                    void             *userdata,
                                                    mdv_channel_t     channel_type,
                                                    mdv_channel_dir   dir,
                                                    mdv_uuid const   *channel_id)
{
    (void)dir;
    (void)channel_type;

    mdv_client *client = userdata;

    mdv_connection *con = mdv_connection_create(fd, channel_id);

    if (!con)
    {
        MDV_LOGE("Connection failed");
        return 0;
    }

    if (!mdv_client_channel_set(client, con))
    {
        MDV_LOGE("Connection failed");
        mdv_connection_release(con);
        con = 0;
    }

    return (mdv_channel*)con;
}


static mdv_errno mdv_client_table_info_handler(mdv_msg const *msg, mdv_msg_table_info *table_info)
{
    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
        return MDV_FAILED;

    if (!mdv_msg_table_info_unbinn(&binn_msg, table_info))
    {
        MDV_LOGE("Invalid table information");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    binn_free(&binn_msg);

    return MDV_OK;
}


static mdv_table_desc * mdv_client_table_desc_handler(mdv_msg const *msg)
{
    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
        return 0;

    mdv_table_desc *desc = mdv_unbinn_table_desc(&binn_msg);

    binn_free(&binn_msg);

    return desc;
}


static mdv_errno mdv_client_view_handler(mdv_msg const *msg, uint32_t *view_id)
{
    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
        return 0;

    mdv_msg_view view;

    if (!mdv_msg_view_unbinn(&binn_msg, &view))
    {
        MDV_LOGE("Invalid view");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    *view_id = view.id;

    binn_free(&binn_msg);

    return MDV_OK;
}


static mdv_errno mdv_client_status_handler(mdv_msg const *msg, mdv_errno *err)
{
    mdv_msg_status status;

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
        return MDV_FAILED;

    if (!mdv_msg_status_unbinn(&binn_msg, &status))
    {
        MDV_LOGE("Invalid status");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    *err = (mdv_errno)status.err;

    binn_free(&binn_msg);

    return MDV_OK;
}


static mdv_rowset * mdv_client_rowset_handler(mdv_msg const *msg, mdv_table const *table, mdv_errno *err)
{
    mdv_msg_rowset rowset_msg;

    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
    {
        *err = MDV_FAILED;
        return 0;
    }

    if (!mdv_msg_rowset_unbinn(&binn_msg, &rowset_msg))
    {
        MDV_LOGE("Invalid rowset");
        binn_free(&binn_msg);
        *err = MDV_FAILED;
        return 0;
    }

    mdv_rowset *rowset = mdv_unbinn_rowset(rowset_msg.rows, mdv_table_description(table));

    if (rowset)
        *err = MDV_OK;
    else
    {
        MDV_LOGE("Invalid serialized rows set");
        *err = MDV_FAILED;
    }

    binn_free(&binn_msg);

    return rowset;
}


static mdv_errno mdv_client_topology_handler(mdv_msg const *msg, mdv_topology **topology)
{
    binn binn_msg;

    if(!binn_load(msg->payload, &binn_msg))
        return MDV_FAILED;

    *topology = mdv_msg_topology_unbinn(&binn_msg);

    if (!*topology)
    {
        MDV_LOGE("Invalid topology");
        binn_free(&binn_msg);
        return MDV_FAILED;
    }

    binn_free(&binn_msg);

    return MDV_OK;
}


mdv_client * mdv_client_connect(mdv_client_config const *config)
{
    mdv_rollbacker *rollbacker = mdv_rollbacker_create(3);

    // Allocate memory for client

    mdv_client *client = mdv_alloc(sizeof(mdv_client), "client");

    if (!client)
    {
        MDV_LOGE("No memory for new client");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_free, client, "client");

    memset(client, 0, sizeof *client);

    client->response_timeout = config->connection.response_timeout * 1000;

    client->connection = mdv_safeptr_create(0,
                                            (mdv_safeptr_retain_fn)mdv_connection_retain,
                                            (mdv_safeptr_release_fn)mdv_connection_release);

    if (!client->connection)
    {
        MDV_LOGE("No memory for new connection");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_safeptr_free, client->connection);

    // Channels manager
    mdv_chaman_config const chaman_config =
    {
        .channel =
        {
            .retry_interval = config->connection.retry_interval,
            .keepidle       = config->connection.keepidle,
            .keepcnt        = config->connection.keepcnt,
            .keepintvl      = config->connection.keepintvl,
            .handshake      = mdv_client_handshake_impl,
            .accept         = mdv_client_accept_impl,
            .create         = mdv_client_channel_create_impl,
        },
        .threadpool =
        {
            .size = config->threadpool.size,
            .thread_attrs =
            {
                .stack_size = MDV_THREAD_STACK_SIZE
            }
        },
        .userdata = client
    };

    client->chaman = mdv_chaman_create(&chaman_config);

    if (!client->chaman)
    {
        MDV_LOGE("Channels manager creation failed");
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_rollbacker_push(rollbacker, mdv_chaman_free, client->chaman);

    // Connect to server
    mdv_errno err = mdv_chaman_dial(client->chaman, config->db.addr, MDV_USER_CHANNEL);

    if (err != MDV_OK)
    {
        char err_msg[128];
        MDV_LOGE("Connection to '%s' failed with error: %s (%d)",
                    config->db.addr, mdv_strerror(err, err_msg, sizeof err_msg), err);
        mdv_rollback(rollbacker);
        return 0;
    }

    // Wait connection
    mdv_connection *con = mdv_client_channel_retain(client);

    for(uint32_t tiemout = 0;
        !con && tiemout < client->response_timeout;
        con = mdv_client_channel_retain(client))
    {
        tiemout += 100;
        mdv_sleep(100);
    }

    if (!con)
    {
        MDV_LOGE("Connection to '%s' failed", config->db.addr);
        mdv_rollback(rollbacker);
        return 0;
    }

    mdv_connection_release(con);

    mdv_rollbacker_free(rollbacker);

    return client;
}


void mdv_client_close(mdv_client *client)
{
    if (client)
    {
        mdv_chaman_free(client->chaman);
        mdv_safeptr_free(client->connection);
        mdv_free(client, "client");
    }
}


mdv_table * mdv_create_table(mdv_client *client, mdv_table_desc *desc)
{
    mdv_msg_create_table create_table =
    {
        .desc = desc
    };

    binn create_table_msg;

    if (!mdv_msg_create_table_binn(&create_table, &create_table_msg))
        return 0;

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_create_table_id,
            .size = binn_size(&create_table_msg)
        },
        .payload = binn_ptr(&create_table_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&create_table_msg);

    mdv_table *tbl = 0;

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(table_info):
            {
                mdv_msg_table_info info;

                err = mdv_client_table_info_handler(&resp, &info);

                tbl = mdv_table_create(&info.id, desc);

                break;
            }

            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return tbl;
}


mdv_table * mdv_get_table(mdv_client *client, mdv_uuid const *uuid)
{
    mdv_msg_get_table get_table =
    {
        .id = *uuid
    };

    binn get_table_msg;

    if (!mdv_msg_get_table_binn(&get_table, &get_table_msg))
        return 0;

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_get_table_id,
            .size = binn_size(&get_table_msg)
        },
        .payload = binn_ptr(&get_table_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&get_table_msg);

    mdv_table *tbl = 0;

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(table_desc):
            {
                mdv_table_desc *desc = mdv_client_table_desc_handler(&resp);

                if (desc)
                {
                    tbl = mdv_table_create(uuid, desc);
                    mdv_free(desc, "table_desc");
                    break;
                }
                // fallthrough
            }

            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return tbl;
}


mdv_topology * mdv_get_topology(mdv_client *client)
{
    mdv_topology *topology = 0;

    mdv_msg_get_topology get_topology = {};

    binn binn_msg;

    if (!mdv_msg_get_topology_binn(&get_topology, &binn_msg))
        return 0;

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_get_topology_id,
            .size = binn_size(&binn_msg)
        },
        .payload = binn_ptr(&binn_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&binn_msg);

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(topology):
            {
                err = mdv_client_topology_handler(&resp, &topology);
                break;
            }

            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return topology;
}


mdv_hashmap * mdv_get_routes(mdv_client *client)
{
    mdv_topology *topology = mdv_get_topology(client);

    mdv_hashmap *routes = 0;

    if (topology)
    {
        mdv_connection *con = mdv_client_channel_retain(client);

        if(con)
        {
            routes = mdv_routes_find(topology, mdv_connection_uuid(con));
            mdv_connection_release(con);
        }

        mdv_topology_release(topology);
    }

    return routes;
}


mdv_errno mdv_insert_rows(mdv_client *client, mdv_table *table, mdv_rowset *rowset)
{
    binn serialized_rows;

    if (!mdv_binn_rowset(rowset, &serialized_rows, mdv_table_description(table)))
        return MDV_FAILED;

    mdv_msg_insert_into insert_msg =
    {
        .table = *mdv_table_uuid(table),
        .rows = &serialized_rows
    };

    binn insert_into_msg;

    if (!mdv_msg_insert_into_binn(&insert_msg, &insert_into_msg))
    {
        binn_free(&serialized_rows);
        return MDV_FAILED;
    }

    binn_free(&serialized_rows);

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_insert_into_id,
            .size = binn_size(&insert_into_msg)
        },
        .payload = binn_ptr(&insert_into_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&insert_into_msg);

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return err;
}


/// Set of rows enumerator
typedef struct
{
    mdv_enumerator          base;       ///< Base type for rowset enumerator
    uint32_t                view_id;    ///< View identifier
    mdv_client             *client;     ///< Client descriptor
    mdv_table              *table;      ///< Table descriptor
    mdv_table              *table_slice;///< Table descriptor slice
    mdv_rowset             *rowset;     ///< Current fetched rowset
    mdv_bitset             *fields;     ///< Fields mask for fetching
} mdv_rowset_enumerator_impl;


static mdv_enumerator * mdv_rowset_enumerator_impl_retain(mdv_enumerator *enumerator)
{
    atomic_fetch_add_explicit(&enumerator->rc, 1, memory_order_acquire);
    return enumerator;
}


static uint32_t mdv_rowset_enumerator_impl_release(mdv_enumerator *enumerator)
{
    uint32_t rc = 0;

    if (enumerator)
    {
        mdv_rowset_enumerator_impl *impl = (mdv_rowset_enumerator_impl *)enumerator;

        rc = atomic_fetch_sub_explicit(&enumerator->rc, 1, memory_order_release) - 1;

        if (!rc)
        {
            mdv_table_release(impl->table);
            mdv_table_release(impl->table_slice);
            mdv_rowset_release(impl->rowset);
            mdv_bitset_release(impl->fields);
            memset(impl, 0, sizeof *impl);
            mdv_free(enumerator, "rowset_enumerator");
        }
    }

    return rc;
}


static mdv_errno mdv_rowset_enumerator_impl_reset(mdv_enumerator *enumerator)
{
    return MDV_NO_IMPL;
}


static mdv_errno mdv_rowset_enumerator_impl_next(mdv_enumerator *enumerator)
{
    mdv_rowset_enumerator_impl *impl = (mdv_rowset_enumerator_impl *)enumerator;
    mdv_client                 *client = impl->client;

    mdv_rowset_release(impl->rowset);
    impl->rowset = 0;

    mdv_msg_fetch const msg =
    {
        .id = impl->view_id
    };

    binn binn_msg;

    if (!mdv_msg_fetch_binn(&msg, &binn_msg))
        return MDV_FAILED;

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_fetch_id,
            .size = binn_size(&binn_msg)
        },
        .payload = binn_ptr(&binn_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&binn_msg);

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(rowset):
            {
                impl->rowset = mdv_client_rowset_handler(&resp, impl->table_slice, &err);
                break;
            }

            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return err;
}


static void * mdv_rowset_enumerator_impl_current(mdv_enumerator *enumerator)
{
    mdv_rowset_enumerator_impl *impl = (mdv_rowset_enumerator_impl *)enumerator;
    return impl->rowset ? mdv_rowset_retain(impl->rowset) : 0;
}


static mdv_errno mdv_select_request(mdv_client *client,
                                    mdv_table  *table,
                                    mdv_bitset *fields,
                                    char const *filter,
                                    uint32_t   *view_id)
{
    mdv_msg_select const select =
    {
        .table  = *mdv_table_uuid(table),
        .fields = fields,
        .filter = filter
    };

    binn select_msg;

    if (!mdv_msg_select_binn(&select, &select_msg))
        return MDV_FAILED;

    mdv_msg req =
    {
        .hdr =
        {
            .id   = mdv_msg_select_id,
            .size = binn_size(&select_msg)
        },
        .payload = binn_ptr(&select_msg)
    };

    mdv_msg resp;

    mdv_errno err = mdv_client_send(client, &req, &resp, client->response_timeout);

    binn_free(&select_msg);

    if (err == MDV_OK)
    {
        switch(resp.hdr.id)
        {
            case mdv_message_id(view):
            {
                err = mdv_client_view_handler(&resp, view_id);
                break;
            }

            case mdv_message_id(status):
            {
                if (mdv_client_status_handler(&resp, &err) == MDV_OK)
                    break;
                // fallthrough
            }

            default:
                err = MDV_FAILED;
                MDV_LOGE("Unexpected response");
                break;
        }

        mdv_free_msg(&resp);
    }

    return err;
}


mdv_enumerator * mdv_select(mdv_client *client,
                            mdv_table  *table,
                            mdv_bitset *fields,
                            char const *filter,
                            mdv_table  **result_table)
{
    mdv_table *table_slice = mdv_table_slice(table, fields);

    if (!table_slice)
    {
        MDV_LOGE("Table descriptor slice failed");
        return 0;
    }

    uint32_t view_id = 0;

    mdv_errno err = mdv_select_request(client,
                                       table,
                                       fields,
                                       filter,
                                       &view_id);

    if (err != MDV_OK)
    {
        mdv_table_release(table_slice);
        return 0;
    }

    mdv_rowset_enumerator_impl *enumerator =
                mdv_alloc(sizeof(mdv_rowset_enumerator_impl),
                        "rowset_enumerator");

    if (!enumerator)
    {
        MDV_LOGE("No memory for rows iterator");
        mdv_table_release(table_slice);
        return 0;
    }

    atomic_init(&enumerator->base.rc, 1);

    static mdv_ienumerator const vtbl =
    {
        .retain  = mdv_rowset_enumerator_impl_retain,
        .release = mdv_rowset_enumerator_impl_release,
        .reset   = mdv_rowset_enumerator_impl_reset,
        .next    = mdv_rowset_enumerator_impl_next,
        .current = mdv_rowset_enumerator_impl_current
    };

    enumerator->base.vptr = &vtbl;

    enumerator->view_id     = view_id;
    enumerator->client      = client;
    enumerator->table       = mdv_table_retain(table);
    enumerator->table_slice = table_slice;
    enumerator->rowset      = 0;
    enumerator->fields      = mdv_bitset_retain(fields);

    if (result_table)
        *result_table = mdv_table_retain(table_slice);

    return &enumerator->base;
}
