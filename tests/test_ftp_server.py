"""Compile the actual IOP FTP units against PS2BUILD ABI types and faulted I/O.

Exercises driver/socket contracts; this does not emulate a PS2 or its storage.
"""
from pathlib import Path
import os
import shutil
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not cc:
    raise SystemExit("A host C compiler is required")
source = root / "iop/oldlibs/ps2ftpd/src"
with tempfile.TemporaryDirectory(prefix="launchstation-ftp-") as tmp:
    # A 64-bit host can inline arithmetic that requires missing helpers on IOP.
    # Compile the production formatter for a freestanding 32-bit target too.
    target = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
    if sys.platform.startswith("linux") and target.startswith(("x86_64", "i386", "i486", "i586", "i686")):
        client = (source / "FtpClient.c").read_text()
        start = client.index("char *uitoa(")
        end = client.index("{", start) + 1
        depth = 1
        while depth:
            depth += (client[end] == "{") - (client[end] == "}")
            end += 1
        formatter = Path(tmp) / "formatter.c"
        formatter.write_text("typedef unsigned long long u64;\n" + client[start:end] + "\n")
        obj = Path(tmp) / "formatter.o"
        subprocess.run([
            cc, "-m32", "-Os", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
            "-fno-stack-protector", "-c", str(formatter), "-o", str(obj),
        ], check=True)
        nm = shutil.which("nm") or shutil.which("llvm-nm")
        if not nm:
            raise SystemExit("nm is required to check the freestanding formatter")
        undefined = subprocess.check_output([nm, "-u", str(obj)], text=True).strip()
        assert not undefined, f"FTP formatter requires external runtime helpers: {undefined}"
        print("PASS: freestanding 32-bit FTP formatter has no runtime dependencies", flush=True)
    else:
        print(f"SKIP: freestanding x86 check is unavailable for {target}", flush=True)
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
