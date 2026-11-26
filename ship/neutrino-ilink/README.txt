RiptOPL's fixed i.LINK pair for the bundled Neutrino
=====================================================

LAUNCHSTATION.zip replaces neutrino/modules/iLinkman.irx and
neutrino/modules/IEEE1394_bd_mini.irx from the upstream Neutrino release with
these two files, exactly as RiptOPL's release kit does (tools/package_edition.py).
Neutrino loads them after its own IOP reset (config/bsd-ilink.toml).

Why: the stock pair never logs out of the drive, so after an IOP reset the drive
still holds the previous EXCLUSIVE login and refuses new ones for a while. The
stock driver gives up after about 0.8 s and the game never sees the disk. The
fixed driver retries the login for up to 5 s and bounds the spin-up wait; the
fixed manager carries the TX cancellation, alarm and receive-FIFO fixes,
reads each received packet by its own length, as the SDK does, and, like the
SDK, does not end a request on the controller's own STO/UResp flags.
Details: RiptOPL modules/ilink/IEEE1394_bd/ORIGIN.txt and
modules/ilink/iLinkman/ORIGIN.txt.

Source:   NathanNeurotic/RiptOPL modules/ilink/ (forked from ps2dev/ps2sdk)
Built by: RiptOPL CI at commit cddee95061e1bb9232ef88bcc5bfbc25f5243066
          (release rolling-ra, v1.2.0-Beta-4390, GCC); taken byte for byte
          from RIPTOPL-RetroAchievements-v1.2.0-Beta-4390.zip neutrino/modules/,
          whose BUILD-MANIFEST.json lists these digests:
  iLinkman.irx          15,969 B  sha256 4b8a89ed700370d23bb59932a98d6553dbe3189616a0478cb46372bbe67216a0
  IEEE1394_bd_mini.irx  10,609 B  sha256 acaebcb288d6a812763db4a561e6a9dde56a53e1093cb0898a352b96c3cce9bf
Licence:  Academic Free License 2.0 (LICENSE, from the ps2sdk sources).

To refresh: copy the two files from a newer RiptOPL release kit and update
NEUTRINO_ILINK in scripts/package_kit.py with their SHA-256.
