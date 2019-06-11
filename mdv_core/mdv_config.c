#include "mdv_config.h"
#include <mdv_log.h>
#include <ini.h>
#include <string.h>


static int mdv_cfg_handler(void* user, const char* section, const char* name, const char* value)
{
    mdv_config *config = (mdv_config *)user;

    #define MDV_CFG_MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    #define MDV_CFG_CHECK(v)                                                                    \
        if(mdv_str_empty(v))                                                                 \
        {                                                                                       \
            MDV_LOGI("No memory for configuration parameter %s:%s '%s'", section, name, value); \
            return 0;                                                                           \
        }

    if (MDV_CFG_MATCH("server", "listen"))
    {
        config->server.listen = mdv_str_pdup(config->mempool, value);
        MDV_CFG_CHECK(config->server.listen);
        MDV_LOGI("Listen: %s", config->server.listen.ptr);
        return 1;
    }
    else if (MDV_CFG_MATCH("storage", "path"))
    {
        config->storage.path = mdv_str_pdup(config->mempool, value);
        MDV_CFG_CHECK(config->storage.path);
        MDV_LOGI("Storage path: %s", config->storage.path.ptr);
        return 1;
    }
    else
    {
        MDV_LOGE("Unknown section/name: [%s] %s", section, name);
        return 0;
    }

    #undef MDV_CFG_MATCH
    #undef MDV_CFG_CHECK

    return 1;
}


bool mdv_load_config(char const *path, mdv_config *config)
{
    mdv_stack_clear(config->mempool);

    if (ini_parse(path, mdv_cfg_handler, config) < 0)
    {
        MDV_LOGE("Can't load '%s'\n", path);
        return false;
    }

    if (mdv_str_empty(config->server.listen)
        || mdv_str_empty(config->storage.path))
    {
        MDV_LOGE("Mandatory configuration parameters weren't not provided\n");
        return false;
    }

    return true;
}
