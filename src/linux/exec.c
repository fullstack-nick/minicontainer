#include "minicontainer/exec.h"

#include "minicontainer/state.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct namespace_set {
    int mount;
    int uts;
    int ipc;
    int network;
    int cgroup;
    int pid;
    int user;
    int root;
};

static int open_namespace(pid_t pid, const char *name) {
    char path[PATH_MAX];
    const int length = snprintf(path, sizeof(path), "/proc/%ld/ns/%s", (long)pid, name);
    if (length < 0 || (size_t)length >= sizeof(path)) { errno = ENAMETOOLONG; return -1; }
    return open(path, O_RDONLY | O_CLOEXEC);
}

static void close_namespaces(struct namespace_set *set) {
    int *descriptors = (int *)set;
    size_t index;
    for (index = 0U; index < sizeof(*set) / sizeof(int); ++index)
        if (descriptors[index] >= 0) (void)close(descriptors[index]);
}

static int install_environment(const struct mc_run_config *config) {
    size_t index;
    if (clearenv() != 0 ||
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0 ||
        setenv("HOME", config->user == 0U ? "/root" : "/", 1) != 0 ||
        setenv("HOSTNAME", config->hostname, 1) != 0) return -1;
    for (index = 0U; index < config->environment_count; ++index) {
        const char *equals = strchr(config->environment[index], '=');
        char *key;
        if (equals == NULL) { errno = EINVAL; return -1; }
        key = strndup(config->environment[index], (size_t)(equals - config->environment[index]));
        if (key == NULL || setenv(key, equals + 1, 1) != 0) { free(key); return -1; }
        free(key);
    }
    return 0;
}

static int move_to_cgroup(const char *cgroup) {
    char path[PATH_MAX], value[32];
    int descriptor;
    const int length = snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup);
    const int value_length = snprintf(value, sizeof(value), "%ld", (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(path) || value_length < 0 ||
        (size_t)value_length >= sizeof(value)) { errno = ENAMETOOLONG; return -1; }
    descriptor = open(path, O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    if (write(descriptor, value, (size_t)value_length) != value_length) {
        const int saved = errno; (void)close(descriptor); errno = saved; return -1;
    }
    return close(descriptor);
}

static int exec_worker(struct namespace_set *set, const char *cgroup,
                       const struct mc_run_config *config, char **command) {
    pid_t child;
    int status;
    if (move_to_cgroup(cgroup) != 0) {
        (void)fprintf(stderr, "minicontainer-exec: cgroup: %s\n", strerror(errno)); return 125;
    }
#define ENTER(fd, kind, label) do { if (setns((fd), (kind)) != 0) { \
    (void)fprintf(stderr, "minicontainer-exec: setns %s: %s\n", (label), strerror(errno)); \
    return 125; } } while (0)
    ENTER(set->cgroup, CLONE_NEWCGROUP, "cgroup");
    ENTER(set->ipc, CLONE_NEWIPC, "ipc");
    ENTER(set->uts, CLONE_NEWUTS, "uts");
    ENTER(set->network, CLONE_NEWNET, "net");
    ENTER(set->mount, CLONE_NEWNS, "mnt");
    ENTER(set->pid, CLONE_NEWPID, "pid");
    ENTER(set->user, CLONE_NEWUSER, "user");
#undef ENTER
    child = fork();
    if (child == 0) {
        if (fchdir(set->root) != 0 || chroot(".") != 0 || chdir(config->workdir) != 0) {
            (void)fprintf(stderr, "minicontainer-exec: enter root: %s\n", strerror(errno)); _exit(125);
        }
        if (setresgid(config->group, config->group, config->group) != 0 ||
            setresuid(config->user, config->user, config->user) != 0) {
            (void)fprintf(stderr, "minicontainer-exec: identity: %s\n", strerror(errno)); _exit(125);
        }
        if (install_environment(config) != 0) {
            (void)fprintf(stderr, "minicontainer-exec: environment: %s\n", strerror(errno)); _exit(125);
        }
        execvp(command[0], command);
        _exit(errno == ENOENT ? 127 : 126);
    }
    if (child < 0) return 125;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 125;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 125;
}

int mc_exec_container(const char *reference, char **command, struct mc_error *error) {
    char id[33], cgroup[PATH_MAX], image[PATH_MAX], root_path[PATH_MAX];
    pid_t init_pid, worker;
    unsigned long long init_start;
    struct namespace_set set;
    struct mc_run_config config = {0};
    int status;
    (void)memset(&set, 0xff, sizeof(set));
    if (geteuid() != 0 || command == NULL || command[0] == NULL) {
        mc_error_set(error, MC_EXIT_PERMISSION, EPERM, "exec-container", reference,
                     "container exec requires root and a command"); return -1;
    }
    if (mc_state_runtime(reference, id, &init_pid, &init_start, cgroup, error) != 0 ||
        mc_state_load_config(id, &config, image, error) != 0) return -1;
    set.mount = open_namespace(init_pid, "mnt"); set.uts = open_namespace(init_pid, "uts");
    set.ipc = open_namespace(init_pid, "ipc"); set.network = open_namespace(init_pid, "net");
    set.cgroup = open_namespace(init_pid, "cgroup"); set.pid = open_namespace(init_pid, "pid");
    set.user = open_namespace(init_pid, "user");
    (void)snprintf(root_path, sizeof(root_path), "/proc/%ld/root", (long)init_pid);
    set.root = open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (set.mount < 0 || set.uts < 0 || set.ipc < 0 || set.network < 0 || set.cgroup < 0 ||
        set.pid < 0 || set.user < 0 || set.root < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "exec-container", id,
                     "cannot open container namespace identity");
        close_namespaces(&set); mc_state_free_config(&config); return -1;
    }
    worker = fork();
    if (worker == 0) _exit(exec_worker(&set, cgroup, &config, command));
    close_namespaces(&set); mc_state_free_config(&config);
    if (worker < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "exec-container", id,
                     "cannot fork namespace worker"); return -1;
    }
    while (waitpid(worker, &status, 0) < 0) if (errno != EINTR) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : MC_EXIT_RUNTIME;
}
