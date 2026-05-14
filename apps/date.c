#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    sys->printf("Current date: %s %s\n", __DATE__, __TIME__);
    sys->exit(0);
}
