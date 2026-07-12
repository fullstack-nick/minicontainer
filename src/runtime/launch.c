#include "minicontainer/runtime.h"
#include "minicontainer/fs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t shim_target = -1;
static const int shim_signals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};

static void forward_to_shim(int signal_number) {
    const pid_t target = (pid_t)shim_target;
    if (target > 0) {
        (void)kill(target, signal_number);
    }
}

static int install_signal_forwarding(struct sigaction previous[4]) {
    struct sigaction action;
    size_t index;
    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = forward_to_shim;
    (void)sigemptyset(&action.sa_mask);
    for (index = 0U; index < 4U; ++index) {
        if (sigaction(shim_signals[index], &action, &previous[index]) != 0) {
            while (index > 0U) {
                --index;
                (void)sigaction(shim_signals[index], &previous[index], NULL);
            }
            return -1;
        }
    }
    return 0;
}

static void restore_signal_forwarding(const struct sigaction previous[4]) {
    size_t index;
    shim_target = -1;
    for (index = 0U; index < 4U; ++index) {
        (void)sigaction(shim_signals[index], &previous[index], NULL);
    }
}

static int shim_path(char output[PATH_MAX], struct mc_error *error) {
    const char *override = getenv("MC_SHIM_PATH");
    ssize_t length;
    char *slash;

    if (override != NULL && override[0] != '\0') {
        if (strlen(override) >= PATH_MAX) {
            mc_error_set(error, MC_EXIT_USAGE, ENAMETOOLONG, "launch-shim", override,
                         "shim path is too long");
            return -1;
        }
        (void)snprintf(output, PATH_MAX, "%s", override);
        return 0;
    }
    if (access("/usr/libexec/minicontainer/minicontainer-shim", X_OK) == 0) {
        (void)snprintf(output, PATH_MAX, "%s",
                       "/usr/libexec/minicontainer/minicontainer-shim");
        return 0;
    }
    length = readlink("/proc/self/exe", output, PATH_MAX - 1U);
    if (length < 0) {
        mc_error_set(error, MC_EXIT_INTERNAL, errno, "launch-shim", "/proc/self/exe",
                     "cannot locate the shim");
        return -1;
    }
    output[(size_t)length] = '\0';
    slash = strrchr(output, '/');
    if (slash == NULL) {
        mc_error_set(error, MC_EXIT_INTERNAL, 0, "launch-shim", output,
                     "executable path has no directory");
        return -1;
    }
    (void)snprintf(slash + 1, (size_t)(output + PATH_MAX - slash - 1), "%s",
                   "minicontainer-shim");
    return 0;
}

