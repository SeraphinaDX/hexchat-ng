#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ ${1:-} == --help ]]; then
    cat <<'EOF'
Usage: packaging/appimage/build.sh [Meson setup options...]

Build hexchat-ng and package it with Qt 6 as an AppImage.
BUILD_DIR   Dedicated build directory (default: build-appimage)
OUTPUT_DIR  Destination directory (default: dist)
TOOLS_DIR   Cached linuxdeploy tools (default: BUILD_DIR/tools)
QMAKE       Qt 6 qmake executable (auto-detected)
VERSION     Output version (default: repository VERSION file)

Missing packaging tools are downloaded from linuxdeploy's continuous releases.
Native plugins are enabled; scripting runtimes are disabled by default.
EOF
    exit 0
fi
[[ $(uname -s) == Linux ]] || { echo 'AppImages must be built on Linux.' >&2; exit 1; }
export ARCH=$(uname -m)
case "$ARCH" in x86_64|aarch64) ;; *) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;; esac
for tool in meson ninja pkg-config curl python3; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
if [[ -z ${QMAKE:-} ]]; then
    QMAKE=$(command -v qmake6 || command -v qmake-qt6 || true)
fi
[[ -n $QMAKE && $($QMAKE -query QT_VERSION) == 6.* ]] || {
    echo 'Set QMAKE to a Qt 6 qmake executable.' >&2; exit 1;
}
export QMAKE
build=$(realpath -m -- "${BUILD_DIR:-$root/build-appimage}")
output=$(realpath -m -- "${OUTPUT_DIR:-$root/dist}")
tools=$(realpath -m -- "${TOOLS_DIR:-$build/tools}")
mkdir -p -- "$build" "$output" "$tools"
# Always use a fresh staging directory so disabled plugins cannot survive a rebuild.
appdir=$(mktemp -d "$build/AppDir.XXXXXX")
trap 'printf "Staged AppDir: %s\n" "$appdir"' EXIT

setup=()
[[ ! -f $build/meson-private/coredata.dat ]] || setup+=(--reconfigure)
meson setup "${setup[@]}" "$build" "$root" \
    --prefix=/usr --libdir=lib --buildtype=release \
    -Dqt-frontend=true -Dtext-frontend=false -Dplugin=true \
    -Dwith-python=false -Dwith-perl=false -Dwith-lua=false \
    -Dwith-fishlim=false -Dwith-sysinfo=false -Dlibcanberra=disabled \
    -Dinstall-docs=false -Dinstall-appdata=true "$@"
meson compile -C "$build"
DESTDIR="$appdir" meson install -C "$build" --no-rebuild

for name in linuxdeploy linuxdeploy-plugin-qt linuxdeploy-plugin-appimage; do
    target="$tools/$name-$ARCH.AppImage"
    if [[ ! -x $target ]]; then
        curl --fail --location --retry 3 \
            "https://github.com/linuxdeploy/$name/releases/download/continuous/$name-$ARCH.AppImage" \
            --output "$target.download"
        chmod +x "$target.download"
        mv -- "$target.download" "$target"
    fi
done
export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1
# Bundled binutils can predate the host's ELF format (for example RELR).
export NO_STRIP=1
export VERSION=${VERSION:-$(cat "$root/VERSION")}
export LDAI_OUTPUT="hexchat-ng-$VERSION-$ARCH.AppImage"
# The custom launcher is wrapped by linuxdeploy, which runs the Qt setup hooks.
cd -- "$output"
"$tools/linuxdeploy-$ARCH.AppImage" --appdir "$appdir" \
    --executable "$appdir/usr/bin/hexchat-ng" \
    --desktop-file "$appdir/usr/share/applications/net.zoite.Zoitechat.Qt.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/48x48/apps/hexchat-ng.png" \
    --custom-apprun "$root/packaging/appimage/AppRun-qt"
# Run Qt explicitly: linuxdeploy does not forward --exclude-library to plugins.
# KDE's optional image codecs can carry unavailable host-only dependencies.
"$tools/linuxdeploy-plugin-qt-$ARCH.AppImage" --appdir "$appdir" \
    --exclude-library 'kimg_*.so'
"$tools/linuxdeploy-$ARCH.AppImage" --appdir "$appdir" --output appimage
sha256sum -- "$LDAI_OUTPUT" > "$LDAI_OUTPUT.sha256"
printf 'Built %s/%s\n' "$output" "$LDAI_OUTPUT"
