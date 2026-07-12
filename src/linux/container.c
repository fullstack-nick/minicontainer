#include "minicontainer/runtime.h"

#include "minicontainer/fs.h"
#include "minicontainer/subid.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <linux/sched.h>
#include <net/if.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_clone3
#define SYS_clone3 435
#endif

struct runtime_paths {
    char base[4096];
    char upper[4096];
    char work[4096];
    char merged[4096];
    char state[4096];
};

struct child_context {
    const struct mc_run_config *config;
    const struct runtime_paths *paths;
    int barrier;
    int ready;
};

static volatile sig_atomic_t forward_target = -1;
static const int forwarded_signals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};

static void forward_signal(int signal_number) {
    const pid_t target = (pid_t)forward_target;
    if (target > 0) {
        (void)kill(target, signal_number);
    }
}

static int install_forwarders(struct sigaction previous[4]) {
    struct sigaction action;
    size_t index;
    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = forward_signal;
    (void)sigemptyset(&action.sa_mask);
    for (index = 0U; index < 4U; ++index) {
        if (sigaction(forwarded_signals[index], &action, &previous[index]) != 0) {
            while (index > 0U) {
                --index;
                (void)sigaction(forwarded_signals[index], &previous[index], NULL);
            }
            return -1;
        }
    }
    return 0;
}

static void restore_forwarders(const struct sigaction previous[4]) {
    size_t index;
    forward_target = -1;
    for (index = 0U; index < 4U; ++index) {
        (void)sigaction(forwarded_signals[index], &previous[index], NULL);
    }
}

