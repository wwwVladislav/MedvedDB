#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <mdv_service.h>

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

    mdv_service service;
    bool        service_is_ok = false;

    while((ret = getopt_long(argc, argv, "hc:", long_options, &option_index)) != -1)
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

                if (!(service_is_ok = mdv_service_init(&service, optarg)))
                    return -1;

                break;
            }
        }
    }

    if (!service_is_ok)
        return usage();

    if (!mdv_service_start(&service))
        return -1;

    return 0;
}
