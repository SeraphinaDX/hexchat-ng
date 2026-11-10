/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relaybackend.h"
#include "relaykeys.h"
#include "relay-bridge.h"
#include "qt-bridge.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <algorithm>
namespace {
QPointer<RelayBackend> instance;
QJsonObject encode(const RelayProfile &p) {
    return {{"id", p.id}, {"name", p.name}, {"host", p.endpoint.host}, {"port", p.endpoint.port}, {"user", p.endpoint.user},
        {"key", p.endpoint.keyPath}, {"fingerprint", p.endpoint.fingerprint}, {"autoconnect", p.autoconnect}, {"reconnect", p.reconnect}};
}
RelayProfile decode(const QJsonObject &o) {
    RelayProfile p; p.id = o.value("id").toString(); p.name = o.value("name").toString();
    p.endpoint.host = o.value("host").toString(); p.endpoint.port = o.value("port").toInt(2222);
    p.endpoint.user = o.value("user").toString("copperline"); p.endpoint.keyPath = o.value("key").toString();
    p.endpoint.fingerprint = o.value("fingerprint").toString(); p.autoconnect = o.value("autoconnect").toBool(); p.reconnect = o.value("reconnect").toBool(true); return p;
}
QString targetKey(const QString &s) { return s.toCaseFolded().replace('[', '{').replace(']', '}').replace('\\', '|').replace('~', '^'); }
bool channelTarget(const QString &s) { return !s.isEmpty() && QStringLiteral("#&!+").contains(s.front()); }
}
struct RelayBackend::Connection {
    struct Remote {
        void *server = nullptr; QString nick;
        QHash<QString, void *> targets;
        QSet<QString> hidden;
        QHash<QString, QString> lastIDs, targetNames;
        QHash<QString, bool> joined;
        bool connected = false;
    };
    RelayProfile profile;
    RelayTransport *worker = nullptr;
    QString status;
    bool wanted = false, ready = false, hello = false, everReady = false, restartPending = false, fatal = false;
    int retry = 0;
    quint64 nextID = 1;
    qint64 started = 0, lastPing = 0;
    QHash<quint64, QPair<qint64, QString>> pending;
    QMap<QString, Remote> remotes;
    QSet<QString> hiddenServers, seenIDs;
    QQueue<QString> seenOrder;
    QJsonArray initialHistory;
    QByteArray passphrase;
};
RelayBackend::RelayBackend(QString directory, QWidget *owner) : QObject(qApp), directory_(std::move(directory)), owner_(owner)
{
    QFile file(QDir(directory_).filePath("ansichat-relays.json"));
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) loadError_ = "Could not read ansichat-relays.json. The existing file has been preserved.";
        else {
            QJsonParseError error; const auto document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isArray()) loadError_ = "Invalid ansichat-relays.json. Correct the file before saving profiles; its contents have been preserved.";
            else for (const auto &value : document.array()) {
                auto profile = decode(value.toObject());
                if (!profile.id.isEmpty() && !profile.name.isEmpty()) profiles_[profile.id] = profile;
            }
        }
    }
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (auto *connection : connections_) {
            if (!connection->worker || !connection->wanted) continue;
            if (!connection->ready && now - connection->started > 20000) {
                setStatus(connection, "The relay did not complete its handshake in time."); connection->worker->requestInterruption(); continue;
            }
            bool timedOut = false;
            for (auto it = connection->pending.cbegin(); it != connection->pending.cend(); ++it) if (now - it->first > 15000) { timedOut = true; break; }
            if (timedOut) { setStatus(connection, "The relay stopped responding; reconnecting."); connection->worker->requestInterruption(); continue; }
            if (connection->ready && now - connection->lastPing >= 5000) { request(connection, {{"action", "ping"}}); connection->lastPing = now; }
        }
    }); timer->start(500);
}
RelayBackend::~RelayBackend() {
    shutdown();
    for (auto *connection : connections_) { if (connection->worker) { connection->worker->wait(); delete connection->worker; } connection->passphrase.fill(0); delete connection; }
}
bool RelayBackend::persist(QString *error) {
    QJsonArray profiles; for (const auto &profile : profiles_) profiles.append(encode(profile));
    QDir().mkpath(directory_); QSaveFile file(QDir(directory_).filePath("ansichat-relays.json"));
    const auto bytes = QJsonDocument(profiles).toJson();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    } return true;
}
bool RelayBackend::saveProfile(RelayProfile profile, QString *error) {
    if (!loadError_.isEmpty()) { if (error) *error = loadError_; return false; }
    profile.name = profile.name.trimmed(); profile.endpoint.host = profile.endpoint.host.trimmed(); profile.endpoint.user = profile.endpoint.user.trimmed();
    profile.endpoint.fingerprint = profile.endpoint.fingerprint.trimmed(); profile.endpoint.passphrase.clear();
    if (profile.name.isEmpty() || profile.endpoint.user.isEmpty() || profile.endpoint.port < 1 || profile.endpoint.port > 65535 ||
        profile.endpoint.host.contains(QChar::Null) || profile.endpoint.user.contains(QChar::Null)) {
        if (error) *error = "Enter a name, a valid port and an SSH username.";
        return false;
    }
    if (!profile.endpoint.fingerprint.isEmpty() && !profile.endpoint.fingerprint.startsWith("SHA256:")) { if (error) *error = "Use the server's SHA256 SSH host-key fingerprint."; return false; }
    if (profile.id.isEmpty()) profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto previous = profiles_;
    profiles_[profile.id] = profile;
    if (!persist(error)) { profiles_ = previous; return false; }
    return true;
}
bool RelayBackend::removeProfile(const QString &id, QString *error) {
    auto *connection = connections_.value(id);
    if (connection && (!connection->remotes.isEmpty() || connection->worker)) { if (error) *error = "Disconnect and close this relay's tabs before removing its profile. Your private key will be retained."; return false; }
    const auto previous = profiles_; profiles_.remove(id);
    if (!persist(error)) { profiles_ = previous; return false; }
    return true;
}
QString RelayBackend::status(const QString &id) const { auto *c = connections_.value(id); return c ? c->status : !loadError_.isEmpty() ? loadError_ : QStringLiteral("Not connected"); }
bool RelayBackend::connectProfile(const QString &id, const QByteArray &passphrase) {
    if (shuttingDown_ || !profiles_.contains(id)) return false;
    auto *&connection = connections_[id]; if (!connection) connection = new Connection;
    if (connection->worker) { setStatus(connection, "This relay is already connected or connecting. Disconnect before changing its settings."); return false; }
    connection->profile = profiles_[id]; connection->passphrase = passphrase;
    if (connection->profile.endpoint.host.trimmed().isEmpty()) {
        connection->wanted = false; setStatus(connection, "Enter the relay hostname before connecting."); return false;
    }
    connection->wanted = true; connection->hiddenServers.clear();
    if (!connection->remotes.contains(QString())) {
        Connection::Remote root; root.server = qt_relay_create_server(("Relay · " + connection->profile.name).toUtf8().constData(), "");
        connection->remotes.insert(QString(), root);
    }
    begin(connection); return true;
}
void RelayBackend::begin(Connection *c) {
    if (!c->wanted || c->worker || shuttingDown_) return;
    c->profile = profiles_.value(c->profile.id, c->profile);
    c->hello = c->ready = false; c->initialHistory = {}; c->pending.clear(); c->started = QDateTime::currentMSecsSinceEpoch();
    RelayEndpoint endpoint = c->profile.endpoint; endpoint.passphrase = c->passphrase;
    auto *worker = new RelayTransport(endpoint, this); worker->setParent(this); c->worker = worker; c->fatal = false;
    worker->frameReceived = [this, c, worker](const QJsonObject &f) { if (c->worker == worker && c->wanted) frame(c, f); };
    worker->failed = [this, c, worker](const QString &text) { if (c->worker == worker) {
        c->fatal = text.contains("host key mismatch", Qt::CaseInsensitive) || text.contains("authentication failed", Qt::CaseInsensitive) || text.contains("refused the AnsiChat", Qt::CaseInsensitive);
        setStatus(c, text);
    } };
    worker->hostKeyRequired = [this, c, worker](const QString &fingerprint) {
        if (c->worker != worker || !c->wanted || shuttingDown_) return;
        c->fatal = true; // Wait for a trust decision; do not retry behind the prompt.
        QTimer::singleShot(0, this, [this, c, fingerprint]() {
            if (!c->wanted || shuttingDown_) return;
            QPointer<RelayBackend> guard(this);
            QPointer<QMessageBox> question = new QMessageBox(owner_); question->setWindowTitle("Verify AnsiChat relay"); question->setTextFormat(Qt::PlainText);
            question->setText(QStringLiteral("The relay at %1:%2 presents this SSH host key:\n\n%3\n\nCompare it with the fingerprint shown by your relay server. Trust this server and save its fingerprint?").arg(c->profile.endpoint.host).arg(c->profile.endpoint.port).arg(fingerprint));
            question->setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel); question->setDefaultButton(QMessageBox::Cancel);
            const int answer = question->exec();
            if (question) delete question;
            if (!guard) return;
            if (answer != QMessageBox::Yes || !c->wanted) { c->wanted = false; setStatus(c, "Host key was not trusted. No authentication was attempted."); return; }
            RelayProfile profile = profiles_.value(c->profile.id); profile.endpoint.fingerprint = fingerprint;
            QString error;
            if (!saveProfile(profile, &error)) { c->wanted = false; setStatus(c, "Could not save the trusted host key: " + error); return; }
            c->profile = profile;
            if (!c->worker) begin(c); else c->restartPending = true;
        });
    };
    connect(worker, &QThread::finished, this, [this, c, worker]() {
        if (c->worker != worker) return;
        const bool retry = c->everReady && c->wanted && c->profile.reconnect && !c->fatal && !shuttingDown_;
        c->worker = nullptr; c->ready = false; c->pending.clear(); worker->deleteLater(); offline(c);
        if (c->restartPending && c->wanted && !shuttingDown_) { c->restartPending = false; begin(c); }
        else if (retry) {
            const int delay = std::min(30, 1 << std::min(c->retry++, 5));
            setStatus(c, QStringLiteral("Relay disconnected. Reconnecting in %1 seconds…").arg(delay));
            QTimer::singleShot(delay * 1000, this, [this, c]() { if (c->wanted && !c->worker) begin(c); });
        }
    });
    setStatus(c, "Connecting to AnsiChat relay…"); worker->start();
}
void RelayBackend::setStatus(Connection *c, const QString &text) {
    c->status = text;
    auto root = c->remotes.find(QString());
    if (root != c->remotes.end() && root->server) {
        void *session = qt_relay_target(root->server, "*server*", 0);
        qt_relay_message(session, text.toUtf8().constData(), time(nullptr), 0, 0, 0);
    }
}
void RelayBackend::offline(Connection *c) { for (auto &remote : c->remotes) qt_relay_server_state(remote.server, remote.nick.toUtf8().constData(), 0); }
void RelayBackend::disconnectProfile(const QString &id) {
    auto *c = connections_.value(id); if (!c) return;
    c->wanted = false; c->ready = false; c->passphrase.fill(0); c->passphrase.clear();
    if (c->worker) c->worker->requestInterruption();
    offline(c); setStatus(c, "Detached from relay. Its IRC sessions remain connected.");
}
void RelayBackend::shutdown() { shuttingDown_ = true; for (auto it = connections_.begin(); it != connections_.end(); ++it) disconnectProfile(it.key()); }
void RelayBackend::autoconnect() { for (const auto &p : profiles_) if (p.autoconnect) connectProfile(p.id); }
bool RelayBackend::request(Connection *c, QJsonObject f) {
    if (!c->ready || !c->worker || !c->wanted) return false;
    const auto id = c->nextID++; f["id"] = double(id); f["type"] = "request";
    if (c->pending.size() >= 512 || !c->worker->send(f)) { setStatus(c, "The relay send queue is full; retry your command shortly."); return false; }
    c->pending[id] = qMakePair(QDateTime::currentMSecsSinceEpoch(), f.value("action").toString() + " " + f.value("target").toString()); return true;
}
void RelayBackend::frame(Connection *c, const QJsonObject &f) {
    const auto type = f.value("type").toString();
    if (!c->hello && type != "hello") {
        setStatus(c, "The relay did not start with a protocol hello."); c->wanted = false; c->worker->requestInterruption(); return;
    }
    if (type == "hello") {
        if (c->hello || f.value("version").toInt() != 2) { setStatus(c, "The relay uses an unsupported protocol version."); c->wanted = false; c->worker->requestInterruption(); return; }
        c->hello = true;
    } else if (type == "snapshot") snapshot(c, f.value("snapshot").toObject());
    else if (type == "history") {
        const auto messages = f.value("messages").toArray();
        if (!c->ready) {
            if (c->initialHistory.size() + messages.size() > 50000) { setStatus(c, "The relay history is too large."); c->worker->requestInterruption(); return; }
            for (const auto &m : messages) c->initialHistory.append(m);
        } else for (const auto &m : messages) message(c, m.toObject(), true);
    } else if (type == "ready") {
        if (!c->hello || c->ready) { setStatus(c, "The relay sent an invalid handshake."); c->wanted = false; c->worker->requestInterruption(); return; }
        c->ready = c->everReady = true; c->retry = 0; c->lastPing = QDateTime::currentMSecsSinceEpoch();
        for (const auto &m : c->initialHistory) message(c, m.toObject(), true);
        c->initialHistory = {};
        if (c->remotes.contains(QString())) qt_relay_server_state(c->remotes[QString()].server, "", 1);
        setStatus(c, "Connected to AnsiChat relay.");
    } else if (type == "message") {
        if (!c->ready) { setStatus(c, "The relay sent live messages before completing its handshake."); c->worker->requestInterruption(); return; }
        message(c, f.value("message").toObject(), false);
    } else if (type == "response") {
        const double number = f.value("id").toDouble();
        if (number < 1 || number > 9007199254740991.0) return;
        const auto id = quint64(number); if (double(id) != number || !c->pending.contains(id)) return;
        const auto pending = c->pending.take(id);
        if (!f.value("error").toString().isEmpty()) setStatus(c, "Relay request failed (" + pending.second + "): " + f.value("error").toString());
    } else if (type == "error") { setStatus(c, "Relay error: " + f.value("error").toString()); c->worker->requestInterruption(); }
    else if (type == "event" && c->ready) {
        const auto event = f.value("event").toObject();
        // Typing changes arrive as events, without an immediate snapshot.
        if (event.value("Command").toString() != "TAGMSG") return;
        const auto name = event.value("Server").toString();
        if (c->hiddenServers.contains(name) || !c->remotes.contains(name)) return;
        auto &remote = c->remotes[name];
        const auto nick = event.value("Source").toString();
        if (nick.isEmpty() || targetKey(nick) == targetKey(remote.nick)) return;
        const auto params = event.value("Params").toArray();
        if (params.isEmpty()) return;
        auto target = params.at(0).toString();
        if (targetKey(target) == targetKey(remote.nick)) target = nick;
        void *session = remote.targets.value(targetKey(target), nullptr);
        if (!session || qt_relay_ignored(session, (nick + "!*@*").toUtf8().constData())) return;
        const auto tags = event.value("Tags").toObject();
        const auto state = tags.value("+typing").toString(tags.value("typing").toString());
        if (state == "active" || state == "paused" || state == "done")
            qt_fe_typing(session, nick.toUtf8().constData(), state.toUtf8().constData());
    }
    // Other event frames mirror message/snapshot content. Never feed them into the
    // IRC parser: that would duplicate messages and automatic CTCP replies.
}
void RelayBackend::snapshot(Connection *c, const QJsonObject &state) {
    QSet<QString> present;
    for (const auto &value : state.value("servers").toArray()) {
        const auto object = value.toObject(); const auto name = object.value("name").toString();
        if (name.isEmpty() || c->hiddenServers.contains(name)) continue;
        present.insert(name); auto &remote = c->remotes[name]; remote.nick = object.value("nick").toString();
        if (!remote.server) remote.server = qt_relay_create_server((c->profile.name + " / " + name).toUtf8().constData(), remote.nick.toUtf8().constData());
        const bool connected = object.value("connected").toBool(); remote.connected = connected; qt_relay_server_state(remote.server, remote.nick.toUtf8().constData(), connected);
        QSet<QString> presentTargets;
        for (const auto &targetValue : object.value("targets").toArray()) {
            const auto target = targetValue.toObject(); const auto targetName = target.value("target").toString(); const auto key = targetKey(targetName);
            if (targetName.isEmpty() || remote.hidden.contains(key)) continue;
            presentTargets.insert(key); remote.targetNames[key] = targetName;
            const bool joined = connected && target.value("joined").toBool(); remote.joined[key] = joined;
            void *session = qt_relay_target(remote.server, targetName.toUtf8().constData(), joined); remote.targets[key] = session;
            qt_relay_topic(session, target.value("topic").toString().toUtf8().constData());
            QList<QByteArray> names, prefixes;
            for (const auto &userValue : (joined ? target.value("users").toArray() : QJsonArray())) {
                const auto user = userValue.toObject(); const auto nick = user.value("nick").toString(); if (nick.isEmpty()) continue;
                names.append(nick.toUtf8()); prefixes.append(user.value("prefix").toString().toUtf8());
            }
            QList<const char *> namePointers, prefixPointers;
            for (int i = 0; i < names.size(); ++i) { namePointers << names[i].constData(); prefixPointers << prefixes[i].constData(); }
            qt_relay_users(session, namePointers.constData(), prefixPointers.constData(), names.size());
            QList<QByteArray> typing;
            for (const auto &nick : (connected ? target.value("typing_users").toArray() : QJsonArray())) typing << nick.toString().toUtf8();
            QList<const char *> pointers; for (const auto &nick : typing) pointers << nick.constData();
            qt_relay_typing(session, pointers.constData(), pointers.size());
        }
        for (auto target = remote.targets.cbegin(); target != remote.targets.cend(); ++target) {
            if (presentTargets.contains(target.key()) || !remote.joined.value(target.key())) continue;
            remote.joined[target.key()] = false;
            qt_relay_target(remote.server, remote.targetNames.value(target.key()).toUtf8().constData(), 0);
            qt_relay_users(target.value(), nullptr, nullptr, 0); qt_relay_typing(target.value(), nullptr, 0);
        }
    }
    for (auto it = c->remotes.begin(); it != c->remotes.end(); ++it) if (!it.key().isEmpty() && !present.contains(it.key())) qt_relay_server_state(it->server, it->nick.toUtf8().constData(), 0);
}
void RelayBackend::message(Connection *c, QJsonObject m, bool replay) {
    const auto name = m.value("Server").toString(), target = m.value("Target").toString();
    if (name.isEmpty() || target.isEmpty() || c->hiddenServers.contains(name)) return;
    const auto id = m.value("RelayID").toString();
    if (!id.isEmpty() && c->seenIDs.contains(id)) return;
    auto &remote = c->remotes[name];
    if (!remote.server) remote.server = qt_relay_create_server((c->profile.name + " / " + name).toUtf8().constData(), remote.nick.toUtf8().constData());
    const auto kind = m.value("Kind").toString(), nick = m.value("Nick").toString(), text = m.value("Text").toString();
    const auto key = targetKey(target); const bool history = replay || m.value("Replay").toBool();
    if (remote.hidden.contains(key)) {
        if (history || kind == "system" || kind == "error") return;
        remote.hidden.remove(key);
    }
    void *session = qt_relay_target(remote.server, target.toUtf8().constData(), remote.joined.value(key)); remote.targets[key] = session; remote.targetNames[key] = target;
    // Match direct IRC: historical and own messages never trigger notifications.
    const bool own = targetKey(nick) == targetKey(remote.nick);
    if (!own && !nick.isEmpty()) {
        const auto source = nick + '!' + m.value("User").toString("*") + '@' + m.value("Host").toString("*");
        if (qt_relay_ignored(session, source.toUtf8().constData())) return;
    }
    const bool unread = !history && !own && !m.value("SuppressUnread").toBool() && kind != "system";
    const bool mention = m.value("Mention").toBool() || (!remote.nick.isEmpty() && text.contains(remote.nick, Qt::CaseInsensitive));
    const auto timestamp = QDateTime::fromString(m.value("Time").toString(), Qt::ISODateWithMs);
    qt_relay_chat_message(session, kind.toUtf8().constData(), nick.toUtf8().constData(), text.toUtf8().constData(),
                          timestamp.isValid() ? timestamp.toSecsSinceEpoch() : time(nullptr), history, unread, mention);
    if (unread && !m.value("SuppressNotify").toBool()) qt_fe_message_alert(session, text.toUtf8().constData(), mention, !channelTarget(target));
    if (!id.isEmpty()) {
        c->seenIDs.insert(id); c->seenOrder.enqueue(id); remote.lastIDs[key] = id;
        while (c->seenOrder.size() > 100000) c->seenIDs.remove(c->seenOrder.dequeue());
    }
}
int RelayBackend::sendLine(void *server, const QByteArray &bytes) {
    Connection *c = nullptr; QString name;
    for (auto *candidate : connections_) for (auto it = candidate->remotes.cbegin(); it != candidate->remotes.cend(); ++it) if (it->server == server) { c = candidate; name = it.key(); }
    if (!c || !c->ready) return -1;
    QString line = QString::fromUtf8(bytes);
    while (line.endsWith('\r') || line.endsWith('\n')) line.chop(1);
    if (line.contains('\n') || line.contains('\r') || line.contains(QChar::Null)) return -1;
    QString prefix = line; QString tags;
    if (prefix.startsWith('@')) { const int space = prefix.indexOf(' '); if (space < 0) return -1; tags = prefix.left(space); prefix.remove(0, space + 1); }
    const int trailing = prefix.indexOf(" :"); const QString text = trailing < 0 ? QString() : prefix.mid(trailing + 2);
    const auto words = (trailing < 0 ? prefix : prefix.left(trailing)).split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) return -1;
    const auto command = words[0].toUpper(); const auto target = words.value(1);
    QJsonObject f{{"server", name}, {"target", target}, {"text", text}};
    if (command == "QUIT") { disconnectProfile(c->profile.id); return bytes.size(); }
    if (name.isEmpty()) { setStatus(c, "Select an IRC network or channel below this relay before sending a command."); return -1; }
    if (!tags.isEmpty() && command != "TAGMSG") { f["action"] = "raw"; f["text"] = line; }
    else if (command == "PRIVMSG" || command == "NOTICE") {
        if (command == "PRIVMSG" && text.startsWith(QChar(1)) && text.endsWith(QChar(1))) {
            const auto ctcp = text.mid(1, text.size() - 2); const int split = ctcp.indexOf(' ');
            const auto action = split < 0 ? ctcp : ctcp.left(split);
            f["action"] = action.compare("ACTION", Qt::CaseInsensitive) == 0 ? "action" : "ctcp";
            f["extra"] = action; f["text"] = split < 0 ? QString() : ctcp.mid(split + 1);
        } else f["action"] = command == "PRIVMSG" ? "message" : "notice";
    } else if (command == "JOIN") { f["action"] = "join"; f["extra"] = words.value(2); c->remotes[name].hidden.remove(targetKey(target)); }
    else if (command == "PART") f["action"] = "part";
    else if (command == "NICK") { f["action"] = "nick"; f["text"] = target; }
    else if (command == "TOPIC" && trailing >= 0) f["action"] = "topic";
    else if (command == "WHOIS") { f["action"] = "whois"; f["target"] = words.last(); }
    else if (command == "TAGMSG" && tags.startsWith("@+typing=")) { f["action"] = "typing"; f["text"] = tags.mid(9).section(';', 0, 0); }
    else { f["action"] = "raw"; f["text"] = line; }
    return request(c, f) ? bytes.size() : -1;
}
void RelayBackend::detachServer(void *server) { for (auto *c : connections_) for (const auto &remote : c->remotes) if (remote.server == server) { disconnectProfile(c->profile.id); return; } }
void RelayBackend::reconnectServer(void *server) {
    for (auto *c : connections_) for (auto it = c->remotes.begin(); it != c->remotes.end(); ++it) if (it->server == server) {
        if (!c->worker) { connectProfile(c->profile.id, c->passphrase); return; }
        if (!it.key().isEmpty() && !it->connected && c->ready) request(c, {{"action", "connect"}, {"server", it.key()}});
        else {
            c->wanted = true; c->restartPending = true; c->ready = false;
            c->worker->requestInterruption(); setStatus(c, "Reconnecting to AnsiChat relay…");
        }
        return;
    }
}
void RelayBackend::serverGone(void *server) {
    for (auto *c : connections_) for (auto it = c->remotes.begin(); it != c->remotes.end(); ++it) if (it->server == server) {
        c->hiddenServers.insert(it.key()); c->remotes.erase(it);
        if (c->remotes.isEmpty()) disconnectProfile(c->profile.id);
        return;
    }
}
void RelayBackend::sessionGone(void *session) {
    for (auto *c : connections_) for (auto &remote : c->remotes) for (auto it = remote.targets.begin(); it != remote.targets.end(); ++it) if (it.value() == session) { remote.hidden.insert(it.key()); remote.targets.erase(it); return; }
}
void RelayBackend::markRead(void *session) {
    for (auto *c : connections_) for (auto remote = c->remotes.begin(); remote != c->remotes.end(); ++remote) for (auto target = remote->targets.cbegin(); target != remote->targets.cend(); ++target) if (target.value() == session) {
        const auto id = remote->lastIDs.value(target.key()); if (!id.isEmpty()) request(c, {{"action", "markread"}, {"server", remote.key()}, {"target", remote->targetNames.value(target.key(), target.key())}, {"text", id}}); return;
    }
}
RelayBackend *relayBackend(QWidget *owner) {
    if (!instance && owner) instance = new RelayBackend(QString::fromUtf8(qt_core_config_dir()), owner);
    return instance;
}
void startRelayBackends(QWidget *owner, bool automatic) { auto *backend = relayBackend(owner); if (automatic) QTimer::singleShot(0, backend, [backend]() { backend->autoconnect(); }); }
void shutdownRelayBackends() { if (instance) instance->shutdown(); }
void relaySessionClosed(void *session) { if (instance) instance->sessionGone(session); }
extern "C" {
int qt_relay_send_line(void *server, const char *line, int length) { return instance ? instance->sendLine(server, QByteArray(line, length)) : -1; }
void qt_relay_detach(void *server) { if (instance) instance->detachServer(server); }
void qt_relay_reconnect(void *server) { if (instance) instance->reconnectServer(server); }
void qt_relay_server_gone(void *server) { if (instance) instance->serverGone(server); }
void qt_relay_read(void *session) { if (instance) instance->markRead(session); }
}
