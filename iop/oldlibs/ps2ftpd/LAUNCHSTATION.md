# Launchstation FTP integration

The default PS2BUILD target compiles this directory into `ps2ftpd.irx` and
embeds it in Launchstation. `src/init.c` loads it when FTP is requested.
It has a separate filesystem layer from the EE file browser.

| File | Responsibility |
| --- | --- |
| `FileSystem.c` / `.h` | Export-based device discovery, virtual-to-driver paths, native file operations, directory metadata and PFS mount generations. |
| `FtpCommands.c` | FTP command replies and dispatch into the filesystem layer. |
| `FtpClient.c` / `.h` | Authentication, per-connection rename state, data connections, exact transfer I/O and completion/error replies. |
| `FtpServer.c` / `.h` | Client/socket ownership, select loop and bounded TCP command framing. |
| `imports.lst` / `irx_imports.h` | IOP imports and SDK header dependencies. |
| `main.c` | Module startup, configuration and the 8 KiB server-thread stack. |

## Device and path contracts

Discovery uses `iomanX_GetDeviceList()` (import 3), without scanning the module
list or calculating private data offsets. The existing 32-slot bound is the
supported PS2BUILD legacy-hook IOMANX contract; the table is sparse, so a NULL
entry is not its end. Recheck that capacity when changing the IOMANX package.

FTP exposes `usb`, `ata`, `mx4sio`, `ilink`, `mc`, `mmce`, `hdd` and `pfs` when registered.
The shared BDM `mass` namespace is not exposed. A non-extended, USB-only legacy
USBMASS driver can be presented as `usb`; it is never used to guess the identity
of a BDM volume. Duplicate registry entries produce one visible device name.
Unit probing opens and closes roots, with two MC/MMCE slots and four PFS mounts.

`FileSystem_BuildPath()` accepts absolute or CWD-relative virtual paths, resolves
`.` and `..`, and rejects overflow. Its output buffer must hold `FS_PATH_MAX`
bytes. `FileSystem_ClassifyPath()` selects an exact device name and decimal
unit, then returns the driver-relative path. The virtual path remains the
stable identity retained by an RNFR command.

`FileSystem_ReadDir()` returns 1 for an entry, 0 for end, or a negative error.
`FileSystem_Close()` clears ownership even on an error; transfers check its
result before sending their success reply.

## Rename and transfer contracts

A pending RNFR belongs to its connection and must be followed by RNTO. It
retains the complete source path, driver and unit, closes its existence-check
handle, and is consumed by one RNTO attempt. Another client's PWD, PASV, SIZE
or RNFR cannot replace that source. PFS mount generations reject a pending
rename after its specific mount point changes.

Native rename is used only within one device and unit. Cross-device requests
and drivers without an extended rename operation return explicit errors.
There is no copy/delete emulation or case-only temporary-name workaround.
Legacy MC drivers without rename support remain unsupported for RNFR/RNTO;
normal file upload/download still uses their read/write operations.

The server uses blocking sockets and one shared transfer buffer. Every chunk
must be fully written or sent before another client can reuse it. Partial I/O
is completed in a loop; read, write, send, directory and close failures abort
the operation instead of reporting success. Failed REST seeks never reopen
with truncation. SIZE, restart offsets and MMCE EOF checks use 64-bit sizes.

The command loop handles split and coalesced TCP commands, reserves room for
the terminating NUL, and disconnects an overlong command. While a transfer is
active, unrelated commands cannot replace its filesystem handle. ABOR closes
that handle and its data connection.

IOP formatting uses the SDK's imported `sprintf`, with bounded strings and
buffers sized for the worst-case fields. Do not introduce an unprovided
`snprintf` or global socket `errno` dependency into this module.

## Validation

Run `python3 tests/test_ftp_server.py` with a host C compiler. It first compiles the IOP startup branch, including its explicit `loadcore.h`
dependency for the module-end constants. It also compiles all five production
filesystem/command/client/server/message units unchanged,
using the preserved PS2BUILD ABI headers and mocked drivers/sockets in
`tests/ftp/`. Fault cases cover short transfers, concurrent clients, directory
errors, close errors, failed resumes, large sizes and command framing. The
normal CI host-test step runs it before both target builds.
`tests/test_ftp_network.py` also builds the POSIX backend and checks two real
loopback clients, passive/active transfers, resume and command pipelining.

These host tests do not simulate IOP scheduling, physical storage, networking,
or GS/UI behavior. Physical PS2 checks should cover two simultaneous client
connections, USB0/USB1, MC uploads/downloads, MMCE and HDD/PFS native renames.
