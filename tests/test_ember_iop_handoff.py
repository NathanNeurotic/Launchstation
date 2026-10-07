"""Guard Ember's launcHER-style USB storage handoff."""
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
usb = body(init, "int prepareEmberUsbHandoffIop(")
embedded_main = body(loader, "int main(")
generic = body(actions, "void CleanUpForExec(")

# Dashboard-owned IO/audio must be gone before the handoff starts.
assert "CleanUpForEmberExec()" in launch
assert "settleBackgroundWork" in prepare
assert "filerRootMcPollIdle()" in prepare
assert "dashSoundShutdown()" in prepare
assert "cleanUpForExecInternal(1)" in cleanup
assert "padPortClose(1, 0)" in internal
assert "padPortClose(0, 0)" in internal
assert "padEnd();" in internal

# USB gets one clean IOP reset BEFORE the final storage stack is built.
assert "prepareEmberUsbHandoffIop(ember_elf)" in launch
assert "SifIopReset" in usb
assert "SifIopSync" in usb
assert "resetUsbHostControllerBeforeIopReset" in usb

# Mirror launcHER's dependency order: basic filesystem/SIO2 services,
# then BDM/FatFs, then the USB transport. PADMAN must remain absent.
order = [
    '"IOMANX"', '"FILEXIO"', "emberStartSio2man()",
    '"MCMAN"', '"MCSERV"', '"BDM"', '"BDMFS_FATFS"',
    '"USBD"', '"USBMASS_BD"',
]
positions = [usb.index(token) for token in order]
assert positions == sorted(positions)

usb_code = re.sub(r"/\*.*?\*/", "", usb, flags=re.S)
usb_code = re.sub(r"//.*", "", usb_code)
assert "padman_irx" not in usb_code
assert "setupPad(" not in usb_code
assert "fileXioInit()" in usb
assert "fileXioGetStat(probe" in usb
assert "DelayThread(100 * 1000)" in usb

# After the stack is ready, the embedded loader MUST NOT reset it again.
assert "RunLoaderElfKeepIOPWithArgv0" in launch
assert "reset_iop = 0;" in embedded_main
assert "case 'R':" in embedded_main

# Ordinary ELF launches retain legacy cleanup semantics.
assert "cleanUpForExecInternal(0)" in generic

print("PASS: Ember USB gets one launcHER-style storage reset, no PADMAN, and no final reset")
