#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int listener(int type, unsigned short port) {
    struct sockaddr_in address;
    int descriptor = socket(AF_INET, type, 0);
    int enabled = 1;
    if (descriptor < 0) return -1;
    (void)setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    (void)memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        (type == SOCK_STREAM && listen(descriptor, 8) != 0)) {
        (void)close(descriptor); return -1;
    }
    return descriptor;
}

int main(void) {
    int tcp = listener(SOCK_STREAM, 8080U);
    int udp = listener(SOCK_DGRAM, 9090U);
    struct pollfd descriptors[2];
    if (tcp < 0 || udp < 0) return 1;
    descriptors[0].fd = tcp; descriptors[0].events = POLLIN;
    descriptors[1].fd = udp; descriptors[1].events = POLLIN;
    (void)puts("NETWORK_READY"); (void)fflush(stdout);
    for (;;) {
        if (poll(descriptors, 2U, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            static const char response[] =
                "HTTP/1.1 200 OK\r\nContent-Length: 19\r\nConnection: close\r\n\r\nMINICONTAINER_HTTP\n";
            int connection = accept(tcp, NULL, NULL);
            char request[1024];
            if (connection >= 0) {
                (void)recv(connection, request, sizeof(request), 0);
                (void)send(connection, response, sizeof(response) - 1U, MSG_NOSIGNAL);
                (void)close(connection);
            }
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            char packet[2048]; struct sockaddr_in peer; socklen_t peer_length = sizeof(peer);
            const ssize_t count = recvfrom(udp, packet, sizeof(packet), 0,
                                           (struct sockaddr *)&peer, &peer_length);
            if (count > 0) (void)sendto(udp, packet, (size_t)count, 0,
                                        (const struct sockaddr *)&peer, peer_length);
        }
    }
}
