# Qt 6 frontend (experimental)

This is the experimental Qt frontend of the HexChat-NG port. The original C core and
GTK frontend remain intact, and the Qt frontend is a **separate executable**.
The Qt app uses the existing IRC command parser and connection engine.

## Build on Linux

Install Qt 6 Widgets, GLib/GIO, **GLib development tools** (including
`glib-genmarshal`), Meson, Ninja, OpenSSL and HexChat's normal build
dependencies. For a Qt-only build, you do not need GTK 2.

On Arch Linux / CachyOS:

    sudo pacman -S --needed base-devel meson ninja pkgconf qt6-base glib2 glib2-devel openssl gettext

**Important:** Arch packages `glib-genmarshal` in `glib2-devel`, not `glib2`.
If Meson reports that `/usr/bin/glib-genmarshal` is missing, install
`glib2-devel` and run `meson setup build-qt --reconfigure`.

Configure and build:

    meson setup build-qt -Dqt-frontend=true -Dgtk-frontend=false \
      -Dplugin=false -Ddbus=disabled -Dlibcanberra=disabled
    meson compile -C build-qt
    ./build-qt/src/fe-qt/hexchat-qt

To configure saved networks, use **Server > Network Manager**. Select a network
and click **Connect**, or add/edit networks with their servers, identities,
authentication, TLS flags, auto-connect status, auto-join channels, and
connect commands. The **Quick Connect** option and typed `/server` commands
still work.
You can also enter regular HexChat commands in the input field. The Qt
executable shares HexChat's ordinary configuration unless you specify
\`--cfgdir /path/to/config\`.

Use **Up/Down** in the message field to browse the last 100 submitted lines
for the current conversation. Returning past the newest entry restores your
unsent draft. History is kept in memory for that conversation's lifetime.
Press **Tab** to complete a nickname from the channel's current user list;
repeated presses cycle matching names. A nickname at the start of the line
gets a `: ` suffix. **Shift+Tab** still moves keyboard focus.

Double-click a nickname (or select it and press Enter) to open a private chat.
Right-click a nickname for **Open Private Chat** and **WHOIS**. Use Ctrl/Shift
to select multiple users; each conversation keeps its own selection as users
join, leave, or change nicknames or privileges. `/uselect nick`,
`/uselect -a nick` (add), `/uselect -s nick` (scroll), and `/uselect` (clear)
also work, using the server's nickname case mapping.

The frontend-to-core boundary lives in `qt-bridge.h`. On Linux, Qt's
GLib-backed event dispatcher drives HexChat's existing GLib sources directly.
Other Qt builds use the previous bounded 15 ms GLib timer fallback (logged
at startup); they are not yet fully native. The Qt frontend now renders
mIRC formatting (bold, italic, underline, strike-through, colours including
hex/RGB, reverse and monospace), time stamps in the local timezone, and
clickable HTTP(S)/IRC URLs without interpreting chat as HTML. Each channel
has its own bounded QTextDocument and scroll position. Tests cover IRC
formatting parsing and links.

Next: DCC windows, menus, plugin GUI support and
more advanced chat customization. GTK-specific plugins are **not** yet
compatible with Qt.

**Known limitations:** incomplete dialogs, DCC UI, dynamic menus,
logging widgets,
ignore list UI, or GTK plugin window handles. Do not treat this as a
feature-complete HexChat replacement. The Qt compile has passed the Ubuntu CI build; live IRC
operation and packaging still require manual testing.

## Network manager notes

The Qt network editor writes the existing `servlist.conf` through HexChat's
own `servlist_save()`, not a separate Qt settings file. Network entries and
channels are line-separated in the editor; use `#channel,key` for a keyed
channel, and `host/+port` to request TLS on a specific endpoint. The network's
TLS checkbox also enables TLS with ordinary host entries. Saved networks
remain available in the GTK client.

**Credential warning:** Existing HexChat saves network authentication passwords
as plaintext in `servlist.conf`. The Qt editor masks password entry but does
not change the storage format. Protect your config directory and do not reuse
important passwords. Deleting a network that is currently connected will
remove it from saved settings but does not force a disconnect; the engine
will stop associating that live connection with the removed network.

Manual test: add a network, save, reopen the manager, connect, verify SASL/TLS
and auto-join, then restart the application and confirm persistence. Existing
GTK and Qt command handling should still work.

User-list manual test: open a private chat by double-clicking a prefixed nick,
request WHOIS from its context menu, Ctrl-select several users, switch channels
and back, and verify the selection remains. Try the `/uselect` variants above.
Have a selected user change nick or privileges and then leave; the selection
should follow the user until departure. Close a channel while its user context
menu is open and verify that dismissing the menu does not act on another channel.
