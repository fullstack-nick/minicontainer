#include "minicontainer/state.h"

#include "minicontainer/fs.h"
#include "minicontainer/network.h"

#include <dirent.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <json-c/json.h>
#include <linux/sched.h>
#include <stdio.h>
#include <signal.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
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
    json_object *publishes = json_object_new_array();
    json_object *mounts = json_object_new_array();
    json_object *seccomp_denies = json_object_new_array();
    size_t index;
    struct timespec now;
    const char *digest = strstr(config->rootfs, "/sha256/");
    (void)clock_gettime(CLOCK_REALTIME, &now);
    if (root == NULL || resources == NULL || environment == NULL || command == NULL ||
        publishes == NULL || mounts == NULL || seccomp_denies == NULL) goto fail;
    json_object_object_add(root, "schema_version", json_object_new_int(1));
    json_object_object_add(root, "id", json_object_new_string(config->id));
    if (config->name != NULL) json_object_object_add(root, "name", json_object_new_string(config->name));
    json_object_object_add(root, "image", json_object_new_string(image));
    if (digest != NULL) {
        char value[65];
        digest += strlen("/sha256/");
        if (strlen(digest) >= 64U) {
            (void)memcpy(value, digest, 64U); value[64] = '\0';
            json_object_object_add(root, "image_digest", json_object_new_string(value));
        }
    }
    json_object_object_add(root, "rootfs", json_object_new_string(config->rootfs));
    json_object_object_add(root, "hostname", json_object_new_string(config->hostname));
    json_object_object_add(root, "workdir", json_object_new_string(config->workdir));
    json_object_object_add(root, "user", json_object_new_int64((int64_t)config->user));
    json_object_object_add(root, "group", json_object_new_int64((int64_t)config->group));
    json_object_object_add(root, "capability_mask", json_object_new_uint64(config->capability_mask));
    json_object_object_add(root, "readonly_root", json_object_new_boolean(config->readonly_root));
    json_object_object_add(root, "network_mode",
                           json_object_new_string(config->network_bridge != 0 ? "bridge" : "none"));
    json_object_object_add(root, "ipv4_host", json_object_new_int64((int64_t)config->ipv4_host));
    for (index = 0U; index < config->publish_count; ++index) {
        json_object *published = json_object_new_object();
        if (published == NULL) goto fail;
        json_object_object_add(published, "host_ipv4",
                               json_object_new_int64(config->publishes[index].host_ipv4));
        json_object_object_add(published, "host_port",
                               json_object_new_int(config->publishes[index].host_port));
        json_object_object_add(published, "container_port",
                               json_object_new_int(config->publishes[index].container_port));
        json_object_object_add(published, "protocol",
                               json_object_new_int(config->publishes[index].protocol));
        json_object_array_add(publishes, published);
    }
    json_object_object_add(root, "publishes", publishes);
    for (index = 0U; index < config->mount_count; ++index) {
        json_object *mounted = json_object_new_object();
        if (mounted == NULL) goto fail;
        json_object_object_add(mounted, "type",
                               json_object_new_int((int)config->mounts[index].type));
        if (config->mounts[index].source != NULL)
            json_object_object_add(mounted, "source", json_object_new_string(config->mounts[index].source));
        json_object_object_add(mounted, "target", json_object_new_string(config->mounts[index].target));
        json_object_object_add(mounted, "size", json_object_new_uint64(config->mounts[index].size));
        json_object_object_add(mounted, "readonly", json_object_new_boolean(config->mounts[index].readonly));
        json_object_array_add(mounts, mounted);
    }
    json_object_object_add(root, "mounts", mounts);
    for (index = 0U; index < config->seccomp_deny_count; ++index)
        json_object_array_add(seccomp_denies,
                              json_object_new_string(config->seccomp_denies[index]));
    json_object_object_add(root, "seccomp_denies", seccomp_denies);
    json_object_object_add(root, "created_at_unix_ns", json_object_new_uint64(
        ((uint64_t)now.tv_sec * UINT64_C(1000000000)) + (uint64_t)now.tv_nsec));
    json_object_object_add(root, "build_version", json_object_new_string(MC_VERSION));
    json_object_object_add(root, "git_commit", json_object_new_string(MC_GIT_COMMIT));
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
    json_object_put(command); json_object_put(publishes); json_object_put(mounts);
    json_object_put(seccomp_denies);
    errno = ENOMEM; return NULL;
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

