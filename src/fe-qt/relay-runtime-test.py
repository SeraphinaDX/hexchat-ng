#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise native relay transport, snapshots, history, reconnect and SSH trust."""
import base64
import hashlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import time
try:
    import paramiko
except ImportError:
    print("SKIP: relay SSH integration requires Python Paramiko")
    sys.exit(77)


def run(binary, key_tool, scenario):
    source = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="hexchat-ng-relay-") as temporary:
        folder = Path(temporary)
        config = folder / "config"
        addons = config / "addons"
        addons.mkdir(parents=True)
        environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
        private_key = folder / "client_ed25519"
        subprocess.run([str(key_tool), "--generate-key", str(private_key)], env=environment, check=True)
        if scenario == "encrypted":
            subprocess.run(["ssh-keygen", "-q", "-p", "-P", "", "-N", "relay-test-passphrase", "-f", str(private_key)], check=True, capture_output=True)
        client_key = paramiko.Ed25519Key.from_private_key_file(str(private_key), password="relay-test-passphrase" if scenario == "encrypted" else None)
        host_key = paramiko.RSAKey.generate(2048)
        fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(host_key.asbytes()).digest()).decode().rstrip("=")
        report = folder / "report"
        (config / "zoitechat.conf").write_text("gui_slist_skip = 1\nidentd_server = 0\nirc_logging = 0\ntext_replay = 1\n")
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Widgets", "Qt6Network"], text=True))
        subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-shared", "-fPIC",
                        "-I" + str(source), "-I" + str(source.parent / "common"),
                        str(source / "relay-runtime-probe.cpp"), *flags, "-o", str(addons / "relay-probe.so")], check=True)
        authenticated, channels, actions, failures = [], [], [], []
        accepted = threading.Event()
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        profile = {"id": "fixture", "name": "Local", "host": "127.0.0.1", "port": listener.getsockname()[1], "user": "copperline",
                   "key": str(private_key), "fingerprint": "SHA256:wrong" if scenario == "mismatch" else "" if scenario in {"trust", "reject"} else fingerprint, "autoconnect": scenario != "encrypted", "reconnect": True}
        (config / "ansichat-relays.json").write_text(json.dumps([profile]))

        class Authentication(paramiko.ServerInterface):
            def check_auth_publickey(self, username, key):
                authenticated.append(username)
                return paramiko.AUTH_SUCCESSFUL if username == "copperline" and key.asbytes() == client_key.asbytes() else paramiko.AUTH_FAILED

            def get_allowed_auths(self, username):
                return "publickey"

            def check_channel_request(self, kind, channel_id):
                channels.append(kind)
                return paramiko.OPEN_SUCCEEDED if kind == "copperline-relay@copperline" else paramiko.OPEN_FAILED_UNKNOWN_CHANNEL_TYPE

        def message(identifier, text, nick="peer", replay=False):
            return {"RelayID": identifier, "Server": "testnet", "Target": "#relay", "Nick": nick, "Text": text, "Kind": "message",
                    "Time": "2026-11-03T15:00:00Z", "Replay": replay}

        history = message("history", "retained history", replay=True)
        live = message("live", "live from relay")
        own = message("own", "message through relay  ", "tester")
        seen = []

        def peer():
            try:
                for attachment in range(3 if scenario == "trust" else 2 if scenario in {"normal", "encrypted"} else 1):
                    iteration = attachment - (1 if scenario == "trust" else 0)
                    connection, _ = listener.accept()
                    with connection:
                        transport = paramiko.Transport(connection)
                        transport.add_server_key(host_key)
                        transport.start_server(server=Authentication())
                        channel = transport.accept(10)
                        if scenario == "mismatch" or (scenario in {"trust", "reject"} and attachment == 0):
                            assert channel is None and not authenticated, (channels, authenticated)
                            transport.close()
                            if scenario == "trust": continue
                            accepted.set()
                            return
                        assert channel is not None, "client did not open relay channel"
                        channel.settimeout(10)
                        stream = channel.makefile("rb")
                        hello = json.loads(stream.readline())
                        assert hello == {"type": "hello", "version": 2, "bool": False}, hello
                        def send(frame):
                            channel.sendall((json.dumps(frame) + "\n").encode())
                        send({"type": "hello", "version": 99 if scenario == "version" else 2})
                        if scenario == "version":
                            channel.recv(1)
                            transport.close()
                            accepted.set()
                            return
                        send({"type": "history", "messages": [history] if iteration == 0 else [history, live, own]})
                        snapshot = {"servers": [{"name": "testnet", "nick": "tester", "connected": True,
                                                 "targets": [{"target": "#relay", "joined": True, "topic": "relay topic <b>literal</b>",
                                                              "users": [{"nick": "tester", "prefix": ""}, {"nick": "peer", "prefix": "@"}], "typing_users": ["peer"]}]}]}
                        if iteration:
                            snapshot["servers"][0]["targets"].append({"target": "#extra", "joined": True, "users": []})
                        send({"type": "snapshot", "snapshot": snapshot})
                        send({"type": "ready"})
                        send({"type": "message", "message": live if iteration == 0 else message("after", "after reconnect")})
                        for raw in stream:
                            frame = json.loads(raw)
                            assert frame["type"] == "request", frame
                            action = frame["action"]
                            actions.append(action)
                            assert action != "disconnect", "detaching client disconnected remote IRC"
                            if action != "ping":
                                assert frame.get("server") == "testnet", frame
                            if action == "message":
                                assert frame["target"] == "#relay" and frame["text"] == "message through relay  ", frame
                                send({"type": "message", "message": own})
                            if action == "action":
                                assert frame["text"] == "waves", frame
                            if action == "typing":
                                assert frame["text"] == "active", frame
                            if action == "raw":
                                assert not frame["text"].startswith(("QUIT", "PRIVMSG", "NOTICE")), frame
                            send({"type": "response", "id": frame["id"], "bool": True})
                            if action in {"message", "action", "join", "typing"}:
                                seen.append(action)
                            if iteration == 0 and {"message", "action", "join", "typing"} <= set(seen):
                                transport.close()
                                break
                        transport.close()
                accepted.set()
            except (EOFError, socket.error) as error:
                if scenario in {"mismatch", "version", "reject"}:
                    accepted.set()
                else:
                    failures.append(repr(error))
            except Exception as error:
                failures.append(repr(error))

        worker = threading.Thread(target=peer, daemon=True)
        worker.start()
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            environment["QT_RELAY_DIRECT_PORT"] = str(reserve.getsockname()[1])
        environment.update({"QT_RELAY_REPORT": str(report), "QT_RELAY_SCENARIO": scenario, "ZOITECHAT_LIBDIR": str(folder / "empty")})
        process = subprocess.Popen([str(binary), "--cfgdir", str(config)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline and not failures:
                if report.exists() or process.poll() is not None:
                    break
                time.sleep(0.05)
            worker.join(timeout=2)
        finally:
            process.terminate()
            try:
                output, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate(timeout=5)
            listener.close()
        status = report.read_text() if report.exists() else "no probe report"
        assert not failures and status.startswith("PASS"), (scenario, failures, status, actions, output.decode())
        assert accepted.is_set(), ("relay did not detach", actions)
        if scenario in {"normal", "trust", "encrypted"}:
            assert channels == ["copperline-relay@copperline"] * 2, channels
            assert {"message", "action", "join", "typing"} <= set(actions), actions
            if scenario == "trust":
                assert json.loads((config / "ansichat-relays.json").read_text())[0]["fingerprint"] == fingerprint
        elif scenario in {"mismatch", "reject"}:
            assert not authenticated and not channels, "client authenticated before verifying server"
        assert "relay-test-passphrase" not in (config / "ansichat-relays.json").read_text()
        print("PASS AnsiChat SSH relay:", scenario)


if __name__ == "__main__":
    for scenario in ("normal", "encrypted", "trust", "reject", "mismatch", "version"):
        run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), scenario)
