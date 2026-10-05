"""Guard the MMCE full-tree backup's safety and write-avoidance contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "src/mmce_backup.c").read_text()

assert '#define MMCE_BACKUP_KEEP 10' in src
assert '"mmce%d:/MemoryCards"' in src
assert '"WLE-MMCE-Backups"' in src

start = src.index("static int backup_one(")
end = src.index("\nstatic int count_candidates(", start)
body = src[start:end]

compare = body.index("files_equal_serial(source, previous_path)")
unchanged = body.index("stats->unchanged++")
copy = body.index("mcCardBackupNoEcc(source, dest, NULL)")
verified_retention = body.index("prune_history(dir)")

assert compare < unchanged < copy, "unchanged cards must be detected before any new backup write"
assert copy < verified_retention, "retention must happen only after verified copy succeeds"
assert "genRemove(dest)" not in body, "unchanged detection should not create-then-delete a duplicate"

fn = src[src.index("int mmceBackupAllCards("):]
assert "strlen(source_root)" in fn
assert "dest_root[source_len]" in fn, "source-tree rejection must be path-boundary aware"

print("PASS: MMCE tree backup skips unchanged cards before writing and prunes only after verified copies")
