#ifndef MOCK_TRANSPORT_H
#define MOCK_TRANSPORT_H

#include "../src/session.h"

/*
 * An in-memory transport for tests. `script` is everything the fake
 * server would send. It's handed back to session_read_line in
 * caller-controlled chunks (chunk_size bytes per read, or the whole
 * remainder at once if chunk_size is 0), so a test can exercise
 * "reply split across several reads" as easily as "reply delivered
 * whole". Everything the client writes is appended to `sent`, so a
 * test can assert on the exact commands the session sent.
 *
 * force_read_error and fail_write_after_calls simulate a broken
 * transport (recv()/send() returning -1) without needing an actual
 * socket, so session.c's transport-error branches can be driven
 * deterministically. Both default to "never fail" (0) after
 * mock_transport_init.
 */
typedef struct {
    transport_t transport;
    const char *script;
    size_t script_len;
    size_t script_pos;
    size_t chunk_size;
    char *sent;
    size_t sent_len;
    size_t sent_cap;
    int force_read_error;          /* nonzero: every read() call returns -1 */
    size_t fail_write_after_calls; /* 0 = never fail; N>0 = the Nth write() call onward returns -1 */
    size_t write_call_count;       /* internal - number of write() calls made so far */
} mock_transport_t;

void mock_transport_init(mock_transport_t *mt, const char *script, size_t chunk_size);
void mock_transport_free(mock_transport_t *mt);

#endif /* MOCK_TRANSPORT_H */
