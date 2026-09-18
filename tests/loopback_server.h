#ifndef LOOPBACK_SERVER_H
#define LOOPBACK_SERVER_H

#include <stddef.h>

/*
 * Opens a TCP listening socket on 127.0.0.1 with an OS-assigned port,
 * and writes that port (as a decimal string) into port_buf. Returns
 * the listening file descriptor, or -1 on failure. Used so
 * socket_transport tests can connect to a real local server without
 * any network access or a hardcoded port.
 */
int loopback_listen(char *port_buf, size_t port_buf_size);

#endif /* LOOPBACK_SERVER_H */
