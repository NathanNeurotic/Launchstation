"""Invariant tests for durable GitHub download counters."""
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "scripts/download_metrics.py"
spec = importlib.util.spec_from_file_location("metrics", path)
metrics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metrics)
one, changed = metrics.merge(None, {
    "101": {"downloads": 5, "name": "standard.zip", "release": "latest"},
    "102": {"downloads": 2, "name": "standard.psu", "release": "latest"},
}, "2026-11-03")
assert changed and one["total_downloads"] == 7
two, changed = metrics.merge(one, {
    "101": {"downloads": 3, "name": "standard.zip", "release": "latest"},
    "103": {"downloads": 4, "name": "standard.zip", "release": "latest"},
}, "2026-11-04")
assert changed and two["total_downloads"] == 11
assert two["assets"]["101"]["downloads"] == 5
three, changed = metrics.merge(two, {"103": {"downloads": 1, "name": "standard.zip", "release": "latest"}}, "2026-11-05")
assert not changed and three["total_downloads"] == 11
four, changed = metrics.merge(two, {"103": {"downloads": 9, "name": "standard.zip", "release": "latest"}}, "2026-11-05")
assert changed and four["total_downloads"] == 16
try:
    metrics.merge({"schema": 2, "assets": {}}, {}, "later")
except ValueError:
    pass
else:
    raise AssertionError("Unexpected schema must be rejected")

# A draft latest missing from the listing must still be sampled via its tag.
def stub_api(repo, method, path, **kwargs):
    if path.startswith("/releases?"):
        return []
    if path == "/releases/tags/latest":
        return {"id": 42, "draft": True, "tag_name": "latest"}
    if path.startswith("/releases/42/assets?"):
        return [{"id": 901, "download_count": 12, "name": "standard.psu"}]
    raise AssertionError(path)
metrics.api = stub_api
assert metrics.observed_assets("owner/repo")["901"]["downloads"] == 12

# A missing tracking branch or ledger is NOT an excuse to reset counts.
metrics.branch_exists = lambda repo, branch: False
try:
    metrics.snapshot("owner/repo", "download-metrics", "master")
except RuntimeError as error:
    assert "missing" in str(error)
else:
    raise AssertionError("Missing metrics branch should prevent publication")
metrics.branch_exists = lambda repo, branch: True
metrics.observed_assets = lambda repo: {}
metrics.read = lambda repo, branch: (None, None)
try:
    metrics.snapshot("owner/repo", "download-metrics", "master")
except RuntimeError as error:
    assert "missing" in str(error)
else:
    raise AssertionError("Missing ledger should prevent publication")


# If another snapshot writes first, reread its ledger and merge *both* new IDs.
initial = {
    "schema": 1, "tracking_since": "2026-11-03", "updated_at": "2026-11-03",
    "total_downloads": 5,
    "assets": {"101": {"downloads": 5, "name": "a.zip", "release": "latest"}},
}
competing = {
    "schema": 1, "tracking_since": "2026-11-03", "updated_at": "2026-11-03",
    "total_downloads": 9,
    "assets": {
        **initial["assets"],
        "102": {"downloads": 4, "name": "b.zip", "release": "latest"},
    },
}
reads = iter([(initial, "sha-before"), (competing, "sha-after")])
writes = []
metrics.branch_exists = lambda repo, branch: True
metrics.observed_assets = lambda repo: {
    "103": {"downloads": 3, "name": "c.zip", "release": "latest"}
}
metrics.read = lambda repo, branch: next(reads)
metrics.time.sleep = lambda seconds: None
def conflicting_write(repo, method, path, payload=None, **kwargs):
    assert method == "PUT"
    writes.append(payload)
    if len(writes) == 1:
        raise metrics.ApiError(409, "concurrent update")
    return {}
metrics.api = conflicting_write
metrics.snapshot("owner/repo", "download-metrics", "master")
assert len(writes) == 2
merged = __import__("json").loads(__import__("base64").b64decode(writes[-1]["content"]))
assert merged["total_downloads"] == 12
assert set(merged["assets"]) == {"101", "102", "103"}
assert writes[1]["sha"] == "sha-after"

print("download-metrics tests passed")
