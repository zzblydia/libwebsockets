#include <stdio.h>

#ifdef _WIN32

#include <winsock2.h> // struct sockaddr
#include <ws2tcpip.h> // socklen_t

#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include "ws-utils.h"

// GetAddrInfo在windows下重名
int ws_GetAddrInfo(int fd, int direction, char *ip, unsigned short *port) {
    if (ip == NULL || port == NULL) {
        return -1;
    }

    if (fd < 0) {
        printf("lws_get_socket_fd failed\n");
        return -1;
    }

    struct sockaddr addr;
    socklen_t addr_len = sizeof(addr);
    if (direction == 0) {
        getsockname(fd, (struct sockaddr *) &addr, &addr_len);
    } else {
        getpeername(fd, (struct sockaddr *) &addr, &addr_len);
    }

    if (addr.sa_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *) &addr;
        *port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ip, INET6_ADDRSTRLEN);
    } else { // AF_INET6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;
        *port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ip, INET6_ADDRSTRLEN);
    }
    return 0;
}

void log2file(int level, const char *line)
{
    printf("log2file: level=%d, line=%s", level, line);
}