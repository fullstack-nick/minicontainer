#include "minicontainer/cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

static int append_string_property(sd_bus_message *message, const char *name,
                                  const char *value) {
    int result = sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "sv");
    if (result >= 0) {
        result = sd_bus_message_append(message, "s", name);
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "s");
    }
    if (result >= 0) {
        result = sd_bus_message_append(message, "s", value);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    return result;
}

static int append_boolean_property(sd_bus_message *message, const char *name, int value) {
    int result = sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "sv");
    if (result >= 0) {
        result = sd_bus_message_append(message, "s", name);
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "b");
    }
    if (result >= 0) {
        result = sd_bus_message_append(message, "b", value);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    return result;
}

static int append_pid_property(sd_bus_message *message, pid_t pid) {
    int result = sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "sv");
    if (result >= 0) {
        result = sd_bus_message_append(message, "s", "PIDs");
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(message, SD_BUS_TYPE_VARIANT, "au");
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "u");
    }
    if (result >= 0) {
        result = sd_bus_message_append(message, "u", (uint32_t)pid);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(message);
    }
    return result;
}

static int create_delegated_scope(const char *id, struct mc_error *error) {
    sd_bus *bus = NULL;
    sd_bus_message *request = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error bus_error = SD_BUS_ERROR_NULL;
    char unit[96];
    char description[128];
    int result;

    (void)snprintf(unit, sizeof(unit), "minicontainer-%s.scope", id);
    (void)snprintf(description, sizeof(description), "MiniContainer %s", id);
    result = sd_bus_default_system(&bus);
    if (result >= 0) {
        result = sd_bus_message_new_method_call(
            bus, &request, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StartTransientUnit");
    }
    if (result >= 0) {
        result = sd_bus_message_append(request, "ss", unit, "fail");
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(request, SD_BUS_TYPE_ARRAY, "(sv)");
    }
    if (result >= 0) {
        result = append_string_property(request, "Description", description);
    }
    if (result >= 0) {
        result = append_string_property(request, "Slice", "system.slice");
    }
    if (result >= 0) {
        result = append_string_property(request, "CollectMode", "inactive-or-failed");
    }
    if (result >= 0) {
        result = append_boolean_property(request, "Delegate", 1);
    }
    if (result >= 0) {
        result = append_pid_property(request, getpid());
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(request);
    }
    if (result >= 0) {
        result = sd_bus_message_open_container(request, SD_BUS_TYPE_ARRAY, "(sa(sv))");
    }
    if (result >= 0) {
        result = sd_bus_message_close_container(request);
    }
    if (result >= 0) {
        result = sd_bus_call(bus, request, 0, &bus_error, &reply);
    }
    if (result < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, -result, "systemd-scope", unit,
                     bus_error.message == NULL ? "cannot create delegated scope"
                                               : bus_error.message);
    }
    sd_bus_error_free(&bus_error);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(request);
    sd_bus_unref(bus);
    return result < 0 ? -1 : 0;
}

static int read_current_cgroup(char *output, size_t size) {
    FILE *stream = fopen("/proc/self/cgroup", "r");
    char line[4096];
    if (stream == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), stream) != NULL) {
        if (strncmp(line, "0::", 3U) == 0) {
            const char *path = line + 3;
            const size_t length = strcspn(path, "\r\n");
            if (length + 1U > size) {
                (void)fclose(stream);
                errno = ENAMETOOLONG;
                return -1;
            }
            (void)memcpy(output, path, length);
            output[length] = '\0';
            (void)fclose(stream);
            return 0;
        }
    }
    (void)fclose(stream);
    errno = ENOENT;
    return -1;
}

static int join_path(char *output, size_t size, const char *left, const char *right) {
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    if (left_length + right_length + 2U > size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)memcpy(output, left, left_length);
    output[left_length] = '/';
    (void)memcpy(output + left_length + 1U, right, right_length + 1U);
    return 0;
}

static int write_value(const char *directory, const char *name, const char *value) {
    char path[4096];
    int descriptor;
    size_t offset = 0U;
    const size_t length = strlen(value);
    if (join_path(path, sizeof(path), directory, name) != 0) {
        return -1;
    }
    descriptor = open(path, O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return -1;
    }
    while (offset < length) {
        const ssize_t count = write(descriptor, value + offset, length - offset);
        if (count < 0) {
            const int saved = errno;
            (void)close(descriptor);
            errno = saved;
            return -1;
        }
        offset += (size_t)count;
    }
    return close(descriptor);
}

