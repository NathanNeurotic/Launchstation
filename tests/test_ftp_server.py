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
    # Compile the actual IOP startup branch, which the POSIX server excludes.
    # loadcore's constants are intentionally available only through its header.
    subprocess.run([
        cc, "-std=gnu11", "-D_IOP", "-DIOMANX_OLD_NAME_COMPATIBILITY=0",
        "-DIOMANX_OLD_NAME_ADDDELDRV=0", "-Wall", "-Werror", "-fno-builtin",
        "-include", str(root / "tests/ftp/startup.h"),
        "-I", str(root / "tests/ftp"),
        "-I", str(root / "tests/pccf-stubs"), "-I", str(source),
        "-c", str(source / "main.c"), "-o", str(Path(tmp) / "ftp-startup.o"),
    ], check=True)
    print("PASS: IOP FTP startup compiles with explicit loadcore definitions", flush=True)
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
