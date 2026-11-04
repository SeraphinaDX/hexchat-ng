/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QString>
#include <QByteArray>
// Generate an Ed25519 OpenSSH key without replacing an existing file.
bool createRelayKey(const QString &path, QString *error);
bool relayKeyIsEncrypted(const QString &path);
QString relayPublicKey(const QString &path, QString *error);
bool exportRelayPublicKey(const QString &privatePath, const QString &destination, QString *error);
QString relayKeyFingerprint(const QByteArray &wireKey);
