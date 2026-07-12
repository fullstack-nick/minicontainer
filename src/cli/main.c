#include "minicontainer/info.h"
#include "minicontainer/error.h"
#include "minicontainer/exec.h"
#include "minicontainer/fs.h"
#include "minicontainer/image.h"
#include "minicontainer/id.h"
#include "minicontainer/runtime.h"
#include "minicontainer/network.h"
#include "minicontainer/mount.h"
#include "minicontainer/resource.h"
#include "minicontainer/security.h"
#include "minicontainer/stats.h"
#include "minicontainer/state.h"
#include "minicontainer/validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
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
                  "[--network bridge|none] "
                  "[--cap-add CAPABILITY] "
                  "[--read-only] "
                  "[--bind SOURCE:TARGET[:ro|rw]] [--tmpfs TARGET[:SIZE]] "
                  "[--seccomp-profile PATH] "
                  "[--publish HOST_IP:HOST_PORT:CONTAINER_PORT/tcp|udp] "
                  "-- COMMAND [ARG...]\n"
                  "  minicontainer create [--name NAME] --image NAME [run flags] -- COMMAND [ARG...]\n"
                  "  minicontainer start ID\n"
                  "  minicontainer stop [--time SECONDS] ID\n"
                  "  minicontainer kill [--signal SIGNAL] ID\n"
                  "  minicontainer rm [--force] ID\n"
                  "  minicontainer exec ID -- COMMAND [ARG...]\n"
                  "  minicontainer gc\n"
                  "  minicontainer ps [--all] [--json]\n"
                  "  minicontainer inspect ID\n"
                  "  minicontainer logs [--follow] [--tail LINES] ID\n"
                  "  minicontainer stats [--no-stream] [--json] ID...\n");
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

