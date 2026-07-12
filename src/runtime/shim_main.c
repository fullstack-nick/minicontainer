#include "minicontainer/error.h"
#include "minicontainer/runtime.h"
#include "minicontainer/validate.h"
#include "minicontainer/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

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
    struct mc_publish *publishes;
    int index;
    int result;

    environment = calloc((size_t)argc, sizeof(*environment));
    publishes = calloc((size_t)argc, sizeof(*publishes));
    if (environment == NULL || publishes == NULL) {
        free(publishes); free(environment);
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
        } else if (strcmp(argv[index], "--memory") == 0) {
            if (!mc_parse_positive_u64(argv[++index], UINT64_MAX, &config.memory_max)) {
                break;
            }
        } else if (strcmp(argv[index], "--memory-swap") == 0) {
            char *end = NULL;
            unsigned long long parsed;
            errno = 0;
            parsed = strtoull(argv[++index], &end, 10);
            if (errno != 0 || end == argv[index] || *end != '\0') {
                break;
            }
            config.swap_max = (uint64_t)parsed;
        } else if (strcmp(argv[index], "--cpu-quota") == 0) {
            if (!mc_parse_positive_u64(argv[++index], UINT64_C(102400000),
                                       &config.cpu_quota)) {
                break;
            }
        } else if (strcmp(argv[index], "--pids-limit") == 0) {
            if (!mc_parse_positive_u64(argv[++index], UINT64_C(4194304),
                                       &config.pids_max)) {
                break;
            }
        } else if (strcmp(argv[index], "--network") == 0) {
            const char *mode = argv[++index];
            if (strcmp(mode, "bridge") == 0) config.network_bridge = 1;
            else if (strcmp(mode, "none") == 0) config.network_bridge = 0;
            else break;
        } else if (strcmp(argv[index], "--ipv4-host") == 0) {
            char *end = NULL;
            unsigned long address;
            errno = 0;
            address = strtoul(argv[++index], &end, 10);
            if (errno != 0 || end == argv[index] || *end != '\0' || address > UINT32_MAX) break;
            config.ipv4_host = (unsigned int)address;
        } else if (strcmp(argv[index], "--publish") == 0) {
            unsigned int host_ipv4, host_port, container_port, protocol;
            char trailing;
            if (sscanf(argv[++index], "%u,%u,%u,%u%c", &host_ipv4, &host_port,
                       &container_port, &protocol, &trailing) != 4 ||
                host_port == 0U || host_port > 65535U || container_port == 0U ||
                container_port > 65535U ||
                (protocol != IPPROTO_TCP && protocol != IPPROTO_UDP)) break;
            publishes[config.publish_count].host_ipv4 = host_ipv4;
            publishes[config.publish_count].host_port = (uint16_t)host_port;
            publishes[config.publish_count].container_port = (uint16_t)container_port;
            publishes[config.publish_count].protocol = (uint8_t)protocol;
            ++config.publish_count;
        } else {
            break;
        }
    }
    config.environment = environment;
    config.publishes = publishes;
    if (config.id == NULL || config.rootfs == NULL || config.hostname == NULL ||
        config.workdir == NULL || config.workdir[0] != '/' || config.command == NULL ||
        config.command[0] == NULL || index >= argc ||
        (config.detach != 0 && config.ready_fd < 0) || config.memory_max == 0U ||
        config.cpu_quota == 0U || config.pids_max == 0U) {
        usage();
        free(publishes); free(environment);
        return MC_EXIT_USAGE;
    }
    result = mc_container_run(&config, &error);
    free(publishes); free(environment);
    if (result < 0) {
        mc_error_print(&error, 0);
        return error.code == 0 ? MC_EXIT_RUNTIME : error.code;
    }
    return result;
}
