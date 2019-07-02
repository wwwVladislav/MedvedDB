#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <mdv_binn.h>
#include <mdv_types.h>


#define mdv_message_id_def(name, id)                    \
    enum { mdv_msg_##name##_id = id };


#define mdv_message_def(name, id, fields)               \
    mdv_message_id_def(name, id);                       \
    typedef struct                                      \
    {                                                   \
        fields;                                         \
    } mdv_msg_##name;


#define MDV_HELLO_SIGNATURE                             \
    {                                                   \
        0xee, 0xe1, 0x78, 0xa7, 0x13, 0xed, 0xeb, 0xd0, \
        0xdb, 0x54, 0x3e, 0xb4, 0x59, 0x9b, 0xa5, 0x66  \
    }


enum
{
    mdv_msg_unknown_id = 0,
    mdv_msg_count      = 5
};


mdv_message_def(status, 1,
    int         err;
    char        message[1];
);


mdv_message_def(hello, 2,
    uint8_t     signature[16];
    uint32_t    version;
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


void                        mdv_msg_free            (void *msg);


bool                        mdv_binn_hello          (mdv_msg_hello const *msg, binn *obj);
bool                        mdv_unbinn_hello        (binn const *obj, mdv_msg_hello *msg);


bool                        mdv_binn_status         (mdv_msg_status const *msg, binn *obj);
mdv_msg_status *            mdv_unbinn_status       (binn const *obj);


bool                        mdv_binn_create_table   (mdv_msg_create_table_base const *msg, binn *obj);
mdv_msg_create_table_base * mdv_unbinn_create_table (binn const *obj);


bool                        mdv_binn_table_info     (mdv_msg_table_info const *msg, binn *obj);
bool                        mdv_unbinn_table_info   (binn const *obj, mdv_msg_table_info *msg);
