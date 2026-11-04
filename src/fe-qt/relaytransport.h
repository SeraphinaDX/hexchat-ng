/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QThread>
#include <QJsonObject>
#include <QMutex>
#include <QQueue>
#include <functional>
struct RelayEndpoint {
    QString host, user, keyPath, fingerprint;
    int port = 2222;
    QByteArray passphrase;
};
// All SSH operations run on this worker; callbacks are delivered to receiver's thread.
class RelayTransport final : public QThread {
public:
    RelayTransport(RelayEndpoint endpoint, QObject *receiver);
    ~RelayTransport() override;
    bool send(const QJsonObject &frame);
    std::function<void(const QJsonObject &)> frameReceived;
    std::function<void(const QString &)> failed;
    std::function<void(const QString &)> hostKeyRequired;
protected:
    void run() override;
private:
    RelayEndpoint endpoint_;
    QObject *receiver_;
    QMutex mutex_;
    QQueue<QByteArray> outgoing_;
    void error(const QString &message);
};
