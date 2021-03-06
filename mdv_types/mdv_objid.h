#pragma once
#include <mdv_def.h>


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Object ID
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
typedef union __attribute__((packed))
{
    uint8_t  u8[12];    ///< representation as u8 array

    struct __attribute__((packed))
    {
        uint32_t node;  ///< Node identifier
        uint64_t id;    ///< Object identifier
    };
} mdv_objid;


enum { MDV_OBJID_STR_LEN = 25 };


char const * mdv_objid_to_str(mdv_objid const *objid, char buf[MDV_OBJID_STR_LEN]);
