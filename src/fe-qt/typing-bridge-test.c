/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../common/zoitechat.h"
#include "qt-bridge.h"
#include <stdio.h>
#include <string.h>
struct zoitechatprefs prefs;
static session conversation;
static server connection;
static int calls, timer;
static int (*pause_callback)(session *);
static char sent[32];
int is_session(session *sess) { return sess == &conversation; }
int fe_timeout_add_seconds(int interval, void *callback, void *userdata)
{
    g_assert(interval == 6 && userdata == &conversation);
    pause_callback = callback;
    return ++timer;
}
void fe_timeout_remove(int tag) { g_assert(tag > 0); }
static void send_tag(server *serv, char *tags, char *target)
{
    g_assert(serv == &connection && !strcmp(target, "#test"));
    g_strlcpy(sent, tags, sizeof(sent)); ++calls;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    conversation.server = &connection; conversation.type = SESS_CHANNEL;
    strcpy(conversation.channel, "#test"); strcpy(prefs.hex_input_command_char, "/");
    connection.p_tagmsg = send_tag; connection.connected = 1;
    qt_core_typing_update(&conversation, "hi"); CHECK(calls == 0);
    connection.have_message_tags = 1;
    qt_core_typing_update(&conversation, "hi"); CHECK(calls == 1 && !strcmp(sent, "+typing=active"));
    qt_core_typing_update(&conversation, "hi again"); CHECK(calls == 1);
    CHECK(pause_callback(&conversation) == 0 && calls == 2 && !strcmp(sent, "+typing=paused"));
    qt_core_typing_update(&conversation, ""); CHECK(calls == 3 && !strcmp(sent, "+typing=done") && !conversation.typing_status);
    qt_core_typing_update(&conversation, "/whois Alice"); CHECK(calls == 3 && !conversation.typing_status);
    conversation.typing_last_sent = 0; connection.clienttagdeny = "*";
    qt_core_typing_update(&conversation, "hi"); CHECK(calls == 3);
    connection.clienttagdeny = "*,-typing";
    qt_core_typing_update(&conversation, "hi"); CHECK(calls == 4);
    connection.connected = 0;
    qt_core_typing_update(&conversation, ""); CHECK(calls == 4);
    qt_core_typing_update(NULL, "hi"); CHECK(calls == 4);
    return 0;
}
