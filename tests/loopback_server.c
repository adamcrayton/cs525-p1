#include "loopback_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int loopback_listen(char *port_buf, size_t port_buf_size) {
    int fd;
    int one = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    /* GCOVR_EXCL_START - socket() failure isn't reproducible on demand */
    if (fd == -1) {
        return -1;
    }
    /* GCOVR_EXCL_STOP */

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* let the OS assign a free port */

    /* GCOVR_EXCL_START - not exercised: bind() to an OS-assigned port doesn't fail */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    /* GCOVR_EXCL_STOP */
    /* GCOVR_EXCL_START - not exercised: listen() on a fresh socket doesn't fail */
    if (listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    /* GCOVR_EXCL_STOP */
    /* GCOVR_EXCL_START - not exercised: getsockname() on a bound socket doesn't fail */
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        close(fd);
        return -1;
    }
    /* GCOVR_EXCL_STOP */

    snprintf(port_buf, port_buf_size, "%d", (int)ntohs(addr.sin_port));
    return fd;
}
