/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* Format without emitting events, sounds, notifications or IRC traffic. */
void qt_relay_format_message(void *session, const char *kind, const char *nick,
                             const char *text, int highlight, char *output, int capacity);
