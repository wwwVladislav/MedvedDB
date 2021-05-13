#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <mdv_service.h>
#include <mdv_alloc.h>
#include <mdv_log.h>


static mdv_service *service = 0;


static int help()
{
    printf("NAME\n");
    printf("    MedvedDB - distributed, column store, NoSQL database\n\n");
    printf("SYNOPSIS\n");
    printf("    medved --cfg=medved.conf\n\n");
    printf("OPTIONS\n");
    printf("    -c, --cfg\n");
    printf("           configuration file path\n");
    printf("    --help display this help and exit\n");
    return 0;
}


static int usage()
{
    printf("Usage: medved --cfg=medved.conf\n");
    printf("Try 'medved --help' for more information.\n");
    return 0;
}


static void termination_signal_handler(int signal)
{
    MDV_LOGI("Service is terminating [Signal: %s]", strsignal(signal));
    mdv_service_stop(service);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("medved: missing operand\n");
        printf("Try 'medved --help' for more information.\n");
        return 0;
    }

    static struct option long_options[] =
    {
        { "help",   no_argument,        0, 0 },
        { "cfg",    required_argument,  0, 0 },
        { 0,        0,                  0, 0 }
    };

    int option_index = 0;
    int ret = 0;

    while(!service && (ret = getopt_long(argc, argv, "hc:", long_options, &option_index)) != -1)
    {
        char op = (char)ret;

        if (!op)
        {
            switch(option_index)
            {
                case 0: op = 'h'; break;
                case 1: op = 'c'; break;
            }
        }

        switch(op)
        {
            case 'h':
                return help();

            case 'c':
            {
                if (!optarg)
                    return usage();

                service = mdv_service_create(optarg);

                if (!service)
                    return -1;

                break;
            }
        }
    }

    if (!service)
        return usage();

    // Register signal handlers
    signal(SIGINT,  termination_signal_handler);
    signal(SIGTERM, termination_signal_handler);
    signal(SIGQUIT, termination_signal_handler);
    signal(SIGPIPE, SIG_IGN);

    MDV_LOGI("Service is starting...");

    if (mdv_service_start(service))
        mdv_service_wait(service);

    MDV_LOGI("Service is stoped");

    mdv_service_free(service);

    return 0;
}
