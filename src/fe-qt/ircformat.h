/*
 * IRC formatting support for the HexChat-NG Qt frontend.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HEXCHAT_QT_IRCFORMAT_H
#define HEXCHAT_QT_IRCFORMAT_H

#include <QPalette>
#include <QString>
#include <QTextCharFormat>
#include <QVector>
#include <ctime>

class QTextDocument;

struct IrcRun {
    QString text;
    QTextCharFormat format;
};

/* Parse mIRC formatting controls. Text is never treated as HTML. */
QVector<IrcRun> parseIrcText(const QString &text, const QPalette &palette);

/* Append to a per-session QTextDocument, with clickable links and local time. */
void appendIrcMessage(QTextDocument *document, const QString &raw,
                      std::time_t timestamp, const QPalette &palette);

#endif
