#ifndef MINICONTAINER_NETWORK_H
#define MINICONTAINER_NETWORK_H

#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

#include <sys/types.h>

struct mc_network {
    char host_interface[16];
    char peer_interface[16];
    unsigned int ipv4_host;
    char id[33];
    int active;
};

int mc_network_setup(const char *id, pid_t namespace_pid, unsigned int ipv4_host,
                     const struct mc_publish *publishes, size_t publish_count,
                     struct mc_network *network, struct mc_error *error);
void mc_network_destroy(struct mc_network *network);
void mc_network_cleanup_owned(const char *id);
int mc_parse_publish(const char *value, struct mc_publish *published);

#endif
