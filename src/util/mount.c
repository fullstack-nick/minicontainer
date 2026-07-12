#include "minicontainer/mount.h"

#include "minicontainer/resource.h"
#include "minicontainer/state.h"

#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int valid_target(const char *target) {
    const char *component;
    static const char *const forbidden[] = {
        "/proc", "/sys", "/dev", "/run/minicontainer", "/var/lib/minicontainer"
    };
    size_t index;
    if (target == NULL || target[0] != '/' || target[1] == '\0' || strlen(target) >= PATH_MAX)
        return 0;
    component = target;
    while ((component = strstr(component, "..")) != NULL) {
        if ((component == target || component[-1] == '/') &&
            (component[2] == '\0' || component[2] == '/')) return 0;
        component += 2;
    }
    for (index = 0U; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        const size_t length = strlen(forbidden[index]);
        if (strncmp(target, forbidden[index], length) == 0 &&
            (target[length] == '\0' || target[length] == '/')) return 0;
    }
    return 1;
}

static int under_path(const char *path, const char *parent) {
    const size_t length = strlen(parent);
    return strncmp(path, parent, length) == 0 &&
           (path[length] == '\0' || path[length] == '/');
}

static int bind_allowed(const char *source) {
    const char *config_path = getenv("MC_CONFIG_PATH");
    json_object *root, *allowed;
    size_t index;
    int result = 0;
    if (config_path == NULL || config_path[0] == '\0') config_path = "/etc/minicontainer/config.json";
    root = json_object_from_file(config_path);
    if (root == NULL || !json_object_object_get_ex(root, "allowed_bind_sources", &allowed) ||
        !json_object_is_type(allowed, json_type_array)) {
        json_object_put(root);
        return 0;
    }
    for (index = 0U; index < json_object_array_length(allowed); ++index) {
        const char *entry = json_object_get_string(json_object_array_get_idx(allowed, index));
        char resolved[PATH_MAX];
        if (entry != NULL && realpath(entry, resolved) != NULL && under_path(source, resolved)) {
            result = 1;
            break;
        }
    }
    json_object_put(root);
    return result;
}

void mc_mount_free(struct mc_mount *mount) {
    if (mount == NULL) return;
    free(mount->source); free(mount->target);
    (void)memset(mount, 0, sizeof(*mount));
}

int mc_parse_bind_mount(const char *value, struct mc_mount *mount, struct mc_error *error) {
    char *copy = NULL, *separator, *mode = NULL;
    char resolved[PATH_MAX];
    struct stat metadata;
    if (value == NULL || mount == NULL || (copy = strdup(value)) == NULL) goto invalid;
    separator = strchr(copy, ':');
    if (separator == NULL) goto invalid;
    *separator++ = '\0';
    mode = strchr(separator, ':');
    if (mode != NULL) *mode++ = '\0';
    if (copy[0] != '/' || !valid_target(separator) ||
        (mode != NULL && strcmp(mode, "ro") != 0 && strcmp(mode, "rw") != 0) ||
        realpath(copy, resolved) == NULL || stat(resolved, &metadata) != 0 ||
        !S_ISDIR(metadata.st_mode) || !bind_allowed(resolved)) goto invalid;
    mount->type = MC_MOUNT_BIND;
    mount->source = strdup(resolved);
    mount->target = strdup(separator);
    mount->readonly = mode != NULL && strcmp(mode, "ro") == 0;
    free(copy);
    if (mount->source == NULL || mount->target == NULL) goto invalid_mount;
    return 1;
invalid:
    free(copy);
invalid_mount:
    mc_mount_free(mount);
    mc_error_set(error, MC_EXIT_USAGE, EINVAL, "parse-bind", value,
                 "bind must be an allowlisted directory SOURCE:TARGET[:ro|rw]");
    return 0;
}

int mc_parse_tmpfs_mount(const char *value, struct mc_mount *mount, struct mc_error *error) {
    char *copy = value == NULL ? NULL : strdup(value);
    char *separator;
    uint64_t size = UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024);
    if (copy == NULL) goto invalid;
    separator = strchr(copy + 1, ':');
    if (separator != NULL) {
        *separator++ = '\0';
        if (!mc_parse_bytes(separator, &size) || size > UINT64_C(1073741824)) goto invalid;
    }
    if (!valid_target(copy)) goto invalid;
    mount->type = MC_MOUNT_TMPFS;
    mount->target = strdup(copy);
    mount->size = size;
    free(copy);
    if (mount->target == NULL) goto invalid_mount;
    return 1;
invalid:
    free(copy);
invalid_mount:
    mc_mount_free(mount);
    mc_error_set(error, MC_EXIT_USAGE, EINVAL, "parse-tmpfs", value,
                 "tmpfs must be TARGET[:SIZE] with an absolute safe target");
    return 0;
}
