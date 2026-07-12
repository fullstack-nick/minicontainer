#ifndef MINICONTAINER_RESOURCE_H
#define MINICONTAINER_RESOURCE_H

#include <stdint.h>

int mc_parse_bytes(const char *value, uint64_t *bytes);
int mc_parse_cpu_quota(const char *value, uint64_t *quota);
int mc_parse_positive_u64(const char *value, uint64_t maximum, uint64_t *result);

#endif
