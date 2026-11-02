"""Compile the actual IOP FTP units against PS2BUILD ABI types and faulted I/O.

Exercises driver/socket contracts; this does not emulate a PS2 or its storage.
"""
from pathlib import Path
import os
import shutil
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not cc:
    raise SystemExit("A host C compiler is required")
source = root / "iop/oldlibs/ps2ftpd/src"
with tempfile.TemporaryDirectory(prefix="launchstation-ftp-") as tmp:
    exe = Path(tmp) / "ftp-test"
    subprocess.run([
        cc, *shlex.split(os.environ.get("CFLAGS", "")), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-g",
        "-include", str(root / "tests/ftp/platform.h"),
        "-I", str(root / "tests/pccf-stubs"), "-I", str(source),
        *[str(source / name) for name in
          ("FileSystem.c", "FtpClient.c", "FtpCommands.c", "FtpServer.c", "FtpMessages.c")],
        str(root / "tests/ftp/test_server.c"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
