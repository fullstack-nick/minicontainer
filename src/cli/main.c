#include "minicontainer/info.h"
#include "minicontainer/error.h"
#include "minicontainer/fs.h"
#include "minicontainer/image.h"
#include "minicontainer/id.h"
#include "minicontainer/runtime.h"
#include "minicontainer/resource.h"
#include "minicontainer/stats.h"
#include "minicontainer/validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

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
                  "  minicontainer run [--detach] --image NAME [--hostname NAME] [--env KEY=VALUE] "
                  "[--workdir PATH] [--user UID[:GID]] [--memory BYTES] "
                  "[--memory-swap BYTES] [--cpus DECIMAL] [--pids-limit COUNT] "
                  "-- COMMAND [ARG...]\n"
                  "  minicontainer inspect ID\n"
                  "  minicontainer logs ID\n"
                  "  minicontainer stats [--no-stream] [--json] ID...\n");
}

static int valid_full_id(const char *id) {
    size_t index;
    if (id == NULL || strlen(id) != 32U) {
        return 0;
    }
    for (index = 0U; index < 32U; ++index) {
        if (!isdigit((unsigned char)id[index]) && !(id[index] >= 'a' && id[index] <= 'f')) {
            return 0;
        }
    }
    return 1;
}

static int show_file(const char *path, const char *operation) {
    unsigned char buffer[16384];
    FILE *stream = fopen(path, "r");
    size_t count;
    if (stream == NULL) {
        (void)fprintf(stderr, "minicontainer: %s: %s: %s\n", operation, path,
                      strerror(errno));
        return MC_EXIT_NOT_FOUND;
    }
    while ((count = fread(buffer, 1U, sizeof(buffer), stream)) > 0U) {
        if (fwrite(buffer, 1U, count, stdout) != count) {
            (void)fclose(stream);
            return MC_EXIT_RUNTIME;
        }
    }
    if (ferror(stream) != 0) {
        (void)fclose(stream);
        return MC_EXIT_RUNTIME;
    }
    (void)fclose(stream);
    return MC_EXIT_OK;
}

int main(int argc, char **argv) {
    int json = 0;
    struct mc_error error = {0};

    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (argc == 3 &&
        (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "info") == 0)) {
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
    if (strcmp(argv[1], "stats") == 0 && argc >= 3) {
        int no_stream = 0;
        int first_id = 2;
        int index;
        while (first_id < argc && strncmp(argv[first_id], "--", 2U) == 0) {
            if (strcmp(argv[first_id], "--no-stream") == 0) no_stream = 1;
            else if (strcmp(argv[first_id], "--json") == 0) json = 1;
            else { usage(stderr); return MC_EXIT_USAGE; }
            ++first_id;
        }
        if (first_id >= argc) { usage(stderr); return MC_EXIT_USAGE; }
        do {
            for (index = first_id; index < argc; ++index) {
                if (!valid_full_id(argv[index]) || mc_stats_print(argv[index], json, &error) != 0) {
                    if (error.code != 0) { mc_error_print(&error, json); return error.code; }
                    usage(stderr); return MC_EXIT_USAGE;
                }
            }
            if (no_stream == 0) (void)sleep(1U);
        } while (no_stream == 0);
        return MC_EXIT_OK;
    }
    if ((strcmp(argv[1], "logs") == 0 || strcmp(argv[1], "inspect") == 0) && argc == 3) {
        char path[PATH_MAX];
        const char *kind = argv[1];
        int length;
        if (!valid_full_id(argv[2])) {
            usage(stderr);
            return MC_EXIT_USAGE;
        }
        if (strcmp(kind, "logs") == 0) {
            length = snprintf(path, sizeof(path), "%s/%s/container.log", mc_log_dir(), argv[2]);
        } else {
            length = snprintf(path, sizeof(path), "%s/containers/%s/state.json", mc_state_dir(),
                              argv[2]);
        }
        if (length < 0 || (size_t)length >= sizeof(path)) {
            return MC_EXIT_USAGE;
        }
        return show_file(path, kind);
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
        int detach = 0;
        uint64_t memory_max = UINT64_C(128) * UINT64_C(1024) * UINT64_C(1024);
        uint64_t swap_max = 0U;
        uint64_t cpu_quota = UINT64_C(50000);
        uint64_t pids_max = UINT64_C(128);
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
            if (strcmp(argv[index], "--detach") == 0) {
                detach = 1;
            } else if (strcmp(argv[index], "--image") == 0 && index + 1 < argc) {
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
            } else if ((strcmp(argv[index], "--memory") == 0 ||
                        strcmp(argv[index], "--mem") == 0) && index + 1 < argc) {
                if (!mc_parse_bytes(argv[++index], &memory_max)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if ((strcmp(argv[index], "--memory-swap") == 0 ||
                        strcmp(argv[index], "--swap") == 0) && index + 1 < argc) {
                char *swap = argv[++index];
                if (strcmp(swap, "0") == 0) {
                    swap_max = 0U;
                } else if (!mc_parse_bytes(swap, &swap_max)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if ((strcmp(argv[index], "--cpus") == 0 ||
                        strcmp(argv[index], "--cpu") == 0) && index + 1 < argc) {
                if (!mc_parse_cpu_quota(argv[++index], &cpu_quota)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--pids-limit") == 0 && index + 1 < argc) {
                if (!mc_parse_positive_u64(argv[++index], UINT64_C(4194304), &pids_max)) {
                    free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
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
        config.detach = detach;
        config.ready_fd = -1;
        config.memory_max = memory_max;
        config.swap_max = swap_max;
        config.cpu_quota = cpu_quota;
        config.pids_max = pids_max;
        config.command = &argv[command_index];
        result = mc_launch_shim(&config, &error);
        free(environment);
        if (result < 0) {
            mc_error_print(&error, 0);
            return error.code;
        }
        if (detach != 0) {
            (void)printf("%s\n", id);
        }
        return result;
    }
    usage(stderr);
    return 2;
}
