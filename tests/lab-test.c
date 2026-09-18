#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "harness/unity.h"
#include "loopback_server.h"
#include "mock_transport.h"
#include "../src/protocol.h"
#include "../src/session.h"
#include "../src/socket_transport.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------
 * Layer 1: protocol.c - pure helpers, no I/O.
 * ------------------------------------------------------------------- */

void test_parse_reply_line_final(void) {
  int code = 0;
  int is_final = 0;

  TEST_ASSERT_EQUAL_INT(0, protocol_parse_reply_line("250 ok", &code, &is_final));
  TEST_ASSERT_EQUAL_INT(250, code);
  TEST_ASSERT_TRUE(is_final);
}

void test_parse_reply_line_continuation(void) {
  int code = 0;
  int is_final = 1;

  TEST_ASSERT_EQUAL_INT(0, protocol_parse_reply_line("250-more coming", &code, &is_final));
  TEST_ASSERT_EQUAL_INT(250, code);
  TEST_ASSERT_FALSE(is_final);
}

void test_parse_reply_line_rejects_malformed_lines(void) {
  int code, is_final;

  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("25 ok", &code, &is_final));   /* too short */
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("X25 ok", &code, &is_final));  /* 1st char not a digit */
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("2X5 ok", &code, &is_final));  /* 2nd char not a digit */
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("25Xok", &code, &is_final));   /* 3rd char not a digit */
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("250Xok", &code, &is_final));  /* bad separator */
}

void test_parse_reply_line_rejects_too_short_line(void) {
  int code, is_final;

  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("25", &code, &is_final));
}

void test_parse_reply_line_accepts_null_output_pointers(void) {
  /* Both output parameters are documented as optional; a caller that
   * only wants the code (or only the final-line flag, or neither) may
   * pass NULL for the other(s). */
  TEST_ASSERT_EQUAL_INT(0, protocol_parse_reply_line("250 ok", NULL, NULL));

  {
    int code = 0;
    TEST_ASSERT_EQUAL_INT(0, protocol_parse_reply_line("250 ok", &code, NULL));
    TEST_ASSERT_EQUAL_INT(250, code);
  }
  {
    int is_final = 0;
    TEST_ASSERT_EQUAL_INT(0, protocol_parse_reply_line("250-more", NULL, &is_final));
    TEST_ASSERT_FALSE(is_final);
  }
}

