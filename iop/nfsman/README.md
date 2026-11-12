# nfsman (the `nfs:` device, NFSv4.0)

Up to 8 NFS exports connected at once, each listed as `nfs:/<NAME>/`. Built from source by
`ps2.yaml` (target `nfsman`) and embedded in the ELF.

| | |
|---|---|
| Protocol | [unfs4](https://github.com/sahlberg/unfs4) by [Ronnie Sahlberg](https://github.com/sahlberg), BSD 2-Clause ([unfs4/LICENSE](unfs4/LICENSE)), commit `3c673fc` ([unfs4/VENDORED.txt](unfs4/VENDORED.txt)), the NFS sibling of usmb2 |
| Front end | `nfsman.c`: smb2man's structure (named exports, the device root, the slot generations, the bounded sockets, the reconnect backoff) on unfs4 |
| `imports.lst`, `irx_imports.h`, `linkfile` | smb2man's, with `lwip_bind` added |
| Interface | [`include/nfsman.h`](../../include/nfsman.h): `NFS_DEVCTL_CONNECT` (`nfsConnect_in_t`: name, URL, a seed from the EE's clock) and `NFS_DEVCTL_DISCONNECT_ALL`, shaped as smb2man's |

## What it does

- `NFS_DEVCTL_CONNECT` takes `nfs://<IPv4>[:port]/<export>[?uid=N&gid=N]`. It connects at once
  (TCP connect bounded at 5 s, every wait for a reply at 15 s with no data moving), looks the
  export up, and returns 0, `-EEXIST` for a name already connected, or `-NFS_CONNECT_ERR_*`
  saying which step failed. An export that failed is not kept.
- The device root lists the connected names; the first path component picks the export.
- NFSv4.0 only, on TCP port 2049 (or the URL's): no portmapper, no MOUNT protocol. The export is
  reached from the server's pseudo root, as `mount -t nfs4` does.
- AUTH_SYS with the URL's uid and gid, 0 if not given. With the usual `root_squash` that is the
  anonymous user, so the export must let it read (and write, to copy to it).
- The connection comes from a port below 1024, so the export does not need `insecure`.
- unfs4 reads and writes with the anonymous stateid and keeps no state on the server, so a
  filehandle stays good across a reconnect: a connection the server closed is connected again on
  the next call, and open files and folders carry on. A reconnect that fails is not tried again
  for 30 s.
- Creating a file opens and closes it on the server at once (unfs4 needs an OPEN for that);
  `O_TRUNC` truncates with SETATTR.
- Rename works within one export only (`-EINVAL` across exports), and replaces an existing
  target, as rename(2) does. `chstat` is not implemented (`-EIO`); the EE skips it for `nfs:`.

## What it does not do

No host names (an IPv4 address only), no NFSv3 or NFSv4.1+, no Kerberos, no locks. Names are
passed as they are (UTF-8). A path is at most 511 bytes, and with its components it must fit in
unfs4's 1 KB request (a 440 byte path with up to about 27 components); a rename carries both
paths. A path that does not fit is `-ENAMETOOLONG`.

## Tests

`tests/nfsman_live/run.sh` builds the driver for the host under AddressSanitizer and UBSan and
drives it against the kernel NFS server (see the script for the cases). It does not exercise the
IOP, its ps2ip stack or real hardware.
