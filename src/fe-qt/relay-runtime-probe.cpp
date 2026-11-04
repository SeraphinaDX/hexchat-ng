/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "zoitechat-plugin.h"
#include "qt-bridge.h"
#include "relaybackend.h"
#include "relay-bridge.h"
#include <QApplication>
#include <QTextBrowser>
#include <QTextDocument>
#include <QMessageBox>
#include <QMainWindow>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static zoitechat_plugin *ph;
static void *conversation;
static int stage, ticks;
static void report(const char *status) {
    FILE *out = std::fopen(std::getenv("QT_RELAY_REPORT"), "w");
    if (out) { std::fprintf(out, "%s stage=%d\n", status, stage); std::fclose(out); }
}
static QString contents(const QString &needle) {
    for (auto *widget : QApplication::allWidgets()) {
        auto *browser = qobject_cast<QTextBrowser *>(widget);
        if (browser && browser->toPlainText().contains(needle)) return browser->toPlainText();
    }
    return {};
}
static int tick(void *) {
    if (++ticks > 800) { report("FAIL timeout"); return 0; }
    const auto scenario = QByteArray(std::getenv("QT_RELAY_SCENARIO"));
    if (scenario == "format") {
        auto *server = qt_relay_create_server("Relay colours", "me");
        auto *channel = qt_relay_target(server, "#colours", 1);
        auto *query = qt_relay_target(server, "peer", 0);
        struct Example { const char *kind, *event, *nick; bool dialog; int highlight; };
        const Example examples[] = {
            {"message", "Channel Message", "peer", false, 0},
            {"message", "Channel Msg Hilight", "peer", false, 1},
            {"message", "Your Message", "me", false, 0},
            {"action", "Channel Action", "peer", false, 0},
            {"action", "Channel Action Hilight", "peer", false, 1},
            {"action", "Your Action", "me", false, 0},
            {"message", "Private Message to Dialog", "peer", true, 0},
            {"action", "Private Action to Dialog", "peer", true, 0},
            {"message", "Your Message", "me", true, 0},
            {"action", "Your Action", "me", true, 0},
            {"notice", "Notice", "peer", true, 0},
            {"notice", "Channel Notice", "peer", false, 0},
        };
        for (int colours : {0, 1}) for (int strip : {0, 1}) for (const auto &example : examples) {
            void *session = example.dialog ? query : channel;
            qt_fe_select_session(session); ph->zoitechat_set_context(ph, static_cast<zoitechat_context *>(session));
            ph->zoitechat_command(ph, colours ? "set text_color_nicks 1" : "set text_color_nicks 0");
            ph->zoitechat_command(ph, strip ? "set text_stripcolor_msg 1" : "set text_stripcolor_msg 0");
            qt_fe_clear(session);
            const char *text = "colour test \00304red\017 and \002bold\002";
            zoitechat_event_attrs attrs{123};
            if (!std::strcmp(example.event, "Channel Notice"))
                ph->zoitechat_emit_print_attrs(ph, &attrs, example.event, example.nick, "#colours", text, nullptr);
            else
                ph->zoitechat_emit_print_attrs(ph, &attrs, example.event, example.nick, text, "", "", nullptr);
            QTextBrowser *chat = nullptr;
            for (auto *widget : QApplication::allWidgets()) {
                auto *browser = qobject_cast<QTextBrowser *>(widget);
                if (browser && browser->toPlainText().contains("colour test")) { chat = browser; break; }
            }
            if (!chat) { report("FAIL direct event output missing"); return 0; }
            const auto expected = chat->document()->toHtml();
            for (int replay : {0, 1}) {
                qt_fe_clear(session);
                qt_relay_chat_message(session, example.kind, example.nick, text, 123, replay, 0, example.highlight);
                if (chat->document()->toHtml() != expected) {
                    std::fprintf(stderr, "DIRECT: %s\nRELAY: %s\n", expected.toUtf8().constData(), chat->document()->toHtml().toUtf8().constData());
                    const auto error = QByteArray("FAIL relay formatting differs: ") + example.event + " colours=" + QByteArray::number(colours) + " strip=" + QByteArray::number(strip) + " replay=" + QByteArray::number(replay);
                    report(error.constData()); return 0;
                }
            }
        }
        report("PASS relay colours match direct IRC for live messages and history"); return 0;
    }
    if (scenario == "encrypted" && ticks == 1) {
        QWidget *owner = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (qobject_cast<QMainWindow *>(widget)) { owner = widget; break; }
        if (!owner) { report("FAIL missing main window"); return 0; }
        showRelayManager(owner);
        auto *dialog = owner->findChild<QDialog *>("relayManager");
        auto *passphrase = dialog ? dialog->findChild<QLineEdit *>("relayPassphrase") : nullptr;
        if (!passphrase || passphrase->isHidden()) { report("FAIL encrypted key has no passphrase input"); return 0; }
        passphrase->setText("relay-test-passphrase");
        dialog->findChild<QPushButton *>("relayConnect")->click(); dialog->hide();
    }
    if (scenario == "trust" || scenario == "reject") {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *question = qobject_cast<QMessageBox *>(widget);
            if (question && question->windowTitle() == QStringLiteral("Verify AnsiChat relay")) {
                if (!question->text().contains("SHA256:")) { report("FAIL trust prompt fingerprint"); return 0; }
                question->done(scenario == "trust" ? QMessageBox::Yes : QMessageBox::Cancel);
            }
        }
    }
    if (scenario != "normal" && scenario != "trust" && scenario != "actual" && scenario != "encrypted") {
        auto *root = ph->zoitechat_find_context(ph, "Relay · Local", nullptr);
        if (root) qt_fe_select_session(root);
        const auto text = contents(scenario == "mismatch" ? "host key mismatch" : scenario == "reject" ? "not trusted" : "unsupported protocol");
        if (!text.isEmpty()) { report("PASS"); return 0; }
        return 1;
    }
    if (!stage) {
        auto *channel = ph->zoitechat_find_context(ph, nullptr, "#relay"); QtSessionInfo info{};
        if (!channel || !qt_core_session_info(channel, &info) || !info.connected || !info.channel) return 1;
        ph->zoitechat_set_context(ph, channel);
        const char *topic = ph->zoitechat_get_info(ph, "topic");
        if (!topic || std::strcmp(topic, "relay topic <b>literal</b>")) { report("FAIL snapshot topic"); return 0; }
        auto *users = ph->zoitechat_list_get(ph, "users"); bool peer = false;
        if (users) {
            while (ph->zoitechat_list_next(ph, users)) {
                const char *nick = ph->zoitechat_list_str(ph, users, "nick"); const char *prefix = ph->zoitechat_list_str(ph, users, "prefix");
                if (nick && !std::strcmp(nick, "peer") && prefix && prefix[0] == '@') peer = true;
            }
            ph->zoitechat_list_free(ph, users);
        }
        if (!peer) { report("FAIL snapshot nick list"); return 0; }
        conversation = channel; qt_fe_select_session(channel);
        if (!contents("retained history").contains("live from relay")) return 1;
        qt_core_submit_literal(channel, "message through relay  ");
        qt_core_submit_command(channel, "me waves");
        qt_core_submit_command(channel, "join #extra");
        qt_core_typing_update(channel, "draft survives reconnect");
        qt_fe_set_input(channel, "draft survives reconnect");
        if (const char *port = std::getenv("QT_RELAY_DIRECT_PORT")) {
            char command[128]; std::snprintf(command, sizeof(command), "server -insecure 127.0.0.1 %s", port);
            qt_core_submit_command(channel, command);
            auto *direct = ph->zoitechat_find_context(ph, "127.0.0.1", nullptr);
            if (!direct || qt_relay_session_server(direct) || !qt_relay_session_server(channel)) { report("FAIL direct/relay transport isolation"); return 0; }
            qt_fe_select_session(channel);
        }
        stage = 1;
    } else if (stage == 1 && scenario == "actual") {
        auto *extra = ph->zoitechat_find_context(ph, nullptr, "#extra"); QtSessionInfo info{};
        const auto text = contents("message through relay");
        if (!extra || !qt_core_session_info(extra, &info) || !info.channel || text.isEmpty()) return 1;
        if (text.count("message through relay") != 1 || text.count("retained history") != 1) { report("FAIL actual relay message duplication"); return 0; }
        relayBackend()->disconnectProfile("fixture"); stage = 3;
    } else if (stage == 1) {
        QtSessionInfo info{};
        if (!qt_core_session_info(conversation, &info) || info.connected) return 1;
        stage = 2;
    } else if (stage == 2) {
        QtSessionInfo info{};
        const auto text = contents("after reconnect");
        if (!qt_core_session_info(conversation, &info) || !info.connected || text.isEmpty()) return 1;
        if (text.count("retained history") != 1 || text.count("live from relay") != 1 || text.count("message through relay") != 1) { report("FAIL replay duplicated messages"); return 0; }
        if (std::strcmp(qt_fe_get_input(conversation), "draft survives reconnect")) { report("FAIL draft lost on reconnect"); return 0; }
        auto *extra = ph->zoitechat_find_context(ph, nullptr, "#extra");
        if (!extra || !qt_core_session_info(extra, &info) || !info.channel) { report("FAIL relay join"); return 0; }
        auto *backend = relayBackend(); if (!backend) { report("FAIL missing backend"); return 0; }
        backend->disconnectProfile("fixture"); stage = 3;
    } else if (stage == 3) {
        QtSessionInfo info{};
        if (!qt_core_session_info(conversation, &info) || info.connected || info.channel) return 1;
        report("PASS"); return 0;
    }
    return 1;
}
extern "C" int zoitechat_plugin_init(zoitechat_plugin *handle, char **name, char **description, char **version, char *) {
    ph = handle; *name = const_cast<char *>("Relay runtime probe"); *description = const_cast<char *>("test"); *version = const_cast<char *>("1");
    ph->zoitechat_hook_timer(ph, 25, tick, nullptr); return 1;
}
