# hexchat-ng Qt 6 frontend

`hexchat-ng` uses Qt 6 Widgets for its graphical interface and the
ZoiteChat/HexChat C core for IRC commands, configuration, networks and plugins.
Qt is the default and only graphical frontend; the GTK frontend has been removed.

Imported from [SeraphinaDX/hexchat-ng](https://github.com/SeraphinaDX/hexchat-ng)
at commit `91b9a7f579a9328abf60713a8d473ef8c24e092f`. The imported code retains
its GPL-2.0-or-later license notices. The integration adapts core headers, plugin
symbols and configuration paths to ZoiteChat. The fork now uses hexchat-ng
branding and the icons from that repository at the same commit. Core ABI and
configuration filenames remain compatible with ZoiteChat.

Copyright © 2026 Britney Lozza.

## Build

Install Meson, Ninja, a C17/C++17 compiler, Qt 6.2 or newer (Core, Gui, Widgets
and resource compiler), Pango/Cairo 1.44 or newer, GLib/GIO development tools
(including `glib-genmarshal`), gettext, libarchive, OpenSSL and the normal
ZoiteChat build dependencies. Enchant 2 enables optional spell checking.

To build without optional plugins:

```sh
meson setup build-qt -Dplugin=false
meson compile -C build-qt
./build-qt/src/fe-qt/hexchat-ng
```

To disable spell checking, use `-Dqt-spellchecking=disabled`. The optional text
frontend can be built with `-Dtext-frontend=true`. For a text-only build, add
`-Dqt-frontend=false`.

For builds created before the GTK removal, use a fresh build directory such as
`build-qt`; old caches can contain removed options. Remove `-Dgtk-frontend`,
`-Dappindicator` and `-Dmac-integration` from saved build commands.

To build with plugins, enable `-Dplugin=true` and select the existing `with-*`
options for the dependencies you have installed. FiSHLiM is disabled by default;
its optional plugin still uses GTK widgets for its key manager and requires GTK
if explicitly enabled. It is separate from the removed GTK application.

The build generates URL rules using the public suffix list. For offline builds,
supply `-Dpublic-suffix-list=/path/to/public_suffix_list.dat`.

## Use

The application shares ZoiteChat's normal configuration. Use
`--cfgdir /path/to/config` for an isolated profile and `--no-auto` to disable
automatic connections. Use **hexchat-ng > Network List** to manage saved networks,
or enter `/server` and other existing IRC commands in the input box.

The frontend includes channel and private conversations, network management,
IRC formatting, nick lists, completion, preferences, themes and palette editing,
search, notifications, tray integration, DCC transfers, add-on management,
custom menus and keybindings. See [FEATURE-PARITY.md](FEATURE-PARITY.md) for the
imported HexChat feature inventory.

Typing indicators show active and paused users in each conversation, including
detached windows. Notifications expire after 6 seconds (active) or 30 seconds
(paused), and clear on messages, departure or disconnect. Editing message text
sends throttled typing notifications on servers supporting message tags and
allowing the typing client tag. Commands do not advertise typing.
Interactive reply metadata is not yet displayed; messages appear as ordinary
text. GTK theme settings remain in the shared configuration for compatibility.
The build installs the `hexchat-ng` executable, desktop launcher, app metadata,
icons and manual page. The former GTK platform packagers are archived under
[packaging/legacy-gtk](../../packaging/legacy-gtk/README.md) for porting reference.

## Tests

```sh
meson test -C build-qt --print-errorlogs
```

Qt UI tests use the offscreen platform. Runtime tests require plugins and local
socket access; scripting tests exercise the scripting modules enabled in the
build. To run without local socket access:

```sh
meson test -C build-qt --no-suite runtime --print-errorlogs
```

The runtime fixtures create isolated temporary profiles and probe plugins. The
[testing notes](TESTING.md) describe the imported regression coverage and manual
checks. `.github/workflows/qt-build.yml` builds and tests the Qt frontend on Linux.
