#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <mdv_binn.h>
#include <mdv_types.h>


/// Client types
typedef enum mdv_cli_type
{
    MDV_CLI_USER = 0,       ///< User
    MDV_CLI_PEER = 1        ///< Peer
} mdv_cli_type;


#define mdv_message_id_def(name, id)                    \
    enum { mdv_msg_##name##_id = id };


#define mdv_message_id(name) mdv_msg_##name##_id


#define mdv_message_def(name, id, fields)               \
    mdv_message_id_def(name, id);                       \
    typedef struct                                      \
    {                                                   \
        fields;                                         \
    } mdv_msg_##name;


mdv_message_def(status, 1,
    int         err;
    char        message[1];
);


mdv_message_def(hello, 2,
    uint32_t    version;
    mdv_uuid    uuid;
);


mdv_message_id_def(create_table, 3);
#define mdv_msg_create_table(N)                         \
    struct mdv_msg_create_table_##N                     \
    {                                                   \
        mdv_table(N)    table;                          \
    }
typedef mdv_msg_create_table(1) mdv_msg_create_table_base;


mdv_message_def(table_info, 4,
    mdv_uuid    uuid;
);


char const *                mdv_msg_name            (uint32_t id);


bool                        mdv_binn_hello          (mdv_msg_hello const *msg, binn *obj);
bool                        mdv_unbinn_hello        (binn const *obj, mdv_msg_hello *msg);


bool                        mdv_binn_status         (mdv_msg_status const *msg, binn *obj);
mdv_msg_status *            mdv_unbinn_status       (binn const *obj);


bool                        mdv_binn_create_table   (mdv_msg_create_table_base const *msg, binn *obj);
mdv_msg_create_table_base * mdv_unbinn_create_table (binn const *obj);


bool                        mdv_binn_table_info     (mdv_msg_table_info const *msg, binn *obj);
bool                        mdv_unbinn_table_info   (binn const *obj, mdv_msg_table_info *msg);

