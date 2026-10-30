/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "chatinput.h"
#include <QKeyEvent>

void InputHistory::record(const QString &line)
{
    if (!line.isEmpty() && (lines.isEmpty() || lines.last() != line)) {
        lines.append(line);
        if (lines.size() > 100) lines.removeFirst();
    }
    position = lines.size();
    draft.clear();
}

void ChatInput::resetCompletion()
{
    matches_.clear();
    match_ = -1;
}

void ChatInput::setConversation(InputHistory *history, const QStringList &nicks)
{
    history_ = history;
    nicks_ = nicks;
    resetCompletion();
}

bool ChatInput::event(QEvent *event)
{
    // QWidget normally consumes Tab for focus traversal before keyPressEvent.
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier) {
            keyPressEvent(key);
            return true;
        }
    }
    return QLineEdit::event(event);
}

void ChatInput::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier && history_ &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        resetCompletion();
        if (event->key() == Qt::Key_Up) {
            if (history_->position == history_->lines.size()) history_->draft = text();
            if (history_->position > 0) --history_->position;
        } else if (history_->position < history_->lines.size()) {
            ++history_->position;
        } else {
            return;
        }
        setText(history_->position == history_->lines.size() ? history_->draft
                                                            : history_->lines[history_->position]);
        return;
    }
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        if (match_ < 0 || text() != completionText_) {
            resetCompletion();
            if (hasSelectedText()) return;
            completionStart_ = cursorPosition();
            while (completionStart_ > 0 && !text()[completionStart_ - 1].isSpace())
                --completionStart_;
            completionLength_ = cursorPosition() - completionStart_;
            const QString prefix = text().mid(completionStart_, completionLength_);
            if (prefix.isEmpty()) return;
            for (const QString &nick : nicks_)
                if (nick.startsWith(prefix, Qt::CaseInsensitive)) matches_.append(nick);
            matches_.sort(Qt::CaseInsensitive);
        }
        if (matches_.isEmpty()) return;
        match_ = (match_ + 1) % matches_.size();
        QString replacement = matches_[match_];
        if (completionStart_ == 0) replacement += QStringLiteral(": ");
        QString line = text();
        line.replace(completionStart_, completionLength_, replacement);
        setText(line);
        completionLength_ = replacement.size();
        setCursorPosition(completionStart_ + completionLength_);
        completionText_ = line;
        return;
    }
    resetCompletion();
    QLineEdit::keyPressEvent(event);
}
