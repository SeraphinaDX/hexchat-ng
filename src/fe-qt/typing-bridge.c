/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../common/zoitechat.h"
#include "../common/zoitechatc.h"
#include "../common/fe.h"
#include "qt-bridge.h"
#include <string.h>

#define TYPING_THROTTLE_USEC (3 * G_USEC_PER_SEC)

static gboolean
qt_client_tag_allowed (server *serv, const char *tag)
{
        char **deny;
        gboolean blocked = FALSE;
        int i;

        if (!serv->have_message_tags)
                return FALSE;

        if (!serv->clienttagdeny || !*serv->clienttagdeny)
                return TRUE;

        deny = g_strsplit (serv->clienttagdeny, ",", 0);
        for (i = 0; deny[i]; i++)
        {
                if (!strcmp (deny[i], "*"))
                        blocked = TRUE;
                else if (deny[i][0] == '-' && !strcmp (deny[i] + 1, tag))
                        blocked = FALSE;
                else if (!strcmp (deny[i], tag) || (deny[i][0] == '+' && !strcmp (deny[i] + 1, tag)))
                        blocked = TRUE;
        }

        g_strfreev (deny);
        return !blocked;
}

static gboolean
qt_typing_can_send (session *sess)
{
        gint64 now = g_get_monotonic_time ();

        return sess->typing_last_sent == 0 ||
               now - sess->typing_last_sent >= TYPING_THROTTLE_USEC;
}

static gboolean
qt_send_typing (session *sess, const char *state)
{
        char tags[32];

        if (!sess || !sess->server->connected || !qt_client_tag_allowed (sess->server, "typing") || !sess->channel[0])
                return FALSE;

        if (sess->type != SESS_CHANNEL && sess->type != SESS_DIALOG)
                return FALSE;

        g_snprintf (tags, sizeof (tags), "+typing=%s", state);
        sess->server->p_tagmsg (sess->server, tags, sess->channel);
        sess->typing_last_sent = g_get_monotonic_time ();
        return TRUE;
}

static int
qt_typing_pause_cb (session *sess)
{
        sess->typing_timeout_tag = 0;
        if (sess->typing_status == 1)
        {
                qt_send_typing (sess, "paused");
                sess->typing_status = 2;
        }
        return 0;
}

void
qt_core_typing_update (void *ptr, const char *text)
{
        session *sess = ptr;
        if (!sess || !is_session(sess))
                return;

        if (sess->typing_timeout_tag)
        {
                fe_timeout_remove (sess->typing_timeout_tag);
                sess->typing_timeout_tag = 0;
        }

        if (!text || !*text || text[0] == prefs.hex_input_command_char[0])
        {
                if (sess->typing_status)
                        qt_send_typing (sess, "done");
                sess->typing_status = 0;
                return;
        }

        if (qt_typing_can_send (sess))
                qt_send_typing (sess, "active");
        sess->typing_status = 1;
        sess->typing_timeout_tag = fe_timeout_add_seconds (6, qt_typing_pause_cb, sess);
}

