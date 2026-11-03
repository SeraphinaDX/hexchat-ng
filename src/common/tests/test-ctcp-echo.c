/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ctcp-echo.h"
#include <string.h>
static server connection;
static gint64 now = G_USEC_PER_SEC * 10;
static gboolean receive(const char *command, const char *nick, const char *target, const char *message)
{
    return !ctcp_echo_duplicate(&connection, command, nick, target, message, now);
}
static void self_version(void)
{
    int requests = 0, replies = 0, i;
    connection.have_echo_message = TRUE;
    ctcp_echo_reset(&connection, 0);
    for (i = 0; i < 2; i++)
        if (receive("PRIVMSG", "Tester", "TESTER", "\001VERSION\001")) requests++;
    g_assert_cmpint(requests, ==, 1);
    ctcp_echo_reset(&connection, 1);
    for (i = 0; i < requests * 2; i++)
        if (receive("NOTICE", "tester", "tester", "\001VERSION ZoiteChat test\001")) replies++;
    g_assert_cmpint(replies, ==, 1);
}
static void unchanged_messages(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        g_assert_true(receive("PRIVMSG", "peer", "tester", "\001VERSION\001"));
        g_assert_true(receive("NOTICE", "peer", "tester", "\001VERSION test\001"));
        g_assert_true(receive("PRIVMSG", "tester", "#test", "\001VERSION\001"));
        g_assert_true(receive("PRIVMSG", "tester", "tester", "\001ACTION waves\001"));
        g_assert_true(receive("PRIVMSG", "tester", "tester", "hello"));
    }
    connection.have_echo_message = FALSE;
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    connection.have_echo_message = TRUE;
}
static void repeated_requests(void)
{
    ctcp_echo_reset(&connection, 0);
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    /* Servers delivering only one self copy must allow the next local send. */
    ctcp_echo_reset(&connection, 0);
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    /* An old unmatched delivery must not swallow a later request. */
    now += 2 * G_USEC_PER_SEC;
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001TIME\001"));
    /* Request and reply pairing are independent even when interleaved. */
    ctcp_echo_reset(&connection, 0); ctcp_echo_reset(&connection, 1);
    g_assert_true(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    g_assert_true(receive("NOTICE", "tester", "tester", "\001VERSION test\001"));
    g_assert_false(receive("PRIVMSG", "tester", "tester", "\001VERSION\001"));
    g_assert_false(receive("NOTICE", "tester", "tester", "\001VERSION test\001"));
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    strcpy(connection.nick, "tester"); connection.p_cmp = g_ascii_strcasecmp;
    g_test_add_func("/ctcp/self-version", self_version);
    g_test_add_func("/ctcp/unchanged-messages", unchanged_messages);
    g_test_add_func("/ctcp/repeated-requests", repeated_requests);
    return g_test_run();
}