static int published_port_is_taken(const struct mc_run_config *config) {
    char directory[PATH_MAX];
    DIR *stream;
    struct dirent *entry;
    size_t requested_index;
    if (config->publish_count == 0U) return 0;
    if (snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0) return 1;
    stream = opendir(directory);
    if (stream == NULL) return errno == ENOENT ? 0 : 1;
    while ((entry = readdir(stream)) != NULL) {
        char path[PATH_MAX]; json_object *root, *array;
        size_t existing_index;
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, config->id) == 0 ||
            make_path(path, sizeof(path), entry->d_name, "config.json") != 0) continue;
        root = json_object_from_file(path);
        if (root == NULL || !json_object_object_get_ex(root, "publishes", &array) ||
            json_object_get_type(array) != json_type_array) { json_object_put(root); continue; }
        for (existing_index = 0U; existing_index < json_object_array_length(array); ++existing_index) {
            json_object *published = json_object_array_get_idx(array, existing_index);
            json_object *value;
            uint32_t host_ipv4; uint16_t host_port; uint8_t protocol;
            if (!json_object_object_get_ex(published, "host_ipv4", &value)) continue;
            host_ipv4 = (uint32_t)json_object_get_int64(value);
            if (!json_object_object_get_ex(published, "host_port", &value)) continue;
            host_port = (uint16_t)json_object_get_int(value);
            if (!json_object_object_get_ex(published, "protocol", &value)) continue;
            protocol = (uint8_t)json_object_get_int(value);
            for (requested_index = 0U; requested_index < config->publish_count; ++requested_index) {
                const struct mc_publish *requested = &config->publishes[requested_index];
                if (requested->protocol == protocol && requested->host_port == host_port &&
                    (requested->host_ipv4 == 0U || host_ipv4 == 0U ||
                     requested->host_ipv4 == host_ipv4)) {
                    json_object_put(root); (void)closedir(stream); return 1;
                }
            }
        }
        json_object_put(root);
    }
    (void)closedir(stream); return 0;
}

