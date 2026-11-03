# Retired GTK release tooling

These files describe the former GTK application and are retained as reference
for porting platform packaging to Qt. They are not active build instructions or
GitHub Actions workflows. Paths inside them refer to the old source tree.

Build the current hexchat-ng application with Meson; see
[the maintained build guide](../../src/fe-qt/README.md).
The active Qt workflow builds and tests Linux. Platform packages must use the
`hexchat-ng` executable and Qt runtime dependencies before being re-enabled.

Flatpak support has been removed, including its manifest, workflow and submodule.
