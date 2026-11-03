"""Check repeatable Mbed TLS __FILE__ strings across temporary build paths.

The release archive binds core ELF assets to an immutable per-build SHA-256.
Debug-enabled Mbed TLS must not leak unique mktemp paths into these bytes.
This is a portable host compiler smoke test, not a PS2 ELF reproducibility proof.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
script = (root / "scripts/build_cloud_tls.sh").read_text()

# Guard the actual CMake flag, not just a stray documentation comment.
assert 'mbedtls_path_map="-ffile-prefix-map=$(native_path "${work}")=/mbedtls-build"' in script
assert '-DCMAKE_C_FLAGS="${target_flags}' in script
assert "${mbedtls_path_map}" in script.split("-DCMAKE_C_FLAGS=", 1)[1].split("\n", 1)[0]
assert 'work="$(mktemp -d)"' in script

compiler = shutil.which("clang") or shutil.which("gcc")
assert compiler, "A host C compiler is needed to test reproducible __FILE__ expansion"

with tempfile.TemporaryDirectory(prefix="tls-file-prefix-test-") as folder:
    objects = []
    raw_objects = []
    for suffix in ("first", "second"):
        work = Path(folder) / suffix
        source = work / "mbedtls-3.6.7" / "library" / "ssl_tls.c"
        source.parent.mkdir(parents=True)
        source.write_text("const char *tls_file_name(void) { return __FILE__; }\n")
        obj = work / "mapped.o"
        plain = work / "unmapped.o"
        flags = ["-g0", "-Os"]
        subprocess.run([compiler, *flags, "-c", str(source), "-o", str(plain)], check=True)
        subprocess.run([
            compiler, *flags, f"-ffile-prefix-map={work}=/mbedtls-build",
            "-c", str(source), "-o", str(obj)
        ], check=True)
        objects.append(obj.read_bytes())
        raw_objects.append(plain.read_bytes())

    # The test must first demonstrate that unique absolute paths do alter code.
    assert raw_objects[0] != raw_objects[1], "Control failed: __FILE__ was not path-sensitive"
    assert objects[0] == objects[1], "File-prefix mapping did not stabilize compiled objects"
    assert b"/mbedtls-build/mbedtls-3.6.7/library/ssl_tls.c" in objects[0]
    assert folder.encode() not in objects[0]

print("PASS: Mbed TLS CMake path-map wiring and reproducible __FILE__ objects")
