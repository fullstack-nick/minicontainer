#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

#include <stdio.h>
#include <string.h>

static void usage(void) {
    (void)fprintf(stderr,
                  "usage: minicontainer-shim --id ID --rootfs PATH --hostname NAME -- COMMAND [ARG...]\n");
}

int main(int argc, char **argv) {
    struct mc_run_config config;
    struct mc_error error = {0};
    int result;

    if (argc < 9 || strcmp(argv[1], "--id") != 0 || strcmp(argv[3], "--rootfs") != 0 ||
        strcmp(argv[5], "--hostname") != 0 || strcmp(argv[7], "--") != 0) {
        usage();
        return MC_EXIT_USAGE;
    }
    config.id = argv[2];
    config.rootfs = argv[4];
    config.hostname = argv[6];
    config.command = &argv[8];
    result = mc_container_run(&config, &error);
    if (result < 0) {
        mc_error_print(&error, 0);
        return error.code == 0 ? MC_EXIT_RUNTIME : error.code;
    }
    return result;
}
