#include "minicontainer/info.h"
#include "minicontainer/error.h"
#include "minicontainer/image.h"
#include "minicontainer/id.h"
#include "minicontainer/runtime.h"
#include "minicontainer/validate.h"

#include <stdio.h>
#include <stdlib.h>
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
                  "  minicontainer image import NAME ROOTFS_TAR [--json]\n"
                  "  minicontainer run --image NAME [--hostname NAME] [--env KEY=VALUE] "
                  "[--workdir PATH] [--user UID[:GID]] -- COMMAND [ARG...]\n");
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
    if (strcmp(argv[1], "run") == 0) {
        const char *image = NULL;
        char *hostname = NULL;
        char generated_hostname[16];
        char id[33];
        char rootfs[4096];
        char default_workdir[] = "/";
        char *workdir = default_workdir;
        char **environment = calloc((size_t)argc, sizeof(*environment));
        size_t environment_count = 0U;
        unsigned int user = 0U;
        unsigned int group = 0U;
        int command_index = -1;
        int index;
        struct mc_run_config config;
        int result;

        if (environment == NULL) {
            return MC_EXIT_INTERNAL;
        }
        for (index = 2; index < argc; ++index) {
            if (strcmp(argv[index], "--") == 0) {
                command_index = index + 1;
                break;
            }
            if (strcmp(argv[index], "--image") == 0 && index + 1 < argc) {
                image = argv[++index];
            } else if (strcmp(argv[index], "--hostname") == 0 && index + 1 < argc) {
                hostname = argv[++index];
            } else if (strcmp(argv[index], "--workdir") == 0 && index + 1 < argc) {
                workdir = argv[++index];
                if (workdir[0] != '/') {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--user") == 0 && index + 1 < argc) {
                if (!mc_parse_user(argv[++index], &user, &group)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--env") == 0 && index + 1 < argc) {
                char *assignment = argv[++index];
                if (!mc_valid_environment(assignment)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
                environment[environment_count++] = assignment;
            } else {
                free(environment);
                usage(stderr);
                return MC_EXIT_USAGE;
            }
        }
        if (image == NULL || command_index < 0 || command_index >= argc ||
            mc_image_resolve(image, rootfs, sizeof(rootfs), &error) != 0 ||
            mc_generate_id(id, &error) != 0) {
            if (error.code != 0) {
                mc_error_print(&error, 0);
                free(environment);
                return error.code;
            }
            free(environment);
            usage(stderr);
            return MC_EXIT_USAGE;
        }
        if (hostname == NULL) {
            (void)snprintf(generated_hostname, sizeof(generated_hostname), "mc-%.12s", id);
            hostname = generated_hostname;
        }
        config.id = id;
        config.rootfs = rootfs;
        config.hostname = hostname;
        config.workdir = workdir;
        config.user = (uid_t)user;
        config.group = (gid_t)group;
        config.environment = environment;
        config.environment_count = environment_count;
        config.command = &argv[command_index];
        result = mc_launch_shim(&config, &error);
        free(environment);
        if (result < 0) {
            mc_error_print(&error, 0);
            return error.code;
        }
        return result;
    }
    usage(stderr);
    return 2;
}
