#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <mdv_binn.h>
#include <mdv_types.h>
#include <mdv_topology.h>


/*
   User                               Server
     | HELLO >>>>>                      |
     |             <<<<< HELLO / STATUS |
     |                                  |
     | CREATE TABLE >>>>>               |
     |        <<<<< TABLE INFO / STATUS |
     |                                  |
     | GET TOPOLOGY >>>>>               |
     |          <<<<< TOPOLOGY / STATUS |
     |                                  |
     | INSERT ROW >>>>>                 |
     |       <<<<< INSERT INFO / STATUS |
 */


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
    char const *message;
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
    mdv_growid  id;
);


mdv_message_def(get_topology, 5,
);


mdv_message_def(topology, 6,
    mdv_topology *topology;
);


mdv_message_id_def(insert_row, 7);
#define mdv_msg_insert_row(N)                           \
    struct mdv_msg_insert_row_##N                       \
    {                                                   \
        mdv_uuid      table;                            \
        mdv_row(N)    row;                              \
    }
typedef mdv_msg_insert_row(1) mdv_msg_insert_row_base;

mdv_message_def(row_info, 8,
    mdv_growid  id;
);

char const *                mdv_msg_name                    (uint32_t id);


bool                        mdv_binn_hello                  (mdv_msg_hello const *msg, binn *obj);
bool                        mdv_unbinn_hello                (binn const *obj, mdv_msg_hello *msg);


bool                        mdv_binn_status                 (mdv_msg_status const *msg, binn *obj);
bool                        mdv_unbinn_status               (binn const *obj, mdv_msg_status *msg);


bool                        mdv_binn_create_table           (mdv_msg_create_table_base const *msg, binn *obj);
mdv_msg_create_table_base * mdv_unbinn_create_table         (binn const *obj);


bool                        mdv_binn_table_info             (mdv_msg_table_info const *msg, binn *obj);
bool                        mdv_unbinn_table_info           (binn const *obj, mdv_msg_table_info *msg);


bool                        mdv_binn_get_topology           (mdv_msg_get_topology const *msg, binn *obj);
bool                        mdv_unbinn_get_topology         (binn const *obj, mdv_msg_get_topology *msg);


bool                        mdv_binn_topology               (mdv_msg_topology const *msg, binn *obj);
mdv_topology              * mdv_unbinn_topology             (binn const *obj);


bool                        mdv_binn_insert_row             (mdv_msg_insert_row_base const *msg,  mdv_field const * fields, binn *obj);
mdv_msg_insert_row_base   * mdv_unbinn_insert_row           (binn const * obj, mdv_field const * fields);


bool                        mdv_binn_row_info               (mdv_msg_row_info const *msg, binn *obj);
bool                        mdv_unbinn_row_info             (binn const *obj, mdv_msg_row_info *msg);