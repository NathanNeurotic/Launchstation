"""Guard the MMCE full-tree backup's safety and write-avoidance contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/mmce_backup.c").read_text()

assert "MmceBackupOptions" in (root / "include/mmce_backup.h").read_text()
assert "MMCE_BACKUP_FORMAT_PCSX2" in (root / "include/mmce_backup.h").read_text()
assert '"mmce%d:/MemoryCards"' in src
assert '"WLE-MMCE-Backups"' in src

start = src.index("static int backup_one(")
end = src.index("\nstatic int count_candidates(", start)
body = src[start:end]

fingerprint = body.index("fingerprint_matches(source, previous_path)")
compare = body.index("files_equal_serial(source, previous_path)")
unchanged = body.index("stats->unchanged++")
copy = body.index("mcCardBackupNoEcc(source, staged, NULL)")
assert fingerprint < unchanged < compare < copy, "index fingerprint, then full compare, before any write"
assert "!options->verify" in body[:compare], "verify = 1 must bypass the fingerprint"
assert "write_sidecar(dest)" in body and body.index("write_sidecar(dest)") > body.index("fileXioRename(staged, dest)"), \
    "a fingerprint describes only a committed history copy"
assert "genRemove(fp_path)" in src[src.index("static int prune_history("):], "pruned copies take their sidecar"
unchanged = body.index("stats->unchanged++", compare)
publish = body.index("fileXioRename(staged, dest)")
verified_retention = body.index("prune_history(dir, options->keep, format, protect_name, name)")

assert compare < unchanged < copy, "unchanged cards must be detected before any new backup write"
assert copy < publish < verified_retention, "verified export must publish before retention"
assert '"%s.PART"' in body, "interrupted writes must not use final backup extensions"
assert 'wleExists(dest)' in body, "never overwrite an existing history entry"
assert "genRemove(staged)" in body, "failed export or publication must clean up staging"
assert "if (same < 0)" in body, "failed PCSX2 comparison must not prune history"
mcd_fast = body.index("format == MMCE_BACKUP_FORMAT_MCD")
assert mcd_fast < copy, "native MCD history must check for unchanged data before writing"
assert "mcCardExportPcsx2(source, staged, NULL)" in body, "PCSX2 format must use the verified converter"
assert "keep == 0" in src, "zero retention must mean keep all"
assert "backup_ext(format)" in src, "history extension must follow configured format"
assert "!wleExists(staged)" in src, "do not reuse a prior interrupted backup file"

fn = src[src.index("int mmceBackupAllCards("):]
assert "strlen(source_root)" in fn
assert "dest_root[source_len]" in fn, "source-tree rejection must be path-boundary aware"

header = (root / "include/mmce_backup.h").read_text()
assert "MmceBackupScanStats" in header
assert "mmceBackupScanCards" in header
assert "files_equal_pcsx2_history" in src
assert "mcCardPcsx2Page" in src, "pending scan must compare PCSX2 history without creating a converted file"
scan_one = src[src.index("static int scan_one("):src.index("\nstatic int count_candidates(", src.index("static int scan_one("))]
assert "mcCardExportPcsx2" not in scan_one
assert "mcCardBackupNoEcc" not in scan_one
assert "genRemove" not in scan_one
assert "ensure_dir_tree" not in scan_one, "status scan must stay read-only"
scan_api = src[src.index("int mmceBackupScanCards("):]
assert "ensure_dir_tree" not in scan_api, "public pending scan must not create history directories"

print("PASS: MMCE tree backup and read-only pending scan preserve write/verification safety")
