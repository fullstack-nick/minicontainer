#ifndef MINICONTAINER_RUNTIME_H
#define MINICONTAINER_RUNTIME_H

#include "minicontainer/error.h"

struct mc_run_config {
    char *id;
    char *rootfs;
    char *hostname;
    char *const *command;
};

int mc_launch_shim(const struct mc_run_config *config, struct mc_error *error);
int mc_container_run(const struct mc_run_config *config, struct mc_error *error);

#endif
