#include "protocol.h"
#include "session.h"
#include "socket_transport.h"

#include <stdio.h>
#include <stdlib.h>

/* The test build (-DTEST) compiles every file under src/ together with
 * the test binary's own main() in lab-test.c, so main() here must get
 * out of the way rather than collide with it. */
#ifdef TEST
#define main main_exclude
#endif

#include <getopt.h>
#include <string.h>

#define EXIT_USAGE_ERR 1
#define EXIT_SMTP_ERR 2

static void print_usage(FILE *out, const char *prog) {
    fprintf(out,
        "Usage: %s -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
        "          [-H helo-host] <server>\n"
        "\n"
        "  -f <from>       envelope sender, for example you@example.com\n"
        "  -t <to>         envelope recipient\n"
        "  -s <subject>    subject line (default: empty)\n"
        "  -b <body>       message body (default: read from stdin)\n"
        "  -p <port>       port or service name (default: 25)\n"
        "  -H <helo-host>  host name sent with HELO (default: localhost)\n"
        "  <server>        host name or address of the mail server\n",
        prog);
}

static char *read_stdin_body(void) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);

    if (buf == NULL) {
        return NULL;
    }

    for (;;) {
        size_t n;

        if (len + 4096 > cap) {
            char *new_buf;

            cap *= 2;
            new_buf = realloc(buf, cap);
            if (new_buf == NULL) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }

        n = fread(buf + len, 1, 4096, stdin);
        len += n;

        if (n < 4096) {
            if (ferror(stdin)) {
                free(buf);
                return NULL;
            }
            break; /* EOF reached */
        }
    }

    if (len >= cap) {
        char *new_buf = realloc(buf, len + 1);
        if (new_buf == NULL) {
            free(buf);
            return NULL;
        }
        buf = new_buf;
    }

    buf[len] = '\0';
    return buf;
}

int main(int argc, char *argv[]) {
    const char *from = NULL;
    const char *to = NULL;
    const char *subject = "";
    const char *port = "25";
    const char *helo_host = "localhost";
    const char *server = NULL;
    const char *body_arg = NULL;
    char *body = NULL;
    int owns_body = 0;
    int opt;
    socket_transport_t st;
    int result = EXIT_SMTP_ERR;

    st.fd = -1;

    /* make leak runs with no arguments and expects a clean exit */
    if (argc == 1) {
        print_usage(stdout, argv[0]);
        return 0;
    }

    while ((opt = getopt(argc, argv, "f:t:s:b:p:H:")) != -1) {
        switch (opt) {
            case 'f':
                from = optarg;
                break;
            case 't':
                to = optarg;
                break;
            case 's':
                subject = optarg;
                break;
            case 'b':
                body_arg = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'H':
                helo_host = optarg;
                break;
            default:
                print_usage(stderr, argv[0]);
                return EXIT_USAGE_ERR;
        }
    }

    if (optind != argc - 1) {
        fprintf(stderr, "myapp: expected exactly one server argument\n");
        print_usage(stderr, argv[0]);
        return EXIT_USAGE_ERR;
    }
    server = argv[optind];

    if (from == NULL || to == NULL) {
        fprintf(stderr, "myapp: both -f and -t options are required\n");
        print_usage(stderr, argv[0]);
        return EXIT_USAGE_ERR;
    }

    if (protocol_has_bare_crlf(from) || protocol_has_bare_crlf(to) ||
        protocol_has_bare_crlf(subject) || protocol_has_bare_crlf(helo_host) ||
        protocol_has_bare_crlf(server)) {
        fprintf(stderr, "myapp: argument must not contain bare CR or LF\n");
        return EXIT_USAGE_ERR;
    }

    if (body_arg != NULL) {
        body = (char *)body_arg;
        owns_body = 0; /* points into argv - do not free */
    } else {
        body = read_stdin_body();
        if (body == NULL) {
            fprintf(stderr, "myapp: failed to read body from stdin\n");
            return EXIT_SMTP_ERR;
        }
        owns_body = 1;
    }

    if (socket_transport_connect(&st, server, port) != 0) {
        goto cleanup;
    }

    {
        smtp_message_t msg;
        msg.from = from;
        msg.to = to;
        msg.subject = subject;
        msg.body = body;
        msg.helo_host = helo_host;

        if (session_run(&st.transport, &msg) == 0) {
            result = 0;
        }
    }

cleanup:
    socket_transport_close(&st);
    if (owns_body && body != NULL) {
        free(body);
    }
    return result;
}
