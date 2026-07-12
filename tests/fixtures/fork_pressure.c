#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    long requested = argc == 2 ? strtol(argv[1], NULL, 10) : 64L;
    long failures = 0L;
    long index;
    for (index = 0L; index < requested; ++index) {
        const pid_t child = fork();
        if (child == 0) {
            for (;;) pause();
        }
        if (child < 0 && errno == EAGAIN) ++failures;
    }
    (void)printf("fork_failures=%ld\n", failures);
    (void)fflush(stdout);
    for (;;) pause();
}
