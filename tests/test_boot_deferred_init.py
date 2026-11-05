"""Console identity and audio start wait for the dashboard fade-in (#277)."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
main = (root / "src/dash_main.c").read_text()


def function(source, signature):
    a = source.index(signature)
    b = source.index("{", a) + 1
    depth = 1
    while depth:
        depth += (source[b] == "{") - (source[b] == "}")
        b += 1
    return source[a:b]


first_init = function(main, "static void firstInit(void)")
dash_run = function(main, "void DashRun(const MainExecuteContext *ctx, int from_boot)")
pre_loop, loop = dash_run.split("while (1) {", 1)

# nothing on the first screen needs these; they used to run before the first frame
assert "dashConsoleIdentityInit" not in first_init
assert "dashSoundInit" not in pre_loop
assert "dashBgmStart" not in pre_loop
assert "dashSoundPlay(DASH_SND_BOOT)" not in pre_loop

# one deferred step, gated on the fade like the first-run welcome and on an idle
# worker: identity reads use CDVD RPC and audio loads IOP modules; it must not
# eat into the boot delay (held-key sample) or the auto-start countdown (cancel)
step = re.search(r"if \(boot_init_pending && !boot_phase && fade_in > 0\.95f && !dashWorkerIoBusy\(\) && !dashIopParked\(\)\) \{(.*?)\n\t\t\}", loop, re.S)
assert step, "deferred boot step missing"
body = step.group(1)
assert "boot_init_pending = 0;" in body
assert body.index("dashConsoleIdentityInit()") < body.index("dashSoundInit()") < body.index("dashBgmStart()")
assert "dashSoundPlay(DASH_SND_BOOT)" in body
# with animations off the fade snaps to 1.0 on the first update, so the step
# must sit after the frame present or it still delays the first frame
assert loop.index("dashGfxPresent();") < step.start()

# the maintenance pass would otherwise start audio on the first frame by itself
assert "if (!boot_init_pending)\n\t\t\tdashSoundMaintain();" in loop

print("PASS: console identity and audio start are deferred until the dashboard has faded in")
