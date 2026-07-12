#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <linux/capability.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int denied(const char *name, long result) {
    if (result != -1 || errno != EPERM) {
        (void)fprintf(stderr, "%s unexpectedly returned %ld (%s)\n", name, result,
                      strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2] = {{0}};
    int status = 0;
    pid_t child;
    char cwd[16];
    int descriptor;
    if (prctl(PR_GET_NO_NEW_PRIVS, 0L, 0L, 0L, 0L) != 1 ||
        syscall(SYS_capget, &header, data) != 0) return 2;
    for (descriptor = 3; descriptor < 64; ++descriptor) {
        int flags;
        errno = 0;
        flags = fcntl(descriptor, F_GETFD);
        if (flags >= 0 || errno != EBADF) {
            (void)fprintf(stderr, "unexpected-fd=%d flags=%d errno=%d\n", descriptor, flags, errno);
            return 6;
        }
    }
    (void)printf("nnp=1 effective=%08x%08x permitted=%08x%08x\n",
                 data[1].effective, data[0].effective,
                 data[1].permitted, data[0].permitted);
    if (argc == 2 && strcmp(argv[1], "deny-getcwd") == 0) {
        errno = 0;
        if (denied("getcwd", syscall(SYS_getcwd, cwd, sizeof(cwd))) != 0) return 5;
        (void)puts("PASS custom seccomp deny");
        return 0;
    }
    errno = 0; status |= denied("unshare", unshare(CLONE_NEWNS));
    errno = 0; status |= denied("mount", mount("none", "/", "tmpfs", 0U, NULL));
    errno = 0; status |= denied("ptrace", ptrace(PTRACE_TRACEME, 0, NULL, NULL));
    errno = 0; status |= denied("bpf", syscall(SYS_bpf, BPF_MAP_CREATE, NULL, 0U));
    errno = 0; status |= denied("raw-socket", socket(AF_INET, SOCK_RAW, 1));
    if (socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0) < 0) return 3;
    child = fork();
    if (child == 0) _exit(0);
    if (child < 0 || waitpid(child, NULL, 0) != child) return 4;
    if (status == 0) (void)puts("PASS denied dangerous syscalls; allowed workload and FD closure work");
    return status;
}
