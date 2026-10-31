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

The only Launchstation change is the device name: `smb2man.irx` (SMB2/3)
registers `smb`, so this driver is built with `SMBMAN_SMB1_DEVICE` and
registers `smb1` (see `smb_fio.c`). Without the define it is `smb`, as
upstream.
