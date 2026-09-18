#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

/* Max length of one SMTP line (command or reply), not counting CRLF. */
#define SMTP_LINE_MAX 4096

/*
 * Parses one already-unterminated SMTP reply line (no CRLF at the end).
 * On success stores the 3-digit reply code in *code_out and whether
 * this is the final line of a multi-line reply (line[3] == ' ') versus
 * a continuation (line[3] == '-') in *is_final_out.
 * Returns 0 if `line` is well-formed, -1 otherwise. Pure - no I/O.
 */
int protocol_parse_reply_line(const char *line, int *code_out, int *is_final_out);

/*
 * Returns 1 if `text` contains a bare CR or LF, 0 otherwise. Used to
 * reject untrusted input (command-line args) before it is ever placed
 * on the wire, where an embedded CRLF would let it smuggle extra
 * SMTP commands into the session.
 */
int protocol_has_bare_crlf(const char *text);

/*
 * Formats `text` followed by CRLF into out (size out_size). Returns the
 * number of bytes written, excluding the terminating NUL, or -1 if it
 * does not fit. Pure - this only builds the bytes; a transport decides
 * whether/how to send them.
 */
int protocol_format_line(const char *text, char *out, size_t out_size);

/*
 * Dot-stuffs `body` per RFC 5321 4.5.2 while normalizing line endings:
 * a bare '\r' is dropped, '\n' becomes CRLF, and any line that starts
 * with '.' gets a second leading '.'. The result always ends in CRLF
 * (one is appended if `body` doesn't already end with a newline).
 * Returns a newly malloc'd, NUL-terminated string, or NULL on
 * allocation failure. Caller frees.
 */
char *protocol_dot_stuff(const char *body);

/*
 * Builds the full DATA payload - From/To/Subject headers, a blank
 * line, the dot-stuffed body, and the terminating "." line - as one
 * block ready to hand to a transport. Returns a newly malloc'd,
 * NUL-terminated string, or NULL on failure. Caller frees.
 */
char *protocol_build_data_payload(const char *from, const char *to,
                                   const char *subject, const char *body);

#endif /* PROTOCOL_H */
