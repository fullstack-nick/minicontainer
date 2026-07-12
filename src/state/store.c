#include "minicontainer/state.h"

#include "minicontainer/fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <json-c/json.h>
#include <linux/sched.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int make_path(char *output, size_t size, const char *id, const char *leaf) {
    const int length = id == NULL
                           ? snprintf(output, size, "%s/%s", mc_state_dir(), leaf)
                           : snprintf(output, size, "%s/containers/%s/%s", mc_state_dir(), id,
                                      leaf);
    if (length < 0 || (size_t)length >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int take_lock(const char *path, struct mc_state_lock *lock, struct mc_error *error) {
    lock->descriptor = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock->descriptor < 0 || flock(lock->descriptor, LOCK_EX) != 0) {
        const int saved = errno;
        if (lock->descriptor >= 0) (void)close(lock->descriptor);
        lock->descriptor = -1;
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "state-lock", path,
                     "cannot acquire exclusive lifecycle lock");
        return -1;
    }
    return 0;
}

int mc_state_registry_lock(struct mc_state_lock *lock, struct mc_error *error) {
    char path[PATH_MAX];
    if (mc_mkdir_p(mc_state_dir(), 0700, error) != 0 ||
        make_path(path, sizeof(path), NULL, "registry.lock") != 0) {
        if (error->code == 0)
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "state-lock", mc_state_dir(),
                         "cannot prepare registry lock");
        return -1;
    }
    return take_lock(path, lock, error);
}

int mc_state_container_lock(const char *id, struct mc_state_lock *lock,
                            struct mc_error *error) {
    char directory[PATH_MAX], path[PATH_MAX];
    if (make_path(directory, sizeof(directory), id, "") != 0 ||
        mc_mkdir_p(directory, 0700, error) != 0 ||
        make_path(path, sizeof(path), id, "container.lock") != 0) {
        if (error->code == 0)
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "state-lock", id,
                         "cannot prepare container lock");
        return -1;
    }
    return take_lock(path, lock, error);
}

void mc_state_unlock(struct mc_state_lock *lock) {
    if (lock != NULL && lock->descriptor >= 0) {
        (void)flock(lock->descriptor, LOCK_UN);
        (void)close(lock->descriptor);
        lock->descriptor = -1;
    }
}

static json_object *config_json(const struct mc_run_config *config, const char *image) {
    json_object *root = json_object_new_object();
    json_object *resources = json_object_new_object();
    json_object *environment = json_object_new_array();
    json_object *command = json_object_new_array();
    size_t index;
    if (root == NULL || resources == NULL || environment == NULL || command == NULL) goto fail;
    json_object_object_add(root, "schema_version", json_object_new_int(1));
    json_object_object_add(root, "id", json_object_new_string(config->id));
    if (config->name != NULL) json_object_object_add(root, "name", json_object_new_string(config->name));
    json_object_object_add(root, "image", json_object_new_string(image));
    json_object_object_add(root, "rootfs", json_object_new_string(config->rootfs));
    json_object_object_add(root, "hostname", json_object_new_string(config->hostname));
    json_object_object_add(root, "workdir", json_object_new_string(config->workdir));
    json_object_object_add(root, "user", json_object_new_int64((int64_t)config->user));
    json_object_object_add(root, "group", json_object_new_int64((int64_t)config->group));
    json_object_object_add(resources, "memory_max", json_object_new_uint64(config->memory_max));
    json_object_object_add(resources, "swap_max", json_object_new_uint64(config->swap_max));
    json_object_object_add(resources, "cpu_quota", json_object_new_uint64(config->cpu_quota));
    json_object_object_add(resources, "pids_max", json_object_new_uint64(config->pids_max));
    json_object_object_add(root, "resources", resources);
    for (index = 0U; index < config->environment_count; ++index)
        json_object_array_add(environment, json_object_new_string(config->environment[index]));
    json_object_object_add(root, "environment", environment);
    for (index = 0U; config->command[index] != NULL; ++index)
        json_object_array_add(command, json_object_new_string(config->command[index]));
    json_object_object_add(root, "command", command);
    return root;
fail:
    json_object_put(root); json_object_put(resources); json_object_put(environment);
    json_object_put(command); errno = ENOMEM; return NULL;
}

