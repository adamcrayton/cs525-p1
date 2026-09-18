#include "protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int protocol_parse_reply_line(const char *line, int *code_out, int *is_final_out) {
    size_t len = strlen(line);
    char code_str[4];

    if (len < 4) {
        return -1;
    }
    if (!isdigit((unsigned char)line[0]) || !isdigit((unsigned char)line[1]) ||
        !isdigit((unsigned char)line[2])) {
        return -1;
    }
    if (line[3] != ' ' && line[3] != '-') {
        return -1;
    }

    code_str[0] = line[0];
    code_str[1] = line[1];
    code_str[2] = line[2];
    code_str[3] = '\0';

    if (code_out != NULL) {
        *code_out = atoi(code_str);
    }
    if (is_final_out != NULL) {
        *is_final_out = (line[3] == ' ');
    }
    return 0;
}

int protocol_has_bare_crlf(const char *text) {
    for (; *text != '\0'; text++) {
        if (*text == '\r' || *text == '\n') {
            return 1;
        }
    }
    return 0;
}

int protocol_format_line(const char *text, char *out, size_t out_size) {
    size_t len = strlen(text);

    if (len + 3 > out_size) { /* '\r' + '\n' + '\0' */
        return -1;
    }
    memcpy(out, text, len);
    out[len] = '\r';
    out[len + 1] = '\n';
    out[len + 2] = '\0';
    return (int)(len + 2);
}

/* Small dynamic byte buffer, private to this file. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static int strbuf_init(strbuf_t *sb, size_t initial_cap) {
    if (initial_cap == 0) {
        initial_cap = 16;
    }
    sb->data = malloc(initial_cap);
    if (sb->data == NULL) {
        return -1;
    }
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = initial_cap;
    return 0;
}

static int strbuf_append(strbuf_t *sb, const char *bytes, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t new_cap = sb->cap;
        while (new_cap < sb->len + n + 1) {
            new_cap *= 2;
        }
        char *new_data = realloc(sb->data, new_cap);
        if (new_data == NULL) {
            return -1;
        }
        sb->data = new_data;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, bytes, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

char *protocol_dot_stuff(const char *body) {
    strbuf_t sb;
    int at_line_start = 1;
    const char *p;

    if (strbuf_init(&sb, 256) != 0) {
        return NULL;
    }

    for (p = body; *p != '\0'; p++) {
        char ch = *p;

        if (ch == '\r') {
            continue; /* normalize away a bare CR */
        }

        if (ch == '\n') {
            if (strbuf_append(&sb, "\r\n", 2) != 0) {
                free(sb.data);
                return NULL;
            }
            at_line_start = 1;
            continue;
        }

        if (at_line_start && ch == '.') {
            if (strbuf_append(&sb, "..", 2) != 0) {
                free(sb.data);
                return NULL;
            }
            at_line_start = 0;
            continue;
        }

        if (strbuf_append(&sb, &ch, 1) != 0) {
            free(sb.data);
            return NULL;
        }
        at_line_start = 0;
    }

    if (!at_line_start) {
        if (strbuf_append(&sb, "\r\n", 2) != 0) {
            free(sb.data);
            return NULL;
        }
    }

    return sb.data;
}

char *protocol_build_data_payload(const char *from, const char *to, const char *subject,
                                   const char *body) {
    char header[SMTP_LINE_MAX];
    int hn;
    char *stuffed;
    strbuf_t sb;

    hn = snprintf(header, sizeof(header), "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n", from, to,
                  subject);
    if (hn < 0 || (size_t)hn >= sizeof(header)) {
        return NULL;
    }

    stuffed = protocol_dot_stuff(body);
    if (stuffed == NULL) {
        return NULL;
    }

    if (strbuf_init(&sb, (size_t)hn + strlen(stuffed) + 8) != 0) {
        free(stuffed);
        return NULL;
    }

    if (strbuf_append(&sb, header, (size_t)hn) != 0 ||
        strbuf_append(&sb, stuffed, strlen(stuffed)) != 0 ||
        strbuf_append(&sb, ".\r\n", 3) != 0) {
        free(stuffed);
        free(sb.data);
        return NULL;
    }

    free(stuffed);
    return sb.data;
}
