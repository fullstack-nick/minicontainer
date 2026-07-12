#ifndef MINICONTAINER_SUBID_H
#define MINICONTAINER_SUBID_H

#include "minicontainer/error.h"

#include <stdint.h>

int mc_subid_range(uint32_t *uid_start, uint32_t *gid_start, struct mc_error *error);

#endif
