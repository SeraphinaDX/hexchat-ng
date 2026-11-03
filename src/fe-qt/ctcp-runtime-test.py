#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Self CTCP VERSION through a real Qt/core connection, with and without echo."""
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import time

PROBE = r'''
#include "zoitechat-plugin.h"
#include <stdio.h>
#include <stdlib.h>
static zoitechat_plugin *ph;
static int requests, replies, ticks, round;
static int request(char *word[], void *unused) { ++requests; return 0; }
static int reply(char *word[], void *unused) { ++replies; return 0; }
static void send_version(void) {
    char line[256];
    snprintf(line, sizeof(line), "ctcp %s VERSION", ph->zoitechat_get_info(ph, "nick"));
    ph->zoitechat_command(ph, line);
}
static int registered(char *word[], char *eol[], void *unused) {
    round = 1; ticks = 0; send_version(); return 0;
}
static int tick(void *unused) {
    if (!round || ++ticks < 20) return 1;
    if (requests == round && replies == round && round == 1) {
        round = 2; ticks = 0; send_version(); return 1;
    }
    FILE *out = fopen(getenv("CTCP_REPORT"), "w");
    if (out) { fprintf(out, "%d %d %d", round, requests, replies); fclose(out); }
    ph->zoitechat_command(ph, "quit"); return 0;
}
static int connect_server(void *unused) {
    char line[256];
    snprintf(line, sizeof(line), "server -insecure 127.0.0.1 %s", getenv("CTCP_PORT"));
    ph->zoitechat_command(ph, line); return 0;
}
int zoitechat_plugin_init(zoitechat_plugin *handle, char **name, char **desc, char **version, char *arg) {
    ph = handle; *name = "CTCP regression probe"; *desc = "test"; *version = "1";
    ph->zoitechat_hook_server(ph, "001", 0, registered, NULL);
    ph->zoitechat_hook_print(ph, "CTCP Generic", 0, request, NULL);
    ph->zoitechat_hook_print(ph, "Notice", 0, reply, NULL);
    ph->zoitechat_hook_timer(ph, 25, tick, NULL);
    ph->zoitechat_hook_timer(ph, 25, connect_server, NULL);
    return 1;
}
'''


def run(binary, echo, copies):
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="ctcp-self-test-") as temporary:
        folder = Path(temporary)
        config = folder / "config"
        addons = config / "addons"
        addons.mkdir(parents=True)
        (config / "zoitechat.conf").write_text(
            "gui_slist_skip = 1\nidentd_server = 0\nnet_throttle = 0\n"
            "irc_hide_version = 0\nirc_logging = 0\ntext_replay = 0\n")
        source = folder / "probe.c"
        source.write_text(PROBE)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-shared", "-fPIC",
                        "-I" + str(root / "src/common"), str(source),
                        "-o", str(addons / "ctcp-probe.so")], check=True)
        report = folder / "report"
        errors, lines = [], []
        counts = {"PRIVMSG": 0, "NOTICE": 0}
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(10)

            def peer():
                try:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(10)
                        nick = ""
                        user = ready = registered = False
                        def send(line):
                            connection.sendall((line + "\r\n").encode())
                        for raw in connection.makefile("rb"):
                            line = raw.decode().rstrip("\r\n")
                            lines.append(line)
                            parts = line.split()
                            if not parts:
                                continue
                            command = parts[0]
                            if command == "CAP":
                                if "LS" in parts:
                                    send(":local CAP * LS :" + ("echo-message" if echo else ""))
                                elif "REQ" in parts:
                                    assert echo and parts[-1] == ":echo-message", line
                                    send(":local CAP * ACK :echo-message")
                                elif "END" in parts:
                                    ready = True
                            elif command == "NICK":
                                nick = parts[1]
                            elif command == "USER":
                                user = True
                            elif command in counts:
                                assert parts[1].lower() == nick.lower(), line
                                counts[command] += 1
                                assert counts[command] <= 2, "duplicate CTCP response: " + repr(lines)
                                for _ in range(copies):
                                    send(f":{nick}!test@localhost " + line)
                            elif command == "QUIT":
                                break
                            if nick and user and ready and not registered:
                                registered = True
                                send(f":local 001 {nick} :Welcome")
                                send(f":local 376 {nick} :End of MOTD")
                except Exception as error:
                    errors.append(error)

            worker = threading.Thread(target=peer, daemon=True)
            worker.start()
            environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen",
                           "ZOITECHAT_LIBDIR": str(folder / "empty"),
                           "CTCP_PORT": str(listener.getsockname()[1]), "CTCP_REPORT": str(report)}
            process = subprocess.Popen([str(binary), "--no-auto", "--cfgdir", str(config)],
                                       env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while not report.exists() and not errors and time.monotonic() < deadline:
                    if process.poll() is not None: break
                    time.sleep(0.05)
                worker.join(timeout=1)
            finally:
                process.terminate()
                try:
                    output, _ = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    output, _ = process.communicate(timeout=5)
            assert not errors, (errors, lines, output.decode())
            assert report.exists() and report.read_text() == "2 2 2", (report.read_text() if report.exists() else "no report", lines, output.decode())
            assert counts == {"PRIVMSG": 2, "NOTICE": 2}, counts
            print(f"PASS self VERSION: echo={echo}, deliveries={copies}, two intentional requests, two displayed replies")


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    run(binary, False, 1)
    run(binary, True, 1)
    run(binary, True, 2)
