#include "minicontainer/nft.h"

#include "minicontainer/fs.h"

#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *nft_path(void) {
    if (access("/usr/sbin/nft", X_OK) == 0) return "/usr/sbin/nft";
    return "/usr/bin/nft";
}

static int execute_nft(char *const arguments[], const char *input, int quiet) {
    int pipe_descriptors[2] = {-1, -1};
    pid_t child;
    int status;
    if (input != NULL && pipe2(pipe_descriptors, O_CLOEXEC) != 0) return -1;
    child = fork();
    if (child == 0) {
        if (input != NULL) {
            (void)close(pipe_descriptors[1]);
            if (dup2(pipe_descriptors[0], STDIN_FILENO) < 0) _exit(126);
            (void)close(pipe_descriptors[0]);
        }
        if (quiet != 0) {
            const int null_descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (null_descriptor < 0 || dup2(null_descriptor, STDOUT_FILENO) < 0 ||
                dup2(null_descriptor, STDERR_FILENO) < 0) _exit(126);
            (void)close(null_descriptor);
        }
        execv(nft_path(), arguments);
        _exit(127);
    }
    if (child < 0) {
        if (input != NULL) { (void)close(pipe_descriptors[0]); (void)close(pipe_descriptors[1]); }
        return -1;
    }
    if (input != NULL) {
        size_t written = 0U;
        const size_t length = strlen(input);
        (void)close(pipe_descriptors[0]);
        while (written < length) {
            const ssize_t count = write(pipe_descriptors[1], input + written, length - written);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) break;
            written += (size_t)count;
        }
        (void)close(pipe_descriptors[1]);
    }
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 125;
}

static int capture_nft(char *const arguments[], char *output, size_t capacity) {
    int descriptors[2];
    pid_t child;
    int status;
    size_t used = 0U;
    if (pipe2(descriptors, O_CLOEXEC) != 0) return -1;
    child = fork();
    if (child == 0) {
        const int null_descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0 || null_descriptor < 0 ||
            dup2(null_descriptor, STDERR_FILENO) < 0) _exit(126);
        (void)close(descriptors[1]); (void)close(null_descriptor);
        execv(nft_path(), arguments); _exit(127);
    }
    (void)close(descriptors[1]);
    if (child < 0) { (void)close(descriptors[0]); return -1; }
    while (used + 1U < capacity) {
        const ssize_t count = read(descriptors[0], output + used, capacity - used - 1U);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += (size_t)count;
    }
    output[used] = '\0'; (void)close(descriptors[0]);
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 125;
}

