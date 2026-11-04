/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relaykeys.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <memory>
namespace {
void integer(QByteArray &data, quint32 value) { const auto v = qToBigEndian(value); data.append(reinterpret_cast<const char *>(&v), 4); }
void field(QByteArray &data, const QByteArray &value) { integer(data, value.size()); data.append(value); }
bool take(QByteArray &data, QByteArray &value) {
    if (data.size() < 4) return false;
    const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData()));
    if (size > quint32(data.size() - 4)) return false;
    value = data.mid(4, size); data.remove(0, 4 + size); return true;
}
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
}
QString relayKeyFingerprint(const QByteArray &key) { return QStringLiteral("SHA256:") + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toBase64(QByteArray::OmitTrailingEquals)); }
bool createRelayKey(const QString &path, QString *error)
{
    if (QFileInfo::exists(path)) return fail(error, QStringLiteral("A key already exists at this location. Choose a new filename to generate another key."));
    const auto parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent)) return fail(error, QStringLiteral("Could not create the key directory."));
    using Context = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    Context context(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY *raw = nullptr;
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 || EVP_PKEY_keygen(context.get(), &raw) != 1)
        return fail(error, QStringLiteral("Could not generate an Ed25519 key."));
    Key key(raw, EVP_PKEY_free);
    QByteArray publicBytes(32, 0), seed(32, 0); size_t publicSize = 32, seedSize = 32;
    if (EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<uchar *>(publicBytes.data()), &publicSize) != 1 ||
        EVP_PKEY_get_raw_private_key(key.get(), reinterpret_cast<uchar *>(seed.data()), &seedSize) != 1)
        return fail(error, QStringLiteral("Could not encode the generated key."));
    QByteArray publicBlob; field(publicBlob, "ssh-ed25519"); field(publicBlob, publicBytes);
    quint32 check; if (RAND_bytes(reinterpret_cast<uchar *>(&check), sizeof(check)) != 1) return fail(error, QStringLiteral("Could not generate key randomness."));
    QByteArray secret; integer(secret, check); integer(secret, check); field(secret, "ssh-ed25519"); field(secret, publicBytes);
    field(secret, seed + publicBytes); field(secret, "hexchat-ng-relay");
    for (int padding = 1; secret.size() % 8; ++padding) secret.append(char(padding));
    QByteArray envelope("openssh-key-v1\0", 15); field(envelope, "none"); field(envelope, "none"); field(envelope, {});
    integer(envelope, 1); field(envelope, publicBlob); field(envelope, secret);
    const auto encoded = envelope.toBase64(); QByteArray pem("-----BEGIN OPENSSH PRIVATE KEY-----\n");
    for (int offset = 0; offset < encoded.size(); offset += 70) pem += encoded.mid(offset, 70) + '\n';
    pem += "-----END OPENSSH PRIVATE KEY-----\n";
    // NewOnly closes the check/create race and never overwrites an existing identity.
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail(error, output.errorString());
    if (!output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || output.write(pem) != pem.size() || !output.flush()) {
        output.remove(); return fail(error, QStringLiteral("Could not save the private key with owner-only permissions."));
    }
    seed.fill(0); secret.fill(0); envelope.fill(0); pem.fill(0);
    return true;
}
QString relayPublicKey(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { fail(error, file.errorString()); return {}; }
    if (file.size() > 1024 * 1024) { fail(error, QStringLiteral("The key file is too large.")); return {}; }
    const QByteArray pem = file.readAll();
    const QByteArray begin("-----BEGIN OPENSSH PRIVATE KEY-----"), end("-----END OPENSSH PRIVATE KEY-----");
    const int start = pem.indexOf(begin), stop = pem.indexOf(end);
    if (start < 0 || stop <= start) { fail(error, QStringLiteral("Choose an OpenSSH-format private key, or generate a relay key here.")); return {}; }
    QByteArray data = QByteArray::fromBase64(pem.mid(start + begin.size(), stop - start - begin.size()));
    if (!data.startsWith(QByteArray("openssh-key-v1\0", 15))) { fail(error, QStringLiteral("Invalid OpenSSH key.")); return {}; }
    data.remove(0, 15); QByteArray cipher, kdf, options, publicBlob;
    if (!take(data, cipher) || !take(data, kdf) || !take(data, options) || data.size() < 4 ||
        qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData())) != 1) {
        fail(error, QStringLiteral("Invalid OpenSSH key envelope.")); return {};
    }
    data.remove(0, 4);
    if (!take(data, publicBlob)) { fail(error, QStringLiteral("The key has no public identity.")); return {}; }
    QByteArray type, copy = publicBlob;
    if (!take(copy, type) || (type != "ssh-ed25519" && type != "ssh-rsa" && !type.startsWith("ecdsa-sha2-"))) {
        fail(error, QStringLiteral("Unsupported public key type.")); return {};
    }
    return QString::fromLatin1(type + ' ' + publicBlob.toBase64() + " hexchat-ng-relay\n");
}
bool exportRelayPublicKey(const QString &privatePath, const QString &destination, QString *error)
{
    const auto publicKey = relayPublicKey(privatePath, error); if (publicKey.isEmpty()) return false;
    const auto original = QFileInfo(privatePath).canonicalFilePath();
    if (QFileInfo(destination).absoluteFilePath() == QFileInfo(privatePath).absoluteFilePath() ||
        (!original.isEmpty() && QFileInfo(destination).canonicalFilePath() == original))
        return fail(error, QStringLiteral("Choose a different filename; exporting must not replace your private key."));
    QSaveFile output(destination); const auto bytes = publicKey.toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return fail(error, output.errorString());
    return true;
}
