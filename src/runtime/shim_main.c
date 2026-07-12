#include "minicontainer/error.h"
#include "minicontainer/runtime.h"
#include "minicontainer/validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void usage(void) {
    (void)fprintf(stderr,
                  "usage: minicontainer-shim --id ID --rootfs PATH --hostname NAME "
                  "--workdir PATH --user UID:GID [--env KEY=VALUE] -- COMMAND [ARG...]\n");
}

int main(int argc, char **argv) {
    struct mc_run_config config;
    struct mc_error error = {0};
    unsigned int user = 0U;
    unsigned int group = 0U;
    char **environment;
    int index;
    int result;

    environment = calloc((size_t)argc, sizeof(*environment));
    if (environment == NULL) {
        return MC_EXIT_INTERNAL;
    }
    (void)memset(&config, 0, sizeof(config));
    config.ready_fd = -1;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--") == 0) {
            config.command = &argv[index + 1];
            break;
        }
        if (strcmp(argv[index], "--detach") == 0) {
            config.detach = 1;
            continue;
        }
        if (index + 1 >= argc) {
            break;
        }
        if (strcmp(argv[index], "--id") == 0) {
            config.id = argv[++index];
        } else if (strcmp(argv[index], "--rootfs") == 0) {
            config.rootfs = argv[++index];
        } else if (strcmp(argv[index], "--hostname") == 0) {
            config.hostname = argv[++index];
        } else if (strcmp(argv[index], "--workdir") == 0) {
            config.workdir = argv[++index];
        } else if (strcmp(argv[index], "--user") == 0) {
            if (!mc_parse_user(argv[++index], &user, &group)) {
                break;
            }
            config.user = (uid_t)user;
            config.group = (gid_t)group;
        } else if (strcmp(argv[index], "--env") == 0) {
            char *assignment = argv[++index];
            if (!mc_valid_environment(assignment)) {
                break;
            }
            environment[config.environment_count++] = assignment;
        } else if (strcmp(argv[index], "--ready-fd") == 0) {
            char *end = NULL;
            errno = 0;
            const long descriptor = strtol(argv[++index], &end, 10);
            if (errno != 0 || end == argv[index] || *end != '\0' || descriptor < 0 ||
                descriptor > 1048576L) {
                break;
            }
            config.ready_fd = (int)descriptor;
        } else {
            break;
        }
    }
    config.environment = environment;
    if (config.id == NULL || config.rootfs == NULL || config.hostname == NULL ||
        config.workdir == NULL || config.workdir[0] != '/' || config.command == NULL ||
        config.command[0] == NULL || index >= argc ||
        (config.detach != 0 && config.ready_fd < 0)) {
        usage();
        free(environment);
        return MC_EXIT_USAGE;
    }
    result = mc_container_run(&config, &error);
    free(environment);
    if (result < 0) {
        mc_error_print(&error, 0);
        return error.code == 0 ? MC_EXIT_RUNTIME : error.code;
    }
    return result;
}