static int take_nft_lock(void) {
    char path[PATH_MAX];
    int descriptor;
    if (snprintf(path, sizeof(path), "%s/nft.lock", mc_runtime_dir()) < 0) return -1;
    descriptor = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0 || flock(descriptor, LOCK_EX) != 0) {
        if (descriptor >= 0) (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static void release_nft_lock(int descriptor) {
    if (descriptor >= 0) { (void)flock(descriptor, LOCK_UN); (void)close(descriptor); }
}

static int write_sysctl(const char *path) {
    int descriptor = open(path, O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    if (write(descriptor, "1\n", 2U) != 2) {
        const int saved = errno; (void)close(descriptor); errno = saved; return -1;
    }
    return close(descriptor);
}

static int write_forwarding(void) {
    return write_sysctl("/proc/sys/net/ipv4/ip_forward") == 0 &&
                   write_sysctl("/proc/sys/net/ipv4/conf/all/route_localnet") == 0 &&
                   write_sysctl("/proc/sys/net/ipv4/conf/lo/route_localnet") == 0
               ? 0
               : -1;
}

int mc_nft_ensure_base(struct mc_error *error) {
    static char nft[] = "nft";
    static char list[] = "list";
    static char table[] = "table";
    static char family[] = "inet";
    static char name[] = "minicontainer";
    static char file_flag[] = "-f";
    static char standard_input[] = "-";
    char *list_arguments[] = {nft, list, table, family, name, NULL};
    char *file_arguments[] = {nft, file_flag, standard_input, NULL};
    const char rules[] =
        "add table inet minicontainer\n"
        "add chain inet minicontainer prerouting { type nat hook prerouting priority dstnat; policy accept; }\n"
        "add chain inet minicontainer output { type nat hook output priority -100; policy accept; }\n"
        "add chain inet minicontainer forward { type filter hook forward priority filter; policy drop; }\n"
        "add chain inet minicontainer postrouting { type nat hook postrouting priority srcnat; policy accept; }\n"
        "add rule inet minicontainer forward ct state established,related accept comment \"minicontainer-base-return\"\n"
        "add rule inet minicontainer forward iifname \"mcbr0\" accept comment \"minicontainer-base-egress\"\n"
        "add rule inet minicontainer postrouting ip saddr 10.44.0.0/24 oifname != \"mcbr0\" masquerade comment \"minicontainer-base-nat\"\n"
        "add rule inet minicontainer postrouting ip saddr 127.0.0.0/8 ip daddr 10.44.0.0/24 masquerade comment \"minicontainer-base-hairpin\"\n";
    char lock_path[PATH_MAX];
    int lock_descriptor = -1;
    int result;
    if (snprintf(lock_path, sizeof(lock_path), "%s/nft.lock", mc_runtime_dir()) < 0 ||
        mc_mkdir_p(mc_runtime_dir(), 0700, error) != 0 ||
        (lock_descriptor = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600)) < 0 ||
        flock(lock_descriptor, LOCK_EX) != 0) goto failure;
    result = execute_nft(list_arguments, NULL, 1);
    if (result != 0) result = execute_nft(file_arguments, rules, 0);
    if (result == 0 && write_forwarding() != 0) result = -1;
    (void)flock(lock_descriptor, LOCK_UN); (void)close(lock_descriptor);
    if (result == 0) return 0;
failure:
    if (lock_descriptor >= 0) (void)close(lock_descriptor);
    mc_error_set(error, MC_EXIT_RUNTIME, errno, "nftables", "inet minicontainer",
                 "cannot install forwarding and masquerade policy");
    return -1;
}

int mc_nft_add_container(const char *id, unsigned int ipv4_host,
                         const struct mc_publish *publishes, size_t publish_count,
                         struct mc_error *error) {
    static char nft[] = "nft", file_flag[] = "-f", standard_input[] = "-";
    char *arguments[] = {nft, file_flag, standard_input, NULL};
    char container_ip[INET_ADDRSTRLEN];
    struct in_addr address = {.s_addr = htonl(ipv4_host)};
    char *rules;
    size_t capacity = (publish_count * 1024U) + 1U;
    size_t used = 0U, index;
    int lock_descriptor;
    if (publish_count == 0U) return 0;
    if (inet_ntop(AF_INET, &address, container_ip, sizeof(container_ip)) == NULL) return -1;
    rules = calloc(capacity, 1U);
    if (rules == NULL) return -1;
    for (index = 0U; index < publish_count; ++index) {
        char host_ip[INET_ADDRSTRLEN];
        const char *protocol = publishes[index].protocol == IPPROTO_TCP ? "tcp" : "udp";
        struct in_addr host_address = {.s_addr = htonl(publishes[index].host_ipv4)};
        int length;
        if (inet_ntop(AF_INET, &host_address, host_ip, sizeof(host_ip)) == NULL) goto failure;
        length = snprintf(rules + used, capacity - used,
            "add rule inet minicontainer prerouting %s%s%s %s dport %u dnat ip to %s:%u comment \"minicontainer:%s\"\n"
            "add rule inet minicontainer output %s%s%s %s dport %u dnat ip to %s:%u comment \"minicontainer:%s\"\n"
            "add rule inet minicontainer forward ip daddr %s %s dport %u accept comment \"minicontainer:%s\"\n",
            publishes[index].host_ipv4 == 0U ? "" : "ip daddr ",
            publishes[index].host_ipv4 == 0U ? "" : host_ip,
            publishes[index].host_ipv4 == 0U ? "" : " ",
            protocol, (unsigned int)publishes[index].host_port, container_ip,
            (unsigned int)publishes[index].container_port, id,
            publishes[index].host_ipv4 == 0U ? "fib daddr type local " : "ip daddr ",
            publishes[index].host_ipv4 == 0U ? "" : host_ip,
            publishes[index].host_ipv4 == 0U ? "" : " ",
            protocol, (unsigned int)publishes[index].host_port, container_ip,
            (unsigned int)publishes[index].container_port, id,
            container_ip, protocol, (unsigned int)publishes[index].container_port, id);
        if (length < 0 || (size_t)length >= capacity - used) goto failure;
        used += (size_t)length;
    }
    lock_descriptor = take_nft_lock();
    if (lock_descriptor < 0 || execute_nft(arguments, rules, 0) != 0) {
        release_nft_lock(lock_descriptor); goto failure;
    }
    release_nft_lock(lock_descriptor); free(rules); return 0;
failure:
    free(rules);
    mc_error_set(error, MC_EXIT_RUNTIME, errno, "nftables-publish", id,
                 "cannot install published-port rules");
    return -1;
}

void mc_nft_remove_container(const char *id) {
    static char nft[] = "nft", handles[] = "-a", list[] = "list", chain_word[] = "chain";
    static char family[] = "inet", table[] = "minicontainer", file_flag[] = "-f";
    static char standard_input[] = "-";
    static char prerouting[] = "prerouting", output_chain[] = "output", forward[] = "forward";
    char *chains[] = {prerouting, output_chain, forward};
    char marker[80], output[32768], script[8192];
    size_t script_used = 0U, chain_index;
    int lock_descriptor = take_nft_lock();
    if (lock_descriptor < 0 ||
        snprintf(marker, sizeof(marker), "comment \"minicontainer:%s\"", id) < 0) {
        release_nft_lock(lock_descriptor); return;
    }
    for (chain_index = 0U; chain_index < sizeof(chains) / sizeof(chains[0]); ++chain_index) {
        char *arguments[] = {nft, handles, list, chain_word, family, table,
                             chains[chain_index], NULL};
        char *line;
        if (capture_nft(arguments, output, sizeof(output)) != 0) continue;
        line = strtok(output, "\n");
        while (line != NULL) {
            char *owned = strstr(line, marker);
            char *handle = owned == NULL ? NULL : strstr(owned, "# handle ");
            if (handle != NULL) {
                char *end = NULL;
                const unsigned long number = strtoul(handle + strlen("# handle "), &end, 10);
                int length;
                if (end != handle && number > 0UL) {
                    length = snprintf(script + script_used, sizeof(script) - script_used,
                                      "delete rule inet minicontainer %s handle %lu\n",
                                      chains[chain_index], number);
                    if (length > 0 && (size_t)length < sizeof(script) - script_used)
                        script_used += (size_t)length;
                }
            }
            line = strtok(NULL, "\n");
        }
    }
    if (script_used > 0U) {
        char *arguments[] = {nft, file_flag, standard_input, NULL};
        (void)execute_nft(arguments, script, 1);
    }
    release_nft_lock(lock_descriptor);
}
