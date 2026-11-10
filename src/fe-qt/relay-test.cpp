/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relaybackend.h"
#include "relaykeys.h"
#include "relay-test-engine.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>
static QByteArray directory;
extern "C" const char *qt_core_config_dir() { return directory.constData(); }
extern "C" void qt_fe_message_alert(void *, const char *, int, int) {}
extern "C" void qt_fe_typing(void *, const char *, const char *) {}
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); return 1; } } while (0)
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc == 3 && QByteArray(argv[1]) == "--generate-key") { QString error; if (!createRelayKey(QString::fromLocal8Bit(argv[2]), &error)) { std::fprintf(stderr, "%s\n", error.toUtf8().constData()); return 1; } return 0; }
    QTemporaryDir temporary; CHECK(temporary.isValid()); directory = temporary.path().toUtf8();
    const auto key = temporary.filePath("keys/client"); QString error;
    CHECK(createRelayKey(key, &error));
    QFile privateFile(key); CHECK(privateFile.open(QIODevice::ReadOnly)); const auto privateBytes = privateFile.readAll(); privateFile.close();
    CHECK(!createRelayKey(key, &error));
    const auto publicKey = relayPublicKey(key, &error); CHECK(publicKey.startsWith("ssh-ed25519 ")); CHECK(!relayKeyIsEncrypted(key)); CHECK(!publicKey.contains("PRIVATE"));
#ifndef Q_OS_WIN
    CHECK(!(QFile::permissions(key) & (QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ReadGroup | QFileDevice::WriteGroup)));
#endif
    CHECK(exportRelayPublicKey(key, temporary.filePath("client.pub"), &error));
    QFile publicFile(temporary.filePath("client.pub")); CHECK(publicFile.open(QIODevice::ReadOnly)); CHECK(publicFile.readAll() == publicKey.toUtf8());
    CHECK(!exportRelayPublicKey(key, key, &error));
    CHECK(privateFile.open(QIODevice::ReadOnly)); CHECK(privateFile.readAll() == privateBytes); privateFile.close();
    QWidget owner;
    auto *backend = relayBackend(&owner); CHECK(backend);
    RelayProfile profile; profile.id = "test"; profile.name = "Home"; profile.endpoint.host = "127.0.0.1"; profile.endpoint.user = "copperline"; profile.endpoint.keyPath = key;
    profile.endpoint.passphrase = "must-not-be-saved";
    CHECK(backend->saveProfile(profile, &error));
    QFile settings(temporary.filePath("ansichat-relays.json")); CHECK(settings.open(QIODevice::ReadOnly)); const auto data = settings.readAll(); settings.close();
    CHECK(!data.contains("must-not-be-saved")); CHECK(!data.contains("PRIVATE KEY"));
    RelayBackend reloaded(temporary.path(), &owner); CHECK(reloaded.profiles().size() == 1); CHECK(reloaded.profiles().first().endpoint.keyPath == key);
    auto incomplete = profile; incomplete.endpoint.host.clear(); CHECK(backend->saveProfile(incomplete, &error));
    CHECK(!backend->connectProfile(incomplete.id)); CHECK(backend->status(incomplete.id).contains("hostname"));
    CHECK(backend->saveProfile(profile, &error));
    auto invalid = profile; invalid.name.clear(); CHECK(!backend->saveProfile(invalid, &error));
    showRelayManager(&owner);
    auto *dialog = owner.findChild<QDialog *>("relayManager"); CHECK(dialog);
    auto *copy = dialog->findChild<QPushButton *>("relayCopyPublicKey"); CHECK(copy); copy->click();
    CHECK(QApplication::clipboard()->text() == publicKey);
    CHECK(dialog->findChild<QPlainTextEdit *>("relayPublicKey")->toPlainText() == publicKey);
    CHECK(dialog->findChild<QLineEdit *>("relayPassphrase")->isHidden());
    CHECK(!dialog->findChild<QLineEdit *>("relayPassphrase")->text().contains("must-not-be-saved"));
    dialog->close(); QApplication::processEvents();
    CHECK(backend->removeProfile("test", &error)); CHECK(backend->profiles().isEmpty());
    QFile broken(temporary.filePath("ansichat-relays.json")); CHECK(broken.open(QIODevice::WriteOnly)); broken.write("not valid JSON"); broken.close();
    RelayBackend damaged(temporary.path(), &owner); CHECK(!damaged.saveProfile(profile, &error));
    CHECK(broken.open(QIODevice::ReadOnly)); CHECK(broken.readAll() == "not valid JSON");
    return 0;
}
