"""Guard Ember's storage-preserving inherited-IOP handoff."""
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
generic = body(actions, "void CleanUpForExec(")

# Ember's documented contract is inherited storage: no IOP reset before the child.
assert "CleanUpForEmberExec()" in launch
assert "RunLoaderElfKeepIOPWithArgv0" in launch
assert "prepareEmberCleanUsbIop" not in launch
assert "SifIopReset" not in launch
assert "Reset()" not in launch
assert "unmountAll" not in launch

# Drain dashboard activity and stop audio without destroying storage drivers.
assert "settleBackgroundWork" in prepare
assert "filerRootMcPollIdle()" in prepare
assert "dashSoundShutdown()" in prepare

# Release this EE process's pad RPC client so Ember can initialize ROM pad drivers.
assert "cleanUpForExecInternal(1)" in cleanup
assert "padPortClose(1, 0)" in internal
assert "padPortClose(0, 0)" in internal
assert "padEnd();" in internal

# Keep ordinary launches on their established legacy cleanup path.
assert "cleanUpForExecInternal(0)" in generic

# Embedded loader reset remains opt-in via R; Ember's keep-IOP caller does not request it.
assert "reset_iop = 0;" in embedded_main
assert "case 'R':" in embedded_main

print("PASS: Ember drains EE clients and preserves inherited storage IOP")
