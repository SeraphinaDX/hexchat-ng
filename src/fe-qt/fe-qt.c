/*
 * HexChat-NG: provisional Qt 6 frontend implementation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The C engine continues to own sessions, sockets, and plugin contexts.
 * Functionality not yet in this first milestone is deliberately marked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/hexchat.h"
#include <glib-object.h>
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/userlist.h"
#include "../common/fe.h"
#include "qt-bridge.h"

static int qt_argc;
static char **qt_argv;
static char *option_cfgdir;
static gint show_version;
static gint show_cfgdir;

static GOptionEntry qt_options[] = {
  { "no-auto", 'a', 0, G_OPTION_ARG_NONE, &arg_dont_autoconnect, "Don't auto connect", NULL },
  { "cfgdir", 'd', 0, G_OPTION_ARG_STRING, &option_cfgdir, "Use configuration directory", "PATH" },
  { "no-plugins", 'n', 0, G_OPTION_ARG_NONE, &arg_skip_plugins, "Don't autoload plugins", NULL },
  { "configdir", 'u', 0, G_OPTION_ARG_NONE, &show_cfgdir, "Display configuration directory", NULL },
  { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version, "Display version", NULL },
  { "url", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &arg_url, "IRC URL", "URL" },
  { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, "IRC URLs", "URL" },
  { NULL }
};

int
fe_args(int argc, char *argv[])
{
  GError *error = NULL;
  GOptionContext *context = g_option_context_new(NULL);
  g_option_context_add_main_entries(context, qt_options, GETTEXT_PACKAGE);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("hexchat-qt: %s\n", error->message);
    g_clear_error(&error);
    g_option_context_free(context);
    return 1;
  }
  g_option_context_free(context);
  if (show_version) {
    g_print("%s %s (Qt 6 prototype)\n", PACKAGE_NAME, PACKAGE_VERSION);
    return 0;
  }
  if (option_cfgdir) {
    g_free(xdir);
    xdir = g_strdup(option_cfgdir);
    g_strchomp(xdir);
    g_free(option_cfgdir);
    option_cfgdir = NULL;
  }
  if (show_cfgdir) {
    g_print("%s\n", get_xdir());
    return 0;
  }
  qt_argc = argc;
  qt_argv = argv;
  return -1;
}

void fe_init(void)
{
  /* A first-run session is created even without configured autoconnect. */
  prefs.hex_gui_tab_server = TRUE;
  prefs.hex_gui_slist_skip = TRUE;  /* Qt server editor is a later milestone */
  qt_fe_initialize(&qt_argc, qt_argv);
}
void fe_main(void) { (void)qt_fe_run(); }
void fe_exit(void) { qt_fe_quit(); }
void fe_cleanup(void) { }

void fe_new_window(session *sess, int focus)
{
  if (!sess->server->front_session)
    sess->server->front_session = sess;
  if (!sess->server->server_session)
    sess->server->server_session = sess;
  if (!current_tab || focus)
    current_tab = sess;
  current_sess = current_tab;
  qt_fe_new_session(sess, sess->channel[0] ? sess->channel : "Server", focus);
}
void fe_new_server(server *serv) { (void)serv; }
void fe_close_window(session *sess) { session_free(sess); }
void fe_session_callback(session *sess) { qt_fe_remove_session(sess); }
void fe_server_callback(server *serv) { (void)serv; }

void fe_print_text(session *sess, char *text, time_t stamp, gboolean no_activity)
{
  (void)no_activity;
  qt_fe_print(sess, text, stamp);
}
void fe_text_clear(session *sess, int lines) { (void)lines; qt_fe_clear(sess); }
void fe_clear_channel(session *sess) { qt_fe_clear(sess); }
void fe_set_topic(session *sess, char *topic, char *stripped_topic)
{
  qt_fe_topic(sess, stripped_topic ? stripped_topic : topic);
}
void fe_set_channel(session *sess)
{
  qt_fe_rename_session(sess, sess->channel[0] ? sess->channel : "Server");
}
void fe_set_title(session *sess) { fe_set_channel(sess); }
void fe_set_tab_color(session *sess, tabcolor color)
{
  qt_fe_activity(sess, (int)color);
}
void fe_flash_window(session *sess) { qt_fe_activity(sess, FE_COLOR_NEW_HILIGHT); }
void fe_message(char *text, int flags)
{
  qt_fe_message(text, (flags & (FE_MSG_WARN | FE_MSG_ERROR)) != 0);
}
void fe_beep(session *sess) { (void)sess; g_print("\a"); }

