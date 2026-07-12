#include "minicontainer/network.h"
#include "minicontainer/nft.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct link_request {
    struct nlmsghdr header;
    struct ifinfomsg link;
    unsigned char attributes[2048];
};

struct address_request {
    struct nlmsghdr header;
    struct ifaddrmsg address;
    unsigned char attributes[256];
};

struct route_request {
    struct nlmsghdr header;
    struct rtmsg route;
    unsigned char attributes[256];
};

static uint32_t sequence = 1U;

static int add_attribute(void *buffer, struct nlmsghdr *header, size_t capacity, unsigned short type,
                         const void *data, size_t length) {
    const size_t offset = NLMSG_ALIGN(header->nlmsg_len);
    const size_t attribute_length = RTA_LENGTH(length);
    struct rtattr *attribute;
    if (offset + RTA_ALIGN(attribute_length) > capacity) { errno = EOVERFLOW; return -1; }
    attribute = (struct rtattr *)((unsigned char *)buffer + offset);
    attribute->rta_type = type;
    attribute->rta_len = (unsigned short)attribute_length;
    if (length > 0U) (void)memcpy(RTA_DATA(attribute), data, length);
    header->nlmsg_len = (uint32_t)(offset + RTA_ALIGN(attribute_length));
    return 0;
}

static struct rtattr *begin_nested(void *buffer, struct nlmsghdr *header, size_t capacity,
                                   unsigned short type) {
    const size_t offset = NLMSG_ALIGN(header->nlmsg_len);
    struct rtattr *nested;
    if (offset + RTA_ALIGN(RTA_LENGTH(0U)) > capacity) { errno = EOVERFLOW; return NULL; }
    nested = (struct rtattr *)((unsigned char *)buffer + offset);
    nested->rta_type = (unsigned short)(type | NLA_F_NESTED);
    nested->rta_len = (unsigned short)RTA_LENGTH(0U);
    header->nlmsg_len = (uint32_t)(offset + RTA_ALIGN(RTA_LENGTH(0U)));
    return nested;
}

static void end_nested(struct nlmsghdr *header, struct rtattr *nested) {
    nested->rta_len = (unsigned short)((unsigned char *)header + header->nlmsg_len -
                                       (unsigned char *)nested);
}

static int netlink_exchange(struct nlmsghdr *request) {
    struct sockaddr_nl address;
    unsigned char response[8192];
    int descriptor;
    ssize_t count;
    request->nlmsg_seq = sequence++;
    descriptor = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (descriptor < 0) return -1;
    (void)memset(&address, 0, sizeof(address)); address.nl_family = AF_NETLINK;
    if (sendto(descriptor, request, request->nlmsg_len, 0,
               (const struct sockaddr *)&address, sizeof(address)) < 0) {
        const int saved = errno; (void)close(descriptor); errno = saved; return -1;
    }
    for (;;) {
        struct nlmsghdr *message;
        int remaining;
        count = recv(descriptor, response, sizeof(response), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { const int saved = errno; (void)close(descriptor); errno = saved; return -1; }
        remaining = (int)count;
        message = (struct nlmsghdr *)response;
        while (remaining >= (int)sizeof(*message) && message->nlmsg_len >= sizeof(*message) &&
               message->nlmsg_len <= (uint32_t)remaining) {
            const uint32_t aligned = NLMSG_ALIGN(message->nlmsg_len);
            if (message->nlmsg_seq == request->nlmsg_seq &&
                message->nlmsg_type == NLMSG_ERROR) {
                const struct nlmsgerr *failure = (const struct nlmsgerr *)NLMSG_DATA(message);
                (void)close(descriptor);
                if (failure->error == 0) return 0;
                errno = -failure->error; return -1;
            }
            if (aligned > (uint32_t)remaining) break;
            remaining -= (int)aligned;
            message = (struct nlmsghdr *)((unsigned char *)message + aligned);
        }
    }
}

static int create_bridge(void) {
    struct link_request request;
    struct rtattr *link_info;
    const char name[] = "mcbr0";
    const char kind[] = "bridge";
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.link));
    request.header.nlmsg_type = RTM_NEWLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
    request.link.ifi_family = AF_UNSPEC;
    if (add_attribute(&request, &request.header, sizeof(request), IFLA_IFNAME, name, sizeof(name)) != 0 ||
        (link_info = begin_nested(&request, &request.header, sizeof(request), IFLA_LINKINFO)) == NULL ||
        add_attribute(&request, &request.header, sizeof(request), IFLA_INFO_KIND, kind, sizeof(kind)) != 0)
        return -1;
    end_nested(&request.header, link_info);
    if (netlink_exchange(&request.header) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int configure_link(int index, int up, int master, pid_t netns_pid,
                          const char *new_name) {
    struct link_request request;
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.link));
    request.header.nlmsg_type = RTM_NEWLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.link.ifi_family = AF_UNSPEC; request.link.ifi_index = index;
    if (up != 0) { request.link.ifi_flags = IFF_UP; request.link.ifi_change = IFF_UP; }
    if ((master > 0 && add_attribute(&request, &request.header, sizeof(request), IFLA_MASTER,
                                     &master, sizeof(master)) != 0) ||
        (netns_pid > 0 && add_attribute(&request, &request.header, sizeof(request), IFLA_NET_NS_PID,
                                        &netns_pid, sizeof(netns_pid)) != 0) ||
        (new_name != NULL && add_attribute(&request, &request.header, sizeof(request), IFLA_IFNAME,
                                           new_name, strlen(new_name) + 1U) != 0)) return -1;
    return netlink_exchange(&request.header);
}

