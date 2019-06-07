#include "mdv_service.h"


bool mdv_service_init(mdv_service *svc, char const *cfg_file_path)
{
    if (!mdv_load_config(cfg_file_path, &svc->config))
        return false;
    return true;
}


bool mdv_service_start(mdv_service *svc)
{
    return true;
}

