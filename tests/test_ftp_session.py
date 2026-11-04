"""Compile the production EE FTP session client against controllable SIF RPC stubs.

Exercises the cooperative bind/call/stop state machine; this does not emulate
a PS2 or the IOP FTP module (tests/test_ftp_server.py covers that side).
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not cc:
    raise SystemExit("A host C compiler is required")

with tempfile.TemporaryDirectory(prefix="launchstation-ftp-ee-") as tmp:
    exe = Path(tmp) / "ftp-session-test"
    if sys.platform == "win32":
        exe = exe.with_suffix(".exe")
    subprocess.run([
        cc, "-std=gnu11", "-DETH", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-g",
        "-I", str(root / "tests/ftp-ee"), "-I", str(root / "include"),
        str(root / "src/ftp_session.c"),
        str(root / "tests/ftp-ee/test_session.c"),
        "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
print("PASS: EE FTP session client state machine (host fixture)", flush=True)
