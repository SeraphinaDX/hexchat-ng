/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "relaytransport.h"
#include <QObject>
#include <QMap>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
class QWidget;
struct RelayProfile {
    QString id, name;
    RelayEndpoint endpoint;
    bool autoconnect = false, reconnect = true;
};
class RelayBackend final : public QObject {
public:
    explicit RelayBackend(QString configDirectory, QWidget *owner);
    ~RelayBackend() override;
    QList<RelayProfile> profiles() const { return profiles_.values(); }
    bool saveProfile(RelayProfile profile, QString *error);
    bool removeProfile(const QString &id, QString *error);
    bool connectProfile(const QString &id, const QByteArray &passphrase = {});
    void disconnectProfile(const QString &id);
    QString status(const QString &id) const;
    void autoconnect();
    void shutdown();
    int sendLine(void *server, const QByteArray &line);
    void detachServer(void *server);
    void reconnectServer(void *server);
    void serverGone(void *server);
    void sessionGone(void *session);
    void markRead(void *session);
    QString configDirectory() const { return directory_; }
private:
    struct Connection;
    QString directory_, loadError_;
    QPointer<QWidget> owner_;
    QMap<QString, RelayProfile> profiles_;
    QHash<QString, Connection *> connections_;
    bool shuttingDown_ = false;
    bool persist(QString *error);
    void begin(Connection *connection);
    void frame(Connection *connection, const QJsonObject &frame);
    void snapshot(Connection *connection, const QJsonObject &snapshot);
    void message(Connection *connection, QJsonObject message, bool replay);
    void setStatus(Connection *connection, const QString &text);
    bool request(Connection *connection, QJsonObject frame);
    void offline(Connection *connection);
};
RelayBackend *relayBackend(QWidget *owner = nullptr);
void showRelayManager(QWidget *owner);
void startRelayBackends(QWidget *owner, bool autoconnect);
void shutdownRelayBackends();
void relaySessionClosed(void *session);
