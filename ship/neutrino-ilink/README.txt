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
fixed manager carries the TX cancellation, alarm and receive-FIFO fixes.
Details: RiptOPL modules/ilink/IEEE1394_bd/ORIGIN.txt and
modules/ilink/iLinkman/ORIGIN.txt.

Source:   NathanNeurotic/RiptOPL modules/ilink/ (forked from ps2dev/ps2sdk)
Built by: RiptOPL CI at commit 712845008f079087cf975bcc78edb3d25051ff54
          (release rolling-korium, v1.2.0-Beta-4235, GCC); taken byte for byte
          from RIPTOPL-Korium-v1.2.0-Beta-4235.zip neutrino/modules/, whose
          BUILD-MANIFEST.json lists these digests:
  iLinkman.irx          15,713 B  sha256 1b0b27ea7f65b550dafac38a38c806f0e84227b244300701f41c69c8afb85ffc
  IEEE1394_bd_mini.irx  10,577 B  sha256 958325d36a10cc6c4d33db60c9bee522085df9b5f88054eeae4b21f8245d9beb
Licence:  Academic Free License 2.0 (LICENSE, from the ps2sdk sources).

To refresh: copy the two files from a newer RiptOPL release kit and update
NEUTRINO_ILINK in scripts/package_kit.py with their SHA-256.