static int show_log(const char *path, const char *id, long tail, int follow) {
    FILE *stream = fopen(path, "r");
    char *line = NULL;
    size_t capacity = 0U;
    char **ring = NULL;
    size_t count = 0U, next = 0U, index;
    if (stream == NULL) return show_file(path, "logs");
    if (tail >= 0) {
        ring = calloc((size_t)(tail == 0 ? 1 : tail), sizeof(*ring));
        if (ring == NULL) { (void)fclose(stream); return MC_EXIT_INTERNAL; }
    }
    while (getline(&line, &capacity, stream) >= 0) {
        if (tail < 0) (void)fputs(line, stdout);
        else if (tail > 0) {
            free(ring[next]); ring[next] = strdup(line);
            if (ring[next] == NULL) { free(line); (void)fclose(stream); return MC_EXIT_INTERNAL; }
            next = (next + 1U) % (size_t)tail;
            if (count < (size_t)tail) ++count;
        }
    }
    if (tail > 0) {
        const size_t start = count == (size_t)tail ? next : 0U;
        for (index = 0U; index < count; ++index)
            (void)fputs(ring[(start + index) % (size_t)tail], stdout);
    }
    for (index = 0U; ring != NULL && index < (size_t)(tail > 0 ? tail : 1); ++index) free(ring[index]);
    free(ring); free(line);
    while (follow != 0) {
        char status[16];
        clearerr(stream);
        line = NULL; capacity = 0U;
        if (getline(&line, &capacity, stream) >= 0) {
            (void)fputs(line, stdout); (void)fflush(stdout); free(line); continue;
        }
        free(line);
        if (mc_state_get_status(id, status, NULL, &(struct mc_error){0}) != 0 ||
            strcmp(status, "running") != 0) break;
        (void)usleep(100000U);
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
    if (strcmp(argv[1], "ps") == 0) {
        int include_stopped = 0;
        int index;
        for (index = 2; index < argc; ++index) {
            if (strcmp(argv[index], "--all") == 0) include_stopped = 1;
            else if (strcmp(argv[index], "--json") == 0) json = 1;
            else { usage(stderr); return MC_EXIT_USAGE; }
        }
        if ((geteuid() == 0 && mc_state_reconcile(0, &error) != 0) ||
            mc_state_print_list(include_stopped, json, &error) != 0) {
            mc_error_print(&error, json); return error.code;
        }
        return MC_EXIT_OK;
    }
    if (strcmp(argv[1], "gc") == 0 && argc == 2) {
        struct mc_state_lock registry = {-1};
        if (geteuid() != 0) return MC_EXIT_PERMISSION;
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_reconcile(1, &error) != 0) {
            mc_error_print(&error, 0); mc_state_unlock(&registry); return error.code;
        }
        mc_state_unlock(&registry); return MC_EXIT_OK;
    }
    if (strcmp(argv[1], "kill") == 0 && (argc == 3 || argc == 5)) {
        int signal_number = SIGKILL;
        const char *reference = argv[argc - 1];
        char resolved[33];
        struct mc_state_lock registry = {-1}, container = {-1};
        if (argc == 5) {
            const char *value;
            if (strcmp(argv[2], "--signal") != 0) { usage(stderr); return MC_EXIT_USAGE; }
            value = argv[3];
            if (strcmp(value, "TERM") == 0 || strcmp(value, "SIGTERM") == 0) signal_number = SIGTERM;
            else if (strcmp(value, "KILL") == 0 || strcmp(value, "SIGKILL") == 0) signal_number = SIGKILL;
            else if (strcmp(value, "INT") == 0 || strcmp(value, "SIGINT") == 0) signal_number = SIGINT;
            else if (strcmp(value, "HUP") == 0 || strcmp(value, "SIGHUP") == 0) signal_number = SIGHUP;
            else if (strcmp(value, "QUIT") == 0 || strcmp(value, "SIGQUIT") == 0) signal_number = SIGQUIT;
            else { usage(stderr); return MC_EXIT_USAGE; }
        }
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_resolve(reference, resolved, &error) != 0 ||
            mc_state_container_lock(resolved, &container, &error) != 0 ||
            mc_state_signal(resolved, signal_number, &error) != 0) {
            mc_error_print(&error, 0); mc_state_unlock(&container);
            mc_state_unlock(&registry); return error.code;
        }
        mc_state_unlock(&container); mc_state_unlock(&registry);
        return MC_EXIT_OK;
    }
    if (strcmp(argv[1], "stop") == 0 && (argc == 3 || argc == 5)) {
        uint64_t timeout_seconds = 10U;
        const char *reference = argv[argc - 1];
        char resolved[33], status[16];
        struct mc_state_lock registry = {-1}, container = {-1};
        unsigned int attempt;
        if (argc == 5 && (strcmp(argv[2], "--time") != 0 ||
            !mc_parse_positive_u64(argv[3], UINT64_C(3600), &timeout_seconds))) {
            usage(stderr); return MC_EXIT_USAGE;
        }
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_resolve(reference, resolved, &error) != 0 ||
            mc_state_container_lock(resolved, &container, &error) != 0 ||
            mc_state_signal(resolved, SIGTERM, &error) != 0) {
            mc_error_print(&error, 0); mc_state_unlock(&container);
            mc_state_unlock(&registry); return error.code;
        }
        mc_state_unlock(&container); mc_state_unlock(&registry);
        for (attempt = 0U; attempt < (unsigned int)(timeout_seconds * 10U); ++attempt) {
            if (mc_state_get_status(resolved, status, NULL, &error) == 0 &&
                strcmp(status, "running") != 0) return MC_EXIT_OK;
            (void)usleep(100000U);
        }
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_container_lock(resolved, &container, &error) != 0 ||
            mc_state_signal(resolved, SIGKILL, &error) != 0) {
            mc_error_print(&error, 0); mc_state_unlock(&container);
            mc_state_unlock(&registry); return error.code;
        }
        mc_state_unlock(&container); mc_state_unlock(&registry);
        return MC_EXIT_OK;
    }
    if (strcmp(argv[1], "rm") == 0 && (argc == 3 || argc == 4)) {
        int force = 0;
        const char *reference;
        char resolved[33], status[16];
        struct mc_state_lock registry = {-1}, container = {-1};
        if (argc == 4) {
            if (strcmp(argv[2], "--force") != 0) { usage(stderr); return MC_EXIT_USAGE; }
            force = 1;
        }
        reference = argv[argc - 1];
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_reconcile(0, &error) != 0 ||
            mc_state_resolve(reference, resolved, &error) != 0 ||
            mc_state_container_lock(resolved, &container, &error) != 0 ||
            mc_state_get_status(resolved, status, NULL, &error) != 0) goto rm_error;
        if (strcmp(status, "running") == 0) {
            unsigned int attempt;
            if (force == 0) {
                mc_error_set(&error, MC_EXIT_CONFLICT, EBUSY, "remove-container", resolved,
                             "running container requires --force");
                goto rm_error;
            }
            if (mc_state_signal(resolved, SIGKILL, &error) != 0) goto rm_error;
            for (attempt = 0U; attempt < 100U; ++attempt) {
                if (mc_state_get_status(resolved, status, NULL, &error) == 0 &&
                    strcmp(status, "running") != 0) break;
                (void)usleep(50000U);
            }
            if (strcmp(status, "running") == 0) {
                mc_error_set(&error, MC_EXIT_RUNTIME, ETIMEDOUT, "remove-container", resolved,
                             "forced container did not stop");
                goto rm_error;
            }
        }
        mc_state_unlock(&container);
        if (mc_state_remove(resolved, &error) != 0) goto rm_error;
        mc_state_unlock(&registry);
        return MC_EXIT_OK;