static int name_is_taken(const char *name, const char *own_id) {
    char directory[PATH_MAX];
    DIR *stream;
    struct dirent *entry;
    if (name == NULL) return 0;
    if (snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0) return 1;
    stream = opendir(directory);
    if (stream == NULL) return errno == ENOENT ? 0 : 1;
    while ((entry = readdir(stream)) != NULL) {
        char path[PATH_MAX];
        json_object *root, *value;
        int matches = 0;
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, own_id) == 0 ||
            make_path(path, sizeof(path), entry->d_name, "config.json") != 0) continue;
        root = json_object_from_file(path);
        if (root != NULL && json_object_object_get_ex(root, "name", &value) &&
            strcmp(json_object_get_string(value), name) == 0) matches = 1;
        json_object_put(root);
        if (matches != 0) { (void)closedir(stream); return 1; }
    }
    (void)closedir(stream); return 0;
}

int mc_state_save_config(const struct mc_run_config *config, const char *image,
                         struct mc_error *error) {
    char directory[PATH_MAX], path[PATH_MAX];
    json_object *root;
    const char *document;
    int result;
    if (name_is_taken(config->name, config->id) != 0) {
        mc_error_set(error, MC_EXIT_CONFLICT, EEXIST, "save-config", config->name,
                     "container name already exists");
        return -1;
    }
    root = config_json(config, image);
    if (root == NULL) {
        mc_error_set(error, MC_EXIT_INTERNAL, errno, "save-config", config->id,
                     "cannot serialize container configuration");
        return -1;
    }
    if (make_path(directory, sizeof(directory), config->id, "") != 0 ||
        make_path(path, sizeof(path), config->id, "config.json") != 0 ||
        mc_mkdir_p(directory, 0700, error) != 0) {
        json_object_put(root); return -1;
    }
    document = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    result = mc_write_atomic(path, document, strlen(document), 0600, error);
    json_object_put(root);
    return result;
}

int mc_state_mark_created(const char *id, struct mc_error *error) {
    char path[PATH_MAX];
    char document[256];
    const int length = snprintf(document, sizeof(document),
                                "{\"schema_version\":1,\"id\":\"%s\","
                                "\"status\":\"created\",\"shim_pid\":0,"
                                "\"shim_start_time\":0,"
                                "\"init_pid\":0,\"exit_code\":0,\"cgroup_path\":\"\"}\n",
                                id);
    if (length < 0 || (size_t)length >= sizeof(document) ||
        make_path(path, sizeof(path), id, "state.json") != 0) {
        mc_error_set(error, MC_EXIT_INTERNAL, EOVERFLOW, "create-state", id,
                     "cannot serialize created state");
        return -1;
    }
    return mc_write_atomic(path, document, (size_t)length, 0600, error);
}

static char *copy_string(json_object *root, const char *key) {
    json_object *value;
    if (!json_object_object_get_ex(root, key, &value) ||
        json_object_get_type(value) != json_type_string) return NULL;
    return strdup(json_object_get_string(value));
}

static int load_arrays(json_object *root, struct mc_run_config *config) {
    json_object *array;
    size_t index, count;
    if (!json_object_object_get_ex(root, "environment", &array) ||
        json_object_get_type(array) != json_type_array) return -1;
    count = json_object_array_length(array);
    config->environment = calloc(count + 1U, sizeof(*config->environment));
    if (config->environment == NULL) return -1;
    config->environment_count = count;
    for (index = 0U; index < count; ++index)
        config->environment[index] = strdup(json_object_get_string(json_object_array_get_idx(array, index)));
    if (!json_object_object_get_ex(root, "command", &array) ||
        json_object_get_type(array) != json_type_array) return -1;
    count = json_object_array_length(array);
    config->command = calloc(count + 1U, sizeof(*config->command));
    if (config->command == NULL || count == 0U) return -1;
    for (index = 0U; index < count; ++index)
        config->command[index] = strdup(json_object_get_string(json_object_array_get_idx(array, index)));
    return 0;
}

