#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    int total = 0, used = 0;
    sys->get_fs_info(&total, &used);
    int free = total - used;
    int pct = total > 0 ? (used * 100 / total) : 0;

    sys->printf("Volume: /spiffs\n");
    sys->printf("Total space:  %dK\n", total / 1024);
    sys->printf("Used space:   %dK\n", used / 1024);
    sys->printf("Free space:   %dK\n", free / 1024);
    sys->printf("Usage:        %d%%\n", pct);
    sys->exit(0);
}
