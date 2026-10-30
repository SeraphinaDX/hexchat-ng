/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nicklist.h"
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>

NickList::NickList(QWidget *parent) : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setToolTip(QStringLiteral("Double-click a nickname to open a private chat; right-click for actions."));
    connect(this, &QListWidget::itemSelectionChanged, this, [this]() {
        if (!state_) return;
        state_->selected.clear();
        for (auto *item : selectedItems())
            state_->selected.insert(item->data(Qt::UserRole).toString());
    });
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString nick = item->data(Qt::UserRole).toString();
        if (action && state_ && state_->users.contains(nick)) action(nick, false);
    });
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = point.x() < 0 ? currentItem() : itemAt(point);
        if (!item || !state_) return;
        const QString nick = item->data(Qt::UserRole).toString();
        const auto state = state_;
        QMenu menu(this);
        auto *query = menu.addAction(QStringLiteral("Open Private Chat"));
        auto *whois = menu.addAction(QStringLiteral("WHOIS"));
        auto *chosen = menu.exec(point.x() < 0 ? mapToGlobal(rect().center())
                                               : viewport()->mapToGlobal(point));
        // IRC events can remove a user/session while the menu is open.
        if (action && state_ == state && state->users.contains(nick) &&
            (chosen == query || chosen == whois)) action(nick, chosen == whois);
    });
}

void NickList::setState(const std::shared_ptr<NickListState> &state)
{
    state_ = state;
    refresh();
}

void NickList::refresh()
{
    const QSignalBlocker blocker(this);
    const int scroll = verticalScrollBar()->value();
    const QString current = currentItem() ? currentItem()->data(Qt::UserRole).toString() : QString();
    clear();
    if (!state_) return;
    for (auto it = state_->users.cbegin(); it != state_->users.cend(); ++it) {
        auto *item = new QListWidgetItem(it.value(), this);
        item->setData(Qt::UserRole, it.key());
        if (it.key() == current) setCurrentItem(item, QItemSelectionModel::NoUpdate);
        item->setSelected(state_->selected.contains(it.key()));
    }
    verticalScrollBar()->setValue(scroll);
}

void NickList::selectNick(const QString &nick, bool scroll)
{
    if (!state_ || !state_->users.contains(nick)) return;
    state_->selected.insert(nick);
    for (int row = 0; row < count(); ++row) {
        auto *item = this->item(row);
        if (item->data(Qt::UserRole).toString() == nick) {
            item->setSelected(true);
            if (scroll) scrollToItem(item);
            return;
        }
    }
}
