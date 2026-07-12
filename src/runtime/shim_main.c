#include "minicontainer/error.h"
#include "minicontainer/runtime.h"
#include "minicontainer/validate.h"
#include "minicontainer/resource.h"
#include "minicontainer/security.h"
#include "minicontainer/mount.h"
#include "minicontainer/state.h"
#include "minicontainer/fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MC_LOG_MAX_SIZE (UINT64_C(1024) * UINT64_C(1024))
#define MC_LOG_MAX_FILES 3

static int write_all(int descriptor, const char *buffer, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t written = write(descriptor, buffer + offset, length - offset);
        if (written < 0) { if (errno == EINTR) continue; return -1; }
        offset += (size_t)written;
    }
    return 0;
}

static int rotate_log(const char *path) {
    char older[4096], newer[4096];
    int index;
    if (snprintf(older, sizeof(older), "%s.%d", path, MC_LOG_MAX_FILES) < 0) return -1;
    (void)unlink(older);
    for (index = MC_LOG_MAX_FILES - 1; index >= 1; --index) {
        if (snprintf(older, sizeof(older), "%s.%d", path, index) < 0 ||
            snprintf(newer, sizeof(newer), "%s.%d", path, index + 1) < 0) return -1;
        if (rename(older, newer) != 0 && errno != ENOENT) return -1;
    }
    if (snprintf(newer, sizeof(newer), "%s.1", path) < 0 ||
        (rename(path, newer) != 0 && errno != ENOENT)) return -1;
    return open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
}

static int log_pump(int input, int output, const char *path) {
    char buffer[16384];
    struct stat metadata;
    uint64_t size = fstat(output, &metadata) == 0 ? (uint64_t)metadata.st_size : 0U;
    for (;;) {
        const ssize_t count = read(input, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; return 1; }
        if (size > 0U && size + (uint64_t)count > MC_LOG_MAX_SIZE) {
            (void)close(output); output = rotate_log(path); size = 0U;
            if (output < 0) return 1;
        }
        if (write_all(output, buffer, (size_t)count) != 0) return 1;
        size += (uint64_t)count;
    }
    (void)fsync(output); (void)close(output); return 0;
}

static pid_t start_log_pump(const char *id) {
    char path[4096];
    int channel[2], output;
    pid_t logger;
    if (snprintf(path, sizeof(path), "%s/%s/container.log", mc_log_dir(), id) < 0 ||
        pipe2(channel, O_CLOEXEC) != 0 || (output = dup(STDOUT_FILENO)) < 0) return -1;
    logger = fork();
    if (logger == 0) {
        (void)close(channel[1]);
        (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
        _exit(log_pump(channel[0], output, path));
    }
    (void)close(channel[0]); (void)close(output);
    if (logger < 0 || dup2(channel[1], STDOUT_FILENO) < 0 ||
        dup2(channel[1], STDERR_FILENO) < 0) {
        (void)close(channel[1]); return -1;
    }
    (void)close(channel[1]); return logger;
}

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
    struct mc_mount *mounts;
    int index;
    int result;
    pid_t logger = -1;

    environment = calloc((size_t)argc, sizeof(*environment));
    publishes = calloc((size_t)argc, sizeof(*publishes));
    mounts = calloc((size_t)argc, sizeof(*mounts));
    if (environment == NULL || publishes == NULL || mounts == NULL) {
        free(mounts); free(publishes); free(environment);
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
        if (strcmp(argv[index], "--read-only") == 0) {
            config.readonly_root = 1;
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
        } else if (strcmp(argv[index], "--cap-add") == 0) {
            unsigned int capability;
            if (!mc_capability_parse(argv[++index], &capability) || capability >= 64U) break;
            config.capability_mask |= UINT64_C(1) << capability;
        } else if (strcmp(argv[index], "--bind") == 0) {
            if (!mc_parse_bind_mount(argv[++index], &mounts[config.mount_count], &error)) break;
            ++config.mount_count;
        } else if (strcmp(argv[index], "--tmpfs") == 0) {
            if (!mc_parse_tmpfs_mount(argv[++index], &mounts[config.mount_count], &error)) break;
            ++config.mount_count;
        } else if (strcmp(argv[index], "--seccomp-deny") == 0) {
            char *name = argv[++index];
            char **resized;
            if (!mc_seccomp_name_valid(name)) break;
            resized = realloc(config.seccomp_denies,
                (config.seccomp_deny_count + 1U) * sizeof(*config.seccomp_denies));
            if (resized == NULL) break;
            config.seccomp_denies = resized;
            config.seccomp_denies[config.seccomp_deny_count++] = name;
        } else {
            break;
        }
    }
    config.environment = environment;
    config.publishes = publishes;
    config.mounts = mounts;
    if (config.id == NULL || config.rootfs == NULL || config.hostname == NULL ||
        config.workdir == NULL || config.workdir[0] != '/' || config.command == NULL ||
        config.command[0] == NULL || index >= argc ||
        (config.detach != 0 && config.ready_fd < 0) || config.memory_max == 0U ||
        config.cpu_quota == 0U || config.pids_max == 0U) {
        usage();
        while (config.mount_count > 0U) mc_mount_free(&mounts[--config.mount_count]);
        free(config.seccomp_denies);
        free(mounts); free(publishes); free(environment);
        return MC_EXIT_USAGE;
    }
    if (config.detach != 0) logger = start_log_pump(config.id);
    if (config.detach != 0 && logger < 0) {
        result = -1;
        mc_error_set(&error, MC_EXIT_RUNTIME, errno, "log-pump", config.id,
                     "cannot start rotating log pump");
    } else {
        result = mc_container_run(&config, &error);
    }
    if (logger > 0) {
        (void)close(STDOUT_FILENO); (void)close(STDERR_FILENO);
        while (waitpid(logger, NULL, 0) < 0 && errno == EINTR) { }
    }
    while (config.mount_count > 0U) mc_mount_free(&mounts[--config.mount_count]);
    free(config.seccomp_denies);
    free(mounts); free(publishes); free(environment);
    if (result < 0) {
        mc_error_print(&error, 0);
        return error.code == 0 ? MC_EXIT_RUNTIME : error.code;
    }
    return result;
}
