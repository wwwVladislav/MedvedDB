#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <mdv_binn.h>
#include <mdv_table.h>
#include <mdv_rowset.h>
#include <mdv_topology.h>
#include <mdv_bitset.h>


/*
   User                               Server
     | HELLO >>>>>                      |
     |             <<<<< HELLO / STATUS |
     |                                  |
     | CREATE TABLE >>>>>               |
     |        <<<<< TABLE INFO / STATUS |
     |                                  |
     | GET TABLE >>>>>                  |
     |        <<<<< TABLE DESC / STATUS |
     |                                  |
     | GET TOPOLOGY >>>>>               |
     |          <<<<< TOPOLOGY / STATUS |
     |                                  |
     | INSERT INTO >>>>>                |
     |                     <<<<< STATUS |
     |                                  |
     | SELECT >>>>>                     |
     |                       <<<<< VIEW |
     |                                  |
     | FETCH >>>>>                      |
     |            <<<<< ROWSET / STATUS |
     |                                  |
     | DELETE FROM >>>>>                |
     |                     <<<<< STATUS |
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


mdv_message_def(create_table, 3,
    mdv_table_desc *desc;
);


mdv_message_def(get_table, 4,
    mdv_uuid    id;
);


mdv_message_def(table_info, 5,
    mdv_uuid    id;
);


mdv_message_def(table_desc, 6,
    mdv_table_desc const *desc;
);


mdv_message_def(get_topology, 7,
);


mdv_message_def(topology, 8,
    mdv_topology *topology;
);


mdv_message_def(insert_into, 9,
    mdv_uuid    table;
    binn       *rows;
);


mdv_message_def(select, 10,
    mdv_uuid    table;
    mdv_bitset *fields;
    char const *filter;
);


mdv_message_def(view, 11,
    uint32_t    id;
);


mdv_message_def(fetch, 12,
    uint32_t    id;
);


mdv_message_def(rowset, 13,
    binn       *rows;
);


mdv_message_def(delete_from, 14,
    mdv_uuid    table;
    char const *filter;
);

char const *                mdv_msg_name                    (uint32_t id);


bool                        mdv_msg_status_binn             (mdv_msg_status const *msg, binn *obj);
bool                        mdv_msg_status_unbinn           (binn const *obj, mdv_msg_status *msg);


bool                        mdv_msg_create_table_binn       (mdv_msg_create_table const *msg, binn *obj);
bool                        mdv_msg_create_table_unbinn     (binn const *obj, mdv_msg_create_table *msg);
void                        mdv_msg_create_table_free       (mdv_msg_create_table *msg);


bool                        mdv_msg_get_table_binn          (mdv_msg_get_table const *msg, binn *obj);
bool                        mdv_msg_get_table_unbinn        (binn const *obj, mdv_msg_get_table *msg);


bool                        mdv_msg_table_info_binn         (mdv_msg_table_info const *msg, binn *obj);
bool                        mdv_msg_table_info_unbinn       (binn const *obj, mdv_msg_table_info *msg);


bool                        mdv_msg_get_topology_binn       (mdv_msg_get_topology const *msg, binn *obj);
bool                        mdv_msg_get_topology_unbinn     (binn const *obj, mdv_msg_get_topology *msg);


bool                        mdv_msg_topology_binn           (mdv_msg_topology const *msg, binn *obj);
mdv_topology              * mdv_msg_topology_unbinn         (binn const *obj);


bool                        mdv_msg_insert_into_binn        (mdv_msg_insert_into const *msg, binn *obj);
bool                        mdv_msg_insert_into_unbinn      (binn const * obj, mdv_msg_insert_into *msg);


bool                        mdv_msg_select_binn             (mdv_msg_select const *msg, binn *obj);
bool                        mdv_msg_select_unbinn           (binn const * obj, mdv_msg_select *msg);
void                        mdv_msg_select_free             (mdv_msg_select *msg);


bool                        mdv_msg_view_binn               (mdv_msg_view const *msg, binn *obj);
bool                        mdv_msg_view_unbinn             (binn const * obj, mdv_msg_view *msg);


bool                        mdv_msg_fetch_binn              (mdv_msg_fetch const *msg, binn *obj);
bool                        mdv_msg_fetch_unbinn            (binn const * obj, mdv_msg_fetch *msg);


bool                        mdv_msg_rowset_binn             (mdv_msg_rowset const *msg, binn *obj);
bool                        mdv_msg_rowset_unbinn           (binn const * obj, mdv_msg_rowset *msg);


bool                        mdv_msg_delete_from_binn        (mdv_msg_delete_from const *msg, binn *obj);
bool                        mdv_msg_delete_from_unbinn      (binn const * obj, mdv_msg_delete_from *msg);