int mc_state_load_config(const char *reference, struct mc_run_config *config,
                         char image[PATH_MAX], struct mc_error *error) {
    char id[33], path[PATH_MAX];
    json_object *root, *value, *resources;
    if (mc_state_resolve(reference, id, error) != 0 ||
        make_path(path, sizeof(path), id, "config.json") != 0) return -1;
    root = json_object_from_file(path);
    if (root == NULL) goto invalid;
    (void)memset(config, 0, sizeof(*config)); config->ready_fd = -1;
    config->id = copy_string(root, "id"); config->name = copy_string(root, "name");
    config->rootfs = copy_string(root, "rootfs"); config->hostname = copy_string(root, "hostname");
    config->workdir = copy_string(root, "workdir");
    if (!json_object_object_get_ex(root, "image", &value) ||
        snprintf(image, PATH_MAX, "%s", json_object_get_string(value)) < 0 ||
        !json_object_object_get_ex(root, "user", &value)) goto invalid_loaded;
    config->user = (uid_t)json_object_get_int64(value);
    if (!json_object_object_get_ex(root, "group", &value)) goto invalid_loaded;
    config->group = (gid_t)json_object_get_int64(value);
    if (!json_object_object_get_ex(root, "resources", &resources)) goto invalid_loaded;
#define LOAD_U64(key, member) do { if (!json_object_object_get_ex(resources, key, &value)) goto invalid_loaded; config->member = json_object_get_uint64(value); } while (0)
    LOAD_U64("memory_max", memory_max); LOAD_U64("swap_max", swap_max);
    LOAD_U64("cpu_quota", cpu_quota); LOAD_U64("pids_max", pids_max);
#undef LOAD_U64
    if (config->id == NULL || config->rootfs == NULL || config->hostname == NULL ||
        config->workdir == NULL || load_arrays(root, config) != 0) goto invalid_loaded;
    json_object_put(root); return 0;
invalid_loaded:
    mc_state_free_config(config);
invalid:
    json_object_put(root);
    mc_error_set(error, MC_EXIT_RUNTIME, EINVAL, "load-config", path,
                 "stored container configuration is invalid");
    return -1;
}

void mc_state_free_config(struct mc_run_config *config) {
    size_t index;
    if (config == NULL) return;
    free(config->id); free(config->name); free(config->rootfs); free(config->hostname); free(config->workdir);
    for (index = 0U; index < config->environment_count; ++index) free(config->environment[index]);
    free(config->environment);
    if (config->command != NULL) {
        for (index = 0U; config->command[index] != NULL; ++index) free(config->command[index]);
        free(config->command);
    }
    (void)memset(config, 0, sizeof(*config));
}

int mc_state_resolve(const char *reference, char id[33], struct mc_error *error) {
    char directory[PATH_MAX]; DIR *stream; struct dirent *entry; size_t matches = 0U;
    if (reference == NULL || reference[0] == '\0' ||
        snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0) goto missing;
    stream = opendir(directory);
    if (stream == NULL) goto missing;
    while ((entry = readdir(stream)) != NULL) {
        char path[PATH_MAX]; json_object *root; json_object *name;
        if (entry->d_name[0] == '.' || strlen(entry->d_name) != 32U) continue;
        if (strncmp(entry->d_name, reference, strlen(reference)) == 0) {
            (void)memcpy(id, entry->d_name, 33U); ++matches; continue;
        }
        if (make_path(path, sizeof(path), entry->d_name, "config.json") != 0) continue;
        root = json_object_from_file(path);
        if (root != NULL && json_object_object_get_ex(root, "name", &name) &&
            strcmp(json_object_get_string(name), reference) == 0) {
            (void)memcpy(id, entry->d_name, 33U); ++matches;
        }
        json_object_put(root);
    }
    (void)closedir(stream);
    if (matches == 1U) return 0;
    mc_error_set(error, matches == 0U ? MC_EXIT_NOT_FOUND : MC_EXIT_CONFLICT, 0,
                 "resolve-container", reference, matches == 0U ? "container not found" : "ambiguous container reference");
    return -1;
missing:
    mc_error_set(error, MC_EXIT_NOT_FOUND, errno, "resolve-container", reference,
                 "container not found"); return -1;
}

