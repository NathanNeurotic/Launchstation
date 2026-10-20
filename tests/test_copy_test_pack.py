"""Verify deterministic fixtures, boundary lengths and returned-copy detection."""
import importlib.util
from pathlib import Path
import tempfile
import zipfile
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('copy_test_pack',root/'scripts/copy_test_pack.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
with tempfile.TemporaryDirectory(prefix='copy-pack-') as folder:
    p=Path(folder);pack.create(p/'one.zip');pack.create(p/'two.zip')
    assert (p/'one.zip').read_bytes()==(p/'two.zip').read_bytes()
    assert sum(pack.CASES.values())<256*1024
    assert all(len(part)<=32 for name in pack.CASES for part in Path(name).parts)
    with zipfile.ZipFile(p/'one.zip') as z:
        assert len(z.namelist())==len(pack.CASES)+2
        z.extractall(p/'returned')
    target=p/'returned/LS-COPY-TEST'
    assert pack.verify(target)
    (target/'B16385.BIN').write_bytes(bytes(16385))
    assert not pack.verify(target)
    (target/'ONE.BIN').unlink()
    assert not pack.verify(target)
print('PASS: reproducible bounded pack and corrupt/missing returned-copy detection')
