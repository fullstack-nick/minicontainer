#include "minicontainer/fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *mc_state_dir(void) {
    const char *override = getenv("MC_STATE_DIR");
    return override == NULL || override[0] == '\0' ? "/var/lib/minicontainer" : override;
}

int mc_mkdir_p(const char *path, mode_t mode, struct mc_error *error) {
    char copy[PATH_MAX];
    char *cursor;

    if (path == NULL || strlen(path) >= sizeof(copy)) {
        mc_error_set(error, MC_EXIT_USAGE, ENAMETOOLONG, "mkdir", path, "path is too long");
        return -1;
    }
    (void)snprintf(copy, sizeof(copy), "%s", path);
    for (cursor = copy + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(copy, mode) != 0 && errno != EEXIST) {
                mc_error_set(error, MC_EXIT_RUNTIME, errno, "mkdir", copy,
                             "cannot create directory");
                return -1;
            }
            *cursor = '/';
        }
    }
    if (mkdir(copy, mode) != 0 && errno != EEXIST) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "mkdir", copy,
                     "cannot create directory");
        return -1;
    }
    return 0;
}

int mc_write_atomic(const char *path, const void *data, size_t length, mode_t mode,
                    struct mc_error *error) {
    char temporary[PATH_MAX];
    int descriptor;
    size_t written = 0U;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid()) < 0 ||
        strlen(temporary) >= sizeof(temporary)) {
        mc_error_set(error, MC_EXIT_INTERNAL, ENAMETOOLONG, "atomic-write", path,
                     "temporary path is too long");
        return -1;
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (descriptor < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "atomic-write", path,
                     "cannot create temporary file");
        return -1;
    }
    while (written < length) {
        const ssize_t result = write(descriptor, (const char *)data + written, length - written);
        if (result < 0) {
            const int saved = errno;
            (void)close(descriptor);
            (void)unlink(temporary);
            mc_error_set(error, MC_EXIT_RUNTIME, saved, "atomic-write", path, "write failed");
            return -1;
        }
        written += (size_t)result;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0 || rename(temporary, path) != 0) {
        const int saved = errno;
        (void)unlink(temporary);
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "atomic-write", path,
                     "commit failed");
        return -1;
    }
    return 0;
}
