#include "minicontainer/stats.h"
#include "minicontainer/fs.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mc_stats {
    uint64_t memory_current, memory_max, swap_current, swap_max;
    uint64_t cpu_usage, cpu_throttled, cpu_throttled_usec;
    uint64_t pids_current, pids_max;
    uint64_t memory_oom, memory_oom_kill, io_read, io_write;
};

static int read_text(const char *path, char *output, size_t size) {
    FILE *stream = fopen(path, "r");
    size_t count;
    if (stream == NULL) return -1;
    count = fread(output, 1U, size - 1U, stream);
    if (ferror(stream) != 0 || (count == size - 1U && !feof(stream))) {
        const int saved = ferror(stream) != 0 ? EIO : EOVERFLOW;
        (void)fclose(stream); errno = saved; return -1;
    }
    output[count] = '\0'; (void)fclose(stream); return 0;
}

static int state_cgroup(const char *id, char *output, size_t size) {
    char path[PATH_MAX], document[4096];
    const char marker[] = "\"cgroup_path\":\"";
    char *start, *end;
    int length = snprintf(path, sizeof(path), "%s/containers/%s/state.json", mc_state_dir(), id);
    if (length < 0 || (size_t)length >= sizeof(path) || read_text(path, document, sizeof(document)) != 0) return -1;
    start = strstr(document, marker);
    if (start == NULL) { errno = EINVAL; return -1; }
    start += sizeof(marker) - 1U; end = strchr(start, '"');
    if (end == NULL || end == start || (size_t)(end - start) >= size) { errno = EINVAL; return -1; }
    (void)memcpy(output, start, (size_t)(end - start)); output[end - start] = '\0'; return 0;
}

static int read_u64(const char *directory, const char *name, uint64_t *value) {
    char path[PATH_MAX], text[128], *end = NULL;
    unsigned long long parsed;
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (length < 0 || (size_t)length >= sizeof(path) || read_text(path, text, sizeof(text)) != 0) return -1;
    if (strncmp(text, "max", 3U) == 0) { *value = UINT64_MAX; return 0; }
    errno = 0; parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || (*end != '\n' && *end != '\0')) { errno = EINVAL; return -1; }
    *value = (uint64_t)parsed; return 0;
}

static int cpu_field(const char *directory, const char *field, uint64_t *value) {
    char path[PATH_MAX], text[2048], *line;
    int length = snprintf(path, sizeof(path), "%s/cpu.stat", directory);
    if (length < 0 || (size_t)length >= sizeof(path) || read_text(path, text, sizeof(text)) != 0) return -1;
    line = strtok(text, "\n");
    while (line != NULL) {
        char key[64]; unsigned long long parsed;
        if (sscanf(line, "%63s %llu", key, &parsed) == 2 && strcmp(key, field) == 0) {
            *value = (uint64_t)parsed; return 0;
        }
        line = strtok(NULL, "\n");
    }
    errno = ENOENT; return -1;
}

static int named_field(const char *directory, const char *file, const char *field,
                       uint64_t *value) {
    char path[PATH_MAX], text[4096], *token;
    int length = snprintf(path, sizeof(path), "%s/%s", directory, file);
    if (length < 0 || (size_t)length >= sizeof(path) || read_text(path, text, sizeof(text)) != 0) return -1;
    *value = 0U;
    token = strtok(text, " \n");
    while (token != NULL) {
        const size_t field_length = strlen(field);
        if (strncmp(token, field, field_length) == 0 &&
            (token[field_length] == '\0' || token[field_length] == '=')) {
            const char *number = token + field_length + (token[field_length] == '=' ? 1 : 0);
            if (*number == '\0') number = strtok(NULL, " \n");
            if (number != NULL) *value += (uint64_t)strtoull(number, NULL, 10);
        }
        token = strtok(NULL, " \n");
    }
    return 0;
}

static void print_limit(uint64_t value) {
    if (value == UINT64_MAX) (void)printf("max"); else (void)printf("%" PRIu64, value);
}

int mc_stats_print(const char *id, int json, struct mc_error *error) {
    char cgroup[PATH_MAX]; struct mc_stats s;
    if (state_cgroup(id, cgroup, sizeof(cgroup)) != 0 ||
        read_u64(cgroup, "memory.current", &s.memory_current) != 0 || read_u64(cgroup, "memory.max", &s.memory_max) != 0 ||
        read_u64(cgroup, "memory.swap.current", &s.swap_current) != 0 || read_u64(cgroup, "memory.swap.max", &s.swap_max) != 0 ||
        cpu_field(cgroup, "usage_usec", &s.cpu_usage) != 0 || cpu_field(cgroup, "nr_throttled", &s.cpu_throttled) != 0 ||
        cpu_field(cgroup, "throttled_usec", &s.cpu_throttled_usec) != 0 || read_u64(cgroup, "pids.current", &s.pids_current) != 0 ||
        read_u64(cgroup, "pids.max", &s.pids_max) != 0 ||
        named_field(cgroup, "memory.events", "oom", &s.memory_oom) != 0 ||
        named_field(cgroup, "memory.events", "oom_kill", &s.memory_oom_kill) != 0 ||
        named_field(cgroup, "io.stat", "rbytes", &s.io_read) != 0 ||
        named_field(cgroup, "io.stat", "wbytes", &s.io_write) != 0) {
        mc_error_set(error, MC_EXIT_NOT_FOUND, errno, "stats", id, "container is not running or its cgroup is unavailable"); return -1;
    }
    if (json != 0) {
        (void)printf("{\"schema_version\":1,\"id\":\"%s\",\"memory\":{\"current\":%" PRIu64 ",\"max\":", id, s.memory_current); print_limit(s.memory_max);
        (void)printf(",\"swap_current\":%" PRIu64 ",\"swap_max\":", s.swap_current); print_limit(s.swap_max);
        (void)printf(",\"oom\":%" PRIu64 ",\"oom_kill\":%" PRIu64 "},\"cpu\":{\"usage_usec\":%" PRIu64 ",\"nr_throttled\":%" PRIu64 ",\"throttled_usec\":%" PRIu64 "},\"pids\":{\"current\":%" PRIu64 ",\"max\":", s.memory_oom, s.memory_oom_kill, s.cpu_usage, s.cpu_throttled, s.cpu_throttled_usec, s.pids_current); print_limit(s.pids_max); (void)printf("},\"io\":{\"read_bytes\":%" PRIu64 ",\"write_bytes\":%" PRIu64 "}}\n", s.io_read, s.io_write);
    } else {
        (void)printf("ID\tMEMORY\tSWAP\tCPU USEC\tTHROTTLED\tPIDS\n%s\t%" PRIu64 "/", id, s.memory_current); print_limit(s.memory_max);
        (void)printf("\t%" PRIu64 "/", s.swap_current); print_limit(s.swap_max);
        (void)printf("\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "/", s.cpu_usage, s.cpu_throttled, s.pids_current); print_limit(s.pids_max); (void)printf("\n");
    }
    return 0;
}
