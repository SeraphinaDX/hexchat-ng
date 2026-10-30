/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nicklist.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    NickList list;
    bool ok = true;
    auto verify = [&](bool condition, const char *message) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ok = false; }
    };
    auto first = std::make_shared<NickListState>();
    first->users.insert(QStringLiteral("Alice"), QStringLiteral("@Alice"));
    first->users.insert(QStringLiteral("Bob"), QStringLiteral("+Bob"));
    list.setState(first);
    list.item(0)->setSelected(true);
    list.item(1)->setSelected(true);
    verify(first->selected.size() == 2, "multi-selection updates conversation state");
    first->users.insert(QStringLiteral("Carol"), QStringLiteral("Carol"));
    first->users[QStringLiteral("Alice")] = QStringLiteral("+Alice");
    list.refresh();
    verify(list.selectedItems().size() == 2 && first->selected.size() == 2,
           "join and prefix update preserve selection");
    verify(list.item(0)->text() == QStringLiteral("+Alice"), "prefix update displayed");
    QString target;
    list.action = [&](const QString &nick, bool whois) {
        verify(!whois, "activation opens private chat");
        target = nick;
    };
    list.itemActivated(list.item(0));
    verify(target == QStringLiteral("Alice"), "action uses nickname without privilege prefix");
    list.resize(200, 200);
    list.show();
    app.processEvents();
    bool requestedWhois = false;
    list.action = [&](const QString &nick, bool whois) {
        target = nick;
        requestedWhois = whois;
    };
    auto chooseWhois = [&]() {
        auto *menu = list.findChild<QMenu *>();
        if (!menu) { verify(false, "context menu created"); return; }
        menu->setActiveAction(menu->actions().last());
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(menu, &enter);
    };
    QTimer::singleShot(0, &list, chooseWhois);
    list.customContextMenuRequested(list.visualItemRect(list.item(0)).center());
    verify(requestedWhois && target == QStringLiteral("Alice"), "WHOIS menu action");
    target.clear();
    QTimer::singleShot(0, &list, [&]() {
        first->users.remove(QStringLiteral("Alice"));
        first->selected.remove(QStringLiteral("Alice"));
        list.refresh();
        chooseWhois();
    });
    list.customContextMenuRequested(list.visualItemRect(list.item(0)).center());
    verify(target.isEmpty(), "departed user cannot receive stale menu action");
    first->users.insert(QStringLiteral("Alice"), QStringLiteral("Alice"));
    first->selected.insert(QStringLiteral("Alice"));
    list.refresh();
    auto second = std::make_shared<NickListState>();
    second->users = first->users;
    list.setState(second);
    verify(list.selectedItems().isEmpty(), "conversation selections are independent");
    list.selectNick(QStringLiteral("Carol"), true);
    verify(second->selected == QSet<QString>{QStringLiteral("Carol")}, "programmatic selection");
    list.setState(first);
    verify(list.selectedItems().size() == 2, "selection restored after channel switch");
    first->selected.clear();
    list.refresh();
    verify(list.selectedItems().isEmpty(), "programmatic clear");
    list.selectNick(QStringLiteral("Missing"), true);
    verify(first->selected.isEmpty(), "unknown nickname ignored");
    QTimer::singleShot(0, &list, [&]() {
        list.setState(nullptr);
        chooseWhois();
    });
    list.customContextMenuRequested(list.visualItemRect(list.item(0)).center());
    verify(target.isEmpty(), "closed conversation cannot receive stale menu action");
    list.setState(nullptr);
    verify(list.count() == 0, "closing a conversation detaches its user list");
    return ok ? 0 : 1;
}
