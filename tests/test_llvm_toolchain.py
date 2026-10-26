"""Protect the LLVM/Clang release toolchain configuration.

Host-side only: checks build definitions and CI wiring, not compiled code.
Every EE archive linked into a Clang build must itself be Clang-built, so the
GCC-only audsrv package and the SDK's mispackaged LLVM-tier libjpeg must stay
out of ps2.yaml.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
yaml = (root / "ps2.yaml").read_text()
pccf = (root / "iop/pccf/ps2.yaml").read_text()
workflow = (root / ".github/workflows/compile.yml").read_text()
loader = (root / "loader/loader.c").read_text()
blobs = (root / "src/embed_blobs.S").read_text()
helper = (root / "scripts/ee_toolchain.sh").read_text()
tls = (root / "scripts/build_cloud_tls.sh").read_text()
jpeg = (root / "scripts/build_jpeg.sh").read_text()

assert re.search(r"^toolchain: llvm$", yaml, re.M)
assert re.search(r"^toolchain: llvm$", pccf, re.M)

# audsrv: EE client from source, unchanged IOP server embedded as a blob.
assert "name: audsrv_ee" in yaml and "ee_libs/audsrv/audsrv_rpc.c" in yaml
assert (root / "ee_libs/audsrv/audsrv_rpc.c").is_file()
assert (root / "ee_libs/audsrv/include/audsrv.h").is_file()
assert 'EMBED_FILE(audsrv_irx, "packages/world/audsrv/bin/audsrv.irx")' in blobs
assert not re.search(r"^\s+- audsrv\s*$", yaml, re.M)  # no package driver embed
assert "headers: [sior]" in yaml
libs = re.search(r"^    libs: \[iopreboot.*\]$", yaml, re.M).group(0)
assert "audsrv_ee" in libs and " audsrv," not in libs and " audsrv]" not in libs

# libjpeg-turbo: built by the active toolchain, never the SDK package archive.
assert "jpegturbo" in libs and " jpeg," not in libs
assert '"-L../.jpeg-deps/lib"' in yaml and ".jpeg-deps/include" in yaml
assert 'version="3.1.3"' in jpeg and "-DWITH_SIMD=OFF" in jpeg
assert "libjpegturbo.a" in jpeg

# Third-party archives follow ps2build's toolchain precedence.
assert "PS2BUILD_TOOLCHAIN" in helper and "llvm-toolchain" in helper
assert "mips64r5900el-ps2-elf-clang.cfg" in helper
assert 'source "${repo_root}/scripts/ee_toolchain.sh"' in tls
assert 'source "${repo_root}/scripts/ee_toolchain.sh"' in jpeg

# -mgpopt is a no-op with -G0 and unsupported by clang's implicit abicalls.
assert "-mgpopt" not in yaml

# The fixed-window loader must not drag in libpthread under LLVM newlib.
assert "PS2_DISABLE_AUTOSTART_PTHREAD();" in loader
assert "int pthread_setcancelstate(int state, int *oldstate)" in loader

# CI: LLVM present, deps built before the ELF, Clang-only objects verified.
assert "ps2build update llvm-toolchain" in workflow
assert "Provide audsrv if absent" not in workflow
assert (workflow.index("scripts/build_cloud_tls.sh") < workflow.index("scripts/build_jpeg.sh")
        < workflow.index("ps2build build\n"))
assert "scripts/check_toolchain_purity.py" in workflow
assert "/toolchain/ee/bin/" not in workflow  # no GCC binutils in release checks
assert '"$LLVM_BIN/llvm-nm"' in workflow

print("PASS: LLVM toolchain configuration")
