# hexchat-ng

<img src="data/icons/hexchat.svg" height="180" alt="hexchat-ng icon">

hexchat-ng is an IRC client with a Qt 6 frontend built on the ZoiteChat/HexChat
C engine. This fork is beginning its rebrand from ZoiteChat. Existing core
configuration paths and plugin symbols are retained for compatibility.

See the [Qt build and usage guide](src/fe-qt/README.md) for the `hexchat-ng`
executable, dependencies and tests. The Qt frontend includes typing indicators
for channels and private conversations.

Copyright © 2026 Britney Lozza. Existing contributor notices are retained below.

Features include HexChat-compatible Python, Perl and Lua scripting support, a plugin API,
multiple server/channel windows, spell checking, multiple authentication methods including SASL,
and customizable notifications.

See [IRCHelp.org](http://irchelp.org) for information about IRC in general.  

For more information on ZoiteChat:

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

## Experimental Qt 6 frontend

An optional Qt 6 Widgets frontend is available as `zoitechat-qt`, alongside the
GTK frontend. See [the Qt frontend guide](src/fe-qt/README.md) for dependencies,
features and tests. To build only Qt without plugins:

```sh
meson setup build-qt -Dqt-frontend=true -Dgtk-frontend=false -Dplugin=false
meson compile -C build-qt
./build-qt/src/fe-qt/zoitechat-qt
```