static int remove_tree_entry(const char *path, const struct stat *metadata, int kind,
                             struct FTW *walk) {
    (void)metadata; (void)kind; (void)walk;
    return remove(path);
}

int mc_state_remove(const char *id, struct mc_error *error) {
    char directory[PATH_MAX];
    char log[PATH_MAX], log_directory[PATH_MAX];
    if (make_path(directory, sizeof(directory), id, "") != 0 ||
        snprintf(log_directory, sizeof(log_directory), "%s/%s", mc_log_dir(), id) < 0 ||
        snprintf(log, sizeof(log), "%s/container.log", log_directory) < 0) return -1;
    (void)chmod(directory, 0700);
    if (nftw(directory, remove_tree_entry, 32, FTW_DEPTH | FTW_PHYS) != 0 ||
        (unlink(log) != 0 && errno != ENOENT) || (rmdir(log_directory) != 0 && errno != ENOENT)) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "remove-container", id,
                     "cannot remove container state"); return -1;
    }
    return 0;
}

int mc_state_print_list(int include_stopped, int json, struct mc_error *error) {
    char directory[PATH_MAX];
    DIR *stream;
    struct dirent *entry;
    int first = 1;
    if (snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0 ||
        (stream = opendir(directory)) == NULL) {
        if (errno == ENOENT) {
            (void)printf(json != 0 ? "[]\n" : "ID\tNAME\tSTATUS\n");
            return 0;
        }
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "list-containers", directory,
                     "cannot read container registry");
        return -1;
    }
    if (json != 0) (void)printf("["); else (void)printf("ID\tNAME\tSTATUS\n");
    while ((entry = readdir(stream)) != NULL) {
        char state_path[PATH_MAX], config_path[PATH_MAX];
        json_object *state, *config, *value;
        const char *status;
        const char *name = "";
        if (entry->d_name[0] == '.' || strlen(entry->d_name) != 32U ||
            make_path(state_path, sizeof(state_path), entry->d_name, "state.json") != 0 ||
            make_path(config_path, sizeof(config_path), entry->d_name, "config.json") != 0) continue;
        state = json_object_from_file(state_path);
        config = json_object_from_file(config_path);
        if (state == NULL || !json_object_object_get_ex(state, "status", &value)) {
            json_object_put(state); json_object_put(config); continue;
        }
        status = json_object_get_string(value);
        if (include_stopped == 0 && strcmp(status, "running") != 0) {
            json_object_put(state); json_object_put(config); continue;
        }
        if (config != NULL && json_object_object_get_ex(config, "name", &value))
            name = json_object_get_string(value);
        if (json != 0) {
            (void)printf("%s{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\"}",
                         first != 0 ? "" : ",", entry->d_name, name, status);
        } else {
            (void)printf("%.12s\t%s\t%s\n", entry->d_name, name, status);
        }
        first = 0;
        json_object_put(state); json_object_put(config);
    }
    (void)closedir(stream);
    if (json != 0) (void)printf("]\n");
    return 0;
}

int mc_state_get_status(const char *id, char status[16], pid_t *shim_pid,
                        struct mc_error *error) {
    char path[PATH_MAX];
    json_object *root = NULL, *value;
    const char *stored;
    if (make_path(path, sizeof(path), id, "state.json") != 0 ||
        (root = json_object_from_file(path)) == NULL ||
        !json_object_object_get_ex(root, "status", &value)) {
        mc_error_set(error, MC_EXIT_NOT_FOUND, errno, "read-state", id,
                     "container state is unavailable");
        return -1;
    }
    stored = json_object_get_string(value);
    if (strlen(stored) >= 16U) {
        json_object_put(root);
        mc_error_set(error, MC_EXIT_RUNTIME, EINVAL, "read-state", id,
                     "container status is invalid");
        return -1;
    }
    (void)snprintf(status, 16U, "%s", stored);
    if (shim_pid != NULL) {
        *shim_pid = 0;
        if (json_object_object_get_ex(root, "shim_pid", &value))
            *shim_pid = (pid_t)json_object_get_int64(value);
    }
    json_object_put(root);
    return 0;
}

