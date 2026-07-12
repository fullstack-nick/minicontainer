#include "minicontainer/error.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *destination, size_t size, const char *source) {
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)snprintf(destination, size, "%s", source);
}

void mc_error_set(struct mc_error *error, int code, int saved_errno,
                  const char *operation, const char *resource, const char *message) {
    if (error == NULL) {
        return;
    }
    error->code = code;
    error->saved_errno = saved_errno;
    copy_text(error->operation, sizeof(error->operation), operation);
    copy_text(error->resource, sizeof(error->resource), resource);
    copy_text(error->message, sizeof(error->message), message);
}

void mc_error_print(const struct mc_error *error, int json) {
    const char *system_message;

    if (error == NULL) {
        return;
    }
    system_message = error->saved_errno == 0 ? "" : strerror(error->saved_errno);
    if (json != 0) {
        (void)fprintf(stderr,
                      "{\"error\":{\"code\":%d,\"operation\":\"%s\","
                      "\"resource\":\"%s\",\"message\":\"%s\","
                      "\"system\":\"%s\"}}\n",
                      error->code, error->operation, error->resource, error->message,
                      system_message);
    } else {
        (void)fprintf(stderr, "minicontainer: %s: %s", error->operation, error->message);
        if (error->resource[0] != '\0') {
            (void)fprintf(stderr, " [%s]", error->resource);
        }
        if (system_message[0] != '\0') {
            (void)fprintf(stderr, ": %s", system_message);
        }
        (void)fputc('\n', stderr);
    }
}
