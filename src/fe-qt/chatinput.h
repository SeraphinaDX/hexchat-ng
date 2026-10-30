/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HEXCHAT_QT_CHATINPUT_H
#define HEXCHAT_QT_CHATINPUT_H
#include <QLineEdit>
#include <QStringList>

// State belongs to a conversation, so switching channels preserves history.
struct InputHistory {
    QStringList lines;
    QString draft;
    int position = 0;
    void record(const QString &line);
};

class ChatInput final : public QLineEdit {
public:
    explicit ChatInput(QWidget *parent = nullptr) : QLineEdit(parent) {}
    void setConversation(InputHistory *history, const QStringList &nicks);
    void setNicks(const QStringList &nicks) { nicks_ = nicks; resetCompletion(); }
protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void resetCompletion();
    InputHistory *history_ = nullptr;
    QStringList nicks_, matches_;
    QString completionText_;
    int completionStart_ = 0, completionLength_ = 0, match_ = -1;
};
#endif
