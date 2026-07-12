#include "minicontainer/subid.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_start(const char *environment_name, const char *file, uint32_t *start) {
    const char *override = getenv(environment_name);
    FILE *stream;
    char line[512];

    if (override != NULL && override[0] != '\0') {
        char *end = NULL;
        const unsigned long parsed = strtoul(override, &end, 10);
        if (end == override || *end != '\0' || parsed > UINT32_MAX - 65535U) {
            errno = EINVAL;
            return -1;
        }
        *start = (uint32_t)parsed;
        return 0;
    }
    stream = fopen(file, "r");
    if (stream == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), stream) != NULL) {
        char owner[128];
        unsigned long parsed_start;
        unsigned long count;
        if (sscanf(line, "%127[^:]:%lu:%lu", owner, &parsed_start, &count) == 3 &&
            strcmp(owner, "minicontainer") == 0 && count >= 65536UL &&
            parsed_start <= UINT32_MAX - 65535U) {
            *start = (uint32_t)parsed_start;
            (void)fclose(stream);
            return 0;
        }
    }
    (void)fclose(stream);
    errno = ENOENT;
    return -1;
}

int mc_subid_range(uint32_t *uid_start, uint32_t *gid_start, struct mc_error *error) {
    if (uid_start == NULL || gid_start == NULL ||
        read_start("MC_SUBUID_START", "/etc/subuid", uid_start) != 0 ||
        read_start("MC_SUBGID_START", "/etc/subgid", gid_start) != 0) {
        mc_error_set(error, MC_EXIT_PREREQUISITE, errno, "subordinate-ids", "minicontainer",
                     "a dedicated 65536-ID UID/GID range is required");
        return -1;
    }
    return 0;
}
