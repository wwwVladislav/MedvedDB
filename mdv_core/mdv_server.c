#include "mdv_server.h"
#include "mdv_config.h"
#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <mdv_log.h>
#include <mdv_alloc.h>
#include <mdv_version.h>
#include <mdv_protocol.h>
#include <mdv_status.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>


typedef struct mdv_server_work
{
    enum
    {
        MDV_SERVER_WORK_INIT,
        MDV_SERVER_WORK_RECV,
        MDV_SERVER_WORK_SEND
    }                           state;
    nng_socket                  sock;
    nng_aio                    *aio;
    nng_msg                    *msg;
    mdv_message_handler        *handlers;
} mdv_server_work;


struct mdv_server
{
    nng_socket      sock;
    nng_listener    listener;
    mdv_tablespace *tablespace;

    struct
    {
        uint32_t         count;
        mdv_server_work *list;
    } works;

    mdv_message_handler handlers[mdv_msg_count];
};


static bool mdv_server_hello_handler(mdv_message const *msg, void *arg, mdv_message *response)
{
    (void)arg;

    if (msg->id != mdv_msg_hello_id)
    {
        MDV_LOGE("Invalid handler was registered for '%s' message", mdv_msg_name(msg->id));
        return false;
    }

    mdv_msg_hello hello = {};

    if (!mdv_unbinn_hello(&msg->body, &hello))
    {
        MDV_LOGE("Invalid '%s' message", mdv_msg_name(msg->id));
        return false;
    }

    static uint8_t const signature[] = MDV_HELLO_SIGNATURE;

    if (memcmp(signature, hello.signature, sizeof signature) != 0)
    {
        MDV_LOGE("Signature for '%s' message is incorrect", mdv_msg_name(msg->id));
        return false;
    }

    response->id = mdv_msg_status_id;

    if(hello.version != MDV_VERSION)
    {
        MDV_LOGE("Invalid client version");
        mdv_msg_status status = { MDV_STATUS_INVALID_PROTOCOL_VERSION, { 0 } };
        return mdv_binn_status(&status, &response->body);
    }

    mdv_msg_status status = { MDV_STATUS_OK, { 0 } };
    return mdv_binn_status(&status, &response->body);
}


static bool mdv_server_create_table_handler(mdv_message const *msg, void *arg, mdv_message *response)
{
    mdv_tablespace *tablespace = (mdv_tablespace *)arg;

    if (msg->id != mdv_msg_create_table_id)
    {
        MDV_LOGE("Invalid handler was registered for '%s' message", mdv_msg_name(msg->id));
        return false;
    }

    mdv_msg_create_table_base *create_table = mdv_unbinn_create_table(&msg->body);

    if (!create_table)
    {
        MDV_LOGE("Invalid '%s' message", mdv_msg_name(msg->id));
        return false;
    }

    int err = mdv_tablespace_create_table(tablespace, (mdv_table_base*)&create_table->table);

    if (err == MDV_STATUS_OK)
    {
        mdv_msg_table_info table_info =
        {
            .uuid = create_table->table.uuid
        };
        mdv_free(create_table);

        response->id = mdv_msg_table_info_id;

        return mdv_binn_table_info(&table_info, &response->body);
    }

    mdv_free(create_table);

    mdv_msg_status status = { err, { 0 } };

    response->id = mdv_msg_status_id;

    return mdv_binn_status(&status, &response->body);
}


static bool mdv_server_handle_message(mdv_server_work *work, uint32_t msg_id, uint8_t *msg_body, mdv_message *response)
{
    if (msg_id <= mdv_msg_unknown_id || msg_id >= mdv_msg_count)
    {
        MDV_LOGW("Invalid message id '%u'", msg_id);
        return false;
    }

    mdv_message_handler handler = work->handlers[msg_id];

    if(!handler.fn)
    {
        MDV_LOGW("Handler for '%s' message was not registered.", mdv_msg_name(msg_id));
        return false;
    }

    mdv_message request = { msg_id };

    if(!binn_load(msg_body, &request.body))
    {
        MDV_LOGW("Message '%s' discarded", mdv_msg_name(msg_id));
        return false;
    }

    bool ret = handler.fn(&request, handler.arg, response);

    binn_free(&request.body);

    return ret;
}


static bool mdv_server_nng_message_build(nng_msg *nng_msg, uint32_t req_id, mdv_message const *message)
{
    nng_msg_clear(nng_msg);

    mdv_msg_hdr const hdr =
    {
        .msg_id = message->id,
        .req_id = req_id
    };

    if (hdr.msg_id > mdv_msg_unknown_id
        && hdr.msg_id < mdv_msg_count)
    {
        int err;

        if (!(err = nng_msg_append_u32(nng_msg, hdr.msg_id))
            && !(err = nng_msg_append_u32(nng_msg, hdr.req_id)))
        {
            err = nng_msg_append(nng_msg, binn_ptr((void *)&message->body), binn_size((void *)&message->body));
            if (!err)
                return true;
            MDV_LOGE("%s (%d)", nng_strerror(err), err);
        }
        else
            MDV_LOGE("%s (%d)", nng_strerror(err), err);
    }

    return false;
}


