#include "minicontainer/validate.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

int mc_valid_name(const char *name) {
    size_t index;
    size_t length;

    if (name == NULL) {
        return 0;
    }
    length = strlen(name);
    if (length == 0U || length > 64U || !isalnum((unsigned char)name[0])) {
        return 0;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)name[index];
        if (!(islower(character) || isdigit(character) || character == '.' ||
              character == '_' || character == '-')) {
            return 0;
        }
    }
    return strstr(name, "..") == NULL;
}

int mc_safe_archive_path(const char *path) {
    const char *component;

    if (path == NULL || path[0] == '\0' || path[0] == '/' || strchr(path, '\\') != NULL) {
        return 0;
    }
    component = path;
    while (*component != '\0') {
        const char *separator = strchr(component, '/');
        const size_t length = separator == NULL ? strlen(component)
                                                : (size_t)(separator - component);
        if (length == 0U || (length == 1U && component[0] == '.') ||
            (length == 2U && component[0] == '.' && component[1] == '.')) {
            return 0;
        }
        if (separator == NULL) {
            break;
        }
        component = separator + 1;
    }
    return 1;
}

int mc_safe_link_target(const char *path) {
    if (path == NULL) {
        return 0;
    }
    return mc_safe_archive_path(path[0] == '/' ? path + 1 : path);
}

static int apply_components(const char *path, size_t *depth) {
    const char *component = path;

    while (*component != '\0') {
        const char *separator = strchr(component, '/');
        const size_t length = separator == NULL ? strlen(component)
                                                : (size_t)(separator - component);
        if (length == 2U && component[0] == '.' && component[1] == '.') {
            if (*depth == 0U) {
                return 0;
            }
            --(*depth);
        } else if (length != 0U && !(length == 1U && component[0] == '.')) {
            ++(*depth);
        }
        if (separator == NULL) {
            break;
        }
        component = separator + 1;
    }
    return 1;
}

int mc_link_stays_beneath(const char *entry_path, const char *target) {
    size_t depth = 0U;
    const char *slash;
    char parent[4096];
    size_t parent_length;

    if (entry_path == NULL || target == NULL || target[0] == '\0' ||
        strchr(target, '\\') != NULL) {
        return 0;
    }
    if (target[0] == '/') {
        return apply_components(target + 1, &depth);
    }
    slash = strrchr(entry_path, '/');
    parent_length = slash == NULL ? 0U : (size_t)(slash - entry_path);
    if (parent_length >= sizeof(parent)) {
        return 0;
    }
    (void)memcpy(parent, entry_path, parent_length);
    parent[parent_length] = '\0';
    if (!apply_components(parent, &depth)) {
        return 0;
    }
    return apply_components(target, &depth);
}

int mc_valid_environment(const char *assignment) {
    const char *equals;
    const char *cursor;

    if (assignment == NULL || (equals = strchr(assignment, '=')) == NULL ||
        equals == assignment || !(isalpha((unsigned char)assignment[0]) || assignment[0] == '_')) {
        return 0;
    }
    for (cursor = assignment + 1; cursor < equals; ++cursor) {
        if (!(isalnum((unsigned char)*cursor) || *cursor == '_')) {
            return 0;
        }
    }
    return strchr(equals + 1, '\n') == NULL && strchr(equals + 1, '\r') == NULL;
}

int mc_parse_user(const char *value, unsigned int *user, unsigned int *group) {
    char *end = NULL;
    unsigned long parsed_user;
    unsigned long parsed_group;

    if (value == NULL || user == NULL || group == NULL || value[0] == '\0') {
        return 0;
    }
    errno = 0;
    parsed_user = strtoul(value, &end, 10);
    if (errno != 0 || end == value || parsed_user > 65535UL) {
        return 0;
    }
    parsed_group = parsed_user;
    if (*end == ':') {
        const char *group_start = end + 1;
        errno = 0;
        parsed_group = strtoul(group_start, &end, 10);
        if (errno != 0 || end == group_start || parsed_group > 65535UL) {
            return 0;
        }
    }
    if (*end != '\0') {
        return 0;
    }
    *user = (unsigned int)parsed_user;
    *group = (unsigned int)parsed_group;
    return 1;
}
