/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ctcp-echo.h"
#include <string.h>

void
ctcp_echo_reset(server *serv, int notice)
{
    g_clear_pointer(&serv->self_ctcp_echo[notice].message, g_free);
    serv->self_ctcp_echo[notice].received = 0;
}

gboolean
ctcp_echo_duplicate(server *serv, const char *command, const char *nick,
                    const char *target, const char *message, gint64 now)
{
    int notice;
    if (!serv->have_echo_message || message[0] != '\001' ||
        serv->p_cmp(nick, serv->nick) || serv->p_cmp(target, serv->nick))
        return FALSE;
    if (!strcmp(command, "PRIVMSG")) notice = 0;
    else if (!strcmp(command, "NOTICE")) notice = 1;
    else return FALSE;
    /* ACTION is chat text, handled by the normal message display path. */
    if (!g_ascii_strncasecmp(message + 1, "ACTION ", 7)) return FALSE;

    /* Some echo-message servers deliver both the self-addressed message and
     * its echo. Pair identical CTCP deliveries, separately for requests and
     * replies, before a request can generate a second automatic response.
     * Bound the pairing window, and reset on a new local CTCP send, so a later
     * intentional /ctcp to oneself is still processed normally. */
    if (serv->self_ctcp_echo[notice].message &&
        now - serv->self_ctcp_echo[notice].received <= G_USEC_PER_SEC &&
        !strcmp(serv->self_ctcp_echo[notice].message, message))
    {
        ctcp_echo_reset(serv, notice);
        return TRUE;
    }
    ctcp_echo_reset(serv, notice);
    serv->self_ctcp_echo[notice].message = g_strdup(message);
    serv->self_ctcp_echo[notice].received = now;
    return FALSE;
}
