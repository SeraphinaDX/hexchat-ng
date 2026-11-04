/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
void *qt_relay_create_server(const char *label, const char *nick);
void qt_relay_server_state(void *server, const char *nick, int connected);
void *qt_relay_target(void *server, const char *target, int joined);
void qt_relay_users(void *session, const char *const *nicks, const char *const *prefixes, int count);
int qt_relay_ignored(void *session, const char *source);
void qt_relay_topic(void *session, const char *topic);
void qt_relay_message(void *session, const char *formatted, time_t timestamp, int replay, int unread, int highlight);
void qt_relay_typing(void *session, const char *const *nicks, int count);
void *qt_relay_session_server(void *session);
/* Qt backend callbacks; never run on the SSH worker. */
int qt_relay_send_line(void *server, const char *line, int length);
void qt_relay_detach(void *server);
void qt_relay_reconnect(void *server);
void qt_relay_server_gone(void *server);
void qt_relay_read(void *session);
#ifdef __cplusplus
}
#endif
