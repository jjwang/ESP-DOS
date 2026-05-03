#include "../include/command_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    sys->println("OpenCrab Terminal v0.1.0");
    sys->exit(0);
}
