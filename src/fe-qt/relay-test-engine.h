/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "relay-bridge.h"
extern "C" {
void *qt_relay_create_server(const char *, const char *) { return nullptr; }
void qt_relay_server_state(void *, const char *, int) {}
void *qt_relay_target(void *, const char *, int) { return nullptr; }
void qt_relay_users(void *, const char *const *, const char *const *, int) {}
int qt_relay_ignored(void *, const char *) { return 0; }
void qt_relay_topic(void *, const char *) {}
void qt_relay_message(void *, const char *, time_t, int, int, int) {}
void qt_relay_chat_message(void *, const char *, const char *, const char *, time_t, int, int, int) {}
void qt_relay_typing(void *, const char *const *, int) {}
void *qt_relay_session_server(void *) { return nullptr; }
}
