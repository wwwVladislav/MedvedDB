#include "mdv_service.h"
#include <mdv_log.h>
#include <mdv_thread.h>


static void mdv_service_configure_logging(mdv_config const *config)
{
    if (!mdv_str_empty(config->log.level))
    {
        switch(*config->log.level.ptr)
        {
            case 'f':   mdv_logf_set_level(ZF_LOG_FATAL);   break;
            case 'e':   mdv_logf_set_level(ZF_LOG_ERROR);   break;
            case 'w':   mdv_logf_set_level(ZF_LOG_WARN);    break;
            case 'i':   mdv_logf_set_level(ZF_LOG_INFO);    break;
            case 'd':   mdv_logf_set_level(ZF_LOG_DEBUG);   break;
            case 'v':   mdv_logf_set_level(ZF_LOG_VERBOSE); break;
            case 'n':   mdv_logf_set_level(ZF_LOG_NONE);    break;
        }
    }
}


bool mdv_service_init(mdv_service *svc, char const *cfg_file_path)
{
    if (!mdv_load_config(cfg_file_path, &svc->config))
    {
        MDV_LOGE("Service initialization failed. Can't load '%s'\n", cfg_file_path);
        return false;
    }

    mdv_service_configure_logging(&svc->config);

    svc->storage.metainf = mdv_metainf_storage_open(svc->config.storage.path.ptr);
    if (!svc->storage.metainf)
    {
        MDV_LOGE("Service initialization failed. Can't create metainf storage '%s'\n", svc->config.storage.path.ptr);
        return false;
    }

    if (!mdv_metainf_load(&svc->metainf, svc->storage.metainf))
    {
        MDV_LOGE("DB meta information loading was failed. Path: '%s'\n", svc->config.storage.path.ptr);
        return false;
    }

    mdv_metainf_validate(&svc->metainf);
    mdv_metainf_flush(&svc->metainf, svc->storage.metainf);

    // Print node information to log
    MDV_LOGI("Storage version: %u", svc->metainf.version.value);
    char tmp[33];
    mdv_string uuid_str = mdv_str_static(tmp);
    if (mdv_uuid_to_str(&svc->metainf.uuid.value, &uuid_str))
        MDV_LOGI("Node UUID: %s", uuid_str.ptr);

    svc->is_started = false;

    return true;
}


void mdv_service_free(mdv_service *svc)
{
    mdv_storage_release(svc->storage.metainf);
}


bool mdv_service_start(mdv_service *svc)
{
    svc->is_started = true;

    while(svc->is_started)
    {
        mdv_usleep(1000);
    }
    return true;
}


void mdv_service_stop(mdv_service *svc)
{
    svc->is_started = false;
}
