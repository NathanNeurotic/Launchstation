"""Compile and execute the actual PS2CF parser/ATA/MMIO implementations.
Run directly with Python; no console or installed SDK is needed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc") or shutil.which("clang")
if not CC:
    raise SystemExit("A host C compiler is required")

cases = {
    "module": ["tests/test_pccf_module.c"],
    "cis": ["tests/test_cis.c", "iop/pccf/pcdiag_cis.c"],
    "profile": ["tests/test_cf.c", "iop/pccf/pcdiag_cf.c", "iop/pccf/pcdiag_cis.c"],
    "rw": ["tests/test_pccf_rw.c"],
    "ata": ["tests/test_pccf_ata.c", "iop/pccf/pccf_ata.c"],
    "hardware": ["tests/test_pccf_hw.c", "iop/pccf/pcdiag_cf.c", "iop/pccf/pcdiag_cis.c"],
}
with tempfile.TemporaryDirectory(prefix="pccf-") as tmp:
    for name, sources in cases.items():
        exe = str(Path(tmp) / (name + ".exe"))
        subprocess.run([CC, "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-Itests/pccf-stubs", "-Iiop/pccf", *sources, "-o", exe],
                       cwd=ROOT, check=True)
        subprocess.run([exe], cwd=ROOT, check=True)
print("PASS: PC CARD CF host tests")
