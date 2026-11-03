/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ZOITECHAT_CTCP_ECHO_H
#define ZOITECHAT_CTCP_ECHO_H
#include "zoitechat.h"
void ctcp_echo_reset(server *serv, int notice);
gboolean ctcp_echo_duplicate(server *serv, const char *command, const char *nick,
                            const char *target, const char *message, gint64 now);
#endif