static void mdv_server_cb(mdv_server_work *work)
{
    mdv_alloc_thread_initialize();

    switch(work->state)
    {
        case MDV_SERVER_WORK_INIT:
        {
            work->state = MDV_SERVER_WORK_RECV;
            nng_recv_aio(work->sock, work->aio);
            break;
        }

        case MDV_SERVER_WORK_RECV:
        {
            int err = nng_aio_result(work->aio);

            if(err == NNG_ECLOSED)
                break;

            if (!err)
            {
                nng_msg *msg = nng_aio_get_msg(work->aio);

                if (msg)
                {
                    //nng_pipe pipe = nng_msg_get_pipe(msg);

                    size_t len = nng_msg_len(msg);
                    uint8_t *body = (uint8_t *)nng_msg_body(msg);

                    if (len >= sizeof(mdv_msg_hdr) && body)
                    {
                        mdv_msg_hdr hdr = *(mdv_msg_hdr*)body;
                        hdr.msg_id = ntohl(hdr.msg_id);
                        hdr.req_id = ntohl(hdr.req_id);

                        MDV_LOGI("<<< '%s' %zu bytes", mdv_msg_name(hdr.msg_id), len - sizeof hdr);

                        mdv_message response;

                        if (mdv_server_handle_message(work, hdr.msg_id, body + sizeof hdr, &response)
                            && mdv_server_nng_message_build(msg, hdr.req_id, &response))
                        {
                            work->state = MDV_SERVER_WORK_SEND;
                            binn_free(&response.body);
                            work->msg = msg;
                            nng_aio_set_msg(work->aio, msg);
                            nng_send_aio(work->sock, work->aio);
                            break;
                        }
                        else
                            MDV_LOGW("Message '%s' discarded", mdv_msg_name(hdr.msg_id));

                        binn_free(&response.body);
                    }
                    else
                        MDV_LOGW("Invalid message discarded. Size is %zu bytes", len);

                    nng_msg_free(msg);
                }
            }
            else
                MDV_LOGE("%s (%d)", nng_strerror(err), err);

            nng_recv_aio(work->sock, work->aio);

            break;
        }

        case MDV_SERVER_WORK_SEND:
        {
            int err = nng_aio_result(work->aio);

            if(err == NNG_ECLOSED)
                break;

            if (err)
            {
                MDV_LOGE("%s (%d)", nng_strerror(err), err);
                nng_msg_free(work->msg);
            }

            work->msg = 0;

            nng_recv_aio(work->sock, work->aio);

            work->state = MDV_SERVER_WORK_RECV;

            break;
        }
    }
}


static bool mdv_server_works_create(mdv_server *srvr)
{
    srvr->works.count = MDV_CONFIG.server.workers;

    srvr->works.list = mdv_alloc(srvr->works.count * sizeof(mdv_server_work));

    if (!srvr->works.list)
    {
        MDV_LOGE("No memory for server workers");
        return false;
    }

    memset(srvr->works.list, 0, srvr->works.count * sizeof(mdv_server_work));

    for(uint32_t i = 0; i < srvr->works.count; ++i)
    {
        srvr->works.list[i].sock = srvr->sock;
        srvr->works.list[i].state = MDV_SERVER_WORK_INIT;
        srvr->works.list[i].msg = 0;
        srvr->works.list[i].handlers = srvr->handlers;

        int rv = nng_aio_alloc(&srvr->works.list[i].aio, (void (*)(void *))mdv_server_cb, &srvr->works.list[i]);

        if (rv != 0)
        {
            MDV_LOGE("nng_aio_alloc failed: %d", rv);
            return false;
	}
    }

    return true;
}


static void mdv_server_works_init(mdv_server *srvr)
{
    for(uint32_t i = 0; i < srvr->works.count; ++i)
        mdv_server_cb(srvr->works.list + i);
}


mdv_server * mdv_server_create(mdv_tablespace *tablespace)
{
    mdv_server *server = mdv_alloc(sizeof(mdv_server));
    if(!server)
    {
        MDV_LOGE("Memory allocation for server was failed");
        return 0;
    }

    server->tablespace = tablespace;

    memset(server->handlers, 0, sizeof server->handlers);

    int err = nng_pair0_open_raw(&server->sock);
    if (err)
    {
        MDV_LOGE("NNG socket creation failed: %s (%d)", nng_strerror(err), err);
        mdv_server_free(server);
        return 0;
    }

    if (!mdv_server_works_create(server))
    {
        mdv_server_free(server);
        return 0;
    }

    err = nng_listener_create(&server->listener, server->sock, MDV_CONFIG.server.listen.ptr);
    if (err)
    {
        MDV_LOGE("NNG listener not created: %s (%d)", nng_strerror(err), err);
        mdv_server_free(server);
        return 0;
    }

    mdv_server_handler_reg(server, mdv_msg_hello_id,        (mdv_message_handler){ 0,                  mdv_server_hello_handler });
    mdv_server_handler_reg(server, mdv_msg_create_table_id, (mdv_message_handler){ server->tablespace, mdv_server_create_table_handler });

    return server;
}


bool mdv_server_start(mdv_server *srvr)
{
    int err = nng_listener_start(srvr->listener, 0);

    if (!err)
    {
        mdv_server_works_init(srvr);
        return true;
    }

    MDV_LOGE("NNG listener not started: %s (%d)", nng_strerror(err), err);

    return false;
}


void mdv_server_free(mdv_server *srvr)
{
    nng_listener_close(srvr->listener);

    nng_close(srvr->sock);

    for(uint32_t i = 0; i < srvr->works.count; ++i)
    {
        nng_aio_stop(srvr->works.list[i].aio);
        nng_aio_free(srvr->works.list[i].aio);
        nng_msg_free(srvr->works.list[i].msg);
    }

    mdv_free(srvr->works.list);

    mdv_free(srvr);
}


bool mdv_server_handler_reg(mdv_server *srvr, uint32_t msg_id, mdv_message_handler handler)
{
    if (msg_id >= sizeof srvr->handlers / sizeof *srvr->handlers)
    {
        MDV_LOGE("Server message handler not registered. Message ID %u is invalid.", msg_id);
        return false;
    }

    srvr->handlers[msg_id] = handler;

    return true;
}
