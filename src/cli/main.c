#include "minicontainer/info.h"
#include "minicontainer/error.h"
#include "minicontainer/image.h"

#include <stdio.h>
#include <string.h>

#ifndef MC_VERSION
#define MC_VERSION "unknown"
#endif
#ifndef MC_GIT_COMMIT
#define MC_GIT_COMMIT "unknown"
#endif

static void usage(FILE *stream) {
    (void)fprintf(stream,
                  "usage:\n"
                  "  minicontainer version [--json]\n"
                  "  minicontainer info [--json]\n"
                  "  minicontainer image import NAME ROOTFS_TAR [--json]\n");
}

int main(int argc, char **argv) {
    int json = 0;
    struct mc_error error = {0};

    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (argc == 3 && strcmp(argv[1], "image") != 0) {
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
    if (strcmp(argv[1], "image") == 0 && argc >= 3 && strcmp(argv[2], "import") == 0) {
        struct mc_image_result imported;
        if (argc != 5 && argc != 6) {
            usage(stderr);
            return MC_EXIT_USAGE;
        }
        json = argc == 6 && strcmp(argv[5], "--json") == 0;
        if (argc == 6 && json == 0) {
            usage(stderr);
            return MC_EXIT_USAGE;
        }
        if (mc_image_import(argv[3], argv[4], &imported, &error) != 0) {
            mc_error_print(&error, json);
            return error.code;
        }
        if (json != 0) {
            (void)printf("{\"name\":\"%s\",\"digest\":\"sha256:%s\","
                         "\"rootfs\":\"%s\"}\n", argv[3], imported.digest,
                         imported.rootfs);
        } else {
            (void)printf("imported %s sha256:%s\n", argv[3], imported.digest);
        }
        return MC_EXIT_OK;
    }
    usage(stderr);
    return 2;
}
