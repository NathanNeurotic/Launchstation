"""Guard the RiptOPL-style inherited-IOP contract used for Ember."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ember = (root / "src/ember.c").read_text(encoding="utf-8")
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
dash = (root / "src/dash_main.c").read_text(encoding="utf-8")
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
embedded_main = body(loader, "int main(")

# Ember is the special keep-IOP leg, not the legacy generic cleanup.
assert "CleanUpForEmberExec()" in launch
assert "CleanUpForExec();" not in launch
assert "RunLoaderElfKeepIOPWithArgv0" in launch

# The dashboard must not leave a background fileXio or audsrv client active
# when this EE process disappears but the IOP survives.
assert "settleBackgroundWork" in prepare
assert "filerRootMcPollIdle()" in prepare
assert "dashSoundShutdown()" in prepare

# Match RiptOPL's ordinary USB/BDM Ember teardown: close our ports and end
# the EE libpad RPC session, while leaving the IOP pad server and storage
# modules resident for Ember to reopen.
assert "cleanUpForExecInternal(1)" in cleanup
assert "padPortClose(1, 0)" in internal
assert "padPortClose(0, 0)" in internal
assert "if (end_pad_rpc)" in internal
assert "padEnd();" in internal

# The generic path intentionally keeps its legacy behaviour; this fix must
# not silently change every ELF launch.
generic = body(actions, "void CleanUpForExec(")
assert "cleanUpForExecInternal(0)" in generic

# No IOP reset or storage teardown is allowed in Ember's handoff.
for forbidden in ("SifIopReset", "Reset_Iop", "unmountAll", "loadUsbModules"):
    assert forbidden not in cleanup
    assert forbidden not in launch

# Embedded loader's reset is flag-controlled; Ember's keep-IOP caller does
# not request R, so the live USB/MMCE/etc stack reaches the child.
assert "reset_iop = 0;" in embedded_main
assert "case 'R':" in embedded_main

print("PASS: Ember drains EE clients, releases libpad RPC and preserves the live IOP/storage stack")
