#define _POSIX_C_SOURCE 200809L

#include "socket_transport.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static ssize_t socket_transport_read(void *ctx, char *buf, size_t len) {
    socket_transport_t *st = (socket_transport_t *)ctx;
    ssize_t n = recv(st->fd, buf, len, 0);

    if (n < 0) {
        fprintf(stderr, "myapp: error reading from socket: %s\n", strerror(errno));
    }
    return n;
}

static ssize_t socket_transport_write(void *ctx, const char *buf, size_t len) {
    socket_transport_t *st = (socket_transport_t *)ctx;
    ssize_t n = send(st->fd, buf, len, 0);

    if (n < 0) {
        fprintf(stderr, "myapp: error sending data: %s\n", strerror(errno));
    }
    return n;
}

int socket_transport_connect(socket_transport_t *st, const char *host, const char *port) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    int sock = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */

    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "myapp: could not resolve %s:%s: %s\n", host, port, gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            continue;
        }
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; /* success */
        }
        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock == -1) {
        fprintf(stderr, "myapp: could not connect to %s:%s: %s\n", host, port, strerror(errno));
        return -1;
    }

    st->fd = sock;
    st->transport.read = socket_transport_read;
    st->transport.write = socket_transport_write;
    st->transport.ctx = st;
    return 0;
}

void socket_transport_close(socket_transport_t *st) {
    if (st->fd != -1) {
        close(st->fd);
        st->fd = -1;
    }
}
