#include "minicontainer/security.h"

#include <errno.h>
#include <grp.h>
#include <json-c/json.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <seccomp.h>
#include <stddef.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static char capability_names[][20] = {
    "CHOWN", "DAC_OVERRIDE", "DAC_READ_SEARCH", "FOWNER", "FSETID", "KILL",
    "SETGID", "SETUID", "SETPCAP", "LINUX_IMMUTABLE", "NET_BIND_SERVICE",
    "NET_BROADCAST", "NET_ADMIN", "NET_RAW", "IPC_LOCK", "IPC_OWNER", "SYS_MODULE",
    "SYS_RAWIO", "SYS_CHROOT", "SYS_PTRACE", "SYS_PACCT", "SYS_ADMIN", "SYS_BOOT",
    "SYS_NICE", "SYS_RESOURCE", "SYS_TIME", "SYS_TTY_CONFIG", "MKNOD", "LEASE",
    "AUDIT_WRITE", "AUDIT_CONTROL", "SETFCAP", "MAC_OVERRIDE", "MAC_ADMIN", "SYSLOG",
    "WAKE_ALARM", "BLOCK_SUSPEND", "AUDIT_READ", "PERFMON", "BPF", "CHECKPOINT_RESTORE"
};

int mc_capability_parse(const char *name, unsigned int *number) {
    const char *candidate = name;
    size_t index;
    if (candidate == NULL || number == NULL) return 0;
    if (strncmp(candidate, "CAP_", 4U) == 0) candidate += 4;
    for (index = 0U; index < sizeof(capability_names) / sizeof(capability_names[0]); ++index) {
        if (strcmp(candidate, capability_names[index]) == 0) {
            *number = (unsigned int)index;
            return 1;
        }
    }
    return 0;
}

char *mc_capability_name(unsigned int number) {
    return number < sizeof(capability_names) / sizeof(capability_names[0])
               ? capability_names[number]
               : NULL;
}

static int allow_syscall(scmp_filter_ctx filter, const char *name) {
    const int number = seccomp_syscall_resolve_name(name);
    return number == __NR_SCMP_ERROR ? 0 : seccomp_rule_add(filter, SCMP_ACT_ALLOW, number, 0U);
}

int mc_seccomp_name_valid(const char *name) {
    return name != NULL && name[0] != '\0' &&
           seccomp_syscall_resolve_name(name) != __NR_SCMP_ERROR;
}

