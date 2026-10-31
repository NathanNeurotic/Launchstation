# smb2man.irx (SMB2/SMB3 for the `smb:` device)

| | |
|---|---|
| File | `smb2man.irx`, 141265 bytes |
| SHA-256 | `d46e88a957b90011164cf50fc2e32fc75b7db6203cff584caf5b54ade65a528d` |
| Source | [libsmb2](https://github.com/sahlberg/libsmb2), `wLaunchELF` branch, commit `d61b78126e77f731ce85a3cf663eb6c780d4f371` (`lib/ps2`) |
| Author | [Ronnie Sahlberg](https://github.com/sahlberg) |
| Licence | LGPL-2.1 (libsmb2) |
| Taken from | [saildot4k](https://github.com/saildot4k)'s [wLaunchELF_R3Z](https://github.com/saildot4k/wLaunchELF_R3Z) pull request [#63](https://github.com/saildot4k/wLaunchELF_R3Z/pull/63), `iop/__precompiled/smb2man.irx`, byte for byte |

It registers the iomanX device `smb`. `SMB2_DEVCTL_CONNECT` (see
`include/smb2man.h`) connects one share as the folder `smb:/<NAME>`, and the
device root lists the shares that are connected.

Not verified here: that this binary rebuilds from that commit. It is used as
shipped in PR #63. Rebuilding it is `make -C lib/ps2` in that libsmb2 checkout
with a ps2sdk toolchain; compare the SHA-256 above before replacing it.

Things the source at that commit does that Launchstation works around:

- `chstat` is not implemented (returns `-EIO`), so copies to `smb:` skip it,
  as for `host:` and `udpfs:`.
- Each share has a 30 s timeout, so a share that does not answer holds the
  connect for that long. Launchstation retries failed shares at most every
  30 s, and only when the device is opened.
- Rename works only within one share (`-EINVAL` across shares).
