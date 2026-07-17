#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    int total = sys->get_total_heap();
    int free_h = sys->get_free_heap();
    int total_p = sys->get_total_psram();
    int free_p = sys->get_free_psram();

    sys->printf("%-12s %10s %10s %10s\n", "Type", "Total", "Used", "Free");
    sys->printf("---------------------------------------------\n");
    sys->printf("%-12s %9dK %9dK %9dK\n", "Internal", total / 1024, (total - free_h) / 1024, free_h / 1024);
    if (total_p > 0) {
        sys->printf("%-12s %9dK %9dK %9dK\n", "PSRAM", total_p / 1024, (total_p - free_p) / 1024, free_p / 1024);
    }
    sys->exit(0);
}
