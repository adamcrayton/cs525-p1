#include "mock_transport.h"

#include <stdlib.h>
#include <string.h>

static ssize_t mock_read(void *ctx, char *buf, size_t len) {
    mock_transport_t *mt = (mock_transport_t *)ctx;
    size_t remaining = mt->script_len - mt->script_pos;
    size_t want;

    if (remaining == 0) {
        return 0; /* EOF */
    }

    want = remaining;
    if (mt->chunk_size > 0 && mt->chunk_size < want) {
        want = mt->chunk_size;
    }
    if (want > len) {
        want = len;
    }

    memcpy(buf, mt->script + mt->script_pos, want);
    mt->script_pos += want;
    return (ssize_t)want;
}

static ssize_t mock_write(void *ctx, const char *buf, size_t len) {
    mock_transport_t *mt = (mock_transport_t *)ctx;

    if (mt->sent_len + len + 1 > mt->sent_cap) {
        size_t new_cap = mt->sent_cap == 0 ? 256 : mt->sent_cap * 2;
        while (new_cap < mt->sent_len + len + 1) {
            new_cap *= 2;
        }
        char *new_data = realloc(mt->sent, new_cap);
        if (new_data == NULL) {
            return -1;
        }
        mt->sent = new_data;
        mt->sent_cap = new_cap;
    }
    memcpy(mt->sent + mt->sent_len, buf, len);
    mt->sent_len += len;
    mt->sent[mt->sent_len] = '\0';
    return (ssize_t)len;
}

void mock_transport_init(mock_transport_t *mt, const char *script, size_t chunk_size) {
    mt->transport.read = mock_read;
    mt->transport.write = mock_write;
    mt->transport.ctx = mt;
    mt->script = script;
    mt->script_len = strlen(script);
    mt->script_pos = 0;
    mt->chunk_size = chunk_size;
    mt->sent = NULL;
    mt->sent_len = 0;
    mt->sent_cap = 0;
}

void mock_transport_free(mock_transport_t *mt) {
    free(mt->sent);
    mt->sent = NULL;
}
