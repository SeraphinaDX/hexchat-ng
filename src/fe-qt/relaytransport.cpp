/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relaytransport.h"
#include "relaykeys.h"
#include <QTcpSocket>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <libssh2.h>
#include <memory>
#include <mutex>
#include <atomic>
RelayTransport::RelayTransport(RelayEndpoint endpoint, QObject *receiver) : endpoint_(std::move(endpoint)), receiver_(receiver) {}
RelayTransport::~RelayTransport() { requestInterruption(); wait(); endpoint_.passphrase.fill(0); }
bool RelayTransport::send(const QJsonObject &frame)
{
    const auto bytes = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
    QMutexLocker lock(&mutex_);
    if (outgoing_.size() >= 512 || bytes.size() > 1024 * 1024 || isInterruptionRequested()) return false;
    outgoing_.enqueue(bytes); return true;
}
void RelayTransport::error(const QString &message) {
    if (!isInterruptionRequested() && failed) QMetaObject::invokeMethod(receiver_, [callback = failed, message]() { callback(message); }, Qt::QueuedConnection);
}
void RelayTransport::run()
{
    static std::once_flag init; static int initialization = -1;
    std::call_once(init, []() { initialization = libssh2_init(0); });
    if (initialization) { error(QStringLiteral("Could not initialize SSH.")); return; }
    QFile key(endpoint_.keyPath);
    if (!key.open(QIODevice::ReadOnly) || key.size() > 1024 * 1024) { error(QStringLiteral("Could not read the relay private key.")); return; }
    QByteArray privateKey = key.readAll();
    QTcpSocket socket; socket.connectToHost(endpoint_.host, quint16(endpoint_.port));
    if (!socket.waitForConnected(5000)) { error(QStringLiteral("Could not reach the relay: ") + socket.errorString()); privateKey.fill(0); return; }
    using Session = std::unique_ptr<LIBSSH2_SESSION, decltype(&libssh2_session_free)>;
    Session session(libssh2_session_init(), libssh2_session_free);
    if (!session) { error(QStringLiteral("Could not allocate an SSH session.")); return; }
    libssh2_session_set_blocking(session.get(), 0);
    libssh2_session_banner_set(session.get(), "SSH-2.0-hexchat-ng");
    auto until = [this](auto operation) {
        QElapsedTimer timer; timer.start(); int result;
        do {
            if (isInterruptionRequested()) return int(LIBSSH2_ERROR_SOCKET_DISCONNECT);
            result = operation();
            if (result != LIBSSH2_ERROR_EAGAIN) return result;
            msleep(10);
        } while (timer.elapsed() < 15000);
        return int(LIBSSH2_ERROR_TIMEOUT);
    };
    auto sshError = [&]() { char *message = nullptr; libssh2_session_last_error(session.get(), &message, nullptr, 0); return QString::fromUtf8(message ? message : "SSH operation failed"); };
    if (until([&]() { return libssh2_session_handshake(session.get(), libssh2_socket_t(socket.socketDescriptor())); })) { error(QStringLiteral("SSH handshake failed: ") + sshError()); return; }
    size_t hostKeyLength = 0; int hostKeyType = 0;
    const char *hostKey = libssh2_session_hostkey(session.get(), &hostKeyLength, &hostKeyType);
    if (!hostKey || hostKeyLength > 65536) { error(QStringLiteral("The relay did not provide a valid SSH host key.")); return; }
    const auto fingerprint = relayKeyFingerprint(QByteArray(hostKey, int(hostKeyLength)));
    if (endpoint_.fingerprint.isEmpty()) {
        if (hostKeyRequired) QMetaObject::invokeMethod(receiver_, [callback = hostKeyRequired, fingerprint]() { callback(fingerprint); }, Qt::QueuedConnection);
        return;
    }
    if (fingerprint != endpoint_.fingerprint) {
        error(QStringLiteral("Relay host key mismatch. Expected %1; received %2. Verify the server before changing the saved fingerprint.").arg(endpoint_.fingerprint, fingerprint)); return;
    }
    const auto user = endpoint_.user.toUtf8();
    const int authenticated = until([&]() { return libssh2_userauth_publickey_frommemory(session.get(), user.constData(), user.size(), nullptr, 0, privateKey.constData(), privateKey.size(), endpoint_.passphrase.constData()); });
    privateKey.fill(0); endpoint_.passphrase.fill(0);
    if (authenticated) { error(QStringLiteral("Relay authentication failed. Add this client's public key to the relay's authorized_keys file and check the key passphrase. ") + sshError()); return; }
    LIBSSH2_CHANNEL *channel = nullptr;
    const int opened = until([&]() {
        channel = libssh2_channel_open_ex(session.get(), "copperline-relay@copperline", sizeof("copperline-relay@copperline") - 1, LIBSSH2_CHANNEL_WINDOW_DEFAULT, LIBSSH2_CHANNEL_PACKET_DEFAULT, nullptr, 0);
        return channel ? 0 : libssh2_session_last_errno(session.get());
    });
    if (opened || !channel) { error(QStringLiteral("The server refused the AnsiChat relay channel: ") + sshError()); return; }
    send({{QStringLiteral("type"), QStringLiteral("hello")}, {QStringLiteral("version"), 2}, {QStringLiteral("bool"), false}});
    auto queuedFrames = std::make_shared<std::atomic<int>>(0);
    auto queuedBytes = std::make_shared<std::atomic<int>>(0);
    QByteArray incoming, pending; qsizetype offset = 0;
    bool healthy = true;
    while (!isInterruptionRequested() && healthy) {
        if (pending.isEmpty()) { QMutexLocker lock(&mutex_); if (!outgoing_.isEmpty()) { pending = outgoing_.dequeue(); offset = 0; } }
        if (!pending.isEmpty()) {
            const auto written = libssh2_channel_write(channel, pending.constData() + offset, pending.size() - offset);
            if (written > 0) { offset += written; if (offset == pending.size()) pending.clear(); }
            else if (written != LIBSSH2_ERROR_EAGAIN) { error(QStringLiteral("The relay connection stopped while sending.")); break; }
        }
        char buffer[32768];
        const auto received = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (received > 0) {
            incoming.append(buffer, int(received));
            if (incoming.size() > 8 * 1024 * 1024) { error(QStringLiteral("The relay sent an oversized frame.")); break; }
            int newline;
            while ((newline = incoming.indexOf('\n')) >= 0) {
                const auto line = incoming.left(newline); incoming.remove(0, newline + 1);
                QJsonParseError parseError; const auto document = QJsonDocument::fromJson(line, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) { error(QStringLiteral("The relay sent invalid JSON.")); healthy = false; break; }
                if (queuedFrames->load() >= 256 || queuedBytes->load() + line.size() > 16 * 1024 * 1024) {
                    error(QStringLiteral("The relay is sending data faster than this client can display it.")); healthy = false; break;
                }
                if (frameReceived) {
                    ++*queuedFrames; queuedBytes->fetch_add(line.size());
                    QMetaObject::invokeMethod(receiver_, [callback = frameReceived, frame = document.object(), queuedFrames, queuedBytes, size = line.size()]() {
                        --*queuedFrames; queuedBytes->fetch_sub(size); callback(frame);
                    }, Qt::QueuedConnection);
                }
            }
        } else if ((received < 0 && received != LIBSSH2_ERROR_EAGAIN) || libssh2_channel_eof(channel)) { error(QStringLiteral("The relay connection closed.")); break; }
        if (received <= 0) msleep(10);
    }
    // Free with the socket closed: cancellation must never wait for a peer's SSH window.
    socket.abort(); libssh2_session_set_blocking(session.get(), 1); libssh2_channel_free(channel);
}
