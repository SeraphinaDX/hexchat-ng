# AnsiChat relay connections

hexchat-ng can attach to an [AnsiChat](https://github.com/SeraphinaDX/AnsiChat)
SSH relay alongside ordinary direct IRC
connections. The relay keeps its IRC sessions online while hexchat-ng is closed.
The client uses libssh2 directly; it does not run system `ssh`, use SSH agents,
or read system SSH configuration. No AnsiChat executable is needed on the client
device.

You can copy or export a public key before entering a relay hostname. The
identity is saved so it remains the same when you finish setup later.

## Connect

Start with a running AnsiChat relay. If you are setting up your own, follow
AnsiChat's [relay server setup](https://github.com/SeraphinaDX/AnsiChat/blob/main/RELAY.md#1-configure-the-relay-server)
to configure its IRC networks, autojoin channels and SSH listener. Only the relay
machine needs AnsiChat installed. Your desktop uses the built-in hexchat-ng
relay manager below.

1. Open **hexchat-ng → AnsiChat Relays…** and enter a name, host, SSH port and
   SSH username. The defaults are port **2222** and username **copperline**;
   use the username configured on your relay if it differs.
2. Click **Copy Public Key**. hexchat-ng generates an Ed25519 identity if the
   selected private-key file does not exist, saves the profile, and copies its
   public key in OpenSSH `authorized_keys` format. **Export Public Key…** saves
   the same public key as a `.pub` file. These controls never export the private
   key, and exporting over the private-key file is refused.
3. Add that public key to the AnsiChat relay server's configured `authorized_keys`
   file. Your private key stays on this device.
4. Paste the relay server's `SHA256:…` host-key fingerprint into the settings,
   or click **Connect** and compare the first-connection trust prompt with the
   fingerprint shown by your server. Accept only if they match.

Use the authorized-keys file configured on the **running relay**. A server still
using a Copperline configuration may read its file under `~/.config/copperline/`;
adding a key to a new AnsiChat directory does not change that running configuration.
The relay's startup log prints the exact authorized-keys path.

The saved fingerprint is checked before public-key authentication on every
attachment. A changed host key is rejected. Verify the new fingerprint through
your server before editing the saved value; there is no skip-verification mode.

The relay's networks and conversations appear in the normal channel tree with
labels such as `Home / libera`. Direct IRC connections can remain open at the
same time. Drafts and conversations survive a lost relay connection, and retained
history is deduplicated when the client attaches again. Historical messages do
not generate desktop notifications or unread alerts.

Relay chat uses the same text-event templates, nickname colours and colour-strip
settings as direct IRC. This includes live and replayed messages, actions,
notices and highlights; changes to your text-event templates apply to newly
displayed relay messages too.

**Detach**, `/quit`, and the ordinary Disconnect action detach this client from
the relay. They do not shut down the relay's IRC connections. Closing the whole
application also detaches without sending IRC QUIT. Closing an individual joined
channel is an explicit PART, as it is for a direct IRC connection.

## Keys and saved settings

The settings live in `ansichat-relays.json` inside hexchat-ng's configuration
directory. Generated private keys live in its `relay-keys` directory and are
created with owner-only read/write permissions. Keys are not overwritten when
generating a new identity. Removing a relay profile does not delete its key.

**Choose Key…** imports an existing OpenSSH-format private key. If it is
passphrase-protected, the **Key passphrase** field appears when you select it.
Generated keys are unencrypted and do not show this field. A key passphrase is
not a relay login password.
The passphrase is never saved to the profile file. It is held in memory for
reconnections during the current attachment and cleared on detach. An encrypted
key therefore needs its passphrase again after restarting hexchat-ng.

Select **Connect when hexchat-ng starts** for automatic attachment.
`--no-auto` disables automatic relay attachment as well as direct IRC autoconnect.
**Reconnect after a lost relay connection** retries with a bounded delay. Changing
profile settings takes effect on the next connection.

## Protocol and supported operations

This backend implements AnsiChat relay protocol **2** over the compatibility SSH
channel `copperline-relay@copperline`. It uses snapshots for network state, topics,
nick lists and typing indicators, live typing events for immediate active/paused/done
updates, and message frames for live chat and history.
Mirrored raw events are not reparsed as new IRC messages, so they cannot produce
duplicate chat or automatic CTCP replies.

Messages, actions, notices, joins, parts, nick changes, topics, WHOIS, typing and
raw IRC commands are forwarded through the relay. The IRC connection and its
protocol negotiation belong to the relay server. Relay-managed DCC offers and
file uploads are not integrated into hexchat-ng's local DCC manager; use a direct
IRC connection for local DCC transfers. Advanced dialogs that require IRC state
not included in AnsiChat's snapshots do not have the same state detail as direct
IRC connections.

## Tests

`qt-relay` checks key generation, OpenSSH compatibility, public export, private
key overwrite protection, clipboard support and profile persistence.

`qt-relay-runtime` uses a local SSH fixture and the real Qt/C application to test
public-key authentication, host-key trust/rejection, protocol mismatch, snapshots,
topics, nick lists, messaging, typing, reconnects, draft retention, replay
deduplication and detach semantics. Install Python **Paramiko** to run this fixture;
it skips with exit code 77 when Paramiko is unavailable. CI installs it explicitly.