void fe_userlist_insert(session *sess, struct User *user, gboolean select)
{
  if (user) qt_fe_user_add(sess, user->nick, user->prefix, select || user->selected);
}
int fe_userlist_remove(session *sess, struct User *user)
{
  if (!user) return 0;
  user->selected = qt_fe_user_selected(sess, user->nick);
  qt_fe_user_remove(sess, user->nick);
  return user->selected;
}
void fe_userlist_rehash(session *sess, struct User *user)
{
  if (user) qt_fe_user_add(sess, user->nick, user->prefix, FALSE);
}
void fe_userlist_update(session *sess, struct User *user) { fe_userlist_rehash(sess, user); }
void fe_userlist_clear(session *sess) { qt_fe_users_clear(sess); }
void fe_userlist_numbers(session *sess) { (void)sess; }
void fe_userlist_set_selected(session *sess)
{
  GSList *users = userlist_flat_list(sess), *entry;
  for (entry = users; entry; entry = entry->next) {
    struct User *user = entry->data;
    user->selected = qt_fe_user_selected(sess, user->nick);
  }
  g_slist_free(users);
}
void fe_uselect(session *sess, char *word[], int clear, int scroll_to)
{
  int i;
  if (clear) qt_fe_user_select(sess, NULL, TRUE, FALSE);
  for (i = 0; word[i] && *word[i]; ++i) {
    /* Let the IRC engine resolve the server's nickname case mapping. */
    struct User *user = userlist_find(sess, word[i]);
    if (user) qt_fe_user_select(sess, user->nick, FALSE, scroll_to);
  }
}

void fe_ctrl_gui(session *sess, fe_gui_action action, int arg)
{
  if (action == FE_GUI_FOCUS || action == FE_GUI_SHOW) {
    qt_fe_select_session(sess);
  } else if (action == FE_GUI_COLOR) {
    qt_fe_activity(sess, arg);
  } else if (action == FE_GUI_FLASH) {
    fe_flash_window(sess);
  }
}
int fe_gui_info(session *sess, int type)
{ (void)sess; return type == 0 ? (qt_fe_is_active() ? 1 : 0) : -1; }
void *fe_gui_info_ptr(session *sess, int type)
{ (void)sess; (void)type; return NULL; } /* no GTK widget is exposed */

static char *input_cache;
char *fe_get_inputbox_contents(session *sess)
{
  g_free(input_cache);
  input_cache = g_strdup(qt_fe_get_input(sess));
  return input_cache;
}
void fe_set_inputbox_contents(session *sess, char *text) { qt_fe_set_input(sess, text); }
int fe_get_inputbox_cursor(session *sess)
{ return (int)strlen(qt_fe_get_input(sess)); }
void fe_set_inputbox_cursor(session *sess, int delta, int pos)
{ (void)sess; (void)delta; (void)pos; /* TODO: cursor API */ }
void fe_open_url(const char *url) { qt_fe_open_url(url); }

int fe_timeout_add(int interval, void *callback, void *userdata)
{ return (int)g_timeout_add(interval, (GSourceFunc)callback, userdata); }
int fe_timeout_add_seconds(int interval, void *callback, void *userdata)
{ return (int)g_timeout_add_seconds(interval, (GSourceFunc)callback, userdata); }
void fe_timeout_remove(int tag) { if (tag > 0) g_source_remove((guint)tag); }
void fe_idle_add(void *func, void *data) { g_idle_add((GSourceFunc)func, data); }
int fe_input_add(int socket, int flags, void *func, void *data)
{
  GIOChannel *channel;
  GIOCondition conditions = 0;
  guint id;
#ifdef G_OS_WIN32
  channel = (flags & FIA_FD) ?
    g_io_channel_win32_new_fd(socket) : g_io_channel_win32_new_socket(socket);
#else
  channel = g_io_channel_unix_new(socket);
#endif
  if (flags & FIA_READ) conditions |= G_IO_IN | G_IO_HUP | G_IO_ERR;
  if (flags & FIA_WRITE) conditions |= G_IO_OUT | G_IO_ERR;
  if (flags & FIA_EX) conditions |= G_IO_PRI;
  id = g_io_add_watch(channel, conditions, (GIOFunc)func, data);
  g_io_channel_unref(channel);
  return (int)id;
}
void fe_input_remove(int tag) { if (tag > 0) g_source_remove((guint)tag); }

/* Commands and events from Qt go straight through HexChat's existing parser. */
void qt_core_activate(void *ptr)
{
  session *sess = ptr;
  if (!sess || !is_session(sess)) return;
  current_tab = current_sess = sess;
  if (sess->server) sess->server->front_session = sess;
}
void qt_core_submit(void *ptr, const char *line)
{
  session *sess = ptr;
  char *mutable_line;
  if (!sess || !is_session(sess) || !line || !*line) return;
  qt_core_activate(sess);
  mutable_line = g_strdup(line);
  handle_multiline(sess, mutable_line, TRUE, FALSE);
  g_free(mutable_line);
}
void qt_core_quit(void) { if (!hexchat_is_quitting) hexchat_exit(); }

