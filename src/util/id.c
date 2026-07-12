#include "minicontainer/id.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/random.h>

int mc_generate_id(char output[33], struct mc_error *error) {
    unsigned char random_bytes[16];
    size_t received = 0U;
    size_t index;

    while (received < sizeof(random_bytes)) {
        const ssize_t result = getrandom(random_bytes + received,
                                         sizeof(random_bytes) - received, 0U);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            mc_error_set(error, MC_EXIT_INTERNAL, errno, "generate-id", "getrandom",
                         "cannot obtain cryptographic randomness");
            return -1;
        }
        received += (size_t)result;
    }
    for (index = 0U; index < sizeof(random_bytes); ++index) {
        (void)snprintf(output + (index * 2U), 3U, "%02x", random_bytes[index]);
    }
    output[32] = '\0';
    return 0;
}
