/*
 * HexChat-NG Qt 6 prototype. The engine and plugin ABI remain C.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qt-bridge.h"
#include "ircformat.h"
#include "networkdialog.h"
#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextBrowser>
#include <QTextDocument>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <glib.h>

namespace {
QString plainIrcText(const char *text)
{
    QString result;
    const QPalette palette;
    for (const IrcRun &run : parseIrcText(QString::fromUtf8(text ? text : ""), palette))
        result += run.text;
    return result.trimmed();
}

struct Conversation {
    QString name;
    QString topic;
    QTextDocument *document = nullptr;
    int scrollPosition = -1;
    QMap<QString, QString> nicks;
    QString draft;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("HexChat-NG — Qt 6"));
        resize(1080, 700);

        auto *center = new QWidget(this);
        auto *layout = new QVBoxLayout(center);
        layout->setContentsMargins(5, 5, 5, 5);
        auto *split = new QSplitter(Qt::Horizontal, center);
        channels_ = new QTreeWidget(split);
        channels_->setHeaderHidden(true);
        channels_->setMinimumWidth(130);
        channels_->setMaximumWidth(340);
        auto *middle = new QWidget(split);
        auto *middleLayout = new QVBoxLayout(middle);
        middleLayout->setContentsMargins(0, 0, 0, 0);
        topic_ = new QLabel(middle);
        topic_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        topic_->setWordWrap(true);
        chat_ = new QTextBrowser(middle);
        chat_->setReadOnly(true);
        chat_->setOpenLinks(false);
        chat_->setOpenExternalLinks(false);
        chat_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        chat_->setUndoRedoEnabled(false);
        connect(chat_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
            const QString scheme = url.scheme().toLower();
            if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
                scheme == QStringLiteral("irc") || scheme == QStringLiteral("ircs"))
                QDesktopServices::openUrl(url);
        });
        middleLayout->addWidget(topic_);
        middleLayout->addWidget(chat_, 1);
        input_ = new QLineEdit(middle);
        input_->setPlaceholderText(QStringLiteral("Message or /command"));
        middleLayout->addWidget(input_);
        nicks_ = new QListWidget(split);
        nicks_->setMinimumWidth(100);
        nicks_->setMaximumWidth(300);
        split->setStretchFactor(0, 0);
        split->setStretchFactor(1, 1);
        split->setStretchFactor(2, 0);
        split->setSizes({175, 720, 170});
        layout->addWidget(split);
        setCentralWidget(center);

        auto *serverMenu = menuBar()->addMenu(QStringLiteral("&Server"));
        serverMenu->addAction(QStringLiteral("&Network Manager…"), this, [this]() {
            showNetworkManager(this);
        });
        serverMenu->addSeparator();
        serverMenu->addAction(QStringLiteral("&Quick Connect…"), this, [this]() {
            bool ok = false;
            const QString host = QInputDialog::getText(
                this, QStringLiteral("Connect to IRC"),
                QStringLiteral("Server hostname (optionally :port):"),
                QLineEdit::Normal, QStringLiteral("irc.libera.chat"), &ok).trimmed();
            if (ok && !host.isEmpty()) submit(QStringLiteral("/server ") + host);
        });
        serverMenu->addAction(QStringLiteral("&Join Channel…"), this, [this]() {
            bool ok = false;
            const QString channel = QInputDialog::getText(
                this, QStringLiteral("Join Channel"), QStringLiteral("Channel:"),
                QLineEdit::Normal, QStringLiteral("#"), &ok).trimmed();
            if (ok && !channel.isEmpty()) submit(QStringLiteral("/join ") + channel);
        });
        serverMenu->addSeparator();
        serverMenu->addAction(QStringLiteral("&Quit"), this, [this]() { close(); });

        connect(input_, &QLineEdit::returnPressed, this, [this]() {
            const QString line = input_->text();
            if (line.isEmpty() || !active_) return;
            input_->clear();
            const QByteArray bytes = line.toUtf8();
            qt_core_submit(active_, bytes.constData());
        });
        connect(channels_, &QTreeWidget::itemSelectionChanged, this, [this]() {
            auto *item = channels_->currentItem();
            if (!item) return;
            void *key = nullptr;
            for (auto it = items_.cbegin(); it != items_.cend(); ++it) {
                if (it.value() == item) { key = it.key(); break; }
            }
            if (key) select(key);
        });
        statusBar()->showMessage(QStringLiteral(
            "Qt 6 prototype — use Server → Connect, then Join Channel"));
        // On Linux Qt's GLib event dispatcher runs the default GMainContext
        // directly, including HexChat's sockets, timers and plugin sources.
        // Only keep the old bounded timer fallback for Qt builds that do not
        // ship the GLib dispatcher (notably Windows).
        auto *dispatcher = QAbstractEventDispatcher::instance();
        if (dispatcher && dispatcher->inherits("QEventDispatcherGlib")) {
            qInfo("HexChat-NG Qt: GLib-backed Qt event loop active");
        } else {
            qWarning("HexChat-NG Qt: non-GLib event dispatcher; using fallback");
            auto *pump = new QTimer(this);
            connect(pump, &QTimer::timeout, this, []() {
                for (int i = 0; i < 32 && g_main_context_pending(nullptr); ++i)
                    g_main_context_iteration(nullptr, FALSE);
            });
            pump->start(15);
        }
    }

    void addSession(void *key, const QString &name, bool focus)
    {
        if (conversations_.contains(key)) return;
        Conversation conversation;
        conversation.name = name;
        conversation.document = new QTextDocument(this);
        conversation.document->setDefaultFont(chat_->font());
        conversation.document->setMaximumBlockCount(5000);
        conversations_.insert(key, conversation);
        auto *item = new QTreeWidgetItem(channels_);
        item->setText(0, name);
        items_.insert(key, item);
        if (!active_ || focus) {
            channels_->setCurrentItem(item);
            select(key);
        }
    }

    void removeSession(void *key)
    {
        // A QTextBrowser must not retain a document after that session dies.
        if (active_ == key)
            chat_->setDocument(new QTextDocument(chat_));
        auto it = items_.find(key);
        if (it != items_.end()) {
            QTreeWidgetItem *item = it.value();
            items_.erase(it);
            delete item;
        }
        QTextDocument *oldDocument = conversations_.value(key).document;
        conversations_.remove(key);
        delete oldDocument;
        if (active_ == key) {
            active_ = nullptr;
            if (!items_.isEmpty()) select(items_.cbegin().key());
            else { chat_->clear(); nicks_->clear(); topic_->clear(); input_->clear(); }
        }
    }

    void renameSession(void *key, const QString &name)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].name = name;
        if (items_.contains(key)) items_[key]->setText(0, name);
        if (active_ == key) updateTitle();
    }

    void select(void *key)
    {
        if (!conversations_.contains(key)) return;
        if (active_ == key) {
            qt_core_activate(key);
            return;
        }
        if (active_ && conversations_.contains(active_)) {
            conversations_[active_].draft = input_->text();
            conversations_[active_].scrollPosition = chat_->verticalScrollBar()->value();
        }
        active_ = key;
        qt_core_activate(key);
        auto &conversation = conversations_[key];
        chat_->setDocument(conversation.document);
        QScrollBar *scroll = chat_->verticalScrollBar();
        scroll->setValue(conversation.scrollPosition < 0 ? scroll->maximum()
                         : qMin(conversation.scrollPosition, scroll->maximum()));
        topic_->setText(conversation.topic);
        input_->setText(conversation.draft);
        refreshUsers();
        updateTitle();
        auto *item = items_.value(key, nullptr);
        if (item && channels_->currentItem() != item)
            channels_->setCurrentItem(item);
        if (item) item->setForeground(0, palette().text());
        input_->setFocus();
    }

    void print(void *key, const QString &message, std::time_t timestamp)
    {
        if (!conversations_.contains(key)) return;
        QScrollBar *scroll = chat_->verticalScrollBar();
        const bool wasAtBottom = (key == active_) &&
            (scroll->maximum() - scroll->value() <= 20);
        appendIrcMessage(conversations_[key].document, message, timestamp, chat_->palette());
        if (wasAtBottom) scroll->setValue(scroll->maximum());
    }

    void clear(void *key)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].document->clear();
        conversations_[key].scrollPosition = -1;
    }

    void topic(void *key, const QString &topic)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].topic = topic;
        if (key == active_) topic_->setText(topic);
    }

    void addUser(void *key, const QString &nick, const QString &prefix)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].nicks[nick] = prefix + nick;
        if (key == active_) refreshUsers();
    }

    void removeUser(void *key, const QString &nick)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].nicks.remove(nick);
        if (key == active_) refreshUsers();
    }

    void clearUsers(void *key)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].nicks.clear();
        if (key == active_) nicks_->clear();
    }

    void activity(void *key, int level)
    {
        if (key == active_ || !items_.contains(key)) return;
        auto *item = items_.value(key);
        if (level == 3) item->setForeground(0, QColor(206, 83, 83));
        else if (level == 2) item->setForeground(0, QColor(65, 135, 210));
        else if (level == 0) item->setForeground(0, palette().text());
    }

    const char *inputText(void *key)
    {
        if (!conversations_.contains(key)) return "";
        inputBytes_ = (key == active_ ? input_->text() : conversations_[key].draft).toUtf8();
        return inputBytes_.constData();
    }

    void setInput(void *key, const QString &text)
    {
        if (!conversations_.contains(key)) return;
        conversations_[key].draft = text;
        if (key == active_) input_->setText(text);
    }

    void notify(const QString &text, bool warning)
    {
        statusBar()->showMessage(text, 12000);
        if (warning) QMessageBox::warning(this, QStringLiteral("HexChat-NG"), text);
        else if (active_) print(active_, text, 0);
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!closing_) {
            closing_ = true;
            qt_core_quit();
        }
        event->accept();
    }

private:
    void submit(const QString &line)
    {
        if (!active_) { statusBar()->showMessage(QStringLiteral("No active session")); return; }
        const QByteArray bytes = line.toUtf8();
        qt_core_submit(active_, bytes.constData());
    }
    void refreshUsers()
    {
        nicks_->clear();
        if (!conversations_.contains(active_)) return;
        nicks_->addItems(conversations_[active_].nicks.values());
    }
    void updateTitle()
    {
        setWindowTitle(QStringLiteral("HexChat-NG — ") +
            (active_ ? conversations_.value(active_).name : QStringLiteral("Qt 6")));
    }

    QTreeWidget *channels_ = nullptr;
    QTextBrowser *chat_ = nullptr;
    QLineEdit *input_ = nullptr;
    QListWidget *nicks_ = nullptr;
    QLabel *topic_ = nullptr;
    QHash<void *, Conversation> conversations_;
    QHash<void *, QTreeWidgetItem *> items_;
    void *active_ = nullptr;
    QByteArray inputBytes_;
    bool closing_ = false;
};

QApplication *app = nullptr;
MainWindow *window = nullptr;
QByteArray pickedFile;

MainWindow *ui()
{
    if (!window) {
        window = new MainWindow();
        window->show();
    }
    return window;
}
} // namespace

extern "C" {
void qt_fe_initialize(int *argc, char **argv)
{
    if (!app) {
        app = new QApplication(*argc, argv);
        app->setApplicationName(QStringLiteral("hexchat-qt"));
        app->setOrganizationName(QStringLiteral("HexChat-NG"));
    }
    (void)ui();
}
int qt_fe_run(void) { return app ? app->exec() : 1; }
void qt_fe_quit(void) { if (app) app->quit(); }
void qt_fe_network_manager(void) { showNetworkManager(ui()); }
void qt_fe_new_session(void *sess, const char *name, int focus)
{ ui()->addSession(sess, QString::fromUtf8(name ? name : "Server"), focus != 0); }
void qt_fe_remove_session(void *sess) { if (window) window->removeSession(sess); }
void qt_fe_rename_session(void *sess, const char *name)
{ if (window) window->renameSession(sess, QString::fromUtf8(name ? name : "Server")); }
void qt_fe_select_session(void *sess) { if (window) window->select(sess); }
void qt_fe_print(void *sess, const char *text, time_t stamp)
{
    if (window && text && *text)
        window->print(sess, QString::fromUtf8(text), stamp);
}
void qt_fe_clear(void *sess) { if (window) window->clear(sess); }
void qt_fe_topic(void *sess, const char *topic)
{ if (window) window->topic(sess, plainIrcText(topic)); }
void qt_fe_user_add(void *sess, const char *nick, const char *prefix)
{ if (window) window->addUser(sess, QString::fromUtf8(nick ? nick : ""),
                              QString::fromUtf8(prefix ? prefix : "")); }
void qt_fe_user_remove(void *sess, const char *nick)
{ if (window) window->removeUser(sess, QString::fromUtf8(nick ? nick : "")); }
void qt_fe_users_clear(void *sess) { if (window) window->clearUsers(sess); }
void qt_fe_activity(void *sess, int level) { if (window) window->activity(sess, level); }
void qt_fe_message(const char *text, int warning)
{ ui()->notify(QString::fromUtf8(text ? text : ""), warning != 0); }
void qt_fe_set_input(void *sess, const char *text)
{ if (window) window->setInput(sess, QString::fromUtf8(text ? text : "")); }
const char *qt_fe_get_input(void *sess) { return window ? window->inputText(sess) : ""; }
void qt_fe_open_url(const char *url)
{ if (url) QDesktopServices::openUrl(QUrl::fromUserInput(QString::fromUtf8(url))); }
int qt_fe_confirm(const char *message)
{
    return QMessageBox::question(ui(), QStringLiteral("HexChat-NG"),
        QString::fromUtf8(message ? message : "")) == QMessageBox::Yes;
}
const char *qt_fe_pick_file(const char *title, const char *initial, int flags)
{
    const QString caption = QString::fromUtf8(title ? title : "Choose file");
    const QString start = QString::fromUtf8(initial ? initial : "");
    QString chosen;
    if (flags & 8) chosen = QFileDialog::getExistingDirectory(ui(), caption, start);
    else if (flags & 1) chosen = QFileDialog::getSaveFileName(ui(), caption, start);
    else chosen = QFileDialog::getOpenFileName(ui(), caption, start);
    pickedFile = chosen.toUtf8();
    return chosen.isEmpty() ? nullptr : pickedFile.constData();
}
int qt_fe_is_active(void) { return window && window->isActiveWindow() ? 1 : 0; }
}
