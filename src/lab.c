#define _POSIX_C_SOURCE 200809L

#include "lab.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define LINE_BUF_SIZE 4096

int connect_to_server(const char *host, const char *port) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *rp;
  int sock = -1;
  int rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* TCP socket */

  rc = getaddrinfo(host, port, &hints, &res);
  if (rc != 0) {
    fprintf(stderr, "myapp: could not resolve %s:%s: %s\n", host, port, gai_sterror(rc));
    return -1;
  }

  for (rp = res; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock == -1)
        continue;

    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
        break; /* Success */

    close(sock);
    sock = -1;
  }

  freeaddrinfo(res);

  if (sock == -1) {
    fprintf(stderr, "myapp: could not connect to %s:%s: %s\n", host, port, strerror(errno));
    return -1;
  }

  return sock;
}

/** 
 * @brief Reads a line from the socket, handling CRLF and bare CR.
 * @param sock The socket file descriptor.
 * @param buf The buffer to store the line.
 * @param buf_size The size of the buffer.
 * @return The number of bytes read, or -1 on error.
 */
static ssize_t read_line(int sock, char *buf, size_t buf_size) {
  size_t len = 0;
  int got_cr = 0;

  if (buf_size == 0) {
    return -1; /* Buffer size must be greater than 0 */
  }

  for (;;) {
    char c;
    ssize_t n = recv(sock, &c, 1, 0);

    if (n == 0) {
      if (len == 0) 
        return 0;
      fprintf(stderr, "myapp: connection closed mid-line\n");
      return -1;
    }
    if (n < 0) {
      fprintf(stderr, "myapp: error reading from socket: %s\n", strerror(errno));
      return -1;
    }

    if (got_cr) {
      got_cr = 0;
      if (c == '\n') {
        buf[len] = '\0';
        return (ssize_t)len;
      }

      if (len + 1 >= buf_size) {
        fprintf(stderr, "myapp: line too long\n");
        return -1;
      }
      buf[len++] = '\r';
    }

    if (c == '\r') {
      got_cr = 1;
      continue;
    }

    if (len +1 >= buf_size) {
      fprintf(stderr, "myapp: server reply line too long\n");
      return -1;
    }
  }
}

int read_smtp_response(int sock, char *last_line, size_t last_line_length) {
  char line[LINE_BUF_SIZE];
  int code = -1;
  
  for (;;) {
    ssize_t n = read_line(sock, line, sizeof(line));

    if (n <= 0) {
      fprintf(stderr, "myapp: server closed connection unexpectedly\n");
      return -1;
    }

    if (n < 4 || !isdigit((unsigned char)line[0]) || !isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2]) || (line[3] != ' ' && line[3] != '-')) {
      fprintf(stderr, "myapp: invalid server response: %s\n", line);
      return -1;
    }

    {
      char code_str[4];
      int this_code;

      code_str[0] = line[0];
      code_str[1] = line[1];
      code_str[2] = line[2];
      code_str[3] = '\0';
      this_code = atoi(code_str);

      if (code == -1) {
        code = this_code;
      } else if (this_code != code) {
        fprintf(stderr, "myapp: inconsistent response codes from server\n");
        return -1;
      }
    }

    if (last_line != NULL && last_line_length > 0) {
      strncpy(last_line, line, last_line_length - 1);
      last_line[last_line_length - 1] = '\0';
    }

    if (line[3] == ' ') {
      break; /* Last line of the response */
    }
  }

  return code;
}

/**
 * @brief Sends raw data over the socket.
 * @param sock The socket file descriptor.
 * @param data The data to send.
 * @param length The length of the data.
 * @return 0 on success, or -1 on failure.
 */
static int send_raw(int sock, const char *data, size_t length) {
  size_t sent = 0;

  while (sent < length) {
    ssize_t n = send(sock, data + sent, length - sent, 0);
    if (n < 0) {
      fprintf(stderr, "myapp: error sending data: %s\n", strerror(errno));
      return -1;
    }
    sent += (size_t)n;
  }

  return 0;
}

int send_line(int sock, const char *line) {
  size_t len = strlen(line);
  char *buf = malloc(len + 2); /* +2 for CRLF */

  if (buf == NULL) {
    fprintf(stderr, "myapp: memory allocation failed\n");
    return -1;
  }

  memcpy(buf, line, len);
  buf[len] = '\r';
  buf[len + 1] = '\n';

  {
    int rc = send_raw(sock, buf, len + 2);
    free(buf);
    return rc;
  }
}

int send_command_expect(int sock, const char *command, int expected_code) {
  char last_line[LINE_BUF_SIZE];
  int code;

  if (send_line(sock, command) != 0) {
    return -1;
  }

  code = read_smtp_response(sock, last_line, sizeof(last_line));
  if (code == -1)
    return -1;

  if (code != expected_code) { 
    fprintf(stderr, "myapp: expected %d after \"%s\" but server said: %s\n", expected_code, command, last_line);
    return -1;
  }

  return 0;
}

int has_bare_crlf(const char *line) {
  for (; *line != '\0'; line++) {
    if (*line == '\r' || *line == '\n') {
      return 1;
    }
  }
  return 0;
}

int send_message_data(int sock, const char *from, const char *to, const char *subject, const char *body) {
  char header[LINE_BUF_SIZE];
  int hn;
  int at_line_start = 1;
  const char *p;

  hn = snprintf(header, sizeof(header), "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n", from, to, subject);
  if (hn < 0 || (size_t)hn >= sizeof(header)) {
    fprintf(stderr, "myapp: header too long\n");
    return -1;
  }

  if (send_raw(sock, header, (size_t)hn) != 0) {
    return -1;
  }

  for (p = body; *p != '\0'; p++) {
    char ch = *p;

    if (ch == '\r') {
      continue; /* Ignore bare CR */
    }

    if (ch == '\n') {
      if (send_raw(sock, "\r\n", 2) != 0) {
        return -1;
      }
      at_line_start = 1;
      continue;
    }

    if (at_line_start && ch == '.') {
      if (send_raw(sock, "..", 2) != 0) {
        return -1;
      }
      at_line_start = 0;
      continue;
    }

    if (send_raw(sock, &ch, 1) != 0) {
      return -1;
    }
    at_line_start = 0;
  }
  
  if (!at_line_start) {
    if (send_raw(sock, "\r\n", 2) != 0) {
      return -1;
    }
  }

  return send_raw(sock, ".\r\n", 3);
}

char *read_stdin_body(void) {
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


