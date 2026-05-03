#include "../include/command_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) sys->print(" ");
        sys->print(argv[i]);
    }
    sys->println("");
    sys->exit(0);
}