static int host_port_is_available(const struct mc_publish *published) {
    struct sockaddr_in address;
    int descriptor = socket(AF_INET,
                            published->protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM,
                            0);
    int result;
    if (descriptor < 0) return 0;
    (void)memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(published->host_ipv4);
    address.sin_port = htons(published->host_port);
    result = bind(descriptor, (const struct sockaddr *)&address, sizeof(address));
    (void)close(descriptor);
    return result == 0;
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
    if (published_port_is_taken(config) != 0) {
        mc_error_set(error, MC_EXIT_CONFLICT, EADDRINUSE, "save-config", config->id,
                     "published host address and port already exists");
        return -1;
    }
    {
        size_t index;
        for (index = 0U; index < config->publish_count; ++index) {
            if (!host_port_is_available(&config->publishes[index])) {
                mc_error_set(error, MC_EXIT_CONFLICT, EADDRINUSE, "save-config", config->id,
                             "published host address and port is already bound");
                return -1;
            }
        }
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
                                "\"init_pid\":0,\"init_start_time\":0,"
                                "\"exit_code\":0,\"cgroup_path\":\"\"}\n",
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
    if (!json_object_object_get_ex(root, "publishes", &array) ||
        json_object_get_type(array) != json_type_array) return -1;
    count = json_object_array_length(array);
    config->publishes = calloc(count == 0U ? 1U : count, sizeof(*config->publishes));
    if (config->publishes == NULL) return -1;
    config->publish_count = count;
    for (index = 0U; index < count; ++index) {
        json_object *published = json_object_array_get_idx(array, index);
        json_object *value;
        if (!json_object_object_get_ex(published, "host_ipv4", &value)) return -1;
        config->publishes[index].host_ipv4 = (uint32_t)json_object_get_int64(value);
        if (!json_object_object_get_ex(published, "host_port", &value)) return -1;
        config->publishes[index].host_port = (uint16_t)json_object_get_int(value);
        if (!json_object_object_get_ex(published, "container_port", &value)) return -1;
        config->publishes[index].container_port = (uint16_t)json_object_get_int(value);
        if (!json_object_object_get_ex(published, "protocol", &value)) return -1;
        config->publishes[index].protocol = (uint8_t)json_object_get_int(value);
    }
    if (!json_object_object_get_ex(root, "mounts", &array)) return 0;
    if (json_object_get_type(array) != json_type_array) return -1;
    count = json_object_array_length(array);
    config->mounts = calloc(count == 0U ? 1U : count, sizeof(*config->mounts));
    if (config->mounts == NULL) return -1;
    config->mount_count = count;
    for (index = 0U; index < count; ++index) {
        json_object *mounted = json_object_array_get_idx(array, index);
        json_object *value;
        if (!json_object_object_get_ex(mounted, "type", &value)) return -1;
        config->mounts[index].type = (enum mc_mount_type)json_object_get_int(value);
        config->mounts[index].source = copy_string(mounted, "source");
        config->mounts[index].target = copy_string(mounted, "target");
        if (!json_object_object_get_ex(mounted, "size", &value)) return -1;
        config->mounts[index].size = json_object_get_uint64(value);
        if (!json_object_object_get_ex(mounted, "readonly", &value)) return -1;
        config->mounts[index].readonly = json_object_get_boolean(value);
        if (config->mounts[index].target == NULL ||
            (config->mounts[index].type == MC_MOUNT_BIND && config->mounts[index].source == NULL))
            return -1;
    }
    if (json_object_object_get_ex(root, "seccomp_denies", &array)) {
        if (json_object_get_type(array) != json_type_array) return -1;
        count = json_object_array_length(array);
        config->seccomp_denies = calloc(count + 1U, sizeof(*config->seccomp_denies));
        if (config->seccomp_denies == NULL) return -1;
        config->seccomp_deny_count = count;
        for (index = 0U; index < count; ++index) {
            const char *name = json_object_get_string(json_object_array_get_idx(array, index));
            if (name == NULL) return -1;
            config->seccomp_denies[index] = strdup(name);
            if (config->seccomp_denies[index] == NULL) return -1;
        }
    }
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
    if (json_object_object_get_ex(root, "capability_mask", &value))
        config->capability_mask = json_object_get_uint64(value);
    if (json_object_object_get_ex(root, "readonly_root", &value))
        config->readonly_root = json_object_get_boolean(value);
    if (!json_object_object_get_ex(root, "network_mode", &value)) goto invalid_loaded;
    config->network_bridge = strcmp(json_object_get_string(value), "bridge") == 0;
    if (!json_object_object_get_ex(root, "ipv4_host", &value)) goto invalid_loaded;
    config->ipv4_host = (unsigned int)json_object_get_int64(value);
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
    free(config->publishes);
    for (index = 0U; index < config->mount_count; ++index) {
        free(config->mounts[index].source); free(config->mounts[index].target);
    }
    free(config->mounts);
    for (index = 0U; index < config->seccomp_deny_count; ++index)
        free(config->seccomp_denies[index]);
    free(config->seccomp_denies);
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
    char log_directory[PATH_MAX];
    if (make_path(directory, sizeof(directory), id, "") != 0 ||
        snprintf(log_directory, sizeof(log_directory), "%s/%s", mc_log_dir(), id) < 0) return -1;
    (void)chmod(directory, 0700);
    (void)chmod(log_directory, 0700);
    if (nftw(directory, remove_tree_entry, 32, FTW_DEPTH | FTW_PHYS) != 0 ||
        (nftw(log_directory, remove_tree_entry, 32, FTW_DEPTH | FTW_PHYS) != 0 && errno != ENOENT)) {
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
    char socket_path[PATH_MAX], request[128], response[256];
    struct sockaddr_un address;
    json_object *root = NULL, *value;
    pid_t pid;
    unsigned long long saved_start;
    int connection;
    ssize_t response_length;
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
    if (!json_object_object_get_ex(root, "shim_start_time", &value)) {
        json_object_put(root); goto stale;
    }
    saved_start = json_object_get_uint64(value);
    json_object_put(root);
    if (pid <= 0 || saved_start == 0ULL || process_start_time(pid) != saved_start) goto stale;
    if (snprintf(socket_path, sizeof(socket_path), "%s/%s/control.sock", mc_runtime_dir(), id) < 0 ||
        strlen(socket_path) >= sizeof(address.sun_path) ||
        snprintf(request, sizeof(request),
                 "{\"version\":1,\"operation\":\"signal\",\"signal\":%d}",
                 signal_number) < 0) goto stale;
    connection = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    (void)memset(&address, 0, sizeof(address)); address.sun_family = AF_UNIX;
    (void)memcpy(address.sun_path, socket_path, strlen(socket_path) + 1U);
    if (connection < 0 || connect(connection, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        send(connection, request, strlen(request), MSG_NOSIGNAL) != (ssize_t)strlen(request) ||
        (response_length = recv(connection, response, sizeof(response) - 1U, 0)) <= 0) {
        const int saved = errno;
        if (connection >= 0) (void)close(connection);
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "signal-container", id,
                     "shim control request failed");
        return -1;
    }
    (void)close(connection); response[(size_t)response_length] = '\0';
    if (strstr(response, "\"ok\":true") == NULL) {
        mc_error_set(error, MC_EXIT_RUNTIME, EIO, "signal-container", id,
                     "shim rejected control request"); return -1;
    }
    return 0;
stale:
    mc_error_set(error, MC_EXIT_CONFLICT, ESRCH, "signal-container", id,
                 "recorded shim identity is stale");
    return -1;
}

int mc_state_runtime(const char *reference, char id[33], pid_t *init_pid,
                     unsigned long long *init_start_time, char cgroup[PATH_MAX],
                     struct mc_error *error) {
    char path[PATH_MAX];
    json_object *root = NULL, *value;
    if (mc_state_resolve(reference, id, error) != 0 ||
        make_path(path, sizeof(path), id, "state.json") != 0 ||
        (root = json_object_from_file(path)) == NULL ||
        !json_object_object_get_ex(root, "status", &value) ||
        strcmp(json_object_get_string(value), "running") != 0 ||
        !json_object_object_get_ex(root, "init_pid", &value)) goto unavailable;
    *init_pid = (pid_t)json_object_get_int64(value);
    if (!json_object_object_get_ex(root, "init_start_time", &value)) goto unavailable;
    *init_start_time = json_object_get_uint64(value);
    if (!json_object_object_get_ex(root, "cgroup_path", &value) ||
        snprintf(cgroup, PATH_MAX, "%s", json_object_get_string(value)) < 0) goto unavailable;
    json_object_put(root);
    if (*init_pid <= 0 || *init_start_time == 0ULL ||
        process_start_time(*init_pid) != *init_start_time) goto stale_runtime;
    return 0;
unavailable:
    json_object_put(root);
stale_runtime:
    mc_error_set(error, MC_EXIT_CONFLICT, ESRCH, "runtime-identity", reference,
                 "container runtime identity is unavailable or stale");
    return -1;
}

static void remove_ephemeral_paths(const char *id) {
    char path[PATH_MAX];
    const char *leaves[] = {"work", "merged"};
    size_t index;
    for (index = 0U; index < sizeof(leaves) / sizeof(leaves[0]); ++index) {
        if (make_path(path, sizeof(path), id, leaves[index]) == 0) {
            (void)chmod(path, 0700);
            (void)nftw(path, remove_tree_entry, 32, FTW_DEPTH | FTW_PHYS);
        }
    }
    if (snprintf(path, sizeof(path), "%s/%s/control.sock", mc_runtime_dir(), id) > 0)
        (void)unlink(path);
    if (snprintf(path, sizeof(path), "%s/%s", mc_runtime_dir(), id) > 0)
        (void)rmdir(path);
}

int mc_state_reconcile(int verbose, struct mc_error *error) {
    char directory[PATH_MAX];
    DIR *stream;
    struct dirent *entry;
    int repaired = 0;
    if (snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0 ||
        (stream = opendir(directory)) == NULL) {
        if (errno == ENOENT) return 0;
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "reconcile", directory,
                     "cannot read container registry"); return -1;
    }
    while ((entry = readdir(stream)) != NULL) {
        char path[PATH_MAX];
        json_object *root, *value;
        pid_t shim_pid;
        unsigned long long start_time;
        const char *document;
        if (entry->d_name[0] == '.' || strlen(entry->d_name) != 32U ||
            make_path(path, sizeof(path), entry->d_name, "state.json") != 0) continue;
        root = json_object_from_file(path);
        if (root == NULL || !json_object_object_get_ex(root, "status", &value) ||
            strcmp(json_object_get_string(value), "running") != 0) {
            json_object_put(root); continue;
        }
        if (!json_object_object_get_ex(root, "shim_pid", &value)) { json_object_put(root); continue; }
        shim_pid = (pid_t)json_object_get_int64(value);
        if (!json_object_object_get_ex(root, "shim_start_time", &value)) { json_object_put(root); continue; }
        start_time = json_object_get_uint64(value);
        if (shim_pid > 0 && start_time > 0ULL && process_start_time(shim_pid) == start_time) {
            json_object_put(root); continue;
        }
        if (json_object_object_get_ex(root, "cgroup_path", &value)) {
            char kill_path[PATH_MAX];
            const char *cgroup = json_object_get_string(value);
            int descriptor = -1;
            if (strncmp(cgroup, "/sys/fs/cgroup/", 15U) == 0 &&
                strstr(cgroup, entry->d_name) != NULL &&
                snprintf(kill_path, sizeof(kill_path), "%s/cgroup.kill", cgroup) > 0)
                descriptor = open(kill_path, O_WRONLY | O_CLOEXEC);
            if (descriptor >= 0) {
                const ssize_t kill_result = write(descriptor, "1", 1U);
                (void)close(descriptor);
                if (kill_result != 1 && verbose != 0)
                    (void)fprintf(stderr, "warning: stale cgroup kill failed for %.12s\n",
                                  entry->d_name);
            }
        }
        json_object_object_add(root, "status", json_object_new_string("stopped"));
        json_object_object_add(root, "shim_pid", json_object_new_int(0));
        json_object_object_add(root, "init_pid", json_object_new_int(0));
        json_object_object_add(root, "exit_code", json_object_new_int(125));
        json_object_object_add(root, "cgroup_path", json_object_new_string(""));
        document = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        if (mc_write_atomic(path, document, strlen(document), 0600, error) != 0) {
            json_object_put(root); (void)closedir(stream); return -1;
        }
        json_object_put(root); remove_ephemeral_paths(entry->d_name); ++repaired;
        mc_network_cleanup_owned(entry->d_name);
        if (verbose != 0) (void)printf("reconciled %.12s stale-running -> stopped\n", entry->d_name);
    }
    (void)closedir(stream);
    if (verbose != 0) (void)printf("reconciled=%d\n", repaired);
    return 0;
}

int mc_state_allocate_ip(const char *id, unsigned int *ipv4_host,
                         struct mc_error *error) {
    char directory[PATH_MAX];
    unsigned char used[256] = {0};
    DIR *stream;
    struct dirent *entry;
    size_t index;
    unsigned int candidate;
    if (id == NULL || ipv4_host == NULL) return -1;
    if (snprintf(directory, sizeof(directory), "%s/containers", mc_state_dir()) < 0) return -1;
    stream = opendir(directory);
    if (stream != NULL) {
        while ((entry = readdir(stream)) != NULL) {
            char path[PATH_MAX]; json_object *root, *value;
            unsigned int address;
            if (entry->d_name[0] == '.' ||
                make_path(path, sizeof(path), entry->d_name, "config.json") != 0) continue;
            root = json_object_from_file(path);
            if (root != NULL && json_object_object_get_ex(root, "ipv4_host", &value)) {
                address = (unsigned int)json_object_get_int64(value);
                if ((address & UINT32_C(0xffffff00)) == UINT32_C(0x0a2c0000))
                    used[address & UINT32_C(0xff)] = 1U;
            }
            json_object_put(root);
        }
        (void)closedir(stream);
    } else if (errno != ENOENT) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "ipam", directory,
                     "cannot inspect address leases"); return -1;
    }
    for (index = 0U; index < 253U; ++index) {
        candidate = 2U + (unsigned int)index;
        if (used[candidate] == 0U) {
            *ipv4_host = UINT32_C(0x0a2c0000) | candidate;
            return 0;
        }
    }
    mc_error_set(error, MC_EXIT_CONFLICT, ENOSPC, "ipam", "10.44.0.0/24",
                 "container address pool is exhausted");
    return -1;
}