static unsigned long long process_start_time(pid_t pid) {
    char path[64], line[4096], *token;
    FILE *stream;
    int field = 3;
    (void)snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    stream = fopen(path, "r");
    if (stream == NULL || fgets(line, sizeof(line), stream) == NULL) {
        if (stream != NULL) (void)fclose(stream);
        return 0ULL;
    }
    (void)fclose(stream);
    token = strrchr(line, ')');
    if (token != NULL) token = strtok(token + 2, " ");
    while (token != NULL && field < 22) { token = strtok(NULL, " "); ++field; }
    return token == NULL ? 0ULL : strtoull(token, NULL, 10);
}

int mc_state_signal(const char *reference, int signal_number, struct mc_error *error) {
    char id[33], path[PATH_MAX];
    char cgroup_path[PATH_MAX] = "";
    json_object *root = NULL, *value;
    pid_t pid;
    unsigned long long saved_start;
    int pidfd;
    if (mc_state_resolve(reference, id, error) != 0 ||
        make_path(path, sizeof(path), id, "state.json") != 0 ||
        (root = json_object_from_file(path)) == NULL ||
        !json_object_object_get_ex(root, "status", &value) ||
        strcmp(json_object_get_string(value), "running") != 0 ||
        !json_object_object_get_ex(root, "shim_pid", &value)) {
        json_object_put(root);
        mc_error_set(error, MC_EXIT_CONFLICT, ESRCH, "signal-container", reference,
                     "container is not running");
        return -1;
    }
    pid = (pid_t)json_object_get_int64(value);
    if (json_object_object_get_ex(root, "cgroup_path", &value))
        (void)snprintf(cgroup_path, sizeof(cgroup_path), "%s", json_object_get_string(value));
    if (!json_object_object_get_ex(root, "shim_start_time", &value)) {
        json_object_put(root); goto stale;
    }
    saved_start = json_object_get_uint64(value);
    json_object_put(root);
    if (pid <= 0 || saved_start == 0ULL || process_start_time(pid) != saved_start) goto stale;
    if (signal_number == SIGKILL) {
        char kill_path[PATH_MAX];
        int descriptor = -1;
        const int length = snprintf(kill_path, sizeof(kill_path), "%s/cgroup.kill", cgroup_path);
        if (length < 0 || (size_t)length >= sizeof(kill_path) ||
            strncmp(cgroup_path, "/sys/fs/cgroup/", 15U) != 0 ||
            strstr(cgroup_path, id) == NULL ||
            (descriptor = open(kill_path, O_WRONLY | O_CLOEXEC)) < 0 ||
            write(descriptor, "1", 1U) != 1) {
            const int saved = errno;
            if (descriptor >= 0) (void)close(descriptor);
            mc_error_set(error, MC_EXIT_RUNTIME, saved, "kill-container", id,
                         "cannot terminate payload cgroup");
            return -1;
        }
        (void)close(descriptor);
        return 0;
    }
    pidfd = (int)syscall(SYS_pidfd_open, pid, 0U);
    if (pidfd < 0 || syscall(SYS_pidfd_send_signal, pidfd, signal_number, NULL, 0U) != 0) {
        const int saved = errno;
        if (pidfd >= 0) (void)close(pidfd);
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "signal-container", id,
                     "pidfd signal delivery failed");
        return -1;
    }
    (void)close(pidfd);
    return 0;
stale:
    mc_error_set(error, MC_EXIT_CONFLICT, ESRCH, "signal-container", id,
                 "recorded shim identity is stale");
    return -1;
}
