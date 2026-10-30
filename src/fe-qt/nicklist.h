/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HEXCHAT_QT_NICKLIST_H
#define HEXCHAT_QT_NICKLIST_H
#include <QListWidget>
#include <QMap>
#include <QSet>
#include <functional>
#include <memory>

struct NickListState {
    QMap<QString, QString> users;
    QSet<QString> selected;
};

class NickList final : public QListWidget {
public:
    explicit NickList(QWidget *parent = nullptr);
    void setState(const std::shared_ptr<NickListState> &state);
    void refresh();
    void selectNick(const QString &nick, bool scroll);
    // false opens a private conversation, true requests WHOIS information.
    std::function<void(const QString &, bool)> action;
private:
    std::shared_ptr<NickListState> state_;
};
#endif
