#ifndef MINICONTAINER_STATE_H
#define MINICONTAINER_STATE_H

#include "minicontainer/error.h"
#include "minicontainer/runtime.h"

#include <limits.h>

struct mc_state_lock {
    int descriptor;
};

int mc_state_registry_lock(struct mc_state_lock *lock, struct mc_error *error);
int mc_state_container_lock(const char *id, struct mc_state_lock *lock,
                            struct mc_error *error);
void mc_state_unlock(struct mc_state_lock *lock);
int mc_state_save_config(const struct mc_run_config *config, const char *image,
                         struct mc_error *error);
int mc_state_mark_created(const char *id, struct mc_error *error);
int mc_state_load_config(const char *reference, struct mc_run_config *config,
                         char image[PATH_MAX], struct mc_error *error);
void mc_state_free_config(struct mc_run_config *config);
int mc_state_resolve(const char *reference, char id[33], struct mc_error *error);
int mc_state_remove(const char *id, struct mc_error *error);
int mc_state_print_list(int include_stopped, int json, struct mc_error *error);
int mc_state_get_status(const char *id, char status[16], pid_t *shim_pid,
                        struct mc_error *error);
int mc_state_signal(const char *reference, int signal_number, struct mc_error *error);

#endif
