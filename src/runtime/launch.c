#include "minicontainer/runtime.h"

#include <errno.h>
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
    char user_value[32];
    char path[PATH_MAX];
    size_t command_count = 0U;
    size_t index;
    char **arguments;
    pid_t child;
    int status;
    struct sigaction previous[4];
    int forwarding_installed = 0;

    while (config->command[command_count] != NULL) {
        ++command_count;
    }
    arguments = calloc(command_count + 13U + (config->environment_count * 2U),
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
    arguments[11U + (config->environment_count * 2U)] = separator;
    for (index = 0U; index < command_count; ++index) {
        arguments[index + 12U + (config->environment_count * 2U)] = config->command[index];
    }
    child = fork();
    if (child == 0) {
        execv(path, arguments);
        _exit(126);
    }
    free(arguments);
    if (child < 0) {
        mc_error_set(error, MC_EXIT_RUNTIME, errno, "launch-shim", path,
                     "cannot execute shim");
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
