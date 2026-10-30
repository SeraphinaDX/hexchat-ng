# Qt 6 frontend (experimental)

This is the first milestone of the HexChat-NG Qt port. The original C core and
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

This is a prototype: use **Server > Connect**, then **Server > Join Channel**.
You can also enter regular HexChat commands in the input field. The Qt
executable shares HexChat's ordinary configuration unless you specify
\`--cfgdir /path/to/config\`.

The frontend-to-core boundary lives in \`qt-bridge.h\`. GLib socket sources
and timers are processed through a temporary Qt timer-driven integration.
The next milestone is a properly integrated GLib event dispatcher, complete
network management, the IRC text renderer, DCC windows, menus and plugin GUI
support. GTK-specific plugins are **not** yet compatible with Qt.

**Known limitations:** no rich IRC formatting; incomplete dialogs, network
list editor, DCC UI, dynamic menus, user-list selection, logging widgets,
ignore list UI, or GTK plugin window handles. Do not treat this as a
feature-complete HexChat replacement. The Qt compile has passed the Ubuntu CI build; live IRC
operation and packaging still require manual testing.
