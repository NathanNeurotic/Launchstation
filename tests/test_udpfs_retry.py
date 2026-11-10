"""Opening a failed UDPFS row in the dashboard retries it with a fresh IOP, as the
classic browser does (saildot4k's Launchstation fork, 6fbac1e). Only the explicit
open resets: auto-start and library refreshes still use the plain loader."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dev = (root / "src/dash_devices.c").read_text(encoding="utf-8")
files = (root / "src/dash_files.c").read_text(encoding="utf-8")

start = dev[dev.index("int dashDevicesStart(const DashDevice *dev"):]
start = start[:start.index("\n}\n")]
retry = start[start.index("#ifdef UDPFS"):start.index("#endif")]
assert 'if (!strcmp(dev->mount, "udpfs:") && storageState(dev->mount) == DASH_DEV_ABSENT) {' in retry
# background work is drained first; backing out returns -1 like startSync
assert retry.index('dashWorkerAwaitAll("Finishing up")') < retry.index("return -1;") < retry.index("reloadUdpfsModules();")
assert start.index("reloadUdpfsModules();") < start.index("ok = startSync(dev->mount, title);")
# a refused reset (FTP still closing files) reports it instead of probing again
refused = retry[retry.index("reloadUdpfsModules();"):]
assert "if (getIopResetGeneration() == generation) {" in refused and "cb(dev->mount, 0, user);" in refused
assert dev.count("reloadUdpfsModules()") == 1  # nowhere else: probeMount keeps load_udpfs()
probe = dev[dev.index("static int probeMount(const char *m)"):]
probe = probe[:probe.index("\n}\n")]
assert "ok = load_udpfs();" in probe
assert '"UDPFS server not found - open to retry");' in dev

sel = files[files.index("static int devSelectable(int idx)"):]
sel = sel[:sel.index("\n}\n")]
assert '#ifdef UDPFS\n\t        (devs[idx].state == DASH_DEV_ABSENT && !strcmp(devs[idx].mount, "udpfs:")) ||\n#endif' in sel
print("PASS: a failed UDPFS row stays selectable and only an explicit open retries it with an IOP reset")