static int join_path(char *output, size_t size, const char *left, const char *right) {
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    if (left_length + right_length + 2U > size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    (void)memcpy(output, left, left_length);
    output[left_length] = '/';
    (void)memcpy(output + left_length + 1U, right, right_length + 1U);
    return 0;
}

static int prepare_paths(const char *id, uint32_t uid, uint32_t gid,
                         struct runtime_paths *paths, struct mc_error *error) {
    char containers[4096];

    if (join_path(containers, sizeof(containers), mc_state_dir(), "containers") != 0 ||
        join_path(paths->base, sizeof(paths->base), containers, id) != 0 ||
        join_path(paths->upper, sizeof(paths->upper), paths->base, "upper") != 0 ||
        join_path(paths->work, sizeof(paths->work), paths->base, "work") != 0 ||
        join_path(paths->merged, sizeof(paths->merged), paths->base, "merged") != 0 ||
        join_path(paths->state, sizeof(paths->state), paths->base, "state.json") != 0) {
        mc_error_set(error, MC_EXIT_USAGE, errno, "prepare-runtime", id,
                     "runtime path is too long");
        return -1;
    }
    if (mc_mkdir_p(paths->upper, 0755, error) != 0 ||
        mc_mkdir_p(paths->work, 0700, error) != 0 ||
        mc_mkdir_p(paths->merged, 0755, error) != 0) {
        return -1;
    }
    if (chmod(containers, 0711) != 0 || chown(paths->base, (uid_t)uid, (gid_t)gid) != 0 ||
        chown(paths->upper, (uid_t)uid, (gid_t)gid) != 0 ||
        chown(paths->work, (uid_t)uid, (gid_t)gid) != 0 ||
        chown(paths->merged, (uid_t)uid, (gid_t)gid) != 0) {
        mc_error_set(error, MC_EXIT_PERMISSION, errno, "prepare-runtime", paths->base,
                     "cannot assign overlay directories to subordinate root");
        return -1;
    }
    return 0;
}

static int write_text(const char *path, const char *value) {
    int descriptor = open(path, O_WRONLY | O_CLOEXEC);
    size_t written = 0U;
    const size_t length = strlen(value);
    if (descriptor < 0) {
        return -1;
    }
    while (written < length) {
        const ssize_t count = write(descriptor, value + written, length - written);
        if (count < 0) {
            const int saved = errno;
            (void)close(descriptor);
            errno = saved;
            return -1;
        }
        written += (size_t)count;
    }
    return close(descriptor);
}

static int configure_id_maps(pid_t pid, uint32_t uid, uint32_t gid) {
    char path[128];
    char mapping[64];

    (void)snprintf(path, sizeof(path), "/proc/%ld/setgroups", (long)pid);
    if (write_text(path, "deny") != 0 && errno != ENOENT) {
        return -1;
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/uid_map", (long)pid);
    (void)snprintf(mapping, sizeof(mapping), "0 %u 65536\n", uid);
    if (write_text(path, mapping) != 0) {
        return -1;
    }
    (void)snprintf(path, sizeof(path), "/proc/%ld/gid_map", (long)pid);
    (void)snprintf(mapping, sizeof(mapping), "0 %u 65536\n", gid);
    return write_text(path, mapping);
}

static int make_directory(const char *path, mode_t mode) {
    return mkdir(path, mode) == 0 || errno == EEXIST ? 0 : -1;
}

static int make_file(const char *path) {
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    return descriptor < 0 ? -1 : close(descriptor);
}

static int bind_device(const char *source, const char *root, const char *name) {
    char target[4096];
    if (join_path(target, sizeof(target), root, name) != 0 || make_file(target) != 0) {
        return -1;
    }
    return mount(source, target, NULL, MS_BIND, NULL);
}

static int setup_devices(const char *root) {
    char path[4096];
    char pts[4096];
    char ptmx[4096];

    if (join_path(path, sizeof(path), root, "dev") != 0 ||
        mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755,size=4m") != 0 ||
        bind_device("/dev/null", path, "null") != 0 ||
        bind_device("/dev/zero", path, "zero") != 0 ||
        bind_device("/dev/random", path, "random") != 0 ||
        bind_device("/dev/urandom", path, "urandom") != 0 ||
        bind_device("/dev/tty", path, "tty") != 0 ||
        join_path(pts, sizeof(pts), path, "pts") != 0 || make_directory(pts, 0755) != 0 ||
        mount("devpts", pts, "devpts", MS_NOSUID | MS_NOEXEC,
              "newinstance,ptmxmode=0666,mode=0620") != 0 ||
        join_path(ptmx, sizeof(ptmx), path, "ptmx") != 0 || symlink("pts/ptmx", ptmx) != 0) {
        return -1;
    }
    return 0;
}

static int setup_root(const struct child_context *context) {
    char options[12288];
    char old_root[4096];
    char path[4096];
    const int option_length = snprintf(options, sizeof(options),
                                       "lowerdir=%s,upperdir=%s,workdir=%s,index=off,metacopy=off,userxattr",
                                       context->config->rootfs, context->paths->upper,
                                       context->paths->work);

    if (option_length < 0 || (size_t)option_length >= sizeof(options)) {
        errno = ENAMETOOLONG;
        (void)fprintf(stderr, "minicontainer-shim: overlay-options: %s\n", strerror(errno));
        return -1;
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: mount-propagation: %s\n", strerror(errno));
        return -1;
    }
    if (mount("overlay", context->paths->merged, "overlay", MS_NODEV, options) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: overlay-mount: %s\n", strerror(errno));
        return -1;
    }
    if (setup_devices(context->paths->merged) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: device-mounts: %s\n", strerror(errno));
        return -1;
    }
    if (join_path(path, sizeof(path), context->paths->merged, "run") != 0 ||
        mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755,size=8m") != 0) {
        (void)fprintf(stderr, "minicontainer-shim: run-tmpfs: %s\n", strerror(errno));
        return -1;
    }
    if (join_path(path, sizeof(path), context->paths->merged, "tmp") != 0 ||
        mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=32m") != 0) {
        (void)fprintf(stderr, "minicontainer-shim: tmp-tmpfs: %s\n", strerror(errno));
        return -1;
    }
    if (join_path(path, sizeof(path), context->paths->merged, "proc") != 0) {
        return -1;
    }
    if (mount("proc", path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
        if (getenv("MC_TEST_INHERIT_PROC") == NULL ||
            mount("/proc", path, NULL, MS_BIND | MS_REC, NULL) != 0) {
            (void)fprintf(stderr, "minicontainer-shim: proc-mount: %s\n", strerror(errno));
            return -1;
        }
    }
    if (join_path(old_root, sizeof(old_root), context->paths->merged, ".oldroot") != 0 ||
        make_directory(old_root, 0700) != 0 || chdir(context->paths->merged) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: pivot-prepare: %s\n", strerror(errno));
        return -1;
    }
    if (syscall(SYS_pivot_root, ".", ".oldroot") != 0 || chdir("/") != 0) {
        (void)fprintf(stderr, "minicontainer-shim: pivot-root: %s\n", strerror(errno));
        return -1;
    }
    if (umount2("/.oldroot", MNT_DETACH) != 0 || rmdir("/.oldroot") != 0) {
        (void)fprintf(stderr, "minicontainer-shim: old-root-cleanup: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int configure_loopback(void) {
    struct ifreq request;
    int descriptor = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        return -1;
    }
    (void)memset(&request, 0, sizeof(request));
    (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", "lo");
    if (ioctl(descriptor, SIOCGIFFLAGS, &request) != 0) {
        (void)close(descriptor);
        return -1;
    }
    request.ifr_flags = (short)(request.ifr_flags | IFF_UP | IFF_RUNNING);
    if (ioctl(descriptor, SIOCSIFFLAGS, &request) != 0) {
        (void)close(descriptor);
        return -1;
    }
    return close(descriptor);
}

static int install_environment(const struct mc_run_config *config) {
    size_t index;

    if (clearenv() != 0 || setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0 ||
        setenv("HOME", config->user == 0U ? "/root" : "/", 1) != 0 ||
        setenv("HOSTNAME", config->hostname, 1) != 0) {
        return -1;
    }
    for (index = 0U; index < config->environment_count; ++index) {
        const char *equals = strchr(config->environment[index], '=');
        const size_t key_length = (size_t)(equals - config->environment[index]);
        char *key = strndup(config->environment[index], key_length);
        if (key == NULL || setenv(key, equals + 1, 1) != 0) {
            free(key);
            return -1;
        }
        free(key);
    }
    return 0;
}

static int supervise(const struct mc_run_config *config) {
    sigset_t signals;
    sigset_t empty;
    pid_t workload;
    int workload_status = 0;
    int workload_exited = 0;

    (void)sigemptyset(&signals);
    (void)sigaddset(&signals, SIGCHLD);
    (void)sigaddset(&signals, SIGTERM);
    (void)sigaddset(&signals, SIGINT);
    (void)sigaddset(&signals, SIGHUP);
    (void)sigaddset(&signals, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &signals, NULL) != 0) {
        return 125;
    }
    workload = fork();
    if (workload == 0) {
        (void)sigemptyset(&empty);
        (void)sigprocmask(SIG_SETMASK, &empty, NULL);
        if (setsid() < 0) {
            (void)fprintf(stderr, "minicontainer-init: setsid: %s\n", strerror(errno));
            _exit(125);
        }
        if (chdir(config->workdir) != 0) {
            (void)fprintf(stderr, "minicontainer-init: workdir %s: %s\n", config->workdir,
                          strerror(errno));
            _exit(125);
        }
        if (setresgid(config->group, config->group, config->group) != 0 ||
            setresuid(config->user, config->user, config->user) != 0) {
            (void)fprintf(stderr, "minicontainer-init: workload identity: %s\n",
                          strerror(errno));
            _exit(125);
        }
        if (install_environment(config) != 0) {
            (void)fprintf(stderr, "minicontainer-init: environment: %s\n", strerror(errno));
            _exit(125);
        }
        execvp(config->command[0], config->command);
        (void)fprintf(stderr, "minicontainer-init: exec %s: %s\n", config->command[0],
                      strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }
    if (workload < 0) {
        return 125;
    }
    while (workload_exited == 0) {
        siginfo_t information;
        const int received = sigwaitinfo(&signals, &information);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)kill(-workload, SIGKILL);
            return 125;
        }
        if (received == SIGCHLD) {
            pid_t reaped;
            int status;
            while ((reaped = waitpid(-1, &status, WNOHANG)) > 0) {
                if (reaped == workload) {
                    workload_status = status;
                    workload_exited = 1;
                }
            }
        } else {
            (void)kill(-workload, received);
        }
    }
    (void)kill(-1, SIGKILL);
    while (waitpid(-1, NULL, 0) > 0) {
    }
    if (WIFEXITED(workload_status)) {
        return WEXITSTATUS(workload_status);
    }
    return WIFSIGNALED(workload_status) ? 128 + WTERMSIG(workload_status) : 125;
}

static int child_main(const struct child_context *context) {
    char ready;
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: parent-death-signal: %s\n", strerror(errno));
        return 125;
    }
    if (read(context->barrier, &ready, 1U) != 1 || close(context->barrier) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: namespace-barrier: %s\n", strerror(errno));
        return 125;
    }
    if (setresgid(0, 0, 0) != 0 || setresuid(0, 0, 0) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: enter subordinate root: %s\n",
                      strerror(errno));
        return 125;
    }
    if (getenv("MC_DEBUG") != NULL) {
        FILE *status = fopen("/proc/self/status", "r");
        char line[256];
        struct stat path_status;
        (void)fprintf(stderr, "minicontainer-shim: debug uid=%lu gid=%lu\n",
                      (unsigned long)getuid(), (unsigned long)getgid());
        if (status != NULL) {
            while (fgets(line, sizeof(line), status) != NULL) {
                if (strncmp(line, "CapEff:", 7U) == 0 || strncmp(line, "Uid:", 4U) == 0 ||
                    strncmp(line, "Gid:", 4U) == 0) {
                    (void)fputs(line, stderr);
                }
            }
            (void)fclose(status);
        }
        if (stat(context->config->rootfs, &path_status) == 0) {
            (void)fprintf(stderr, "rootfs uid=%lu gid=%lu mode=%lo\n",
                          (unsigned long)path_status.st_uid, (unsigned long)path_status.st_gid,
                          (unsigned long)(path_status.st_mode & 07777U));
        }
        if (stat(context->paths->upper, &path_status) == 0) {
            (void)fprintf(stderr, "upper uid=%lu gid=%lu mode=%lo\n",
                          (unsigned long)path_status.st_uid, (unsigned long)path_status.st_gid,
                          (unsigned long)(path_status.st_mode & 07777U));
        }
        if (stat(context->paths->work, &path_status) == 0) {
            (void)fprintf(stderr, "work uid=%lu gid=%lu mode=%lo\n",
                          (unsigned long)path_status.st_uid, (unsigned long)path_status.st_gid,
                          (unsigned long)(path_status.st_mode & 07777U));
        }
        if (stat(context->paths->merged, &path_status) == 0) {
            (void)fprintf(stderr, "merged uid=%lu gid=%lu mode=%lo\n",
                          (unsigned long)path_status.st_uid, (unsigned long)path_status.st_gid,
                          (unsigned long)(path_status.st_mode & 07777U));
        }
    }
    if (sethostname(context->config->hostname, strlen(context->config->hostname)) != 0) {
        (void)fprintf(stderr, "minicontainer-shim: set-hostname: %s\n", strerror(errno));
        return 125;
    }
    if (configure_loopback() != 0) {
        (void)fprintf(stderr, "minicontainer-shim: loopback: %s\n", strerror(errno));
        return 125;
    }
    if (setup_root(context) != 0) {
        return 125;
    }
    if (context->config->ready_fd >= 0) {
        (void)close(context->config->ready_fd);
    }
    if (context->ready >= 0) {
        if (write(context->ready, "1", 1U) != 1) {
            return 125;
        }
        (void)close(context->ready);
    }
    return supervise(context->config);
}

static int remove_entry(const char *path, const struct stat *metadata, int type,
                        struct FTW *walk) {
    (void)metadata;
    (void)type;
    (void)walk;
    return remove(path);
}

static void cleanup_paths(const struct runtime_paths *paths) {
    (void)chmod(paths->upper, 0700);
    (void)chmod(paths->work, 0700);
    (void)nftw(paths->base, remove_entry, 32, FTW_DEPTH | FTW_PHYS);
}

static void cleanup_ephemeral(const struct runtime_paths *paths) {
    (void)chmod(paths->upper, 0700);
    (void)chmod(paths->work, 0700);
    (void)nftw(paths->upper, remove_entry, 32, FTW_DEPTH | FTW_PHYS);
    (void)nftw(paths->work, remove_entry, 32, FTW_DEPTH | FTW_PHYS);
    (void)nftw(paths->merged, remove_entry, 32, FTW_DEPTH | FTW_PHYS);
}

static int write_state(const struct runtime_paths *paths, const struct mc_run_config *config,
                       const char *state, pid_t init_pid, int exit_code,
                       struct mc_error *error) {
    char document[1024];
    const int length = snprintf(document, sizeof(document),
                                "{\"schema_version\":1,\"id\":\"%s\","
                                "\"status\":\"%s\",\"shim_pid\":%ld,"
                                "\"init_pid\":%ld,\"exit_code\":%d}\n",
                                config->id, state, (long)getpid(), (long)init_pid, exit_code);
    if (length < 0 || (size_t)length >= sizeof(document)) {
        mc_error_set(error, MC_EXIT_INTERNAL, EOVERFLOW, "write-state", config->id,
                     "state document is too large");
        return -1;
    }
    return mc_write_atomic(paths->state, document, (size_t)length, 0640, error);
}

int mc_container_run(const struct mc_run_config *config, struct mc_error *error) {
    uint32_t subordinate_uid;
    uint32_t subordinate_gid;
    struct runtime_paths paths;
    int barrier[2];
    int ready[2];
    struct child_context child_context;
    struct clone_args clone_arguments;
    int pidfd = -1;
    pid_t child;
    int status;
    struct sigaction previous[4];
    int forwarders_installed = 0;
    char ready_byte;
    ssize_t ready_count;
    int exit_code;

    if (geteuid() != 0) {
        mc_error_set(error, MC_EXIT_PERMISSION, EPERM, "run", config->id,
                     "container execution requires root");
        return -1;
    }
    if (setgroups(0U, NULL) != 0) {
        mc_error_set(error, MC_EXIT_PERMISSION, errno, "run", config->id,
                     "cannot clear supplementary groups");
        return -1;
    }
    if (strchr(config->rootfs, ',') != NULL ||
        mc_subid_range(&subordinate_uid, &subordinate_gid, error) != 0) {
        return -1;
    }
    if (prepare_paths(config->id, subordinate_uid, subordinate_gid, &paths, error) != 0 ||
        pipe2(barrier, O_CLOEXEC) != 0 || pipe2(ready, O_CLOEXEC) != 0) {
        if (error->code == 0) {
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "run", config->id,
                         "cannot create synchronization barrier");
        }
        return -1;
    }
    (void)memset(&clone_arguments, 0, sizeof(clone_arguments));
    clone_arguments.flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS |
                            CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_PIDFD;
    clone_arguments.pidfd = (uint64_t)(uintptr_t)&pidfd;
    clone_arguments.exit_signal = SIGCHLD;
    child_context.config = config;
    child_context.paths = &paths;
    child_context.barrier = barrier[0];
    child_context.ready = ready[1];
    child = (pid_t)syscall(SYS_clone3, &clone_arguments, sizeof(clone_arguments));
    if (child == 0) {
        int result;
        (void)close(barrier[1]);
        (void)close(ready[0]);
        result = child_main(&child_context);
        _exit(result);
    }
    (void)close(barrier[0]);
    (void)close(ready[1]);
    if (child < 0) {
        const int saved = errno;
        (void)close(barrier[1]);
        (void)close(ready[0]);
        cleanup_paths(&paths);
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "clone3", config->id,
                     "cannot create container namespaces");
        return -1;
    }
    if (configure_id_maps(child, subordinate_uid, subordinate_gid) != 0 ||
        write(barrier[1], "1", 1U) != 1) {
        const int saved = errno;
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        (void)close(barrier[1]);
        (void)close(ready[0]);
        if (config->ready_fd >= 0) {
            (void)close(config->ready_fd);
        }
        if (pidfd >= 0) {
            (void)close(pidfd);
        }
        cleanup_paths(&paths);
        mc_error_set(error, MC_EXIT_RUNTIME, saved, "uid-gid-map", config->id,
                     "cannot configure subordinate identity mapping");
        return -1;
    }
    (void)close(barrier[1]);
    do {
        ready_count = read(ready[0], &ready_byte, 1U);
    } while (ready_count < 0 && errno == EINTR);
    (void)close(ready[0]);
    if (ready_count == 1 && ready_byte == '1') {
        if (config->detach != 0 &&
            write_state(&paths, config, "running", child, 0, error) != 0) {
            (void)kill(child, SIGKILL);
        }
        if (config->ready_fd >= 0) {
            (void)write(config->ready_fd, "1", 1U);
            (void)close(config->ready_fd);
        }
    } else if (config->ready_fd >= 0) {
        (void)close(config->ready_fd);
    }
    forward_target = (sig_atomic_t)child;
    if (install_forwarders(previous) != 0) {
        (void)kill(child, SIGKILL);
    } else {
        forwarders_installed = 1;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "wait-container", config->id,
                     "cannot wait for container init");
        status = -1;
        break;
    }
    if (forwarders_installed != 0) {
        restore_forwarders(previous);
    } else {
        forward_target = -1;
    }
    if (pidfd >= 0) {
        (void)close(pidfd);
    }
    if (status >= 0 && WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (status >= 0 && WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = MC_EXIT_RUNTIME;
    }
    if (config->detach != 0) {
        cleanup_ephemeral(&paths);
        (void)chown(paths.base, 0, 0);
        (void)chmod(paths.base, 0750);
        (void)write_state(&paths, config, ready_count == 1 ? "stopped" : "failed", child,
                          exit_code, error);
    } else {
        cleanup_paths(&paths);
    }
    if (status < 0) {
        return -1;
    }
    return exit_code;
}
