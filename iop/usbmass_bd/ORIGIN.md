# USB mass storage driver provenance

Source: ps2build/sdk-core tag `v2026.11.10` (commit `a3e4956`), `core/usbmass_bd/src`, the
driver Launchstation embedded before this copy. Same code as ps2dev/ps2sdk
`iop/usb/usbmass_bd` at `0c36f32`. Copyright notices are kept; licence AFL-2.0 (LICENSE).

Built by the `usbmass_bd_fork` target in ps2.yaml and embedded under the SDK name
`usbmass_bd`, so every place that loads `usbmass_bd_irx` (startup, the Ember handoff, the
wLE IGR helper) gets this copy.

Changes, all for two USB drives plugged in at once:

1. `usb_mass.c`: one command at a time across both drives. `usb_queue_cmd` builds every
   command in one static block (`ucmd`, with the static `tag`) shared by both drives, with
   no lock. The update thread (a drive starting up), the BDM thread (reading a partition
   table) and the file server (reading a folder) can each be sending a command to a
   different drive at the same time. The second caller then overwrote the first caller's
   command before the USB controller sent it, so one drive received the other drive's
   command and the first caller waited forever for a status that never came (no timeout).
   A semaphore now covers the whole command. The USB 1.1 bus is shared by both ports, so
   this costs no real throughput.
2. `scsi.c`: start-up waits a bounded time for "ready". The `TEST UNIT READY` loop had no
   limit and no delay. A drive marked offline (after a failed reset) fails every command at
   once, so the loop spun forever in the update thread (priority 0x10) and starved the rest
   of the IOP. It now waits 10 ms between tries and gives up after 1000 tries (about 10 s,
   enough for a spinning-up hard drive).
3. `scsi.c`: a drive whose start-up failed is not handed to BDM. It has no size, and BDM's
   connect message divides by the sector size.

Smaller fixes found in review of this copy:

4. `scsi.c`: a request that starts below the 32-bit LBA limit but ends above it uses
   READ/WRITE(16), not READ/WRITE(10).
5. `scsi.c`: a drive that reports a sector size of 0 is left out (the size math divides by it).
6. `usb_mass.c`: a zero-length packet ends a bulk transfer instead of asking again forever, and
   the command fails if data was still missing.
7. `usb_mass.c`: unplugging a drive whose start-up failed frees its slot, so it is found again
   when plugged back in.
8. `usb_mass.c`: a status read (CSW) that returns fewer than 13 bytes is read again instead of
   being taken as success; the drive stays in step.
9. `scsi.c`: a drive unplugged during start-up stops the start-up instead of using a cleared pointer.
10. `usb_mass.c`: start-up fails cleanly if the driver's semaphores cannot be created, and frees
    the one that was created.
11. `scsi.c`: a drive unplugged during its last start-up step is not handed to BDM, and one
    unplugged while being handed to BDM is taken out again.

Host test: `tests/test_usbmass_two_drives.py` runs this source against two fake drives.
Pass that script sdk-core's `core/usbmass_bd/src` to see the unpatched driver fail (a
command never finishes; the offline drive never gives up).

NOT verified on a real PS2. The host test uses host threads and fake drives; it does not
emulate USB timing, real drives or the IOP scheduler. The same code ships in sdk-core's
`usbmass_bd` and `usbmass_bd_mini`, which other PS2 apps use, so the fix belongs upstream
too. Remove this copy once sdk-core carries these changes.
