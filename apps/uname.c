#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc;
    char desc[64];
    sys->get_chip_desc(desc, sizeof(desc));
    sys->printf("ESP-DOS Version 1.0\n");
    sys->printf("(%s)\n", desc);
    sys->exit(0);
}
