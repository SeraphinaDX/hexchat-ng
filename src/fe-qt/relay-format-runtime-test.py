#!/usr/bin/env python3
"""Compare real relay and direct-event text formatting without an SSH server."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

source = Path(__file__).resolve().parent
binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="hexchat-ng-relay-colours-") as temporary:
    folder = Path(temporary)
    config = folder / "config"
    addons = config / "addons"
    addons.mkdir(parents=True)
    (config / "zoitechat.conf").write_text(
        "gui_slist_skip = 1\nidentd_server = 0\nirc_logging = 0\nstamp_text = 0\ntext_replay = 0\n")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "Qt6Widgets", "Qt6Network"], text=True))
    subprocess.run([*shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-shared", "-fPIC",
                    "-I" + str(source), "-I" + str(source.parent / "common"),
                    str(source / "relay-runtime-probe.cpp"), *flags,
                    "-o", str(addons / "relay-probe.so")], check=True)
    report = folder / "report"
    environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_RELAY_SCENARIO": "format",
                   "QT_RELAY_REPORT": str(report), "ZOITECHAT_LIBDIR": str(folder / "empty")}
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen([str(binary), "--cfgdir", str(config), "--no-auto"],
                                   env=environment, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and not report.exists() and process.poll() is None:
                time.sleep(0.05)
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        status = report.read_text() if report.exists() else "no probe report"
        output.seek(0)
        assert status.startswith("PASS"), (status, output.read().decode(errors="replace"))
        print(status)
