LAUNCHSTATION.zip - the full install kit
=========================================

This ZIP has the same layout as RiptOPL's release ZIP, so one copy to the root
of a game device (USB, MMCE, MX4SIO, exFAT HDD, iLink ...) installs
Launchstation together with the external loaders it hands games to. The
ordinary APP_LAUNCHSTATION packages and PSUs are unchanged; this kit is the
same app plus the companions.

  APPS/APP_LAUNCHSTATION/   Launchstation (LAUNCHSTATION.ELF, title.cfg,
                            WLE-UPDATE-CA.PEM, BGM.ADP) - the standard
                            controller build. The DualShock (DS34) build is
                            published separately as APP_LAUNCHSTATION-DS.
  APP_LAUNCHSTATION.psu     The same four files as a memory-card save. Install
                            it with "Extract PSU" (PSU paste) in a memory-card
                            folder from Launchstation or wLaunchELF.
  POPS/                     POPStarter and its companions for PS1 .VCD images.
                            Launchstation starts a VCD in <device>:/POPS/ with
                            <device>:/POPS/POPSTARTER.ELF (or the POPStarter path
                            set under Settings > Launching > Disc & image
                            loaders). "POPSTARTER VERSIONS" holds the alternate
                            POPStarter builds; copy one over POPS/POPSTARTER.ELF
                            if you need it. usbd.irx.<mode> + usbhdfsd.irx.<mode>
                            are the BDMAssault driver pairs POPStarter loads from
                            mc?:/POPSTARTER/usbd.irx and usbhdfsd.irx: copy the
                            pair for your device there WITHOUT the .<mode> suffix
                            (RiptOPL and POPSLoader can do this for you).
                            UDPFS-NOTICES/ carries the UDPFS driver licences.
  EMBER/                    The Ember PS1 core (ember.elf) with its licence and
                            games/ layout. Point Settings > Launching > Disc &
                            image loaders > Ember directory at this folder and
                            put each game in EMBER/games/<Game>/ (.CUE/.BIN/.EXE).
                            Ember needs YOUR PlayStation BIOS as bios.bin
                            (exactly 512 KB) beside ember.elf; no BIOS is
                            included and none may be redistributed.
  neutrino/                 The latest upstream Neutrino build
                            (github.com/ps2max32/neutrino, "latest" release) as
                            downloaded when this kit was built. Launchstation
                            starts PS2 .ISO images through <device>:/neutrino/
                            neutrino.elf on the game's device, mc0:/mc1:/neutrino/,
                            or the Neutrino path set in Settings. ISOs on an SMB
                            share need this folder on a local device.
  *.url                     PC-side companions: PS2-Servers (UDPFS/SMB/HTTP
                            servers), the Android UDPFS server, the OPL PS1
                            AIO converter, OrbitPS2 Manager and the
                            Launchstation documentation.
  KIT-MANIFEST.json         Build number, source commit, the RiptOPL commit the
                            POPS/ and EMBER/ files are pinned to, the Neutrino
                            archive digest and the SHA-256 of every file here.

POPS/ and EMBER/ are the files RiptOPL ships, taken unchanged from the pinned
RiptOPL commit named in KIT-MANIFEST.json. Nothing in this kit is embedded in
LAUNCHSTATION.ELF, and no game, BIOS or key is included.

Credits for the bundled externals (none of them is Launchstation's work):
  Ember        created by Gageformer. Included unmodified and complete with its
               licence (EMBER/LICENSE-BETA.txt), as that licence permits for a
               free launcher. Official release page: the link in
               EMBER/Ember Official Webpage.url (https://github.com/Gageformer/Ember).
  POPStarter   by krHACKen (https://www.psx-place.com/threads/popstarter.19139/).
  Neutrino     by Rick Gaiser (Maximus32) and contributors
               (https://github.com/ps2max32/neutrino); its LICENSE file is in neutrino/.
  BDM Assault  the POPStarter block-device driver pairs (usbd.irx.<mode>):
               original project by israpps, exFAT/UDPFS variants by Okeanos
               (Okeanos86, https://github.com/Okeanos86/BDMAssault); notices in
               POPS/UDPFS-NOTICES/.
  RiptOPL      the POPS/ and EMBER/ folders are assembled by the RiptOPL project
               (https://github.com/NathanNeurotic/RiptOPL).

Documentation: https://nathanneurotic.github.io/Launchstation/
