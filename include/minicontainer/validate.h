#ifndef MINICONTAINER_VALIDATE_H
#define MINICONTAINER_VALIDATE_H

int mc_valid_name(const char *name);
int mc_safe_archive_path(const char *path);
int mc_safe_link_target(const char *path);
int mc_link_stays_beneath(const char *entry_path, const char *target);
int mc_valid_environment(const char *assignment);
int mc_parse_user(const char *value, unsigned int *user, unsigned int *group);

#endif