static int verify_value(const char *directory, const char *name, const char *expected) {
    char path[4096];
    char actual[256];
    FILE *stream;
    if (join_path(path, sizeof(path), directory, name) != 0) {
        return -1;
    }
    stream = fopen(path, "r");
    if (stream == NULL || fgets(actual, sizeof(actual), stream) == NULL) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return -1;
    }
    (void)fclose(stream);
    actual[strcspn(actual, "\r\n")] = '\0';
    if (strcmp(actual, expected) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int mc_cgroup_create(const struct mc_run_config *config, struct mc_cgroup *cgroup,
                     struct mc_error *error) {
    char relative[4096];
    char expected_unit[96];
    char value[128];
    char process[32];
    int attempt;

    (void)memset(cgroup, 0, sizeof(*cgroup));
    cgroup->payload_fd = -1;
    if (getenv("MC_SKIP_CGROUP") != NULL) {
        return 0;
    }
    if (create_delegated_scope(config->id, error) != 0) {
        return -1;
    }
    (void)snprintf(expected_unit, sizeof(expected_unit), "minicontainer-%s.scope", config->id);
    for (attempt = 0; attempt < 100; ++attempt) {
        if (read_current_cgroup(relative, sizeof(relative)) == 0 &&
            strstr(relative, expected_unit) != NULL) {
            break;
        }
        (void)usleep(10000U);
    }
    if (attempt == 100 || snprintf(cgroup->scope, sizeof(cgroup->scope), "/sys/fs/cgroup%s",
                                   relative) < 0 ||
        join_path(cgroup->supervisor, sizeof(cgroup->supervisor), cgroup->scope, "supervisor") !=
            0 ||
        join_path(cgroup->payload, sizeof(cgroup->payload), cgroup->scope, "payload") != 0 ||
        mkdir(cgroup->supervisor, 0755) != 0 || mkdir(cgroup->payload, 0755) != 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "cgroup-create", config->id,
                     "cannot create delegated cgroup layout");
        return -1;
    }
    (void)snprintf(process, sizeof(process), "%ld\n", (long)getpid());
    if (write_value(cgroup->supervisor, "cgroup.procs", process) != 0 ||
        write_value(cgroup->scope, "cgroup.subtree_control", "+cpu +memory +pids +io") != 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "cgroup-delegate", cgroup->scope,
                     "cannot enable delegated controllers");
        return -1;
    }
    (void)snprintf(value, sizeof(value), "%llu", (unsigned long long)config->memory_max);
    if (write_value(cgroup->payload, "memory.max", value) != 0 ||
        verify_value(cgroup->payload, "memory.max", value) != 0) {
        goto limit_error;
    }
    (void)snprintf(value, sizeof(value), "%llu", (unsigned long long)config->swap_max);
    if (write_value(cgroup->payload, "memory.swap.max", value) != 0 ||
        verify_value(cgroup->payload, "memory.swap.max", value) != 0) {
        goto limit_error;
    }
    (void)snprintf(value, sizeof(value), "%llu 100000",
                   (unsigned long long)config->cpu_quota);
    if (write_value(cgroup->payload, "cpu.max", value) != 0 ||
        verify_value(cgroup->payload, "cpu.max", value) != 0) {
        goto limit_error;
    }
    (void)snprintf(value, sizeof(value), "%llu", (unsigned long long)config->pids_max);
    if (write_value(cgroup->payload, "pids.max", value) != 0 ||
        verify_value(cgroup->payload, "pids.max", value) != 0) {
        goto limit_error;
    }
    cgroup->payload_fd = open(cgroup->payload, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (cgroup->payload_fd < 0) {
        goto limit_error;
    }
    cgroup->active = 1;
    return 0;

limit_error:
    mc_error_set(error, MC_EXIT_RUNTIME, errno, "cgroup-limit", cgroup->payload,
                 "cannot program and verify resource limit");
    return -1;
}

void mc_cgroup_destroy(struct mc_cgroup *cgroup) {
    if (cgroup == NULL || cgroup->active == 0) {
        return;
    }
    if (cgroup->payload_fd >= 0) {
        (void)close(cgroup->payload_fd);
        cgroup->payload_fd = -1;
    }
    (void)write_value(cgroup->payload, "cgroup.kill", "1");
    (void)rmdir(cgroup->payload);
    cgroup->active = 0;
}