rm_error:
        mc_error_print(&error, 0); mc_state_unlock(&container); mc_state_unlock(&registry);
        return error.code;
    }
    if (strcmp(argv[1], "exec") == 0 && argc >= 5 && strcmp(argv[3], "--") == 0) {
        const int result = mc_exec_container(argv[2], &argv[4], &error);
        if (result < 0) { mc_error_print(&error, 0); return error.code; }
        return result;
    }
    if (strcmp(argv[1], "start") == 0 && argc == 3) {
        struct mc_run_config stored = {0};
        struct mc_state_lock registry = {-1};
        struct mc_state_lock container = {-1};
        char image[PATH_MAX];
        char status[16];
        int result;
        if (geteuid() != 0) return MC_EXIT_PERMISSION;
        if (mc_state_registry_lock(&registry, &error) != 0 ||
            mc_state_reconcile(0, &error) != 0 ||
            mc_state_load_config(argv[2], &stored, image, &error) != 0 ||
            mc_state_container_lock(stored.id, &container, &error) != 0 ||
            mc_state_get_status(stored.id, status, NULL, &error) != 0) {
            mc_error_print(&error, 0); mc_state_unlock(&container);
            mc_state_unlock(&registry); mc_state_free_config(&stored); return error.code;
        }
        if (strcmp(status, "created") != 0 && strcmp(status, "stopped") != 0) {
            mc_error_set(&error, MC_EXIT_CONFLICT, EBUSY, "start", stored.id,
                         "container is not in a startable state");
            mc_error_print(&error, 0); mc_state_unlock(&container);
            mc_state_unlock(&registry); mc_state_free_config(&stored);
            return error.code;
        }
        stored.detach = 1;
        result = mc_launch_shim(&stored, &error);
        if (result == 0) (void)printf("%s\n", stored.id);
        else mc_error_print(&error, 0);
        mc_state_unlock(&container); mc_state_unlock(&registry);
        mc_state_free_config(&stored);
        return result < 0 ? error.code : result;
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
                char resolved[33];
                if (mc_state_resolve(argv[index], resolved, &error) != 0 ||
                    mc_stats_print(resolved, json, &error) != 0) {
                    if (error.code != 0) { mc_error_print(&error, json); return error.code; }
                    usage(stderr); return MC_EXIT_USAGE;
                }
            }
            if (no_stream == 0) (void)sleep(1U);
        } while (no_stream == 0);
        return MC_EXIT_OK;
    }
    if (strcmp(argv[1], "logs") == 0 && argc >= 3) {
        char path[PATH_MAX];
        char resolved[33];
        const char *reference = argv[argc - 1];
        long tail = -1L;
        int follow = 0;
        int index;
        int length;
        for (index = 2; index < argc - 1; ++index) {
            if (strcmp(argv[index], "--follow") == 0) follow = 1;
            else if (strcmp(argv[index], "--tail") == 0 && index + 1 < argc - 1) {
                char *end = NULL; errno = 0; tail = strtol(argv[++index], &end, 10);
                if (errno != 0 || end == argv[index] || *end != '\0' || tail < 0) {
                    usage(stderr); return MC_EXIT_USAGE;
                }
            } else { usage(stderr); return MC_EXIT_USAGE; }
        }
        if (mc_state_resolve(reference, resolved, &error) != 0) {
            mc_error_print(&error, 0);
            return error.code;
        }
        length = snprintf(path, sizeof(path), "%s/%s/container.log", mc_log_dir(), resolved);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            return MC_EXIT_USAGE;
        }
        return show_log(path, resolved, tail, follow);
    }
    if (strcmp(argv[1], "inspect") == 0 &&
        (argc == 3 || (argc == 4 && strcmp(argv[3], "--json") == 0))) {
        char path[PATH_MAX], resolved[33];
        int length;
        if (mc_state_resolve(argv[2], resolved, &error) != 0) {
            mc_error_print(&error, 0); return error.code;
        }
        length = snprintf(path, sizeof(path), "%s/containers/%s/state.json", mc_state_dir(), resolved);
        if (length < 0 || (size_t)length >= sizeof(path)) return MC_EXIT_USAGE;
        return show_file(path, "inspect");
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
    if (strcmp(argv[1], "run") == 0 || strcmp(argv[1], "create") == 0) {
        const char *image = NULL;
        char *name = NULL;
        char *hostname = NULL;
        char generated_hostname[16];
        char id[33];
        char rootfs[4096];
        char default_workdir[] = "/";
        char *workdir = default_workdir;
        char **environment = calloc((size_t)argc, sizeof(*environment));
        struct mc_publish *publishes = calloc((size_t)argc, sizeof(*publishes));
        struct mc_mount *mounts = calloc((size_t)argc, sizeof(*mounts));
        size_t environment_count = 0U;
        size_t publish_count = 0U;
        size_t mount_count = 0U;
        char **seccomp_denies = NULL;
        size_t seccomp_deny_count = 0U;
        unsigned int user = 0U;
        unsigned int group = 0U;
        int detach = 0;
        const int create_only = strcmp(argv[1], "create") == 0;
        uint64_t memory_max = UINT64_C(128) * UINT64_C(1024) * UINT64_C(1024);
        uint64_t swap_max = 0U;
        uint64_t cpu_quota = UINT64_C(50000);
        uint64_t pids_max = UINT64_C(128);
        uint64_t capability_mask = 0U;
        int readonly_root = 0;
        int network_bridge = 1;
        int command_index = -1;
        int index;
        struct mc_run_config config;
        int result;

        if (environment == NULL || publishes == NULL || mounts == NULL) {
            free(mounts);
            free(publishes);
            free(environment);
            return MC_EXIT_INTERNAL;
        }
        for (index = 2; index < argc; ++index) {
            if (strcmp(argv[index], "--") == 0) {
                command_index = index + 1;
                break;
            }
            if (strcmp(argv[index], "--detach") == 0) {
                detach = 1;
            } else if (strcmp(argv[index], "--read-only") == 0) {
                readonly_root = 1;
            } else if (strcmp(argv[index], "--name") == 0 && index + 1 < argc) {
                name = argv[++index];
                if (!mc_valid_name(name)) {
                    free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--image") == 0 && index + 1 < argc) {
                image = argv[++index];
            } else if (strcmp(argv[index], "--hostname") == 0 && index + 1 < argc) {
                hostname = argv[++index];
            } else if (strcmp(argv[index], "--workdir") == 0 && index + 1 < argc) {
                workdir = argv[++index];
                if (workdir[0] != '/') {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--user") == 0 && index + 1 < argc) {
                if (!mc_parse_user(argv[++index], &user, &group)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--env") == 0 && index + 1 < argc) {
                char *assignment = argv[++index];
                if (!mc_valid_environment(assignment)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
                environment[environment_count++] = assignment;
            } else if ((strcmp(argv[index], "--memory") == 0 ||
                        strcmp(argv[index], "--mem") == 0) && index + 1 < argc) {
                if (!mc_parse_bytes(argv[++index], &memory_max)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if ((strcmp(argv[index], "--memory-swap") == 0 ||
                        strcmp(argv[index], "--swap") == 0) && index + 1 < argc) {
                char *swap = argv[++index];
                if (strcmp(swap, "0") == 0) {
                    swap_max = 0U;
                } else if (!mc_parse_bytes(swap, &swap_max)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if ((strcmp(argv[index], "--cpus") == 0 ||
                        strcmp(argv[index], "--cpu") == 0) && index + 1 < argc) {
                if (!mc_parse_cpu_quota(argv[++index], &cpu_quota)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--pids-limit") == 0 && index + 1 < argc) {
                if (!mc_parse_positive_u64(argv[++index], UINT64_C(4194304), &pids_max)) {
                    free(publishes); free(environment);
                    usage(stderr);
                    return MC_EXIT_USAGE;
                }
            } else if (strcmp(argv[index], "--network") == 0 && index + 1 < argc) {
                const char *mode = argv[++index];
                if (strcmp(mode, "bridge") == 0) network_bridge = 1;
                else if (strcmp(mode, "none") == 0) network_bridge = 0;
                else { free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE; }
            } else if (strcmp(argv[index], "--publish") == 0 && index + 1 < argc) {
                size_t existing;
                struct mc_publish parsed;
                if (!mc_parse_publish(argv[++index], &parsed)) {
                    free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE;
                }
                for (existing = 0U; existing < publish_count; ++existing) {
                    if (publishes[existing].protocol == parsed.protocol &&
                        publishes[existing].host_port == parsed.host_port &&
                        (publishes[existing].host_ipv4 == 0U || parsed.host_ipv4 == 0U ||
                         publishes[existing].host_ipv4 == parsed.host_ipv4)) {
                        free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE;
                    }
                }
                publishes[publish_count++] = parsed;
            } else if (strcmp(argv[index], "--cap-add") == 0 && index + 1 < argc) {
                unsigned int capability;
                if (!mc_capability_parse(argv[++index], &capability) || capability >= 64U) {
                    free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE;
                }
                capability_mask |= UINT64_C(1) << capability;
            } else if (strcmp(argv[index], "--bind") == 0 && index + 1 < argc) {
                if (!mc_parse_bind_mount(argv[++index], &mounts[mount_count], &error)) {
                    mc_error_print(&error, 0); free(mounts); free(publishes); free(environment);
                    return MC_EXIT_USAGE;
                }
                ++mount_count;
            } else if (strcmp(argv[index], "--tmpfs") == 0 && index + 1 < argc) {
                if (!mc_parse_tmpfs_mount(argv[++index], &mounts[mount_count], &error)) {
                    mc_error_print(&error, 0); free(mounts); free(publishes); free(environment);
                    return MC_EXIT_USAGE;
                }
                ++mount_count;
            } else if (strcmp(argv[index], "--seccomp-profile") == 0 && index + 1 < argc &&
                       seccomp_denies == NULL) {
                if (!mc_seccomp_profile_load(argv[++index], &seccomp_denies,
                                             &seccomp_deny_count, &error)) {
                    mc_error_print(&error, 0); free(mounts); free(publishes); free(environment);
                    return MC_EXIT_USAGE;
                }
            } else {
                free(publishes); free(environment);
                usage(stderr);
                return MC_EXIT_USAGE;
            }
        }
        if (image == NULL || command_index < 0 || command_index >= argc ||
            mc_image_resolve(image, rootfs, sizeof(rootfs), &error) != 0 ||
            mc_generate_id(id, &error) != 0) {
            if (error.code != 0) {
                mc_error_print(&error, 0);
                free(publishes); free(environment);
                return error.code;
            }
            free(publishes); free(environment);
            usage(stderr);
            return MC_EXIT_USAGE;
        }
        if (hostname == NULL) {
            (void)snprintf(generated_hostname, sizeof(generated_hostname), "mc-%.12s", id);
            hostname = generated_hostname;
        }
        config.id = id;
        config.name = name;
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
        config.capability_mask = capability_mask;
        config.readonly_root = readonly_root;
        config.mounts = mounts;
        config.mount_count = mount_count;
        config.seccomp_denies = seccomp_denies;
        config.seccomp_deny_count = seccomp_deny_count;
        config.network_bridge = network_bridge;
        config.ipv4_host = 0U;
        config.publishes = publishes;
        config.publish_count = publish_count;
        config.command = &argv[command_index];
        if (network_bridge == 0 && publish_count > 0U) {
            free(publishes); free(environment); usage(stderr); return MC_EXIT_USAGE;
        }
        {
            struct mc_state_lock registry = {-1};
            struct mc_state_lock container = {-1};
            if (mc_state_registry_lock(&registry, &error) != 0 ||
                mc_state_reconcile(0, &error) != 0 ||
                mc_state_container_lock(id, &container, &error) != 0 ||
                (config.network_bridge != 0 &&
                 mc_state_allocate_ip(id, &config.ipv4_host, &error) != 0) ||
                mc_state_save_config(&config, image, &error) != 0 ||
                (create_only != 0 && mc_state_mark_created(id, &error) != 0)) {
                struct mc_error cleanup_error = {0};
                mc_error_print(&error, 0); mc_state_unlock(&container);
                (void)mc_state_remove(id, &cleanup_error);
                mc_state_unlock(&registry); free(publishes); free(environment); return error.code;
            }
            mc_state_unlock(&container); mc_state_unlock(&registry);
        }
        if (create_only != 0) {
            (void)printf("%s\n", id);
            free(environment);
            free(publishes);
            while (mount_count > 0U) mc_mount_free(&mounts[--mount_count]);
            free(mounts);
            while (seccomp_deny_count > 0U) free(seccomp_denies[--seccomp_deny_count]);
            free(seccomp_denies);
            return MC_EXIT_OK;
        }
        result = mc_launch_shim(&config, &error);
        free(environment);
        free(publishes);
        while (mount_count > 0U) mc_mount_free(&mounts[--mount_count]);
        free(mounts);
        while (seccomp_deny_count > 0U) free(seccomp_denies[--seccomp_deny_count]);
        free(seccomp_denies);
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