/* These callbacks are intentionally incomplete in milestone one.
 * The Qt UI is a working chat/command prototype, NOT GTK feature parity. */
void fe_add_rawlog(server *serv, char *text, int len, int outbound)
{ (void)serv; (void)text; (void)len; (void)outbound; }
void fe_update_mode_buttons(session *sess, char mode, char sign)
{ (void)sess; (void)mode; (void)sign; }
void fe_update_channel_key(session *sess) { (void)sess; }
void fe_update_channel_limit(session *sess) { (void)sess; }
int fe_is_chanwindow(server *serv) { (void)serv; return FALSE; }
void fe_add_chan_list(server *serv, char *chan, char *users, char *topic)
{ (void)serv; (void)chan; (void)users; (void)topic; }
void fe_chan_list_end(server *serv) { (void)serv; }
gboolean fe_add_ban_list(session *sess, char *mask, char *who, char *when, int rplcode)
{ (void)sess; (void)mask; (void)who; (void)when; (void)rplcode; return FALSE; }
gboolean fe_ban_list_end(session *sess, int code)
{ (void)sess; (void)code; return FALSE; }
void fe_notify_update(char *name) { (void)name; }
void fe_notify_ask(char *name, char *networks) { (void)name; (void)networks; }
void fe_progressbar_start(session *sess) { (void)sess; }
void fe_progressbar_end(server *serv) { (void)serv; }
void fe_dcc_add(struct DCC *dcc) { (void)dcc; }
void fe_dcc_update(struct DCC *dcc) { (void)dcc; }
void fe_dcc_remove(struct DCC *dcc) { (void)dcc; }
int fe_dcc_open_recv_win(int passive) { (void)passive; return FALSE; }
int fe_dcc_open_send_win(int passive) { (void)passive; return FALSE; }
int fe_dcc_open_chat_win(int passive) { (void)passive; return FALSE; }
void fe_url_add(const char *url) { (void)url; }
void fe_pluginlist_update(void) { }
void fe_buttons_update(session *sess) { (void)sess; }
void fe_dlgbuttons_update(session *sess) { (void)sess; }
void fe_dcc_send_filereq(session *sess, char *nick, int maxcps, int passive)
{ (void)sess; (void)nick; (void)maxcps; (void)passive; }
void fe_set_nonchannel(session *sess, int state) { (void)sess; (void)state; }
void fe_set_nick(server *serv, char *nick) { (void)serv; (void)nick; }
void fe_ignore_update(int level) { (void)level; }
void fe_lastlog(session *sess, session *lastlog, char *search, gtk_xtext_search_flags flags)
{ (void)sess; (void)lastlog; (void)search; (void)flags; }
void fe_set_lag(server *serv, long lag) { (void)serv; (void)lag; }
void fe_set_throttle(server *serv) { (void)serv; }
void fe_set_away(server *serv) { (void)serv; }
void fe_serverlist_open(session *sess) { (void)sess; qt_fe_network_manager(); }
void fe_get_bool(char *title, char *prompt, void *callback, void *userdata)
{ (void)title; (void)prompt; (void)callback; (void)userdata; }
void fe_get_str(char *prompt, char *def, void *callback, void *userdata)
{ (void)prompt; (void)def; (void)callback; (void)userdata; }
void fe_get_int(char *prompt, int def, void *callback, void *userdata)
{ (void)prompt; (void)def; (void)callback; (void)userdata; }
void fe_get_file(const char *title, char *initial,
    void (*callback)(void *, char *), void *userdata, int flags)
{
  const char *path = qt_fe_pick_file(title, initial, flags);
  if (callback) callback(userdata, (char *)path);
}
void fe_confirm(const char *message, void (*yesproc)(void *),
                void (*noproc)(void *), void *userdata)
{
  if (qt_fe_confirm(message)) { if (yesproc) yesproc(userdata); }
  else if (noproc) noproc(userdata);
}
void fe_menu_del(menu_entry *entry) { (void)entry; }
char *fe_menu_add(menu_entry *entry) { (void)entry; return NULL; }
void fe_menu_update(menu_entry *entry) { (void)entry; }
void fe_server_event(server *serv, int type, int arg)
{ (void)serv; (void)type; (void)arg; }
void fe_tray_set_flash(const char *a, const char *b, int timeout)
{ (void)a; (void)b; (void)timeout; }
void fe_tray_set_file(const char *file) { (void)file; }
void fe_tray_set_icon(feicon icon) { (void)icon; }
void fe_tray_set_tooltip(const char *text) { (void)text; }
void fe_open_chan_list(server *serv, char *filter, int refresh)
{ (void)refresh; if (serv && serv->p_list_channels) serv->p_list_channels(serv, filter, 1); }
const char *fe_get_default_font(void) { return "Monospace 10"; }
