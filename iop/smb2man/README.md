# smb2man (the `smb:` device, SMB2/SMB3)

Up to 8 SMB2/SMB3 shares connected at once, each listed as `smb:/<NAME>/`. Built from source
by `ps2.yaml` (target `smb2man`) and embedded in the ELF; `src/init.c` loads it only when `smb:`
is opened.

| | |
|---|---|
| Protocol | [usmb2](https://github.com/sahlberg/usmb2) by [Ronnie Sahlberg](https://github.com/sahlberg), MIT ([usmb2/LICENSE](usmb2/LICENSE)), commit `7171d08`, with [RiptOPL](https://github.com/NathanNeurotic/Open-PS2-Loader)'s patch ([usmb2/riptopl.patch](usmb2/riptopl.patch), described in [usmb2/VENDORED.txt](usmb2/VENDORED.txt)) |
| Front end | `smb2man.c`: RiptOPL's `modules/network/smb2man/smb2man.c` (rebuild/main `9b8fea0`, AFL-3.0), one session made one per share, behind the interface of the libsmb2 driver it replaced |
| Signing | `smb2auth.c/.h`: RiptOPL's `modules/iopcore/cdvdman/smb2auth.c` (AFL-3.0), unchanged |
| `imports.lst`, `irx_imports.h`, `linkfile`, `ps2iop-compat.h` | RiptOPL's, `imports.lst` and `irx_imports.h` trimmed to what this driver calls |
| Interface | [`include/smb2man.h`](../../include/smb2man.h): `SMB2_DEVCTL_CONNECT` (`smb2Connect_in_t`: name, user, password, URL) and `SMB2_DEVCTL_DISCONNECT_ALL`, as libsmb2's `lib/ps2/ps2smb2.h` |

The files under `usmb2/`, `smb2auth.c/.h`, `linkfile` and `ps2iop-compat.h` are byte for byte
RiptOPL's at `9b8fea0`, except one Launchstation change in `usmb2/hmac-md5.c`: the user name
hashed for NTOWFv2 is upper-cased for Latin-1 letters (`é` to `É`, `ÿ` to U+0178), as Windows and
Samba do, where `toupper()` left them, so a user name with such a letter was refused. RiptOPL has
the same code.

`usmb2/VENDORED.txt` is RiptOPL's and its paths are RiptOPL's: `tests/smb2-live/readreply.c` is
RiptOPL's test (this repository's is `tests/smb2man_live/`, which does not cover the READ
DataOffset cases), and the `ps2smb.h` devctls of OPL's network menu are, here, the ones in
`include/smb2man.h`.

## What it does

- `SMB2_DEVCTL_CONNECT` takes `smb://<IPv4>[:port]/<share>[/anything]` (anything after the share
  is ignored, as libsmb2's driver did). It connects at once (TCP connect bounded at 5 s, every
  wait for a reply at 15 s with no data moving) and returns 0, `-EEXIST` for a name already
  connected, or `-SMB2_CONNECT_ERR_*` saying which step failed. A share that failed is not kept.
- The device root lists the connected names; the first path component picks the share.
- Dialects: 2.0.2 and 2.1 are offered first, 3.0 and 3.0.2 only when the server refuses those.
- Signing: when an SMB 2.x server requires it, requests are signed and every reply is verified
  (HMAC-SHA256), file data included. A guest logon is not signed (servers do not require it).
- A session the server closed while idle is found before the next request and connected again;
  one that fails under a request ends, and its next use connects it again. Open files and folders
  of an ended session return `-EBADF`. A reconnect that fails is not tried again for 30 s.
- Rename works within one share only (`-EINVAL` across shares). `chstat` is not implemented
  (`-EIO`); the EE skips it for `smb:`.

## What it does not do

Compared with the libsmb2 driver: no host names (an IPv4 address only; `SMB2.CNF` refuses a name),
no share listing (Launchstation never used it), no SMB 3.1.1, no SMB 3.x signing (a 3.x server that
requires it is refused with its own message), no encryption. User names and passwords must be
Latin-1 (NTLM as usmb2 does it), at most 31 bytes.

## Tests

`tests/smb2man_live/run.sh` builds the driver for the host under AddressSanitizer and UBSan and
drives it against Samba (see the script for the cases), in CI. It does not exercise the IOP, its
ps2ip stack or real hardware.
