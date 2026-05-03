#include "../include/command_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    sys->println("Hello from ELF! Welcome to OpenCrab.");
    sys->exit(0);
}
