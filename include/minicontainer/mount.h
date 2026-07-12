#ifndef MINICONTAINER_MOUNT_H
#define MINICONTAINER_MOUNT_H

#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

int mc_parse_bind_mount(const char *value, struct mc_mount *mount, struct mc_error *error);
int mc_parse_tmpfs_mount(const char *value, struct mc_mount *mount, struct mc_error *error);
void mc_mount_free(struct mc_mount *mount);

#endif
