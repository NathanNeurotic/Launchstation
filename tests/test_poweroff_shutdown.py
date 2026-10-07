"""Guard shutdown against device-stack hangs and lingering dashboard audio."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
init = (root / "src/init.c").read_text(encoding="utf-8")
dash = (root / "src/dash_main.c").read_text(encoding="utf-8")

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

poweroff = body(init, "void closeAllAndPoweroff(void)")
run_misc = body(dash, "static void runMiscCommand(")

# DEV9 shutdown is best effort. Never restore the historical unbounded
# while(dev9x DDIOC_OFF < 0) loop; a wedged/network-only stack must not make
# the console sit forever at Powering off.
assert "int retry = 100;" in poweroff
assert "while (retry-- > 0)" in poweroff
assert 'fileXioDevctl("dev9x:", DDIOC_OFF' in poweroff
assert "if (dev9_ret >= 0)" in poweroff
assert "poweroffShutdown();" in poweroff
assert poweroff.index("while (retry-- > 0)") < poweroff.index("poweroffShutdown();")
assert 'while (fileXioDevctl("dev9x:"' not in poweroff

# Preserve graceful storage handling before the final shutdown call.
assert 'PDIOC_CLOSEALL' in poweroff
assert "idleDev9StorageForPoweroff();" in poweroff
assert "stopUsbMassForPoweroff();" in poweroff

# The dashboard's looping SPU2 music must end before entering the blocking
# poweroff path. If poweroff returns, afterMiscCommand restores sound.
assert "poweroff_command = 1" in run_misc
assert "if (poweroff_command)" in run_misc
assert "dashSoundShutdown();" in run_misc
assert run_misc.index("dashSoundShutdown();") < run_misc.index("ExecuteMainAction(path, &exec_ctx);")
assert "afterMiscCommand();" in run_misc

print("PASS: poweroff bounds DEV9 teardown and stops dashboard audio before shutdown")
