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

compare = body.index("files_equal_serial(source, previous_path)")
unchanged = body.index("stats->unchanged++")
copy = body.index("mcCardBackupNoEcc(source, dest, NULL)")
verified_retention = body.index("prune_history(dir, options->keep, format)")

assert compare < unchanged < copy, "unchanged cards must be detected before any new backup write"
assert copy < verified_retention, "retention must happen only after verified copy succeeds"
mcd_fast = body.index("format == MMCE_BACKUP_FORMAT_MCD")
assert mcd_fast < copy, "native MCD history must check for unchanged data before writing"
assert "mcCardExportPcsx2(source, dest, NULL)" in body, "PCSX2 format must use the verified converter"
assert "keep == 0" in src, "zero retention must mean keep all"
assert "backup_ext(format)" in src, "history extension must follow configured format"

fn = src[src.index("int mmceBackupAllCards("):]
assert "strlen(source_root)" in fn
assert "dest_root[source_len]" in fn, "source-tree rejection must be path-boundary aware"

print("PASS: MMCE tree backup supports configurable retention/format, preserves native no-write unchanged detection, and prunes only verified copies")
