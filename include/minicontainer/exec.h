#ifndef MINICONTAINER_EXEC_H
#define MINICONTAINER_EXEC_H

#include "minicontainer/error.h"

int mc_exec_container(const char *reference, char **command, struct mc_error *error);

#endif
