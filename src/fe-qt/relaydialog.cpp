/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relaybackend.h"
#include "relaykeys.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
namespace {
class RelayDialog final : public QDialog {
public:
    RelayDialog(QWidget *parent, RelayBackend *backend) : QDialog(parent), backend_(backend) {
        setWindowTitle("hexchat-ng — AnsiChat Relays"); setObjectName("relayManager"); resize(820, 620);
        auto *layout = new QVBoxLayout(this);
        auto *intro = new QLabel("Connect to your persistent AnsiChat session over SSH. Add the public key below to the relay server's authorized_keys file.", this);
        intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
        auto *row = new QHBoxLayout; list_ = new QListWidget(this); list_->setObjectName("relayProfiles"); list_->setMaximumWidth(230); row->addWidget(list_);
        auto *fields = new QVBoxLayout; auto *form = new QFormLayout;
        name_ = new QLineEdit(this); name_->setObjectName("relayName"); form->addRow("Name", name_);
        host_ = new QLineEdit(this); host_->setObjectName("relayHost"); host_->setPlaceholderText("relay.example.com"); form->addRow("Host", host_);
        port_ = new QSpinBox(this); port_->setRange(1, 65535); port_->setValue(2222); port_->setObjectName("relayPort"); form->addRow("SSH port", port_);
        user_ = new QLineEdit("copperline", this); user_->setObjectName("relayUser"); form->addRow("SSH username", user_);
        fingerprint_ = new QLineEdit(this); fingerprint_->setObjectName("relayFingerprint"); fingerprint_->setPlaceholderText("SHA256:… (or verify on first connection)"); form->addRow("Server fingerprint", fingerprint_);
        auto *keyRow = new QHBoxLayout; key_ = new QLineEdit(this); key_->setObjectName("relayPrivateKey");
        auto *browse = new QPushButton("Choose Key…", this); browse->setObjectName("relayChooseKey"); keyRow->addWidget(key_); keyRow->addWidget(browse); form->addRow("Private key", keyRow);
        passphrase_ = new QLineEdit(this); passphrase_->setObjectName("relayPassphrase"); passphrase_->setEchoMode(QLineEdit::Password); passphrase_->setPlaceholderText("Only needed for an encrypted key; never saved"); form->addRow("Key passphrase", passphrase_);
        automatic_ = new QCheckBox("Connect when hexchat-ng starts", this); automatic_->setObjectName("relayAutoconnect"); form->addRow(automatic_);
        reconnect_ = new QCheckBox("Reconnect after a lost relay connection", this); reconnect_->setChecked(true); form->addRow(reconnect_);
        fields->addLayout(form);
        auto *publicLabel = new QLabel("Your public key (safe to share)", this); fields->addWidget(publicLabel);
        public_ = new QPlainTextEdit(this); public_->setObjectName("relayPublicKey"); public_->setReadOnly(true); public_->setMaximumHeight(95); fields->addWidget(public_);
        auto *keyButtons = new QHBoxLayout;
        auto *generate = new QPushButton("Generate New Key…", this); generate->setObjectName("relayGenerateKey");
        auto *copy = new QPushButton("Copy Public Key", this); copy->setObjectName("relayCopyPublicKey");
        auto *exportKey = new QPushButton("Export Public Key…", this); exportKey->setObjectName("relayExportPublicKey");
        keyButtons->addWidget(generate); keyButtons->addWidget(copy); keyButtons->addWidget(exportKey); fields->addLayout(keyButtons);
        row->addLayout(fields, 1); layout->addLayout(row);
        status_ = new QLabel(this); status_->setObjectName("relayStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
        auto *buttons = new QHBoxLayout;
        auto *add = new QPushButton("Add Relay", this); auto *remove = new QPushButton("Remove", this); auto *save = new QPushButton("Save", this); save->setObjectName("relaySave");
        auto *connectButton = new QPushButton("Connect", this); connectButton->setObjectName("relayConnect"); auto *disconnectButton = new QPushButton("Detach", this); disconnectButton->setObjectName("relayDetach");
        buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); buttons->addWidget(save); buttons->addWidget(connectButton); buttons->addWidget(disconnectButton); layout->addLayout(buttons);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this); connect(close, &QDialogButtonBox::rejected, this, &QDialog::close); layout->addWidget(close);
        connect(list_, &QListWidget::currentRowChanged, this, [this](int index) { if (index < 0) return; const auto id = list_->item(index)->data(Qt::UserRole).toString(); for (const auto &p : backend_->profiles()) if (p.id == id) { load(p); return; } });
        connect(add, &QPushButton::clicked, this, [this]() { RelayProfile p; p.id = QUuid::createUuid().toString(QUuid::WithoutBraces); p.name = "New relay"; load(p); list_->clearSelection(); });
        connect(remove, &QPushButton::clicked, this, [this]() { QString error; if (!backend_->removeProfile(id_, &error)) { note(error); return; } refresh(); if (list_->count()) list_->setCurrentRow(0); else { RelayProfile p; p.id = QUuid::createUuid().toString(QUuid::WithoutBraces); load(p); } });
        connect(save, &QPushButton::clicked, this, [this]() { if (this->save()) note("Relay settings saved."); });
        connect(connectButton, &QPushButton::clicked, this, [this]() { if (!ensureKey() || !this->save()) return; backend_->connectProfile(id_, passphrase_->text().toUtf8()); passphrase_->clear(); localNote_ = false; });
        connect(disconnectButton, &QPushButton::clicked, this, [this]() { backend_->disconnectProfile(id_); localNote_ = false; });
        connect(browse, &QPushButton::clicked, this, [this]() { const auto path = QFileDialog::getOpenFileName(this, "Choose relay private key", key_->text()); if (!path.isEmpty()) { key_->setText(path); refreshPublicKey(); } });
        connect(key_, &QLineEdit::editingFinished, this, [this]() { refreshPublicKey(); });
        connect(generate, &QPushButton::clicked, this, [this]() {
            const auto path = QFileDialog::getSaveFileName(this, "Generate a new private relay key", defaultKeyPath()); if (path.isEmpty()) return;
            QString error; if (!createRelayKey(path, &error)) { note(error); return; } key_->setText(path); refreshPublicKey(); note("New key generated. Copy or export the public key and add it to your relay's authorized_keys file.");
        });
        connect(copy, &QPushButton::clicked, this, [this]() { if (!ensureKey()) return; QApplication::clipboard()->setText(public_->toPlainText()); note("Public key copied. The private key stays on this device."); });
        connect(exportKey, &QPushButton::clicked, this, [this]() {
            if (!ensureKey()) return;
            const auto path = QFileDialog::getSaveFileName(this, "Export public key", key_->text() + ".pub", "SSH public key (*.pub);;All files (*)"); if (path.isEmpty()) return;
            QString error; if (!exportRelayPublicKey(key_->text(), path, &error)) note(error); else note("Public key exported to " + path);
        });
        auto *timer = new QTimer(this); connect(timer, &QTimer::timeout, this, [this]() { if (!localNote_) status_->setText(backend_->status(id_));
            if (fingerprint_->text().isEmpty() && !fingerprint_->hasFocus())
                for (const auto &p : backend_->profiles()) if (p.id == id_ && !p.endpoint.fingerprint.isEmpty()) fingerprint_->setText(p.endpoint.fingerprint); }); timer->start(250);
        refresh();
        if (list_->count()) list_->setCurrentRow(0); else { RelayProfile p; p.id = QUuid::createUuid().toString(QUuid::WithoutBraces); p.name = "My AnsiChat relay"; load(p); }
    }
private:
    RelayBackend *backend_; QString id_; bool localNote_ = false;
    QListWidget *list_; QLineEdit *name_, *host_, *user_, *key_, *fingerprint_, *passphrase_;
    QSpinBox *port_; QCheckBox *automatic_, *reconnect_; QPlainTextEdit *public_; QLabel *status_;
    void note(const QString &s) { localNote_ = true; status_->setText(s); }
    QString defaultKeyPath() const { return QDir(backend_->configDirectory()).filePath("relay-keys/" + id_ + "_ed25519"); }
    void refresh() { const QSignalBlocker blocker(list_); list_->clear(); for (const auto &p : backend_->profiles()) { auto *item = new QListWidgetItem(p.name, list_); item->setData(Qt::UserRole, p.id); } }
    void load(const RelayProfile &p) {
        id_ = p.id; name_->setText(p.name); host_->setText(p.endpoint.host); port_->setValue(p.endpoint.port); user_->setText(p.endpoint.user.isEmpty() ? "copperline" : p.endpoint.user);
        fingerprint_->setText(p.endpoint.fingerprint); key_->setText(p.endpoint.keyPath.isEmpty() ? defaultKeyPath() : p.endpoint.keyPath);
        automatic_->setChecked(p.autoconnect); reconnect_->setChecked(p.reconnect); passphrase_->clear(); localNote_ = false; refreshPublicKey();
    }
    void refreshPublicKey() { QString error; public_->setPlainText(QFileInfo::exists(key_->text()) ? relayPublicKey(key_->text(), &error) : QString()); if (!error.isEmpty()) note(error); }
    bool ensureKey() {
        QString error;
        if (key_->text().trimmed().isEmpty()) key_->setText(defaultKeyPath());
        if (!save()) return false;
        if (!QFileInfo::exists(key_->text()) && !createRelayKey(key_->text(), &error)) { note(error); return false; }
        const auto value = relayPublicKey(key_->text(), &error); if (value.isEmpty()) { note(error); return false; }
        public_->setPlainText(value); return true;
    }
    bool save() {
        QString keyPath = key_->text().trimmed();
        if (keyPath.startsWith("~/")) keyPath = QDir::home().filePath(keyPath.mid(2));
        key_->setText(keyPath.isEmpty() ? defaultKeyPath() : QFileInfo(keyPath).absoluteFilePath());
        RelayProfile p; p.id = id_; p.name = name_->text(); p.endpoint.host = host_->text(); p.endpoint.port = port_->value(); p.endpoint.user = user_->text();
        p.endpoint.keyPath = key_->text(); p.endpoint.fingerprint = fingerprint_->text(); p.autoconnect = automatic_->isChecked(); p.reconnect = reconnect_->isChecked();
        QString error; if (!backend_->saveProfile(p, &error)) { note(error); return false; } refresh(); return true;
    }
};
}
void showRelayManager(QWidget *owner) {
    static QPointer<RelayDialog> dialog;
    if (dialog) { dialog->showNormal(); dialog->raise(); dialog->activateWindow(); return; }
    dialog = new RelayDialog(owner, relayBackend(owner)); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
}
