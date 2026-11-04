/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../common/zoitechat.h"
#include "../common/zoitechatc.h"
#include "../common/server.h"
#include "../common/userlist.h"
#include "../common/chanopt.h"
#include "../common/text.h"
#include "relayformat.h"
#include <string.h>

void qt_relay_format_message(void *ptr, const char *kind, const char *nick,
                             const char *text, int highlight, char *output, int capacity)
{
    session *sess = ptr;
    const int own = nick[0] && !sess->server->p_cmp(nick, sess->server->nick);
    const int dialog = sess->type == SESS_DIALOG;
    int event;
    char *args[5] = {"", (char *)nick, (char *)text, "", ""};
    char colored[NICKLEN + 4], prefix[2] = {0};
    unsigned int strip = chanopt_is_set(prefs.hex_text_stripcolor_msg, sess->text_strip) ? ~0u : 0;

    if (!strcmp(kind, "message"))
        event = own ? XP_TE_UCHANMSG : dialog ? XP_TE_DPRIVMSG : highlight ? XP_TE_HCHANMSG : XP_TE_CHANMSG;
    else if (!strcmp(kind, "action"))
        event = own ? XP_TE_UACTION : dialog ? XP_TE_DPRIVACTION : highlight ? XP_TE_HCHANACTION : XP_TE_CHANACTION;
    else if (!strcmp(kind, "notice")) {
        event = dialog ? XP_TE_NOTICE : XP_TE_CHANNOTICE;
        if (!dialog) { args[2] = sess->channel; args[3] = (char *)text; }
    } else {
        g_strlcpy(output, text, capacity);
        return;
    }

    if (strcmp(kind, "notice") && !dialog) {
        struct User *user = userlist_find(sess, (char *)nick);
        if (user) prefix[0] = user->prefix[0];
        args[3] = prefix;
    }
    /* Match text_emit's nickname-colour and per-conversation stripping rules,
     * but leave alert/unread policy to the relay delivery path, including replay. */
    if (prefs.hex_text_color_nicks && (event == XP_TE_CHANMSG || event == XP_TE_CHANACTION)) {
        g_snprintf(colored, sizeof(colored), "\003%d%s", text_color_of((char *)nick), nick);
        args[1] = colored;
        strip &= ~(1u << 1);
    }
    format_event(sess, event, args, output, capacity, strip);
}
