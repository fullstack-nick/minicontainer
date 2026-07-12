#ifndef MINICONTAINER_ERROR_H
#define MINICONTAINER_ERROR_H

#include <stddef.h>

enum mc_exit_code {
    MC_EXIT_OK = 0,
    MC_EXIT_USAGE = 2,
    MC_EXIT_PREREQUISITE = 3,
    MC_EXIT_CONFLICT = 4,
    MC_EXIT_NOT_FOUND = 5,
    MC_EXIT_PERMISSION = 6,
    MC_EXIT_RUNTIME = 7,
    MC_EXIT_INTERNAL = 125
};

struct mc_error {
    int code;
    int saved_errno;
    char operation[64];
    char resource[256];
    char message[256];
};

void mc_error_set(struct mc_error *error, int code, int saved_errno,
                  const char *operation, const char *resource, const char *message);
void mc_error_print(const struct mc_error *error, int json);

#endif
