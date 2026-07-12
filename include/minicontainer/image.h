#ifndef MINICONTAINER_IMAGE_H
#define MINICONTAINER_IMAGE_H

#include "minicontainer/error.h"

struct mc_image_result {
    char digest[65];
    char rootfs[4096];
};

int mc_image_import(const char *name, const char *archive_path,
                    struct mc_image_result *result, struct mc_error *error);
int mc_image_resolve(const char *name, char *rootfs, unsigned long rootfs_size,
                     struct mc_error *error);

#endif
