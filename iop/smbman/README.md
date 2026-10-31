# smbman (SMB1) for Launchstation's `smb1:` device

A copy of Open-PS2-Loader's `modules/network/smbman-ra` at
NathanNeurotic/Open-PS2-Loader bfebafdda10fd7c090c4906e4c8930bc5000cceb,
which is itself ps2sdk's `iop/network/smbman` (jimmikaelkael, AFL-3.0) with
these fixes, all described in that repository's `docs/PS2SDK-NOTES.md#smbman`:

- "RA fix": a file the server still holds for a dead session is opened the
  old way (compatibility mode) when NT_CREATE_ANDX is refused.
- "Write-count fix": a write is complete only for the bytes the server's
  WRITE_ANDX Count says it stored.
- "ByteCount fix": a write request carries at most 65534 bytes, so its
  16-bit ByteCount does not wrap.
- a bound on the password buffer in `auth.c`.

Launchstation changes, each marked "Launchstation" in the source:

- The device name: `smb2man.irx` (SMB2/3) registers `smb`, so this driver
  is built with `SMBMAN_SMB1_DEVICE` and registers `smb1` (see
  `smb_fio.c`). Without the define it is `smb`, as upstream.
- `_start` returns `MODULE_NO_RESIDENT_END` when the device cannot be
  registered (`smb_initdev()` fails). Upstream stays resident anyway, so
  the EE, which checks the start result, would take a driver with no
  device for a loaded one.

Credits: [jimmikaelkael](https://github.com/jimmikaelkael) (smbman), the
[ps2sdk](https://github.com/ps2dev/ps2sdk) developers, and the
[Open-PS2-Loader](https://github.com/ps2homebrew/Open-PS2-Loader) fixes
above.