static int add_ipv4_address(int index, unsigned int ipv4_host, unsigned char prefix) {
    struct address_request request;
    const uint32_t address = htonl(ipv4_host);
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.address));
    request.header.nlmsg_type = RTM_NEWADDR;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
    request.address.ifa_family = AF_INET; request.address.ifa_prefixlen = prefix;
    request.address.ifa_scope = RT_SCOPE_UNIVERSE; request.address.ifa_index = (unsigned int)index;
    if (add_attribute(&request, &request.header, sizeof(request), IFA_LOCAL, &address, sizeof(address)) != 0 ||
        add_attribute(&request, &request.header, sizeof(request), IFA_ADDRESS, &address, sizeof(address)) != 0)
        return -1;
    if (netlink_exchange(&request.header) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int create_veth(const char *host, const char *peer) {
    struct link_request request;
    struct rtattr *link_info, *info_data, *peer_data;
    struct ifinfomsg peer_link;
    const char kind[] = "veth";
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.link));
    request.header.nlmsg_type = RTM_NEWLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
    request.link.ifi_family = AF_UNSPEC;
    if (add_attribute(&request, &request.header, sizeof(request), IFLA_IFNAME, host, strlen(host) + 1U) != 0 ||
        (link_info = begin_nested(&request, &request.header, sizeof(request), IFLA_LINKINFO)) == NULL ||
        add_attribute(&request, &request.header, sizeof(request), IFLA_INFO_KIND, kind, sizeof(kind)) != 0 ||
        (info_data = begin_nested(&request, &request.header, sizeof(request), IFLA_INFO_DATA)) == NULL ||
        (peer_data = begin_nested(&request, &request.header, sizeof(request), VETH_INFO_PEER)) == NULL) return -1;
    (void)memset(&peer_link, 0, sizeof(peer_link));
    if ((size_t)request.header.nlmsg_len + NLMSG_ALIGN(sizeof(peer_link)) > sizeof(request)) {
        errno = EOVERFLOW; return -1;
    }
    (void)memcpy((unsigned char *)&request + request.header.nlmsg_len, &peer_link, sizeof(peer_link));
    request.header.nlmsg_len += (uint32_t)NLMSG_ALIGN(sizeof(peer_link));
    if (add_attribute(&request, &request.header, sizeof(request), IFLA_IFNAME, peer, strlen(peer) + 1U) != 0)
        return -1;
    end_nested(&request.header, peer_data); end_nested(&request.header, info_data);
    end_nested(&request.header, link_info);
    return netlink_exchange(&request.header);
}

