#include "mdv_errno.h"
#include <string.h>
#include <errno.h>


mdv_errno mdv_error()
{
    switch(errno)
    {
        case EAGAIN:    return MDV_EAGAIN;
        case EEXIST:    return MDV_EEXIST;
    }
    return errno;
}


char const * mdv_strerror(mdv_errno err)
{
    switch(err)
    {
        case MDV_OK:            return "Operation was successfully completed";
        case MDV_FAILED:        return "Operation failed due the unknown error";
        case MDV_INVALID_ARG:   return "Function argument is invalid";
        case MDV_EAGAIN:        return "Resource temporarily unavailable";
        case MDV_CLOSED:        return "File descriptor was closed";
        case MDV_EEXIST:        return "File exists";
    }

    static _Thread_local char buf[1024];

    strerror_r(err, buf, sizeof buf);
    buf[sizeof buf - 1] = 0;

    return buf;
}