int mc_seccomp_profile_load(const char *path, char ***denies, size_t *count,
                            struct mc_error *error) {
    struct stat metadata;
    json_object *root = NULL, *version, *array;
    char **loaded = NULL;
    size_t index, total;
    if (path == NULL || path[0] != '/' || stat(path, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode) || metadata.st_uid != geteuid() ||
        (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 || metadata.st_size > 65536) goto invalid;
    root = json_object_from_file(path);
    if (root == NULL || !json_object_object_get_ex(root, "version", &version) ||
        json_object_get_int(version) != 1 || !json_object_object_get_ex(root, "deny", &array) ||
        !json_object_is_type(array, json_type_array)) goto invalid;
    total = json_object_array_length(array);
    if (total > 128U || (loaded = calloc(total + 1U, sizeof(*loaded))) == NULL) goto invalid;
    for (index = 0U; index < total; ++index) {
        const char *name = json_object_get_string(json_object_array_get_idx(array, index));
        size_t previous;
        if (!mc_seccomp_name_valid(name)) goto invalid_loaded;
        for (previous = 0U; previous < index; ++previous)
            if (strcmp(name, loaded[previous]) == 0) goto invalid_loaded;
        loaded[index] = strdup(name);
        if (loaded[index] == NULL) goto invalid_loaded;
    }
    json_object_put(root); *denies = loaded; *count = total; return 1;
invalid_loaded:
    for (index = 0U; index < total; ++index) free(loaded[index]);
    free(loaded);
invalid:
    json_object_put(root);
    mc_error_set(error, MC_EXIT_USAGE, EINVAL, "seccomp-profile", path,
                 "profile must be root-owned non-writable JSON version 1 with valid unique deny names");
    return 0;
}

static int denied_by_profile(const char *name, char *const *denies, size_t deny_count) {
    size_t index;
    for (index = 0U; index < deny_count; ++index)
        if (strcmp(name, denies[index]) == 0) return 1;
    return 0;
}

static int install_seccomp(char *const *denies, size_t deny_count) {
    static const char *const allowed[] = {
        "accept", "accept4", "access", "arch_prctl", "bind", "brk", "capget", "capset",
        "chdir", "chroot", "clock_getres", "clock_gettime", "clock_nanosleep", "close",
        "close_range", "connect", "copy_file_range", "dup", "dup2", "dup3", "epoll_create",
        "epoll_create1", "epoll_ctl", "epoll_pwait", "epoll_pwait2", "epoll_wait", "eventfd",
        "eventfd2", "execve", "execveat", "exit", "exit_group", "faccessat", "faccessat2",
        "fadvise64", "fallocate", "fchdir", "fchmod", "fchmodat", "fchown", "fchownat",
        "fcntl", "fdatasync", "flock", "fork", "fstat", "fstatfs", "fsync", "ftruncate",
        "futex", "futex_waitv", "getcwd", "getdents64", "getegid", "geteuid", "getgid",
        "getgroups", "getitimer", "getpeername", "getpgid", "getpgrp", "getpid", "getppid",
        "getpriority", "getrandom", "getresgid", "getresuid", "getrlimit", "getrusage",
        "getsid", "getsockname", "getsockopt", "gettid", "gettimeofday", "getuid", "ioctl",
        "kill", "lgetxattr", "link", "linkat", "listen", "lseek", "lstat", "madvise",
        "memfd_create", "mmap", "mprotect", "mremap", "msync", "munmap", "nanosleep",
        "newfstatat", "open", "openat", "openat2", "pause", "pipe", "pipe2", "poll",
        "ppoll", "prctl", "pread64", "preadv", "preadv2", "prlimit64", "pselect6",
        "pwrite64", "pwritev", "pwritev2", "read", "readlink", "readlinkat", "readv",
        "recvfrom", "recvmmsg", "recvmsg", "rename", "renameat", "renameat2", "restart_syscall",
        "rseq", "rt_sigaction", "rt_sigpending", "rt_sigprocmask", "rt_sigqueueinfo",
        "rt_sigreturn", "rt_sigsuspend", "rt_sigtimedwait", "sched_getaffinity", "sched_yield",
        "sendfile", "sendmmsg", "sendmsg", "sendto", "set_robust_list", "set_tid_address",
        "setgid", "setgroups", "setitimer", "setpgid", "setpriority", "setregid", "setresgid",
        "setresuid", "setreuid", "setrlimit", "setsid", "setsockopt", "setuid", "shutdown",
        "sigaltstack", "signalfd", "signalfd4", "stat", "statfs", "statx", "symlink",
        "symlinkat", "sync", "syncfs", "sysinfo", "tgkill", "time", "timer_create",
        "timer_delete", "timer_gettime", "timer_settime", "timerfd_create", "timerfd_gettime",
        "timerfd_settime", "times", "tkill", "truncate", "umask", "uname", "unlink",
        "unlinkat", "utime", "utimensat", "utimes", "vfork", "wait4", "waitid", "write",
        "writev"
    };
    const uint64_t forbidden_clone = (uint64_t)(CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNET |
                                                CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWTIME |
                                                CLONE_NEWUSER | CLONE_NEWUTS);
    scmp_filter_ctx filter = seccomp_init(SCMP_ACT_ERRNO(EPERM));
    size_t index;
    int result = -1;
    if (filter == NULL) return -1;
    for (index = 0U; index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
        if (!denied_by_profile(allowed[index], denies, deny_count) &&
            allow_syscall(filter, allowed[index]) != 0) goto done;
    }
    if (!denied_by_profile("clone3", denies, deny_count) &&
        seccomp_rule_add(filter, SCMP_ACT_ERRNO(ENOSYS), SCMP_SYS(clone3), 0U) != 0) goto done;
    if (!denied_by_profile("clone", denies, deny_count) &&
        seccomp_rule_add(filter, SCMP_ACT_ALLOW, SCMP_SYS(clone), 1U,
                         SCMP_A0(SCMP_CMP_MASKED_EQ, forbidden_clone, 0U)) != 0) goto done;
    if (!denied_by_profile("socket", denies, deny_count)) {
        static const int families[] = {1, 2, 10};
        for (index = 0U; index < sizeof(families) / sizeof(families[0]); ++index) {
            if (seccomp_rule_add(filter, SCMP_ACT_ALLOW, SCMP_SYS(socket), 2U,
                                 SCMP_A0(SCMP_CMP_EQ, (scmp_datum_t)families[index], 0U),
                                 SCMP_A1(SCMP_CMP_MASKED_EQ, 3U, 1U)) != 0 ||
                seccomp_rule_add(filter, SCMP_ACT_ALLOW, SCMP_SYS(socket), 2U,
                                 SCMP_A0(SCMP_CMP_EQ, (scmp_datum_t)families[index], 0U),
                                 SCMP_A1(SCMP_CMP_MASKED_EQ, 3U, 2U)) != 0) goto done;
        }
        if (seccomp_rule_add(filter, SCMP_ACT_ALLOW, SCMP_SYS(socket), 3U,
                             SCMP_A0(SCMP_CMP_EQ, 16U, 0U),
                             SCMP_A1(SCMP_CMP_MASKED_EQ, 15U, 3U),
                             SCMP_A2(SCMP_CMP_EQ, 0U, 0U)) != 0) goto done;
    }
    if (seccomp_attr_set(filter, SCMP_FLTATR_CTL_NNP, 0U) != 0 || seccomp_load(filter) != 0)
        goto done;
    result = 0;
done:
    seccomp_release(filter);
    return result;
}

int mc_security_prepare(uint64_t capability_mask) {
    unsigned int capability;
    if (setgroups(0U, NULL) != 0) {
        if (errno != EPERM || getgroups(0, NULL) != 0) return -1;
    }
    for (capability = 0U; capability <= 40U; ++capability) {
        if ((capability_mask & (UINT64_C(1) << capability)) == 0U &&
            prctl(PR_CAPBSET_DROP, capability, 0L, 0L, 0L) != 0 && errno != EINVAL) return -1;
    }
    return prctl(PR_SET_KEEPCAPS, 1L, 0L, 0L, 0L);
}

int mc_security_apply(uint64_t capability_mask, char *const *denies, size_t deny_count) {
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct data[2] = {{0}};
    const uint64_t preparation_mask = capability_mask | (UINT64_C(1) << 8U);
    data[0].effective = (uint32_t)preparation_mask;
    data[0].permitted = (uint32_t)preparation_mask;
    data[1].effective = (uint32_t)(preparation_mask >> 32U);
    data[1].permitted = (uint32_t)(preparation_mask >> 32U);
    if (syscall(SYS_capset, &header, data) != 0 ||
        prctl(PR_SET_SECUREBITS, 1U | 2U | 4U | 8U, 0L, 0L, 0L) != 0) return -1;
    data[0].effective = (uint32_t)capability_mask;
    data[0].permitted = (uint32_t)capability_mask;
    data[0].inheritable = (uint32_t)capability_mask;
    data[1].effective = (uint32_t)(capability_mask >> 32U);
    data[1].permitted = (uint32_t)(capability_mask >> 32U);
    data[1].inheritable = (uint32_t)(capability_mask >> 32U);
    if (syscall(SYS_capset, &header, data) != 0 ||
        prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0L, 0L, 0L) != 0) return -1;
    {
        unsigned int capability;
        for (capability = 0U; capability < 64U; ++capability) {
            if ((capability_mask & (UINT64_C(1) << capability)) != 0U &&
                prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, capability, 0L, 0L) != 0) return -1;
        }
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) != 0) return -1;
    return install_seccomp(denies, deny_count);
}
