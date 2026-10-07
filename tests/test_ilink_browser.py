from pathlib import Path

root = Path(__file__).resolve().parents[1]
filer = (root / "src/filer.c").read_text(encoding="utf-8")
devices = (root / "src/dash_devices.c").read_text(encoding="utf-8")
init = (root / "src/init.c").read_text(encoding="utf-8")

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

get_dir = body(filer, "int getDir(")
load_ilink = body(init, "int loadIlinkModules(")

# The device model and driver loader already knew about iLink.
assert '"iLink / FireWire"' in devices
assert 'probeRoot("ilink:/", 3000)' in devices
assert '"ILINKMAN"' in load_ilink
assert '"IEEE1394_BD"' in load_ilink

# Regression: a successful iLink mount must actually reach generic directory
# enumeration instead of falling through getDir() as an unsupported path.
assert '#ifdef ILINK' in get_dir
assert '!strncmp(path, "ilink", 5)' in get_dir
assert 'isPathDeviceStackLoaded(path)' in get_dir
assert 'loadIlinkModules()' in get_dir
assert 'readGENERICWithFirstOpenRetry(path, info, max, 3000)' in get_dir

# Keep the typed path used by the dashboard/browser; this fix is enumeration,
# not a change to Neutrino or launch-path identity.
assert 'strcpy(files[nfiles].name, "ilink:")' in filer

print("PASS: mounted iLink storage is dispatched to File Browser directory enumeration")
