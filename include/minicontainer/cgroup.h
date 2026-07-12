#ifndef MINICONTAINER_CGROUP_H
#define MINICONTAINER_CGROUP_H

#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

struct mc_cgroup {
    char scope[4096];
    char supervisor[4096];
    char payload[4096];
    int payload_fd;
    int active;
};

int mc_cgroup_create(const struct mc_run_config *config, struct mc_cgroup *cgroup,
                     struct mc_error *error);
void mc_cgroup_destroy(struct mc_cgroup *cgroup);

#endif
