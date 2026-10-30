/*
 * HexChat-NG Qt frontend - C/Qt bridge.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * No Qt types escape this interface: the existing IRC engine remains C.
 */
#ifndef HEXCHAT_QT_BRIDGE_H
#define HEXCHAT_QT_BRIDGE_H
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
void qt_fe_initialize(int *argc, char **argv);
int qt_fe_run(void);
void qt_fe_quit(void);
void qt_fe_new_session(void *session, const char *name, int focus);
void qt_fe_remove_session(void *session);
void qt_fe_rename_session(void *session, const char *name);
void qt_fe_select_session(void *session);
void qt_fe_print(void *session, const char *text, time_t stamp);
void qt_fe_clear(void *session);
void qt_fe_topic(void *session, const char *topic);
void qt_fe_user_add(void *session, const char *nick, const char *prefix);
void qt_fe_user_remove(void *session, const char *nick);
void qt_fe_users_clear(void *session);
void qt_fe_activity(void *session, int level);
void qt_fe_message(const char *text, int warning);
void qt_fe_set_input(void *session, const char *text);
const char *qt_fe_get_input(void *session);
void qt_fe_open_url(const char *url);
int qt_fe_confirm(const char *message);
const char *qt_fe_pick_file(const char *title, const char *initial, int flags);
int qt_fe_is_active(void);
void qt_fe_network_manager(void);

/* Implemented on the C side; called by Qt controls. */
void qt_core_activate(void *session);
void qt_core_submit(void *session, const char *line);
void qt_core_quit(void);
#ifdef __cplusplus
}
#endif
#endif