int mc_launch_shim(const struct mc_run_config *config, struct mc_error *error) {
    static char id_flag[] = "--id";
    static char rootfs_flag[] = "--rootfs";
    static char hostname_flag[] = "--hostname";
    static char separator[] = "--";
    static char workdir_flag[] = "--workdir";
    static char user_flag[] = "--user";
    static char environment_flag[] = "--env";
    static char detach_flag[] = "--detach";
    static char ready_flag[] = "--ready-fd";
    static char memory_flag[] = "--memory";
    static char swap_flag[] = "--memory-swap";
    static char cpu_flag[] = "--cpu-quota";
    static char pids_flag[] = "--pids-limit";
    char user_value[32];
    char ready_value[32];
    char memory_value[32];
    char swap_value[32];
    char cpu_value[32];
    char pids_value[32];
    char log_directory[PATH_MAX];
    char log_path[PATH_MAX];
    char path[PATH_MAX];
    size_t command_count = 0U;
    size_t index;
    char **arguments;
    pid_t child;
    int status;
    int readiness[2] = {-1, -1};
    int log_descriptor = -1;
    int null_descriptor = -1;
    size_t position;
    struct sigaction previous[4];
    int forwarding_installed = 0;

    while (config->command[command_count] != NULL) {
        ++command_count;
    }
    arguments = calloc(command_count + 25U + (config->environment_count * 2U),
                       sizeof(*arguments));
    if (arguments == NULL || shim_path(path, error) != 0) {
        free(arguments);
        if (error->code == 0) {
            mc_error_set(error, MC_EXIT_INTERNAL, errno, "launch-shim", "argv",
                         "cannot allocate shim arguments");
        }
        return -1;
    }
    arguments[0] = path;
    arguments[1] = id_flag;
    arguments[2] = config->id;
    arguments[3] = rootfs_flag;
    arguments[4] = config->rootfs;
    arguments[5] = hostname_flag;
    arguments[6] = config->hostname;
    arguments[7] = workdir_flag;
    arguments[8] = config->workdir;
    arguments[9] = user_flag;
    (void)snprintf(user_value, sizeof(user_value), "%u:%u", (unsigned int)config->user,
                   (unsigned int)config->group);
    arguments[10] = user_value;
    for (index = 0U; index < config->environment_count; ++index) {
        arguments[11U + (index * 2U)] = environment_flag;
        arguments[12U + (index * 2U)] = config->environment[index];
    }
    position = 11U + (config->environment_count * 2U);
    (void)snprintf(memory_value, sizeof(memory_value), "%llu",
                   (unsigned long long)config->memory_max);
    (void)snprintf(swap_value, sizeof(swap_value), "%llu",
                   (unsigned long long)config->swap_max);
    (void)snprintf(cpu_value, sizeof(cpu_value), "%llu",
                   (unsigned long long)config->cpu_quota);
    (void)snprintf(pids_value, sizeof(pids_value), "%llu",
                   (unsigned long long)config->pids_max);
    arguments[position++] = memory_flag;
    arguments[position++] = memory_value;
    arguments[position++] = swap_flag;
    arguments[position++] = swap_value;
    arguments[position++] = cpu_flag;
    arguments[position++] = cpu_value;
    arguments[position++] = pids_flag;
    arguments[position++] = pids_value;
    if (config->detach != 0) {
        if (pipe2(readiness, O_CLOEXEC) != 0 ||
            fcntl(readiness[1], F_SETFD, 0) != 0 ||
            snprintf(ready_value, sizeof(ready_value), "%d", readiness[1]) < 0) {
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "launch-shim", config->id,
                         "cannot create readiness channel");
            free(arguments);
            return -1;
        }
        if (snprintf(log_directory, sizeof(log_directory), "%s/%s", mc_log_dir(), config->id) <
                0 ||
            snprintf(log_path, sizeof(log_path), "%s/container.log", log_directory) < 0 ||
            mc_mkdir_p(log_directory, 0750, error) != 0) {
            (void)close(readiness[0]);
            (void)close(readiness[1]);
            free(arguments);
            return -1;
        }
        log_descriptor = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
        null_descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (log_descriptor < 0 || null_descriptor < 0) {
            mc_error_set(error, MC_EXIT_RUNTIME, errno, "launch-shim", log_path,
                         "cannot open detached standard streams");
            if (log_descriptor >= 0) {
                (void)close(log_descriptor);
            }
            if (null_descriptor >= 0) {
                (void)close(null_descriptor);
            }
            (void)close(readiness[0]);
            (void)close(readiness[1]);
            free(arguments);
            return -1;
        }
        arguments[position++] = detach_flag;
        arguments[position++] = ready_flag;
        arguments[position++] = ready_value;
    }
    arguments[position++] = separator;
    for (index = 0U; index < command_count; ++index) {
        arguments[position + index] = config->command[index];
    }
    child = fork();
    if (child == 0) {
        if (config->detach != 0) {
            (void)close(readiness[0]);
            if (setsid() < 0 || dup2(null_descriptor, STDIN_FILENO) < 0 ||
                dup2(log_descriptor, STDOUT_FILENO) < 0 ||
                dup2(log_descriptor, STDERR_FILENO) < 0) {
                _exit(125);
            }
            (void)close(null_descriptor);
            (void)close(log_descriptor);
        }
        execv(path, arguments);
        _exit(126);
    }
    free(arguments);
    if (log_descriptor >= 0) {
        (void)close(log_descriptor);
    }
    if (null_descriptor >= 0) {
        (void)close(null_descriptor);
    }
    if (child < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "launch-shim", path,
                     "cannot execute shim");
        return -1;
    }
    if (config->detach != 0) {
        char ready;
        ssize_t count;
        (void)close(readiness[1]);
        do {
            count = read(readiness[0], &ready, 1U);
        } while (count < 0 && errno == EINTR);
        (void)close(readiness[0]);
        if (count == 1 && ready == '1') {
            return 0;
        }
        (void)waitpid(child, &status, 0);
        mc_error_set(error, MC_EXIT_RUNTIME, 0, "launch-shim", config->id,
                     "detached container failed before reaching running state");
        return -1;
    }
    shim_target = (sig_atomic_t)child;
    if (install_signal_forwarding(previous) == 0) {
        forwarding_installed = 1;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (forwarding_installed != 0) {
            restore_signal_forwarding(previous);
        }
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "launch-shim", path,
                     "cannot wait for shim");
        return -1;
    }
    if (forwarding_installed != 0) {
        restore_signal_forwarding(previous);
    } else {
        shim_target = -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return MC_EXIT_RUNTIME;
}
