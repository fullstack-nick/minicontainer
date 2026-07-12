#ifndef MINICONTAINER_FS_H
#define MINICONTAINER_FS_H

#include "minicontainer/error.h"

#include <stddef.h>
#include <sys/types.h>

int mc_mkdir_p(const char *path, mode_t mode, struct mc_error *error);
int mc_write_atomic(const char *path, const void *data, size_t length, mode_t mode,
                    struct mc_error *error);
const char *mc_state_dir(void);

#endif
