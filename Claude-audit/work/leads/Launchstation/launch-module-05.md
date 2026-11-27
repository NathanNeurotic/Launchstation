## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- iop/nfsman/unfs4/unfs4.h:177  macro `unfs4_close`  (no-other-mention)
- iop/nfsman/unfs4/unfs4.h:195  macro `unfs4_closedir`  (no-other-mention)

## Comment blocks that look like code (cond-commented-code.tsv)
- iop/oldlibs/ps2ftpd/src/FtpServer.c:84-90  5 code-like lines

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `HAVE_SOCK_SIN_LEN`  tested at 2 site(s) in the repo, mentioned in 0 build file(s); here: iop/nfsman/unfs4/unfs4.c:931
- `UNFS4_FEATURE_REASSEMBLY`  tested at 5 site(s) in the repo, mentioned in 0 build file(s); here: iop/nfsman/unfs4/unfs4.c:111, iop/nfsman/unfs4/unfs4.c:125, iop/nfsman/unfs4/unfs4.c:140, iop/nfsman/unfs4/unfs4.c:182, iop/nfsman/unfs4/unfs4.c:231
- `Z80`  tested at 7 site(s) in the repo, mentioned in 0 build file(s); here: iop/nfsman/unfs4/unfs4.c:46, iop/nfsman/unfs4/unfs4.h:34

## Largely identical files (dupes-file-pairs.tsv)
- iop/nfsman/unfs4/unfs4.c is 2% the same as Launchstation/iop/smb2man/usmb2/usmb2.c (15 cloned normalised lines)
- iop/nfsman/unfs4/unfs4.c is 2% the same as RiptOPL/modules/network/smb2man/usmb2/usmb2.c (15 cloned normalised lines)
- iop/nfsman/unfs4/unfs4.c is 94% the same as RiptOPL/modules/network/nfsman/unfs4/unfs4.c (502 cloned normalised lines)
- iop/nfsman/unfs4/unfs4.h is 91% the same as RiptOPL/modules/network/nfsman/unfs4/unfs4.h (74 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as Launchstation/iop/mx4sio_bd/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as Launchstation/iop/usbmass_bd/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/modules/bdmassault/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/modules/debug/ps2link/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/modules/mx4sio_bd/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/modules/usb/usbmass_bd/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/third_party/ps2sdk-ppctty/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as RiptOPL/third_party/ps2sdk-socket/LICENSE (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE is 10% the same as Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0 (15 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpClient.c is 3% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpServer.c (14 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpClient.c is 5% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpCommands.c (25 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpCommands.c is 11% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpClient.c (25 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpCommands.c is 6% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpServer.c (14 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpServer.c is 6% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpClient.c (14 cloned normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpServer.c is 6% the same as Launchstation/iop/oldlibs/ps2ftpd/src/FtpCommands.c (14 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- iop/nfsman/unfs4/unfs4.c:155-220  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:155-220  (40 normalised lines)
- iop/nfsman/unfs4/unfs4.c:213-240  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:213-240  (19 normalised lines)
- iop/nfsman/unfs4/unfs4.c:240-327  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:240-327  (42 normalised lines)
- iop/nfsman/unfs4/unfs4.c:30-161  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:30-161  (83 normalised lines)
- iop/nfsman/unfs4/unfs4.c:30-49  ==  Launchstation/iop/smb2man/usmb2/usmb2.c:3-22  (15 normalised lines)
- iop/nfsman/unfs4/unfs4.c:30-49  ==  RiptOPL/modules/network/smb2man/usmb2/usmb2.c:3-22  (15 normalised lines)
- iop/nfsman/unfs4/unfs4.c:331-405  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:331-404  (38 normalised lines)
- iop/nfsman/unfs4/unfs4.c:396-451  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:395-450  (32 normalised lines)
- iop/nfsman/unfs4/unfs4.c:477-565  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:460-548  (50 normalised lines)
- iop/nfsman/unfs4/unfs4.c:570-610  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:553-593  (20 normalised lines)
- iop/nfsman/unfs4/unfs4.c:645-666  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:625-646  (14 normalised lines)
- iop/nfsman/unfs4/unfs4.c:668-729  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:648-709  (37 normalised lines)
- iop/nfsman/unfs4/unfs4.c:740-789  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:719-768  (35 normalised lines)
- iop/nfsman/unfs4/unfs4.c:797-940  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.c:772-915  (92 normalised lines)
- iop/nfsman/unfs4/unfs4.h:111-227  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.h:110-225  (56 normalised lines)
- iop/nfsman/unfs4/unfs4.h:37-108  ==  RiptOPL/modules/network/nfsman/unfs4/unfs4.h:36-107  (18 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  Launchstation/iop/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  Launchstation/iop/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/modules/bdmassault/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/modules/debug/ps2link/LICENSE:131-146  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/modules/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/modules/usb/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/third_party/ps2sdk-ppctty/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  RiptOPL/third_party/ps2sdk-socket/LICENSE:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/LICENSE:123-138  ==  Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0:123-138  (15 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpClient.c:11-38  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpCommands.c:11-38  (25 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpClient.c:18-31  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpServer.c:16-29  (14 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpCommands.c:11-38  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpClient.c:11-38  (25 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpCommands.c:18-31  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpServer.c:16-29  (14 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpServer.c:16-29  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpClient.c:18-31  (14 normalised lines)
- iop/oldlibs/ps2ftpd/src/FtpServer.c:16-29  ==  Launchstation/iop/oldlibs/ps2ftpd/src/FtpCommands.c:18-31  (14 normalised lines)

## TODO/FIXME/HACK style markers (cond-markers.tsv)
- iop/oldlibs/ps2ftpd/src/FtpClient.c:476  FIXME: non-blocking mode - if this is not enabled, the entire server will
- iop/oldlibs/ps2ftpd/src/FtpCommands.c:184  // TODO: validate IP?
- iop/oldlibs/ps2ftpd/src/FtpCommands.c:202  // TODO: is this proper?
- iop/oldlibs/ps2ftpd/src/FtpServer.c:233  // TODO: handle OOB data for transfer breaks
- iop/oldlibs/ps2ftpd/src/main.c:92  // TODO: fix CD/DVD support
