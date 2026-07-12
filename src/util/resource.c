#include "minicontainer/resource.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int mc_parse_positive_u64(const char *value, uint64_t maximum, uint64_t *result) {
    char *end = NULL;
    unsigned long long parsed;
    if (value == NULL || result == NULL || value[0] == '\0' || value[0] == '-') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0ULL ||
        parsed > maximum) {
        return 0;
    }
    *result = (uint64_t)parsed;
    return 1;
}

int mc_parse_bytes(const char *value, uint64_t *bytes) {
    char *end = NULL;
    unsigned long long number;
    uint64_t multiplier = 1U;
    char suffix[8];
    size_t suffix_length;
    size_t index;

    if (value == NULL || bytes == NULL || value[0] == '\0' || value[0] == '-') {
        return 0;
    }
    errno = 0;
    number = strtoull(value, &end, 10);
    if (errno != 0 || end == value || number == 0ULL) {
        return 0;
    }
    suffix_length = strlen(end);
    if (suffix_length >= sizeof(suffix)) {
        return 0;
    }
    for (index = 0U; index <= suffix_length; ++index) {
        suffix[index] = (char)tolower((unsigned char)end[index]);
    }
    if (strcmp(suffix, "") == 0 || strcmp(suffix, "b") == 0) {
        multiplier = 1U;
    } else if (strcmp(suffix, "k") == 0 || strcmp(suffix, "ki") == 0 ||
               strcmp(suffix, "kib") == 0) {
        multiplier = UINT64_C(1024);
    } else if (strcmp(suffix, "m") == 0 || strcmp(suffix, "mi") == 0 ||
               strcmp(suffix, "mib") == 0) {
        multiplier = UINT64_C(1024) * UINT64_C(1024);
    } else if (strcmp(suffix, "g") == 0 || strcmp(suffix, "gi") == 0 ||
               strcmp(suffix, "gib") == 0) {
        multiplier = UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024);
    } else {
        return 0;
    }
    if ((uint64_t)number > UINT64_MAX / multiplier) {
        return 0;
    }
    *bytes = (uint64_t)number * multiplier;
    return 1;
}

int mc_parse_cpu_quota(const char *value, uint64_t *quota) {
    const char *cursor;
    uint64_t whole = 0U;
    uint64_t fraction = 0U;
    uint64_t scale = 1U;
    uint64_t micros;
    int seen_digit = 0;
    int seen_dot = 0;

    if (value == NULL || quota == NULL || value[0] == '\0') {
        return 0;
    }
    for (cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor == '.') {
            if (seen_dot != 0) {
                return 0;
            }
            seen_dot = 1;
            continue;
        }
        if (!isdigit((unsigned char)*cursor)) {
            return 0;
        }
        seen_digit = 1;
        if (seen_dot == 0) {
            if (whole > 1024U) {
                return 0;
            }
            whole = (whole * 10U) + (uint64_t)(*cursor - '0');
        } else if (scale < UINT64_C(1000000)) {
            fraction = (fraction * 10U) + (uint64_t)(*cursor - '0');
            scale *= 10U;
        } else {
            return 0;
        }
    }
    if (seen_digit == 0 || whole > 1024U) {
        return 0;
    }
    micros = (whole * UINT64_C(1000000)) +
             ((fraction * UINT64_C(1000000)) / scale);
    if (micros == 0U || micros > UINT64_C(1024) * UINT64_C(1000000)) {
        return 0;
    }
    *quota = (micros * UINT64_C(100000)) / UINT64_C(1000000);
    return *quota > 0U;
}
