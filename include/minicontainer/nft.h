#ifndef MINICONTAINER_NFT_H
#define MINICONTAINER_NFT_H

#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

int mc_nft_ensure_base(struct mc_error *error);
int mc_nft_add_container(const char *id, unsigned int ipv4_host,
                         const struct mc_publish *publishes, size_t publish_count,
                         struct mc_error *error);
void mc_nft_remove_container(const char *id);

#endif
