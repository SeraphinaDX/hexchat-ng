Building hexchat-ng
===================

Qt 6 is the default and only graphical frontend. The GTK application and its
build options have been removed. The maintained build uses Meson and Ninja.

Dependencies
------------

Install a C17/C++17 compiler, Meson, Ninja, pkg-config, Qt 6.2 or newer
(Core, Gui, Widgets and the resource compiler), Pango/Cairo 1.44 or newer,
GLib/GIO development tools, gettext, libarchive and OpenSSL. Enchant 2 enables
optional spell checking. Scripting plugins need their language development
packages; disable plugins for the smallest build.

Build and run
-------------

From the repository root::

    meson setup build-qt -Dplugin=false
    meson compile -C build-qt
    ./build-qt/src/fe-qt/hexchat-ng --no-auto

Use ``--cfgdir /path/to/test-profile`` for an isolated configuration. Qt is
selected by default. ``-Dtext-frontend=true`` additionally builds the text
interface; ``-Dqt-frontend=false`` disables the graphical application.

For offline builds, pass
``-Dpublic-suffix-list=/path/to/public_suffix_list.dat`` during setup.

Existing builds
---------------

Use a fresh build directory when upgrading from a GTK-enabled checkout; old
Meson caches can retain removed options. Remove ``-Dgtk-frontend``, ``-Dappindicator`` and ``-Dmac-integration`` from saved
commands. These GTK-only options no longer exist.

Tests
-----

Run::

    meson test -C build-qt --no-suite runtime --print-errorlogs

Plugin-enabled builds also support the runtime suite, which uses local sockets
and isolated profiles to test IRC, CTCP, TLS/SASL, scripts and DCC.

Platform packaging
------------------

The active GitHub workflow builds and tests Qt on Linux. The former GTK
Windows, macOS, AppImage and Fedora release workflows are archived in
``packaging/legacy-gtk`` as porting reference. They are not build instructions
for the current Qt application. Cross-platform packages need Qt deployment
support before those workflows can be re-enabled.
