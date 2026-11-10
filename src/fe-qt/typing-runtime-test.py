#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify IRCv3 typing notifications through the real parser and Qt display."""
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import time

source = Path(__file__).resolve().parent
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="zoitechat-typing-") as temporary:
    folder = Path(temporary)
    config = folder / "config"
    addons = config / "addons"
    addons.mkdir(parents=True)
    (config / "zoitechat.conf").write_text(
        "gui_slist_skip = 1\nidentd_server = 0\nirc_logging = 0\ntext_replay = 0\n")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "Qt6Widgets", "Qt6Network"], text=True))
    subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-shared", "-fPIC",
                    "-I" + str(source), "-I" + str(source.parent / "common"),
                    str(source / "relay-runtime-probe.cpp"), *flags,
                    "-o", str(addons / "typing-probe.so")], check=True)
    report = folder / "report"
    failures, transcript = [], []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)

        def peer():
            try:
                connection, _ = listener.accept()
                connection.settimeout(15)
                with connection, connection.makefile("rb") as stream:
                    def send(line):
                        connection.sendall((line + "\r\n").encode())

                    nick, user, cap_end, registered = "", False, False, False
                    for raw in stream:
                        line = raw.decode().rstrip("\r\n")
                        transcript.append(line)
                        words = line.split()
                        if words[:2] == ["CAP", "LS"]:
                            send(":local CAP * LS :message-tags")
                        elif words[:2] == ["CAP", "REQ"]:
                            assert "message-tags" in line, line
                            send(":local CAP * ACK :message-tags")
                        elif words[:2] == ["CAP", "END"]:
                            cap_end = True
                        elif words[0] == "NICK":
                            nick = words[1]
                        elif words[0] == "USER":
                            user = True
                        elif words[0] == "PRIVMSG" and line.endswith(":saw active"):
                            send("@+typing=paused :peer!user@host TAGMSG #typing")
                        elif words[0] == "PRIVMSG" and line.endswith(":saw paused"):
                            send("@+typing=done :peer!user@host TAGMSG #typing")
                        if nick and user and cap_end and not registered:
                            registered = True
                            send(f":local 001 {nick} :Welcome")
                            send(f":local 005 {nick} CHANTYPES=# CLIENTTAGDENY=*,-typing :supported")
                            send(f":local 376 {nick} :End of MOTD")
                            send(f":{nick}!user@host JOIN #typing")
                            send(f":local 353 {nick} = #typing :{nick} peer")
                            send(f":local 366 {nick} #typing :End of NAMES")
                            send("@+typing=active :peer!user@host TAGMSG #typing")
            except Exception as error:
                failures.append(repr(error))

        worker = threading.Thread(target=peer, daemon=True)
        worker.start()
        environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_RELAY_SCENARIO": "direct-typing",
                       "QT_RELAY_DIRECT_PORT": str(listener.getsockname()[1]),
                       "QT_RELAY_REPORT": str(report), "ZOITECHAT_LIBDIR": str(folder / "empty")}
        with tempfile.TemporaryFile() as output:
            process = subprocess.Popen([str(binary), "--cfgdir", str(config), "--no-auto"],
                                       env=environment, stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline and not report.exists() and not failures and process.poll() is None:
                    time.sleep(0.05)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            worker.join(timeout=1)
            status = report.read_text() if report.exists() else "no probe report"
            output.seek(0)
            assert status.startswith("PASS"), (status, failures, transcript, output.read().decode(errors="replace"))
            print(status)