static int add_default_route(int interface_index) {
    struct route_request request;
    const uint32_t gateway = htonl(UINT32_C(0x0a2c0001));
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.route));
    request.header.nlmsg_type = RTM_NEWROUTE;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
    request.route.rtm_family = AF_INET; request.route.rtm_table = RT_TABLE_MAIN;
    request.route.rtm_protocol = RTPROT_BOOT; request.route.rtm_scope = RT_SCOPE_UNIVERSE;
    request.route.rtm_type = RTN_UNICAST;
    if (add_attribute(&request, &request.header, sizeof(request), RTA_GATEWAY, &gateway, sizeof(gateway)) != 0 ||
        add_attribute(&request, &request.header, sizeof(request), RTA_OIF, &interface_index,
                      sizeof(interface_index)) != 0) return -1;
    if (netlink_exchange(&request.header) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int configure_peer_namespace(pid_t namespace_pid, const char *peer,
                                    unsigned int ipv4_host) {
    char namespace_path[64];
    int namespace_descriptor;
    int interface_index;
    (void)snprintf(namespace_path, sizeof(namespace_path), "/proc/%ld/ns/net", (long)namespace_pid);
    namespace_descriptor = open(namespace_path, O_RDONLY | O_CLOEXEC);
    if (namespace_descriptor < 0 || setns(namespace_descriptor, CLONE_NEWNET) != 0) {
        if (namespace_descriptor >= 0) (void)close(namespace_descriptor);
        return -1;
    }
    (void)close(namespace_descriptor);
    interface_index = (int)if_nametoindex(peer);
    if (interface_index <= 0 || configure_link(interface_index, 0, 0, 0, "eth0") != 0)
        return -1;
    interface_index = (int)if_nametoindex("eth0");
    if (interface_index <= 0 || configure_link(interface_index, 1, 0, 0, NULL) != 0 ||
        add_ipv4_address(interface_index, ipv4_host, 24U) != 0 ||
        add_default_route(interface_index) != 0) return -1;
    interface_index = (int)if_nametoindex("lo");
    return interface_index > 0 ? configure_link(interface_index, 1, 0, 0, NULL) : -1;
}

static int delete_link(const char *name) {
    struct link_request request;
    const int index = (int)if_nametoindex(name);
    if (index <= 0) return 0;
    (void)memset(&request, 0, sizeof(request));
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.link));
    request.header.nlmsg_type = RTM_DELLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.link.ifi_family = AF_UNSPEC; request.link.ifi_index = index;
    return netlink_exchange(&request.header);
}

int mc_network_setup(const char *id, pid_t namespace_pid, unsigned int ipv4_host,
                     const struct mc_publish *publishes, size_t publish_count,
                     struct mc_network *network, struct mc_error *error) {
    int bridge_index, host_index, peer_index;
    pid_t helper;
    int status;
    (void)memset(network, 0, sizeof(*network)); network->ipv4_host = ipv4_host;
    (void)snprintf(network->id, sizeof(network->id), "%s", id);
    (void)snprintf(network->host_interface, sizeof(network->host_interface), "mch%.8s", id);
    (void)snprintf(network->peer_interface, sizeof(network->peer_interface), "mcc%.8s", id);
    if (create_bridge() != 0 || mc_nft_ensure_base(error) != 0 ||
        (bridge_index = (int)if_nametoindex("mcbr0")) <= 0 ||
        configure_link(bridge_index, 1, 0, 0, NULL) != 0 ||
        add_ipv4_address(bridge_index, UINT32_C(0x0a2c0001), 24U) != 0 ||
        create_veth(network->host_interface, network->peer_interface) != 0 ||
        (host_index = (int)if_nametoindex(network->host_interface)) <= 0 ||
        (peer_index = (int)if_nametoindex(network->peer_interface)) <= 0 ||
        configure_link(host_index, 1, bridge_index, 0, NULL) != 0 ||
        configure_link(peer_index, 0, 0, namespace_pid, NULL) != 0) goto failure;
    helper = fork();
    if (helper == 0) _exit(configure_peer_namespace(namespace_pid, network->peer_interface,
                                                    ipv4_host) == 0 ? 0 : 1);
    if (helper < 0 || waitpid(helper, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) goto failure;
    if (mc_nft_add_container(id, ipv4_host, publishes, publish_count, error) != 0) goto failure;
    network->active = 1;
    return 0;
failure:
    {
        const int saved = errno;
        mc_nft_remove_container(id);
        (void)delete_link(network->host_interface);
        if (error->code == 0)
            mc_error_set(error, MC_EXIT_RUNTIME, saved, "network-setup", id,
                         "rtnetlink bridge/veth configuration failed");
        return -1;
    }
}

void mc_network_destroy(struct mc_network *network) {
    if (network == NULL || network->active == 0) return;
    (void)delete_link(network->host_interface);
    mc_nft_remove_container(network->id);
    network->active = 0;
}

void mc_network_cleanup_owned(const char *id) {
    char host_interface[16];
    if (id == NULL || strlen(id) != 32U) return;
    (void)snprintf(host_interface, sizeof(host_interface), "mch%.8s", id);
    (void)delete_link(host_interface);
    mc_nft_remove_container(id);
}
