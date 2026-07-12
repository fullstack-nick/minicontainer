#ifndef MINICONTAINER_RUNTIME_H
#define MINICONTAINER_RUNTIME_H

#include "minicontainer/error.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct mc_run_config {
    char *id;
    char *name;
    char *rootfs;
    char *hostname;
    char *workdir;
    uid_t user;
    gid_t group;
    char **environment;
    size_t environment_count;
    int detach;
    int ready_fd;
    uint64_t memory_max;
    uint64_t swap_max;
    uint64_t cpu_quota;
    uint64_t pids_max;
    char **command;
};

int mc_launch_shim(const struct mc_run_config *config, struct mc_error *error);
int mc_container_run(const struct mc_run_config *config, struct mc_error *error);

#endif
