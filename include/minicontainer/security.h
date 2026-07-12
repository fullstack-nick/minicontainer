#ifndef MINICONTAINER_SECURITY_H
#define MINICONTAINER_SECURITY_H

#include <stdint.h>
#include <stddef.h>

#include "minicontainer/error.h"

int mc_capability_parse(const char *name, unsigned int *number);
char *mc_capability_name(unsigned int number);
int mc_security_prepare(uint64_t capability_mask);
int mc_seccomp_profile_load(const char *path, char ***denies, size_t *count,
                            struct mc_error *error);
int mc_seccomp_name_valid(const char *name);
int mc_security_apply(uint64_t capability_mask, char *const *denies, size_t deny_count);

#endif
