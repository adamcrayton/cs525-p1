#ifndef SOCKET_TRANSPORT_H
#define SOCKET_TRANSPORT_H

#include "session.h"

/*
 * A transport backed by a real TCP socket. `transport` must stay the
 * first field so a `socket_transport_t *` can be passed anywhere a
 * `transport_t *` is expected (e.g. &st.transport, or a cast of &st).
 */
typedef struct {
    transport_t transport;
    int fd;
} socket_transport_t;

/*
 * Resolves host:port and connects a TCP socket, wiring st->transport's
 * read/write callbacks to recv()/send() on it. Returns 0 on success,
 * -1 on failure (a diagnostic is printed to stderr). st->fd is only
 * valid after a successful call.
 */
int socket_transport_connect(socket_transport_t *st, const char *host, const char *port);

/* Closes the underlying socket, if open. Safe to call more than once. */
void socket_transport_close(socket_transport_t *st);

#endif /* SOCKET_TRANSPORT_H */
