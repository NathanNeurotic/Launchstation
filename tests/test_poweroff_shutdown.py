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

poweroff = body(init, "void closeAllAndPoweroff(void)\n{")
run_misc = body(dash, "static void runMiscCommand(")
launch_app = body(dash, "void dashLaunchApp(")

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

# Any action that leaves wLaunchELF must silence dashboard SPU2 audio before
# the handoff. If a MISC handoff returns, afterMiscCommand restores it.
assert "if (leaves)" in run_misc
assert "dashSoundShutdown();" in run_misc
assert run_misc.index("dashSoundShutdown();") < run_misc.index("ExecuteMainAction(path, &launch_ctx);")
assert run_misc.index("DashChooseElfIopMode(") < run_misc.index("dashGfxRestoreLegacyState();")
assert "MainExecuteContext launch_ctx = exec_ctx;" in run_misc
assert "afterMiscCommand();" in run_misc

# Failed validation must preserve the audsrv client and uploaded samples.
assert "dashSoundShutdown();" not in launch_app
assert launch_app.index("dashSoundSuspend();") < launch_app.index("ExecuteMainAction(path, &launch_ctx);")
assert launch_app.index("dashSoundMaintain();") > launch_app.index("ExecuteMainAction(path, &launch_ctx);")
assert launch_app.index("dashBgmStart();") > launch_app.index("ExecuteMainAction(path, &launch_ctx);")
after_misc = body(dash, "static void afterMiscCommand(")
assert "dashSoundShutdown();" not in after_misc
assert "dashSoundMaintain();" in after_misc
actions = (root / "src/main_actions.c").read_text(encoding="utf-8")
cleanup = body(actions, "static void cleanUpForExecInternal(")
assert cleanup.index("dashSoundShutdown();") < cleanup.index("free(setting);")
assert "next_disc_poll = last_frame_ms + 750;" in dash
assert "else if (now >= next_device_poll)" in dash

print("PASS: poweroff is bounded and every leaving handoff stops dashboard audio")
