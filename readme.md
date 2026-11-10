# hexchat-ng

<img src="data/icons/hexchat.svg" height="180" alt="hexchat-ng icon">

hexchat-ng is an IRC client with a Qt 6 frontend built on the ZoiteChat/HexChat
C engine. This fork is beginning its rebrand from ZoiteChat. Existing core
configuration paths and plugin symbols are retained for compatibility.

We will continue reviewing changes made to ZoiteChat and absorbing improvements
when they make sense for hexchat-ng. This includes useful IRC engine fixes,
protocol updates and other changes that fit this fork's direction.

See the [Qt build and usage guide](src/fe-qt/README.md) for the `hexchat-ng`
executable, dependencies and tests. The Qt frontend includes typing indicators
for channels and private conversations.

## Stay connected with AnsiChat

**Native [AnsiChat](https://github.com/SeraphinaDX/AnsiChat) SSH relay support is
built in.** Keep your IRC sessions on an always-on relay and enjoy them in
hexchat-ng's Qt interface. Close the desktop client, come back later, and catch
up with the history retained by your relay.

- **Your channels, ready when you return:** relay networks, conversations,
  topics, nick lists and typing indicators appear in the normal interface.
- **Reconnect without the clutter:** retained history is deduplicated, and
  replayed messages do not trigger fresh unread alerts or desktop notifications.
- **Easy SSH key setup:** generate, copy or export your public key from the
  relay manager, with server fingerprint verification built in.
- **Direct IRC and relays together:** keep ordinary IRC connections alongside
  your relay connections in the same application.

No AnsiChat installation or system SSH client is required on your desktop.
AnsiChat runs the persistent IRC connections on the relay machine; hexchat-ng
connects to it directly over SSH. See the [relay quick start](#relay-quick-start)
below or the [full relay guide](src/fe-qt/RELAY.md).

Copyright © 2026 Britney Lozza. Existing contributor notices are retained below.

Features include HexChat-compatible Python, Perl and Lua scripting support, a plugin API,
multiple server/channel windows, spell checking, multiple authentication methods including SASL,
and customizable notifications.

See [IRCHelp.org](http://irchelp.org) for information about IRC in general.  

Upstream ZoiteChat documentation (some interface details differ):

- [Main Documentation](https://docs.zoitechat.org/) and [FAQ](https://docs.zoitechat.org/en/latest/faq.html)

- [Downloads](https://zoitechat.org/download.php)


---

<sub>
X-Chat ("xchat") Copyright (c) 1998-2010 By Peter Zelezny.  
HexChat ("hexchat") Copyright (c) 2009-2014 By Berke Viktor.
Hexchat ("hexchat") Copyright (c) 2015-2025 By Patrick Griffis.
ZoiteChat ("zoitechat") Copyright (c) 2026 By deepend. 
</sub>

<sub>
This program is released under the GPL v2 with the additional exemption
that compiling, linking, and/or using OpenSSL is allowed. You may
provide binary packages linked to the OpenSSL libraries, provided that
all other requirements of the GPL are met.
See file COPYING for details.
</sub>

## Build and run

Qt 6 is the default and only graphical frontend. The GTK frontend has been
removed. To build without optional plugins:

```sh
meson setup build-qt -Dplugin=false
meson compile -C build-qt
./build-qt/src/fe-qt/hexchat-ng --no-auto
```

## Install

```sh
make install
```

Install Meson, Ninja, a C17/C++17 compiler, Qt 6.2 or newer (including Network), Pango/Cairo,
GLib/GIO, gettext, libarchive, OpenSSL 1.1.1+ and libssh2 1.10+ development packages. Enchant 2 adds
spell checking. See [the build guide](src/fe-qt/README.md) for plugin options,
platform dependencies, offline builds and tests.

When upgrading from a GTK-enabled checkout, use a fresh build directory such as
`build-qt` above. Existing build caches may contain removed options. Remove
`-Dgtk-frontend`, `-Dappindicator` and `-Dmac-integration` from saved build
commands; those options no longer exist.

## Relay quick start

You need a running AnsiChat relay and access to its configured authorized-keys
file, or someone who can add your public key. To host your own, get
[AnsiChat](https://github.com/SeraphinaDX/AnsiChat) and follow its
[relay server setup](https://github.com/SeraphinaDX/AnsiChat/blob/main/RELAY.md#1-configure-the-relay-server).
Configure your IRC networks and autojoin channels on the relay server.

1. Open **hexchat-ng → AnsiChat Relays…**. Enter a profile name, relay hostname,
   SSH port and SSH username. The defaults are **2222** and **copperline**; use
   your relay's configured values if they differ.
2. Click **Copy Public Key** to generate an identity and copy its public key.
   **Export Public Key…** saves a `.pub` file instead; **Choose Key…** lets you
   use an existing private key. Your private key stays on your device.
3. Add the public key to the relay's configured `authorized_keys` file. The
   relay startup log shows the exact path. AnsiChat reloads this file for each
   login, so you do not need to restart the relay.
4. Enter the server's `SHA256:…` fingerprint, or click **Connect** and compare
   the trust prompt with the fingerprint in your relay's startup log. Accept
   only if they match.
5. Click **Connect** if you have not already. Your relay networks and channels
   appear in the channel tree. Enable **Connect when hexchat-ng starts** for
   automatic attachment; **Reconnect after a lost relay connection** handles
   interruptions.

**Detach**, Disconnect, `/quit`, or closing hexchat-ng leaves the relay's IRC
sessions online. Closing an individual joined channel still parts that channel.
Use a direct IRC connection for local DCC transfers; relay-managed DCC is not
integrated into the local transfer manager.

For encrypted keys, saved profiles, reconnect behavior and troubleshooting, see
the [full relay guide](src/fe-qt/RELAY.md). If you launch with `--no-auto` (as in
the build example above), automatic relay attachment is disabled too; connect
manually or omit that option on later launches.

## Tests and packaging

```sh
meson test -C build-qt --no-suite runtime --print-errorlogs
```

Plugin-enabled builds also include local IRC, CTCP, TLS/SASL and DCC runtime
tests. The active [GitHub workflow](.github/workflows/qt-build.yml) builds and
tests the Qt application on Linux. The old GTK platform packagers and workflows
are [archived for porting reference](packaging/legacy-gtk/README.md); they do not
build the current application.
