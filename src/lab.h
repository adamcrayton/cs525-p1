#ifndef LAB_H
#define LAB_H

#include <stddef.h>

/* Exit codes used in main() */
#define EXIT_USAGE_ERR 1
#define EXIT_SMTP_ERR 2

/*
 * @brief Establishes a connection to the SMTP server.
 * @param host The hostname of the SMTP server.
 * @param port The port number of the SMTP server.
 * @return The socket file descriptor on success, or -1 on failure.
 */
int server_connection(const char *host, const char *port);

/*
 * @brief Reads a response from the SMTP server.
 * @param sock The socket file descriptor.
 * @param last_line A buffer to store the last line of the response.
 * @param last_line_length The length of the last_line buffer.
 * @return 0 on success, or -1 on failure.
 */
int read_smtp_response(int sock, char *last_line, size_t last_line_length);

/*
 * @brief Sends a line to the SMTP server.
 * @param sock The socket file descriptor.
 * @param line The line to send.
 * @return 0 on success, or -1 on failure.
 */
int send_line(int sock, const char *line);

/*
 * @brief Sends a command to the SMTP server and expects a specific response.
 * @param sock The socket file descriptor.
 * @param command The command to send.
 * @param expected_code The expected response code.
 * @return 0 on success, or -1 on failure.
 */
int send_command_expect(int sock, const char *command, const char *expected_code);

/*
 * @brief Checks if a line has a bare CRLF (i.e., a line that ends with a carriage return followed by a line feed).
 * @param line The line to check.
 * @return 1 if the line has a bare CRLF, or 0 otherwise.
 */
int has_bare_crlf(const char *line);

/*
 * @brief Sends message data to the SMTP server.
 * @param sock The socket file descriptor.
 * @param form The sender's email address.
 * @param to The recipient's email address.
 * @param subject The subject of the message.
 * @param body The body of the message.
 * @return 0 on success, or -1 on failure.
 */
int send_message_data(int sock, const char *form, const char *to, const char *subject, const char *body);

/*
 * @brief Reads the body of the email from standard input.
 * @return A string containing the email body, or NULL on failure.
 */
char *read_stdin_body(void);

#endif // LAB_H
