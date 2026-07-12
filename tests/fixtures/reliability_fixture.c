#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int memory_pressure(size_t bytes) {
    volatile unsigned char *memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    size_t offset;
    if (memory == MAP_FAILED) return 2;
    for (offset = 0U; offset < bytes; offset += 4096U) memory[offset] = 1U;
    for (;;) pause();
}

static int log_pressure(size_t bytes) {
    char buffer[16384];
    size_t remaining = bytes;
    (void)memset(buffer, 'L', sizeof(buffer));
    while (remaining > 0U) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t offset = 0U;
        while (offset < chunk) {
            const ssize_t written = write(STDOUT_FILENO, buffer + offset, chunk - offset);
            if (written < 0) { if (errno == EINTR) continue; return 3; }
            offset += (size_t)written;
        }
        remaining -= chunk;
    }
    return write(STDOUT_FILENO, "\nMINICONTAINER_LOG_END\n", 23U) == 23 ? 0 : 4;
}

static int cpu_pressure(void) {
    volatile uint64_t value = 1U;
    for (;;) value = (value * UINT64_C(6364136223846793005)) + 1U;
    return (int)value;
}

int main(int argc, char **argv) {
    unsigned long long amount;
    if (argc != 3) return 64;
    amount = strtoull(argv[2], NULL, 10);
    if (strcmp(argv[1], "memory") == 0) return memory_pressure((size_t)amount);
    if (strcmp(argv[1], "log") == 0) return log_pressure((size_t)amount);
    if (strcmp(argv[1], "cpu") == 0) return cpu_pressure();
    return 64;
}
