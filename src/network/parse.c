#include "minicontainer/network.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int mc_parse_publish(const char *value, struct mc_publish *published) {
    char address_text[16], protocol_text[4], trailing;
    unsigned int host_port, container_port;
    struct in_addr address;
    if (value == NULL || published == NULL ||
        sscanf(value, "%15[0-9.]:%u:%u/%3[a-z]%c", address_text, &host_port,
               &container_port, protocol_text, &trailing) != 4 ||
        inet_pton(AF_INET, address_text, &address) != 1 || host_port == 0U ||
        host_port > 65535U || container_port == 0U || container_port > 65535U)
        return 0;
    if (strcmp(protocol_text, "tcp") == 0) published->protocol = IPPROTO_TCP;
    else if (strcmp(protocol_text, "udp") == 0) published->protocol = IPPROTO_UDP;
    else return 0;
    published->host_ipv4 = ntohl(address.s_addr);
    published->host_port = (uint16_t)host_port;
    published->container_port = (uint16_t)container_port;
    return 1;
}
