#include <stdlib.h>
#include <string.h>

#include "harness/unity.h"
#include "mock_transport.h"
#include "../src/protocol.h"
#include "../src/session.h"

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
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("25Xok", &code, &is_final));   /* not a digit */
  TEST_ASSERT_EQUAL_INT(-1, protocol_parse_reply_line("250Xok", &code, &is_final));  /* bad separator */
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

void test_build_data_payload(void) {
  char *out = protocol_build_data_payload("a@example.com", "b@example.com", "Hi", "body\n");

  TEST_ASSERT_NOT_NULL(out);
  TEST_ASSERT_NOT_NULL(strstr(out, "From: a@example.com\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "To: b@example.com\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "Subject: Hi\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\r\n\r\nbody\r\n.\r\n")); /* blank line, body, terminator */
  free(out);
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
   * stop right there and never send DATA or QUIT. This is the "error
   * case is the scripted string with one code changed" case. */
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

  /* The script ends mid-reply (no trailing CRLF) - the "truncated
   * string" error case. */
  mock_transport_init(&mt, "250 ok\r\n250 o", 0);

  TEST_ASSERT_EQUAL_INT(-1, session_run(&mt.transport, &msg));

  mock_transport_free(&mt);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_reply_line_final);
  RUN_TEST(test_parse_reply_line_continuation);
  RUN_TEST(test_parse_reply_line_rejects_malformed_lines);
  RUN_TEST(test_format_line_appends_crlf);
  RUN_TEST(test_format_line_rejects_overflow);
  RUN_TEST(test_has_bare_crlf);
  RUN_TEST(test_dot_stuffing);
  RUN_TEST(test_dot_stuffing_adds_trailing_crlf);
  RUN_TEST(test_build_data_payload);

  RUN_TEST(test_two_replies_in_one_read);
  RUN_TEST(test_multiline_reply_split_across_reads);
  RUN_TEST(test_inconsistent_reply_codes_are_rejected);
  RUN_TEST(test_full_session_succeeds);
  RUN_TEST(test_rejected_recipient_stops_session_before_data);
  RUN_TEST(test_truncated_reply_is_an_error);

  return UNITY_END();
}
