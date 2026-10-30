/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "chatinput.h"
#include <QApplication>
#include <QKeyEvent>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ChatInput input;
    InputHistory first, second;
    bool ok = true;
    auto verify = [&](bool condition, const char *message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ok = false; }
    };
    auto press = [&](int key) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(&input, &event);
    };
    first.record(QStringLiteral("hello"));
    first.record(QStringLiteral("world"));
    first.record(QStringLiteral("world"));
    input.setConversation(&first, {QStringLiteral("Alice"), QStringLiteral("Alfred")});
    input.setText(QStringLiteral("unfinished"));
    press(Qt::Key_Up);
    verify(input.text() == QStringLiteral("world"), "latest history entry");
    press(Qt::Key_Up);
    press(Qt::Key_Up);
    verify(input.text() == QStringLiteral("hello"), "history start and adjacent deduplication");
    press(Qt::Key_Down);
    press(Qt::Key_Down);
    verify(input.text() == QStringLiteral("unfinished"), "restore unsent draft");
    input.setText(QStringLiteral("edited draft"));
    press(Qt::Key_Down);
    verify(input.text() == QStringLiteral("edited draft"), "down at newest preserves edits");
    input.setConversation(&second, {});
    input.setText(QStringLiteral("other draft"));
    press(Qt::Key_Up);
    verify(input.text() == QStringLiteral("other draft"), "independent empty history");
    input.setConversation(&first, {QStringLiteral("Alice"), QStringLiteral("Alfred")});
    press(Qt::Key_Up);
    verify(input.text() == QStringLiteral("world"), "history survives conversation switch");
    input.setText(QStringLiteral("al"));
    press(Qt::Key_Tab);
    verify(input.text() == QStringLiteral("Alfred: "), "case insensitive first-word completion");
    press(Qt::Key_Tab);
    verify(input.text() == QStringLiteral("Alice: "), "cycle matching nicknames");
    press(Qt::Key_Tab);
    verify(input.text() == QStringLiteral("Alfred: "), "completion wraps");
    input.setText(QStringLiteral("hello ali please"));
    input.setCursorPosition(9);
    press(Qt::Key_Tab);
    verify(input.text() == QStringLiteral("hello Alice please"), "complete at cursor preserving suffix");
    input.setNicks({});
    input.setText(QStringLiteral("ali"));
    press(Qt::Key_Tab);
    verify(input.text() == QStringLiteral("ali"), "departed nick is not completed");
    for (int i = 0; i < 150; ++i) second.record(QString::number(i));
    verify(second.lines.size() == 100 && second.lines.first() == QStringLiteral("50"),
           "bounded history");
    return ok ? 0 : 1;
}
