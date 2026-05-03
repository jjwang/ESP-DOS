#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc;
    int a_flag = 0;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'a') a_flag = 1;

    if (a_flag) {
        char desc[64];
        sys->get_chip_desc(desc, sizeof(desc));
        sys->printf("OpenCrab v0.1.0 %s\n", desc);
    } else {
        sys->println("OpenCrab");
    }
    sys->exit(0);
}
