#include "../include/command_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    int total = 0, used = 0;
    sys->get_fs_info(&total, &used);
    sys->printf("Filesystem: %dK / %dK (%d%%)\n",
                used / 1024, total / 1024,
                total > 0 ? (used * 100 / total) : 0);
    sys->exit(0);
}
