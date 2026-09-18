#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>
#include <sys/types.h> /* ssize_t */

#include "protocol.h"

/*
 * Transport callbacks. read()/write() behave like recv()/send() on a
 * blocking stream socket:
 *   - read()  returns the number of bytes read (>0), 0 on orderly EOF,
 *             or -1 on error.
 *   - write() returns the number of bytes written (>=0, possibly less
 *             than requested), or -1 on error.
 * ctx is passed through unchanged so a transport can carry its own
 * state (a socket fd, or a scripted buffer in tests) without globals.
 */
typedef ssize_t (*transport_read_fn)(void *ctx, char *buf, size_t len);
typedef ssize_t (*transport_write_fn)(void *ctx, const char *buf, size_t len);

typedef struct {
    transport_read_fn read;
    transport_write_fn write;
    void *ctx;
} transport_t;

/*
 * Buffered line reader over a transport. Holds bytes the transport has
 * already delivered but that haven't been consumed as a full line yet,
 * and only calls transport->read again when it doesn't already hold a
 * complete line - so a reply split across several reads, or several
 * replies delivered in one read, are both handled correctly.
 */
typedef struct {
    transport_t *transport;
    char buf[SMTP_LINE_MAX * 2];
    size_t start; /* index of first unconsumed byte */
    size_t end;   /* index one past the last buffered byte */
} line_reader_t;

void line_reader_init(line_reader_t *lr, transport_t *transport);

/*
 * Reads one CRLF-terminated line into out (size out_size), stripped of
 * the CRLF and NUL-terminated. Returns the line length on success, 0
 * on a clean EOF with nothing pending, -1 on error (including "line
 * too long for out_size" or "connection closed mid-line").
 */
ssize_t session_read_line(line_reader_t *lr, char *out, size_t out_size);

/*
 * Reads a full, possibly multi-line, SMTP reply and checks that every
 * line shares the same 3-digit code. If last_line is non-NULL, copies
 * the final line into it (size last_line_size). Returns the reply
 * code on success, -1 on error (malformed line, short read, or
 * inconsistent codes across lines).
 */
int session_read_reply(line_reader_t *lr, char *last_line, size_t last_line_size);

/* Writes all of `data` (looping over short writes). Returns 0, or -1 on error. */
int session_write_all(transport_t *transport, const char *data, size_t length);

/*
 * Sends `command` (a bare command with no CRLF - this appends it) and
 * reads the reply, checking that its code is `expected_code`. On
 * mismatch or error, prints a diagnostic to stderr naming the command
 * and what the server actually said. Returns 0 on success, -1 otherwise.
 */
int session_send_command_expect(transport_t *transport, line_reader_t *lr, const char *command,
                                 int expected_code);

typedef struct {
    const char *from;
    const char *to;
    const char *subject;
    const char *body;
    const char *helo_host;
} smtp_message_t;

/*
 * Runs one complete SMTP session - HELO, MAIL FROM, RCPT TO, DATA (and
 * its payload), QUIT - over `transport`. All reading and writing goes
 * through transport->read/write; there is no socket call anywhere in
 * this function, so it can be driven by a real socket transport or by
 * a scripted in-memory transport in tests. Returns 0 on success, -1 on
 * the first failed step (a diagnostic is printed to stderr).
 */
int session_run(transport_t *transport, const smtp_message_t *msg);

#endif /* SESSION_H */
