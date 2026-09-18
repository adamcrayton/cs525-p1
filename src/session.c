#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void line_reader_init(line_reader_t *lr, transport_t *transport) {
    lr->transport = transport;
    lr->start = 0;
    lr->end = 0;
}

ssize_t session_read_line(line_reader_t *lr, char *out, size_t out_size) {
    if (out_size == 0) {
        return -1;
    }

    for (;;) {
        size_t i;
        size_t found = (size_t)-1;

        /* Look for a CRLF pair already sitting in the buffer. Requiring
         * both bytes to be present (rather than treating a lone '\r' as
         * a terminator) is what lets a CRLF that arrives split across
         * two reads still be found correctly on the next call. */
        for (i = lr->start; i + 1 < lr->end; i++) {
            if (lr->buf[i] == '\r' && lr->buf[i + 1] == '\n') {
                found = i;
                break;
            }
        }

        if (found != (size_t)-1) {
            size_t line_len = found - lr->start;

            if (line_len + 1 > out_size) {
                fprintf(stderr, "myapp: line too long\n");
                return -1;
            }
            memcpy(out, lr->buf + lr->start, line_len);
            out[line_len] = '\0';
            lr->start = found + 2; /* skip the CRLF itself */
            return (ssize_t)line_len;
        }

        /* No complete line buffered yet. Compact any leftover bytes to
         * the front, then pull in more from the transport. */
        if (lr->start > 0) {
            memmove(lr->buf, lr->buf + lr->start, lr->end - lr->start);
            lr->end -= lr->start;
            lr->start = 0;
        }

        if (lr->end >= sizeof(lr->buf)) {
            fprintf(stderr, "myapp: line too long\n");
            return -1;
        }

        {
            ssize_t n = lr->transport->read(lr->transport->ctx, lr->buf + lr->end,
                                             sizeof(lr->buf) - lr->end);
            if (n == 0) {
                if (lr->end == 0) {
                    return 0; /* clean EOF, nothing pending */
                }
                fprintf(stderr, "myapp: connection closed mid-line\n");
                return -1;
            }
            if (n < 0) {
                fprintf(stderr, "myapp: error reading from transport\n");
                return -1;
            }
            lr->end += (size_t)n;
        }
    }
}

int session_read_reply(line_reader_t *lr, char *last_line, size_t last_line_size) {
    char line[SMTP_LINE_MAX];
    int code = -1;

    for (;;) {
        ssize_t n = session_read_line(lr, line, sizeof(line));
        int this_code;
        int is_final;

        if (n <= 0) {
            fprintf(stderr, "myapp: server closed connection unexpectedly\n");
            return -1;
        }

        if (protocol_parse_reply_line(line, &this_code, &is_final) != 0) {
            fprintf(stderr, "myapp: invalid server response: %s\n", line);
            return -1;
        }

        if (code == -1) {
            code = this_code;
        } else if (this_code != code) {
            fprintf(stderr, "myapp: inconsistent response codes from server\n");
            return -1;
        }

        if (last_line != NULL && last_line_size > 0) {
            strncpy(last_line, line, last_line_size - 1);
            last_line[last_line_size - 1] = '\0';
        }

        if (is_final) {
            break;
        }
    }

    return code;
}

int session_write_all(transport_t *transport, const char *data, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = transport->write(transport->ctx, data + sent, length - sent);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int session_send_command_expect(transport_t *transport, line_reader_t *lr, const char *command,
                                 int expected_code) {
    char wire[SMTP_LINE_MAX];
    char last_line[SMTP_LINE_MAX];
    int code;

    if (protocol_format_line(command, wire, sizeof(wire)) < 0) {
        fprintf(stderr, "myapp: command too long: %s\n", command);
        return -1;
    }
    if (session_write_all(transport, wire, strlen(wire)) != 0) {
        return -1;
    }

    code = session_read_reply(lr, last_line, sizeof(last_line));
    if (code == -1) {
        return -1;
    }

    if (code != expected_code) {
        fprintf(stderr, "myapp: expected %d after \"%s\" but server said: %s\n", expected_code,
                command, last_line);
        return -1;
    }
    return 0;
}

int session_run(transport_t *transport, const smtp_message_t *msg) {
    line_reader_t lr;
    char helo_cmd[SMTP_LINE_MAX];
    char mail_cmd[SMTP_LINE_MAX];
    char rcpt_cmd[SMTP_LINE_MAX];
    char last_line[SMTP_LINE_MAX];
    char *payload;
    int rc;

    line_reader_init(&lr, transport);

    if (snprintf(helo_cmd, sizeof(helo_cmd), "HELO %s", msg->helo_host) >= (int)sizeof(helo_cmd) ||
        snprintf(mail_cmd, sizeof(mail_cmd), "MAIL FROM:<%s>", msg->from) >=
            (int)sizeof(mail_cmd) ||
        snprintf(rcpt_cmd, sizeof(rcpt_cmd), "RCPT TO:<%s>", msg->to) >= (int)sizeof(rcpt_cmd)) {
        fprintf(stderr, "myapp: command too long\n");
        return -1;
    }

    if (session_send_command_expect(transport, &lr, helo_cmd, 250) != 0) {
        return -1;
    }
    if (session_send_command_expect(transport, &lr, mail_cmd, 250) != 0) {
        return -1;
    }
    if (session_send_command_expect(transport, &lr, rcpt_cmd, 250) != 0) {
        return -1;
    }
    if (session_send_command_expect(transport, &lr, "DATA", 354) != 0) {
        return -1;
    }

    payload = protocol_build_data_payload(msg->from, msg->to, msg->subject, msg->body);
    if (payload == NULL) {
        fprintf(stderr, "myapp: failed to build message data\n");
        return -1;
    }
    rc = session_write_all(transport, payload, strlen(payload));
    free(payload);
    if (rc != 0) {
        return -1;
    }

    {
        int code = session_read_reply(&lr, last_line, sizeof(last_line));
        if (code != 250) {
            fprintf(stderr, "myapp: expected 250 after message data but server said: %s\n",
                    last_line);
            return -1;
        }
    }

    if (session_send_command_expect(transport, &lr, "QUIT", 221) != 0) {
        return -1;
    }

    return 0;
}
