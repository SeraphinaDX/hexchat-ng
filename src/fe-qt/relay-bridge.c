/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../common/zoitechat.h"
#include "../common/zoitechatc.h"
#include "../common/server.h"
#include "../common/userlist.h"
#include "../common/inbound.h"
#include "../common/text.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/ignore.h"
#include "qt-bridge.h"
#include "relay-bridge.h"
#include "relayformat.h"
#include <string.h>
static int relay_send(server *serv, char *line, int length) { return qt_relay_send_line(serv, line, length); }
static void relay_connect(server *serv, char *host, int port, int login) { (void)host; (void)port; (void)login; qt_relay_reconnect(serv); }
static void relay_disconnect(session *sess, int sendquit, int err) { (void)sendquit; (void)err; qt_relay_detach(sess->server); }
static int relay_cleanup(server *serv) { serv->connected = FALSE; qt_relay_server_gone(serv); return 0; }
static void relay_reconnect(server *serv, int quit, int err) { (void)quit; (void)err; qt_relay_reconnect(serv); }
static void relay_quit(server *serv, char *reason) { (void)serv; (void)reason; /* Detaching must not QUIT the relay's IRC session. */ }
void *qt_relay_create_server(const char *label, const char *nick)
{
    session *root = new_ircwindow(NULL, NULL, SESS_SERVER, 0);
    if (!is_session(root)) return NULL;
    server *serv = root->server;
    serv->transport_send = relay_send;
    serv->connect = relay_connect; serv->disconnect = relay_disconnect;
    serv->cleanup = relay_cleanup; serv->auto_reconnect = relay_reconnect; serv->p_quit = relay_quit;
    serv->have_echo_message = TRUE; /* The relay streams confirmed outgoing messages. */
    serv->have_message_tags = TRUE;
    g_strlcpy(serv->nick, nick, sizeof(serv->nick)); server_set_name(serv, (char *)label);
    g_free(serv->nick_prefixes); serv->nick_prefixes = g_strdup("~&@%+");
    g_free(serv->nick_modes); serv->nick_modes = g_strdup("qaohv");
    root->type = SESS_SERVER; qt_fe_rename_session(root, label);
    qt_fe_connection_changed();
    return serv;
}
void qt_relay_server_state(void *ptr, const char *nick, int connected)
{
    server *serv = ptr; if (!is_server(serv)) return;
    g_strlcpy(serv->nick, nick, sizeof(serv->nick));
    serv->connected = connected; serv->connecting = FALSE; serv->end_of_motd = connected; serv->lag_sent = 0;
    if (!connected) {
        for (GSList *list = sess_list; list; list = list->next) {
            session *sess = list->data;
            if (sess->server != serv || sess->type != SESS_CHANNEL) continue;
            if (*sess->channel) g_strlcpy(sess->waitchannel, sess->channel, sizeof(sess->waitchannel));
            sess->channel[0] = 0; userlist_clear(sess); qt_fe_rename_session(sess, sess->waitchannel);
        }
    }
    qt_fe_connection_changed();
}
void *qt_relay_target(void *ptr, const char *target, int joined)
{
    server *serv = ptr; if (!is_server(serv)) return NULL;
    if (!*target || !strcmp(target, "*server*")) return serv->server_session;
    const int channel = strchr(serv->chantypes, target[0]) != NULL;
    session *found = NULL;
    for (GSList *list = sess_list; list; list = list->next) {
        session *sess = list->data;
        if (sess->server == serv && sess != serv->server_session &&
            !serv->p_cmp(*sess->channel ? sess->channel : sess->waitchannel, target)) { found = sess; break; }
    }
    if (!found) found = new_ircwindow(serv, (char *)target, channel ? SESS_CHANNEL : SESS_DIALOG, 0);
    if (!is_session(found)) return NULL;
    if (channel) {
        if (joined) { g_strlcpy(found->channel, target, sizeof(found->channel)); found->waitchannel[0] = 0; }
        else { g_strlcpy(found->waitchannel, target, sizeof(found->waitchannel)); found->channel[0] = 0; }
    }
    qt_fe_rename_session(found, target);
    return found;
}
static char *relay_nick_key(const char *nick)
{
    char *key = g_ascii_strdown(nick, -1);
    for (char *p = key; *p; ++p) {
        if (*p == '[') *p = '{'; else if (*p == ']') *p = '}';
        else if (*p == '\\') *p = '|'; else if (*p == '~') *p = '^';
    }
    return key;
}
void qt_relay_users(void *ptr, const char *const *nicks, const char *const *prefixes, int count)
{
    session *sess = ptr; if (!is_session(sess)) return;
    GHashTable *expected = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (int i = 0; i < count; ++i) g_hash_table_insert(expected, relay_nick_key(nicks[i]), (gpointer)prefixes[i]);
    GSList *current = userlist_flat_list(sess);
    for (GSList *list = current; list; list = list->next) {
        struct User *user = list->data;
        char *key = relay_nick_key(user->nick);
        const char *prefix = g_hash_table_lookup(expected, key); g_free(key);
        if (!prefix || user->prefix[0] != prefix[0]) userlist_remove_user(sess, user);
        else {
            const int me = !sess->server->p_cmp(user->nick, sess->server->nick);
            if (user->me != me) { user->me = me; fe_userlist_rehash(sess, user); }
        }
    }
    g_slist_free(current); g_hash_table_destroy(expected);
    const message_tags_data tags = MESSAGE_TAGS_DATA_INIT;
    for (int i = 0; i < count; ++i) if (!userlist_find(sess, nicks[i])) {
        char *name = g_strconcat(prefixes[i], nicks[i], NULL);
        userlist_add(sess, name, NULL, NULL, NULL, &tags); g_free(name);
    }
}
int qt_relay_ignored(void *ptr, const char *source)
{
    session *sess = ptr; return is_session(sess) && ignore_check((char *)source, sess->type == SESS_CHANNEL ? IG_CHAN : IG_PRIV);
}
void qt_relay_topic(void *ptr, const char *topic) { session *sess = ptr; if (is_session(sess)) set_topic(sess, (char *)topic, (char *)topic); }
void qt_relay_message(void *ptr, const char *formatted, time_t stamp, int replay, int unread, int highlight)
{
    session *sess = ptr; if (!is_session(sess)) return;
    if (replay) { fe_print_text(sess, (char *)formatted, stamp, TRUE); return; }
    PrintTextTimeStampNoActivity(sess, (char *)formatted, stamp);
    if (unread) {
        /* fe_set_tab_color owns the state and preserves highlight priority. */
        fe_set_tab_color(sess, sess == current_tab ? FE_COLOR_NONE : highlight ? FE_COLOR_NEW_HILIGHT : FE_COLOR_NEW_MSG | FE_COLOR_FLAG_NOOVERRIDE);
    }
}
void qt_relay_chat_message(void *ptr, const char *kind, const char *nick, const char *text,
                           time_t stamp, int replay, int unread, int highlight)
{
    if (!is_session(ptr)) return;
    char formatted[4096];
    qt_relay_format_message(ptr, kind, nick, text, highlight, formatted, sizeof(formatted));
    if (formatted[0]) qt_relay_message(ptr, formatted, stamp, replay, unread, highlight);
}
void qt_relay_typing(void *ptr, const char *const *nicks, int count)
{
    session *sess = ptr; if (!is_session(sess)) return;
    GSList *users = userlist_flat_list(sess);
    for (GSList *list = users; list; list = list->next) {
        struct User *user = list->data; int active = 0;
        for (int i = 0; i < count; ++i) if (!sess->server->p_cmp(nicks[i], user->nick)) { active = 1; break; }
        fe_set_typing(sess, user->nick, active ? "active" : "done");
    }
    g_slist_free(users);
    // Queries do not have a channel user list.
    if (sess->type == SESS_DIALOG) {
        int active = 0; for (int i = 0; i < count; ++i) if (!sess->server->p_cmp(nicks[i], sess->channel)) active = 1;
        fe_set_typing(sess, sess->channel, active ? "active" : "done");
    }
}
void *qt_relay_session_server(void *ptr) { session *sess = ptr; return is_session(sess) && sess->server->transport_send ? sess->server : NULL; }
