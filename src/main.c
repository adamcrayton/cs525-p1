#include "lab.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef TEST
#define main main_exclude
#endif

#include <getopt.h>
#include <string.h>
#include <unistd.h>

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

int main(int argc, char *argv[]) {
    const char *from = NULL;
    const char *to = NULL;
    const char *subject = "";
    const char *body = NULL;
    const char *port = "25";
    const char *helo_host = "localhost";
    const char *server = NULL;
    char *body = NULL;
    int owns_body = 0;
    int opt;
    int sock = -1;
    char helo_cmd[512];
    char mail_cmd[512];
    char rcpt_cmd[512];
    int result = EXIT_SMTP_ERR;

    /* make leak runs with no arguments and expects a clean exit*/
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
                body = optarg;
                owns_body = 1; // We own the body string and should free it later
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

    if (has_bare_crlf(from) || has_bare_crlf(to) || has_bare_crlf(subject) || has_bare_crlf(helo_host) || has_bare_crlf(server)) {
        fprintf(stderr, "myapp: argument must not contain bare CR or LF\n");
        return EXIT_USAGE_ERR;
    }

    if (body_arg != NULL) {
        body = (char *)body_arg;
        owns_body = 0; // We do not own the body string, do not free it
    } else {
        body = read_stdin_body();
        if (body == NULL) {
            fprintf(stderr, "myapp: failed to read body from stdin\n");
            return EXIT_SMTP_ERR;
        }
        owns_body = 1; // We own the body string and should free it later
    }

    sock = connect_to_server(server, port);
    if (sock == -1) {
        goto cleanup;
    }

    snprintf(helo_cmd, sizeof(helo_cmd), "HELO %s", helo_host);
    if (send_command_expect(sock, helo_cmd, 250) != 0) {
        goto cleanup;
    }

    snprintf(mail_cmd, sizeof(mail_cmd), "MAIL FROM:<%s>", from);
    if (send_command_expect(sock, mail_cmd, 250) != 0) {
        goto cleanup;
    }

    snprintf(rcpt_cmd, sizeof(rcpt_cmd), "RCPT TO:<%s>", to);
    if (send_command_expect(sock, rcpt_cmd, 250) != 0) {
        goto cleanup;
    }

    if (send_command_expect(sock, "DATA", 354) != 0) {
        goto cleanup;
    }

    if (send_message_data(sock, from, to, subject, body) != 0) {
        goto cleanup;
    }

    {
        char last_line[1024];
        int code = read_smtp_response(sock, last_line, sizeof(last_line));

        if (code != 250) {
            fprintf(stderr, "myapp: expected 250 after RCPT TO but server said: %s\n", last_line);
            goto cleanup;
        }
    }

    if (send_command_expect(sock, "QUIT", 221) != 0) {
        goto cleanup;
    }

    result = 0;

    cleanup:
        if (sock != -1) {
            close(sock);
        }
        if (owns_body && body != NULL) {
            free(body);
        }

        return result;
}
