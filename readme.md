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
for channels and private conversations, plus native [AnsiChat SSH relay support](src/fe-qt/RELAY.md)
with public-key generation, copying and export.

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

Install Meson, Ninja, a C17/C++17 compiler, Qt 6.2 or newer (including Network), Pango/Cairo,
GLib/GIO, gettext, libarchive, OpenSSL 1.1.1+ and libssh2 1.10+ development packages. Enchant 2 adds
spell checking. See [the build guide](src/fe-qt/README.md) for plugin options,
platform dependencies, offline builds and tests.

When upgrading from a GTK-enabled checkout, use a fresh build directory such as
`build-qt` above. Existing build caches may contain removed options. Remove
`-Dgtk-frontend`, `-Dappindicator` and `-Dmac-integration` from saved build
commands; those options no longer exist.

## Tests and packaging

```sh
meson test -C build-qt --no-suite runtime --print-errorlogs
```

Plugin-enabled builds also include local IRC, CTCP, TLS/SASL and DCC runtime
tests. The active [GitHub workflow](.github/workflows/qt-build.yml) builds and
tests the Qt application on Linux. The old GTK platform packagers and workflows
are [archived for porting reference](packaging/legacy-gtk/README.md); they do not
build the current application.
