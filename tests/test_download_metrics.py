"""Invariant tests for durable GitHub download counters."""
import base64
import importlib.util
import io
import json
import os
from pathlib import Path
from urllib.error import HTTPError

path = Path(__file__).resolve().parents[1] / "scripts/download_metrics.py"
spec = importlib.util.spec_from_file_location("metrics", path)
metrics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metrics)
real_api = metrics.api
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

# The by-tag endpoint serves published releases only (404 for drafts); the
# authenticated listing carries drafts, so a draft latest is sampled there.
def stub_api(repo, method, path, **kwargs):
    if path.startswith("/releases?"):
        return [{
            "id": 42, "draft": True, "tag_name": "latest",
            "assets": [{"id": 901, "download_count": 12, "name": "standard.psu"}],
        }]
    if path == "/releases/tags/latest":
        return None
    raise AssertionError(path)
metrics.api = stub_api
assert metrics.observed_assets("owner/repo")["901"]["downloads"] == 12

# A published latest absent from the listing is still sampled via its tag.
def stub_tag_only(repo, method, path, **kwargs):
    if path.startswith("/releases?"):
        return []
    if path == "/releases/tags/latest":
        return {
            "id": 43, "draft": False, "tag_name": "latest",
            "assets": [{"id": 902, "download_count": 7, "name": "standard.zip"}],
        }
    raise AssertionError(path)
metrics.api = stub_tag_only
assert metrics.observed_assets("owner/repo")["902"]["downloads"] == 7

# An inline asset list that is a positive multiple of 100 may be a truncated
# page; it must be confirmed through the paginated endpoint.
hundred = [{"id": 1000 + i, "download_count": i, "name": f"a{i}.zip"} for i in range(100)]
paged_calls = []
def stub_paged(repo, method, path, **kwargs):
    if path.startswith("/releases?"):
        return [{"id": 7, "draft": False, "tag_name": "latest", "assets": hundred}]
    if path == "/releases/tags/latest":
        return None
    if path.startswith("/releases/7/assets?"):
        paged_calls.append(path)
        return [{"id": 2001, "download_count": 5, "name": "late.zip"}]
    raise AssertionError(path)
metrics.api = stub_paged
found = metrics.observed_assets("owner/repo")
assert len(found) == 101 and found["2001"]["downloads"] == 5
assert paged_calls == ["/releases/7/assets?per_page=100&page=2"]

# Over-1MB ledgers arrive without inline content; read() fetches the blob.
ledger_json = {"schema": 1, "tracking_since": "2026-11-03", "updated_at": "2026-11-03",
               "total_downloads": 5,
               "assets": {"101": {"downloads": 5, "name": "a.zip", "release": "latest"}}}
def stub_blob(repo, method, path, **kwargs):
    if path.startswith("/contents/metrics/downloads.json"):
        return {"sha": "blobsha"}  # no inline content: file exceeds 1 MB
    if path == "/git/blobs/blobsha":
        return {"content": base64.b64encode(json.dumps(ledger_json).encode()).decode()}
    raise AssertionError(path)
metrics.api = stub_blob
previous, sha = metrics.read("owner/repo", "download-metrics")
assert previous == ledger_json and sha == "blobsha"

# A 403 carrying a rate-limit signal is retried like a 429.
os.environ.setdefault("GH_TOKEN", "test-token")
attempts = []
class _FakeResponse:
    def __enter__(self):
        return self
    def __exit__(self, *args):
        return False
    def read(self):
        return b'{"ok": true}'
def flaky_urlopen(request, timeout):
    attempts.append(request.full_url)
    if len(attempts) == 1:
        raise HTTPError(request.full_url, 403, "Forbidden", {"Retry-After": "0"},
                        io.BytesIO(b'{"message": "secondary rate limit"}'))
    return _FakeResponse()
metrics.urlopen = flaky_urlopen
metrics.api = real_api
assert metrics.api("owner/repo", "GET", "/rate-limit-probe") == {"ok": True}
assert len(attempts) == 2

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
