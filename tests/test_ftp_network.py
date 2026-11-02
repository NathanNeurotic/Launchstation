"""Exercise production FTP sockets/commands with two real loopback clients.

Uses the POSIX filesystem backend in a temporary directory, not PS2 drivers.
"""
from pathlib import Path
import ftplib
import io
import os
import shlex
import shutil
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[1]
source = root / "iop/oldlibs/ps2ftpd/src"
cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not cc:
    raise SystemExit("A host C compiler is required")
with tempfile.TemporaryDirectory(prefix="launchstation-ftp-network-") as tmp:
    work = Path(tmp)
    exe = work / "ftp-server"
    subprocess.run([
        cc, *shlex.split(os.environ.get("CFLAGS", "")),
        "-std=gnu11", "-DLINUX", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
        *[str(source / name) for name in
          ("FileSystem.c", "FtpClient.c", "FtpCommands.c", "FtpServer.c", "FtpMessages.c", "main.c")],
        "-o", str(exe),
    ], check=True)
    storage = work / "storage"
    storage.mkdir()
    (storage / "folder").mkdir()
    (storage / "first.txt").write_bytes(b"first")
    (storage / "second.txt").write_bytes(b"second")
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    server = subprocess.Popen([str(exe), "-port", str(port)], cwd=storage,
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    clients = []
    try:
        deadline = time.monotonic() + 5
        while True:
            try:
                first = ftplib.FTP()
                first.connect("127.0.0.1", port, timeout=5)
                clients.append(first)
                break
            except (ConnectionRefusedError, ConnectionResetError):
                if server.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.02)
        first.login("ps2dev", "ps2dev")
        second = ftplib.FTP()
        second.connect("127.0.0.1", port, timeout=5)
        clients.append(second)
        second.login("ps2dev", "ps2dev")
        first.sendcmd("RNFR first.txt")
        second.pwd()
        second.sendcmd("SIZE second.txt")
        second.rename("second.txt", "second-renamed.txt")
        first.voidcmd("RNTO first-renamed.txt")
        assert (storage / "first-renamed.txt").read_bytes() == b"first"
        assert (storage / "second-renamed.txt").read_bytes() == b"second"
        payload = bytes((i * 17 + i // 251) & 255 for i in range(100003))
        first.storbinary("STOR upload.bin", io.BytesIO(payload), blocksize=37)
        result = bytearray()
        first.retrbinary("RETR upload.bin", result.extend, blocksize=23)
        assert bytes(result) == payload and first.size("upload.bin") == len(payload)
        second.storbinary("STOR resumed.bin", io.BytesIO(payload[:4099]))
        second.storbinary("STOR resumed.bin", io.BytesIO(payload[4099:]), rest=4099)
        assert (storage / "resumed.bin").read_bytes() == payload
        first.storbinary("STOR empty.bin", io.BytesIO(b""))
        assert first.size("empty.bin") == 0
        first.set_pasv(False)
        result.clear()
        first.retrbinary("RETR upload.bin", result.extend)
        assert bytes(result) == payload
        first.set_pasv(True)
        names = first.nlst()
        assert names.count("folder") == 1 and "upload.bin" in names
        lines = []
        first.retrlines("LIST", lines.append)
        assert sum(line.endswith(" folder") for line in lines) == 1
        first.cwd("folder")
        first.cwd(".")
        assert first.pwd() == "/folder/"
        try:
            first.cwd("missing")
        except ftplib.error_perm:
            pass
        else:
            raise AssertionError("Nonexistent CWD accepted")
        assert first.pwd() == "/folder/"
        first.cwd("/../../")
        assert first.pwd() == "/"
        first.sock.sendall(b"RNFR upload.bin\r\nRNTO pipelined.bin\r\n")
        assert first.getresp().startswith("350 ")
        assert first.getresp().startswith("250 ")
        assert (storage / "pipelined.bin").read_bytes() == payload
        for client in clients:
            client.quit()
        clients.clear()
        print("PASS: real passive/active FTP, two clients, exact transfers, resume, listings, CWD and pipelining")
    finally:
        for client in clients:
            client.close()
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        errors = server.stderr.read().decode(errors="replace")
        server.stderr.close()
        if errors:
            raise AssertionError(errors)
