#pragma once
#include <mdv_messages.h>


mdv_message_def(p2p_hello, 1000,
    uint32_t    version;            ///< Version
    mdv_uuid    uuid;               ///< Unique identifier
    char       *listen;             ///< Listening address
    char        dataspace[1];       ///< Data space
);


char const *        mdv_p2p_msg_name        (uint32_t id);


bool                mdv_binn_p2p_hello      (mdv_msg_p2p_hello const *msg, binn *obj);
mdv_msg_p2p_hello * mdv_unbinn_p2p_hello    (binn const *obj);