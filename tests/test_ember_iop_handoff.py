"""Guard Ember's hardware-proven clean USB IOP handoff."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
ember = (root / "src/ember.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
dash = (root / "src/dash_main.c").read_text(encoding="utf-8")
init = (root / "src/init.c").read_text(encoding="utf-8")
loader = (root / "loader/loader.c").read_text(encoding="utf-8")

def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    pos = brace + 1
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    return source[start:pos]

launch = body(ember, "int LaunchEmberGame(")
cleanup = body(actions, "int CleanUpForEmberExec(")
internal = body(actions, "static void cleanUpForExecInternal(")
prepare = body(dash, "int DashPrepareKeepIopHandoff(")
clean_usb = body(init, "int prepareEmberCleanUsbIop(")
embedded_main = body(loader, "int main(")

# Shared pre-handoff cleanup remains important: no worker/fileXio or audsrv
# activity is allowed to race an IOP reboot.
assert "CleanUpForEmberExec()" in launch
assert "settleBackgroundWork" in prepare
assert "filerRootMcPollIdle()" in prepare
assert "dashSoundShutdown()" in prepare
assert "cleanUpForExecInternal(1)" in cleanup
assert "padPortClose(1, 0)" in internal
assert "padPortClose(0, 0)" in internal
assert "padEnd();" in internal

# Hardware reported pad init rc=-1 with the inherited freepad/freesio2 stack.
# USB therefore MUST take the clean-IOP branch.
assert "isUsbEmberPath(ember_elf)" in launch
assert "prepareEmberCleanUsbIop(ember_elf)" in launch
assert "SifIopReset" in clean_usb
assert "SifIopSync" in clean_usb
assert "resetUsbHostControllerBeforeIopReset" in clean_usb
assert "SifLoadFileInit" in clean_usb
assert "SifInitIopHeap" in clean_usb
assert "initsbv_patches" in clean_usb

# Rebuild exactly the filesystem side needed to re-open the USB Ember tree.
for module in (
    '"IOMANX"', '"FILEXIO"', '"USBD"', '"BDM"',
    '"BDMFS_FATFS"', '"USBMASS_BD"',
):
    assert module in clean_usb
assert "fileXioInit()" in clean_usb
assert "fileXioGetStat(probe" in clean_usb
assert "DelayThread(100 * 1000)" in clean_usb

# Ember, not the launcher, must load ROM SIO2MAN/PADMAN after this reset.
# Strip comments so explanatory function names do not satisfy/fail the check.
clean_usb_code = re.sub(r"/\\*.*?\\*/", "", clean_usb, flags=re.S)
clean_usb_code = re.sub(r"//.*", "", clean_usb_code)
for forbidden in (
    "sio2man_irx", "padman_irx", "ensureCoreIoStackReady",
    "loadBasicModules", "setupPad",
):
    assert forbidden not in clean_usb_code

# Other launch types keep their established cleanup behavior.
generic = body(actions, "void CleanUpForExec(")
assert "cleanUpForExecInternal(0)" in generic

# After rebuilding USB, use the existing no-reset loader. Requesting R here
# would immediately destroy the stack we just constructed.
assert "RunLoaderElfKeepIOPWithArgv0" in launch
assert "reset_iop = 0;" in embedded_main
assert "case 'R':" in embedded_main

print("PASS: USB Ember gets a clean pad-free IOP and a rebuilt USB filesystem stack")
