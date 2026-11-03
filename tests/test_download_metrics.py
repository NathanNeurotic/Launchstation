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
print("download-metrics tests passed")
