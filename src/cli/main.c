#include "minicontainer/info.h"

#include <stdio.h>
#include <string.h>

#ifndef MC_VERSION
#define MC_VERSION "unknown"
#endif
#ifndef MC_GIT_COMMIT
#define MC_GIT_COMMIT "unknown"
#endif

static void usage(FILE *stream) {
    (void)fprintf(stream, "usage: minicontainer <version|info> [--json]\n");
}

int main(int argc, char **argv) {
    int json = 0;

    if (argc < 2 || argc > 3) {
        usage(stderr);
        return 2;
    }
    if (argc == 3) {
        if (strcmp(argv[2], "--json") != 0) {
            usage(stderr);
            return 2;
        }
        json = 1;
    }
    if (strcmp(argv[1], "version") == 0) {
        if (json != 0) {
            (void)printf("{\"name\":\"minicontainer\",\"version\":\"%s\","
                         "\"git_commit\":\"%s\"}\n", MC_VERSION, MC_GIT_COMMIT);
        } else {
            (void)printf("minicontainer %s (%s)\n", MC_VERSION, MC_GIT_COMMIT);
        }
        return 0;
    }
    if (strcmp(argv[1], "info") == 0) {
        return mc_print_info(json);
    }
    usage(stderr);
    return 2;
}