void test_format_line_appends_crlf(void) {
  char out[16];

  TEST_ASSERT_EQUAL_INT(6, protocol_format_line("QUIT", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("QUIT\r\n", out);
}

void test_format_line_rejects_overflow(void) {
  char out[8];

  TEST_ASSERT_EQUAL_INT(-1, protocol_format_line("this does not fit", out, sizeof(out)));
}

void test_has_bare_crlf(void) {
  TEST_ASSERT_FALSE(protocol_has_bare_crlf("clean"));
  TEST_ASSERT_TRUE(protocol_has_bare_crlf("dirty\r\ninjected"));
  TEST_ASSERT_TRUE(protocol_has_bare_crlf("dirty\nonly"));
}

void test_dot_stuffing(void) {
  char *out = protocol_dot_stuff(".leading dot\nnormal line\n..already doubled\n");

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL_STRING(
      "..leading dot\r\n"
      "normal line\r\n"
      "...already doubled\r\n",
      out);
  free(out);
}

void test_dot_stuffing_adds_trailing_crlf(void) {
  char *out = protocol_dot_stuff("no trailing newline");

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL_STRING("no trailing newline\r\n", out);
  free(out);
}

void test_dot_stuffing_normalizes_bare_cr(void) {
  /* A '\r' not immediately followed by (and consumed as part of) a
   * newline is dropped rather than passed through, so the output is
   * always clean CRLF - this is the "handling bare CR" behavior. */
  char *out = protocol_dot_stuff("abc\rdef\n");

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL_STRING("abcdef\r\n", out);
  free(out);
}

void test_dot_stuff_grows_buffer_past_initial_capacity(void) {
  /* protocol_dot_stuff's internal buffer starts at 256 bytes; a longer
   * body forces at least one reallocation. */
  static char body[400];
  char *out;
  size_t i;

  for (i = 0; i < sizeof(body) - 2; i++) {
    body[i] = 'a';
  }
  body[sizeof(body) - 2] = '\n';
  body[sizeof(body) - 1] = '\0';

  out = protocol_dot_stuff(body);

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_EQUAL_INT((int)sizeof(body), (int)strlen(out)); /* 398 'a's + CRLF */
  TEST_ASSERT_EQUAL_INT('a', out[397]);
  TEST_ASSERT_EQUAL_INT('\r', out[398]);
  TEST_ASSERT_EQUAL_INT('\n', out[399]);

  free(out);
}

void test_build_data_payload(void) {
  char *out = protocol_build_data_payload("a@example.com", "b@example.com", "Hi", "body\n");

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_NOT_NULL(strstr(out, "From: a@example.com\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "To: b@example.com\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Subject: Hi\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\r\n\r\nbody\r\n.\r\n")); /* blank line, body, terminator */
  free(out);
}

void test_build_data_payload_rejects_oversized_header(void) {
  static char huge_subject[5000];
  char *out;
  size_t i;

  for (i = 0; i < sizeof(huge_subject) - 1; i++) {
    huge_subject[i] = 'S';
  }
  huge_subject[sizeof(huge_subject) - 1] = '\0';

  out = protocol_build_data_payload("a@example.com", "b@example.com", huge_subject, "body\n");

  TEST_ASSERT_NULL(out);
}

/* ---------------------------------------------------------------------
 * Layer 2: session.c - driven entirely off the scripted mock transport,
 * with no socket involved anywhere in these tests.
 * ------------------------------------------------------------------- */

void test_two_replies_in_one_read(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char line[128];

  /* chunk_size 0: the whole script arrives in a single transport
   * read(); the reader must still hand back one line at a time. */
  mock_transport_init(&mt, "250 first\r\n221 second\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(9, session_read_line(&lr, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING("250 first", line);

  TEST_ASSERT_EQUAL_INT(10, session_read_line(&lr, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING("221 second", line);

  mock_transport_free(&mt);
}

void test_multiline_reply_split_across_reads(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char last_line[128];

  /* chunk_size 5: force every reply line to arrive split across
   * several reads, to prove the buffered reader copes with it. */
  mock_transport_init(&mt,
                       "250-first line\r\n"
                       "250-second line\r\n"
                       "250 last line\r\n",
                       5);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(250, session_read_reply(&lr, last_line, sizeof(last_line)));
  TEST_ASSERT_EQUAL_STRING("250 last line", last_line);

  mock_transport_free(&mt);
}

void test_inconsistent_reply_codes_are_rejected(void) {
  mock_transport_t mt;
  line_reader_t lr;

  mock_transport_init(&mt, "250-first line\r\n251 mismatched\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_reply(&lr, NULL, 0));

  mock_transport_free(&mt);
}

void test_read_line_skips_over_a_lone_cr_not_followed_by_lf(void) {
  /* A '\r' that's followed by something other than '\n' is not a line
   * terminator - scanning must keep going past it and find the real
   * CRLF later in the buffer, instead of stopping (or misreading) at
   * that lone '\r'. */
  mock_transport_t mt;
  line_reader_t lr;
  char line[64];

  mock_transport_init(&mt, "AB\rCD\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(5, session_read_line(&lr, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING("AB\rCD", line);

  mock_transport_free(&mt);
}

void test_read_reply_accepts_non_null_last_line_with_zero_size(void) {
  /* last_line non-NULL but last_line_size 0 means "don't actually copy
   * anything" - it must not write through the pointer or crash. */
  mock_transport_t mt;
  line_reader_t lr;
  char sentinel = 'X';

  mock_transport_init(&mt, "250 ok\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(250, session_read_reply(&lr, &sentinel, 0));
  TEST_ASSERT_EQUAL_INT('X', sentinel); /* untouched */

  mock_transport_free(&mt);
}

void test_read_line_rejects_zero_size_buffer(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char buf[8];

  mock_transport_init(&mt, "250 ok\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_line(&lr, buf, 0));

  mock_transport_free(&mt);
}

void test_read_line_rejects_line_too_long_for_caller_buffer(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char buf[3];

  mock_transport_init(&mt, "hello\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_line(&lr, buf, sizeof(buf)));

  mock_transport_free(&mt);
}

void test_read_line_rejects_line_that_never_terminates(void) {
  /* Longer than the reader's internal buffer (SMTP_LINE_MAX * 2 = 8192
   * bytes) and never contains a CRLF, so the buffer fills completely
   * without ever finding a line. */
  static char script[8300];
  mock_transport_t mt;
  line_reader_t lr;
  char buf[64];
  size_t i;

  for (i = 0; i < sizeof(script) - 1; i++) {
    script[i] = 'A';
  }
  script[sizeof(script) - 1] = '\0';

  mock_transport_init(&mt, script, 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_line(&lr, buf, sizeof(buf)));

  mock_transport_free(&mt);
}

void test_read_line_clean_eof_with_nothing_pending(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char buf[64];

  mock_transport_init(&mt, "", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(0, session_read_line(&lr, buf, sizeof(buf)));

  mock_transport_free(&mt);
}

void test_read_line_reports_transport_read_error(void) {
  mock_transport_t mt;
  line_reader_t lr;
  char buf[64];

  mock_transport_init(&mt, "250 ok\r\n", 0);
  mt.force_read_error = 1;
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_line(&lr, buf, sizeof(buf)));

  mock_transport_free(&mt);
}

void test_read_reply_rejects_malformed_line(void) {
  mock_transport_t mt;
  line_reader_t lr;

  mock_transport_init(&mt, "GARBAGE\r\n", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_read_reply(&lr, NULL, 0));

  mock_transport_free(&mt);
}

void test_write_all_reports_transport_write_error(void) {
  mock_transport_t mt;

  mock_transport_init(&mt, "", 0);
  mt.fail_write_after_calls = 1; /* fail on the very first write() call */

  TEST_ASSERT_EQUAL_INT(-1, session_write_all(&mt.transport, "x", 1));

  mock_transport_free(&mt);
}

void test_send_command_expect_rejects_oversized_command(void) {
  static char huge_command[SMTP_LINE_MAX + 10];
  mock_transport_t mt;
  line_reader_t lr;
  size_t i;

  for (i = 0; i < sizeof(huge_command) - 1; i++) {
    huge_command[i] = 'X';
  }
  huge_command[sizeof(huge_command) - 1] = '\0';

  mock_transport_init(&mt, "", 0);
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_send_command_expect(&mt.transport, &lr, huge_command, 250));
  TEST_ASSERT_EQUAL_INT(0, (int)mt.sent_len); /* never even attempted to send it */

  mock_transport_free(&mt);
}

void test_send_command_expect_reports_write_error(void) {
  mock_transport_t mt;
  line_reader_t lr;

  mock_transport_init(&mt, "", 0);
  mt.fail_write_after_calls = 1;
  line_reader_init(&lr, &mt.transport);

  TEST_ASSERT_EQUAL_INT(-1, session_send_command_expect(&mt.transport, &lr, "QUIT", 221));

  mock_transport_free(&mt);
}

void test_full_session_succeeds(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  /* One reply per command the client is expected to send, in order:
   * HELO, MAIL FROM, RCPT TO, DATA, the message terminator, QUIT.
   * chunk_size 3 forces every one of those replies to arrive split
   * across several reads, with no network involved at all. */
  mock_transport_init(&mt,
                       "250 ok\r\n"
                       "250 ok\r\n"
                       "250 ok\r\n"
                       "354 go ahead\r\n"
                       "250 queued\r\n"
                       "221 bye\r\n",
                       3);

  TEST_ASSERT_EQUAL_INT(0, session_run(&mt.transport, &msg));
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "HELO localhost\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "MAIL FROM:<alice@example.com>\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "RCPT TO:<bob@example.com>\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "DATA\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "\r\n.\r\n")); /* the DATA terminator line */
  TEST_ASSERT_NOT_NULL(strstr(mt.sent, "QUIT\r\n"));

  mock_transport_free(&mt);
}

void test_rejected_recipient_stops_session_before_data(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  /* HELO and MAIL FROM succeed, RCPT TO is rejected - the session must
   * stop right there and never send DATA or QUIT. */
  mock_transport_init(&mt,
                       "250 ok\r\n"
                       "250 ok\r\n"
                       "550 no such user\r\n",
                       0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));
  TEST_ASSERT_NULL(strstr(mt.sent, "DATA"));
  TEST_ASSERT_NULL(strstr(mt.sent, "QUIT"));

  mock_transport_free(&mt);
}

void test_truncated_reply_is_an_error(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  /* The script ends mid-reply (no trailing CRLF). */
  mock_transport_init(&mt, "250 ok\r\n250 o", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_rejects_oversized_envelope_field(void) {
  static char huge_from[SMTP_LINE_MAX + 100];
  mock_transport_t mt;
  smtp_message_t msg;
  size_t i;

  for (i = 0; i < sizeof(huge_from) - 1; i++) {
    huge_from[i] = 'a';
  }
  huge_from[sizeof(huge_from) - 1] = '\0';

  msg.from = huge_from;
  msg.to = "bob@example.com";
  msg.subject = "Hi";
  msg.body = "Hello.\n";
  msg.helo_host = "localhost";

  mock_transport_init(&mt, "", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_rejects_oversized_helo_host(void) {
  static char huge_helo[SMTP_LINE_MAX + 100];
  mock_transport_t mt;
  smtp_message_t msg;
  size_t i;

  for (i = 0; i < sizeof(huge_helo) - 1; i++) {
    huge_helo[i] = 'h';
  }
  huge_helo[sizeof(huge_helo) - 1] = '\0';

  msg.from = "alice@example.com";
  msg.to = "bob@example.com";
  msg.subject = "Hi";
  msg.body = "Hello.\n";
  msg.helo_host = huge_helo;

  mock_transport_init(&mt, "", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));
  TEST_ASSERT_EQUAL_INT(0, (int)mt.sent_len); /* failed before sending anything */

  mock_transport_free(&mt);
}

void test_session_run_rejects_oversized_recipient(void) {
  static char huge_to[SMTP_LINE_MAX + 100];
  mock_transport_t mt;
  smtp_message_t msg;
  size_t i;

  for (i = 0; i < sizeof(huge_to) - 1; i++) {
    huge_to[i] = 'b';
  }
  huge_to[sizeof(huge_to) - 1] = '\0';

  msg.from = "alice@example.com";
  msg.to = huge_to;
  msg.subject = "Hi";
  msg.body = "Hello.\n";
  msg.helo_host = "localhost";

  mock_transport_init(&mt, "", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));
  TEST_ASSERT_EQUAL_INT(0, (int)mt.sent_len); /* failed before sending anything */

  mock_transport_free(&mt);
}

void test_session_run_helo_failure(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  mock_transport_init(&mt, "550 access denied\r\n", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));
  TEST_ASSERT_NULL(strstr(mt.sent, "MAIL FROM"));

  mock_transport_free(&mt);
}

void test_session_run_data_command_rejected(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  mock_transport_init(&mt, "250 ok\r\n250 ok\r\n250 ok\r\n500 no data\r\n", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_oversized_subject_fails_to_build_payload(void) {
  static char huge_subject[SMTP_LINE_MAX + 100];
  mock_transport_t mt;
  smtp_message_t msg;
  size_t i;

  for (i = 0; i < sizeof(huge_subject) - 1; i++) {
    huge_subject[i] = 'S';
  }
  huge_subject[sizeof(huge_subject) - 1] = '\0';

  msg.from = "alice@example.com";
  msg.to = "bob@example.com";
  msg.subject = huge_subject;
  msg.body = "Hello.\n";
  msg.helo_host = "localhost";

  /* HELO, MAIL FROM, RCPT TO, DATA all succeed - the failure happens
   * afterward, while building the payload itself. */
  mock_transport_init(&mt, "250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_payload_write_failure(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  mock_transport_init(&mt, "250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n", 0);
  /* HELO, MAIL FROM, RCPT TO, DATA are writes 1-4 and succeed; the
   * payload send is write 5 and fails. */
  mt.fail_write_after_calls = 5;

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_message_data_rejected(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  mock_transport_init(&mt, "250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n550 rejected\r\n", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

void test_session_run_quit_failure(void) {
  mock_transport_t mt;
  smtp_message_t msg = {"alice@example.com", "bob@example.com", "Hi", "Hello.\n", "localhost"};

  mock_transport_init(
      &mt, "250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n250 queued\r\n500 bad quit\r\n", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

/* ---------------------------------------------------------------------
 * mock_transport.c itself - the test double's own edge cases.
 * ------------------------------------------------------------------- */

void test_mock_transport_read_clamps_to_caller_buffer(void) {
  mock_transport_t mt;
  char small_buf[3];
  ssize_t n;

  mock_transport_init(&mt, "hello world", 0);
  n = mt.transport.read(mt.transport.ctx, small_buf, sizeof(small_buf));

  TEST_ASSERT_EQUAL_INT(3, (int)n);
  TEST_ASSERT_EQUAL_STRING_LEN("hel", small_buf, 3);

  mock_transport_free(&mt);
}

void test_mock_transport_write_grows_buffer_across_multiple_doublings(void) {
  static char big[5000];
  mock_transport_t mt;
  ssize_t n;
  size_t i;

  for (i = 0; i < sizeof(big); i++) {
    big[i] = 'z';
  }

  mock_transport_init(&mt, "", 0);

  /* First write: sent_cap starts at 0, so the initial-allocation side
   * of the "start at 256, or double what's there" choice is taken. */
  n = mt.transport.write(mt.transport.ctx, big, 10);
  TEST_ASSERT_EQUAL_INT(10, (int)n);

  /* Second write: sent_cap is already non-zero (256), so this forces
   * growth from an existing capacity instead of a first allocation -
   * the other side of that choice. */
  n = mt.transport.write(mt.transport.ctx, big, sizeof(big));
  TEST_ASSERT_EQUAL_INT((int)sizeof(big), (int)n);
  TEST_ASSERT_EQUAL_INT(10 + (int)sizeof(big), (int)mt.sent_len);

  mock_transport_free(&mt);
}

/* ---------------------------------------------------------------------
 * Layer 3: socket_transport.c - a real loopback TCP connection, no
 * external network access required.
 * ------------------------------------------------------------------- */

void test_socket_transport_connects_and_transfers_data(void) {
  char port[16];
  int listen_fd = loopback_listen(port, sizeof(port));
  socket_transport_t st;
  int peer_fd;
  char recv_buf[32];
  ssize_t n;

  TEST_ASSERT_TRUE(listen_fd != -1);

  st.fd = -1;
  TEST_ASSERT_EQUAL_INT(0, socket_transport_connect(&st, "127.0.0.1", port));

  peer_fd = accept(listen_fd, NULL, NULL);
  TEST_ASSERT_TRUE(peer_fd != -1);

  n = st.transport.write(st.transport.ctx, "ping", 4);
  TEST_ASSERT_EQUAL_INT(4, (int)n);
  n = recv(peer_fd, recv_buf, sizeof(recv_buf), 0);
  TEST_ASSERT_EQUAL_INT(4, (int)n);
  TEST_ASSERT_EQUAL_STRING_LEN("ping", recv_buf, 4);

  n = send(peer_fd, "pong!", 5, 0);
  TEST_ASSERT_EQUAL_INT(5, (int)n);
  n = st.transport.read(st.transport.ctx, recv_buf, sizeof(recv_buf));
  TEST_ASSERT_EQUAL_INT(5, (int)n);
  TEST_ASSERT_EQUAL_STRING_LEN("pong!", recv_buf, 5);

  close(peer_fd);
  close(listen_fd);
  socket_transport_close(&st);
}

void test_socket_transport_read_write_report_transport_errors(void) {
  char port[16];
  int listen_fd = loopback_listen(port, sizeof(port));
  socket_transport_t st;
  int peer_fd;
  char buf[4];
  ssize_t n;

  TEST_ASSERT_TRUE(listen_fd != -1);

  st.fd = -1;
  TEST_ASSERT_EQUAL_INT(0, socket_transport_connect(&st, "127.0.0.1", port));

  peer_fd = accept(listen_fd, NULL, NULL);
  TEST_ASSERT_TRUE(peer_fd != -1);
  close(peer_fd);
  close(listen_fd);

  socket_transport_close(&st); /* sets st.fd back to -1 */

  n = st.transport.read(st.transport.ctx, buf, sizeof(buf)); /* recv() on fd -1 */
  TEST_ASSERT_EQUAL_INT(-1, (int)n);

  n = st.transport.write(st.transport.ctx, buf, 1); /* send() on fd -1 */
  TEST_ASSERT_EQUAL_INT(-1, (int)n);
}

void test_socket_transport_connect_reports_resolution_failure(void) {
  socket_transport_t st;

  st.fd = -1;
  /* Not a numeric port and not a real service name, so getaddrinfo()
   * fails regardless of whether /etc/services even exists here. */
  TEST_ASSERT_EQUAL_INT(-1,
                         socket_transport_connect(&st, "127.0.0.1", "not-a-real-service-xyz"));

  /* fd is still -1 after a failed connect - closing must be a no-op,
   * not a close(-1). */
  socket_transport_close(&st);
}

void test_socket_transport_connect_reports_connection_refused(void) {
  char port[16];
  int listen_fd = loopback_listen(port, sizeof(port));
  socket_transport_t st;

  TEST_ASSERT_TRUE(listen_fd != -1);
  close(listen_fd); /* release the port; nothing is listening there now */

  st.fd = -1;
  TEST_ASSERT_EQUAL_INT(-1, socket_transport_connect(&st, "127.0.0.1", port));

  socket_transport_close(&st);
}

void test_socket_transport_close_is_idempotent(void) {
  char port[16];
  int listen_fd = loopback_listen(port, sizeof(port));
  socket_transport_t st;

  TEST_ASSERT_TRUE(listen_fd != -1);

  st.fd = -1;
  TEST_ASSERT_EQUAL_INT(0, socket_transport_connect(&st, "127.0.0.1", port));

  socket_transport_close(&st);
  TEST_ASSERT_EQUAL_INT(-1, st.fd);

  /* Closing again must not double-close the (already released) fd. */
  socket_transport_close(&st);
  TEST_ASSERT_EQUAL_INT(-1, st.fd);

  close(listen_fd);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_reply_line_final);
  RUN_TEST(test_parse_reply_line_continuation);
  RUN_TEST(test_parse_reply_line_rejects_malformed_lines);
  RUN_TEST(test_parse_reply_line_rejects_too_short_line);
  RUN_TEST(test_parse_reply_line_accepts_null_output_pointers);
  RUN_TEST(test_format_line_appends_crlf);
  RUN_TEST(test_format_line_rejects_overflow);
  RUN_TEST(test_has_bare_crlf);
  RUN_TEST(test_dot_stuffing);
  RUN_TEST(test_dot_stuffing_adds_trailing_crlf);
  RUN_TEST(test_dot_stuffing_normalizes_bare_cr);
  RUN_TEST(test_dot_stuff_grows_buffer_past_initial_capacity);
  RUN_TEST(test_build_data_payload);
  RUN_TEST(test_build_data_payload_rejects_oversized_header);

  RUN_TEST(test_two_replies_in_one_read);
  RUN_TEST(test_multiline_reply_split_across_reads);
  RUN_TEST(test_inconsistent_reply_codes_are_rejected);
  RUN_TEST(test_read_line_skips_over_a_lone_cr_not_followed_by_lf);
  RUN_TEST(test_read_reply_accepts_non_null_last_line_with_zero_size);
  RUN_TEST(test_read_line_rejects_zero_size_buffer);
  RUN_TEST(test_read_line_rejects_line_too_long_for_caller_buffer);
  RUN_TEST(test_read_line_rejects_line_that_never_terminates);
  RUN_TEST(test_read_line_clean_eof_with_nothing_pending);
  RUN_TEST(test_read_line_reports_transport_read_error);
  RUN_TEST(test_read_reply_rejects_malformed_line);
  RUN_TEST(test_write_all_reports_transport_write_error);
  RUN_TEST(test_send_command_expect_rejects_oversized_command);
  RUN_TEST(test_send_command_expect_reports_write_error);
  RUN_TEST(test_full_session_succeeds);
  RUN_TEST(test_rejected_recipient_stops_session_before_data);
  RUN_TEST(test_truncated_reply_is_an_error);
  RUN_TEST(test_session_run_rejects_oversized_envelope_field);
  RUN_TEST(test_session_run_rejects_oversized_helo_host);
  RUN_TEST(test_session_run_rejects_oversized_recipient);
  RUN_TEST(test_session_run_helo_failure);
  RUN_TEST(test_session_run_data_command_rejected);
  RUN_TEST(test_session_run_oversized_subject_fails_to_build_payload);
  RUN_TEST(test_session_run_payload_write_failure);
  RUN_TEST(test_session_run_message_data_rejected);
  RUN_TEST(test_session_run_quit_failure);

  RUN_TEST(test_mock_transport_read_clamps_to_caller_buffer);
  RUN_TEST(test_mock_transport_write_grows_buffer_across_multiple_doublings);

  RUN_TEST(test_socket_transport_connects_and_transfers_data);
  RUN_TEST(test_socket_transport_read_write_report_transport_errors);
  RUN_TEST(test_socket_transport_connect_reports_resolution_failure);
  RUN_TEST(test_socket_transport_connect_reports_connection_refused);
  RUN_TEST(test_socket_transport_close_is_idempotent);

  return UNITY_END();
}
