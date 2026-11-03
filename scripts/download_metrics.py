#!/usr/bin/env python3
"""Persist maximum observed GitHub release download count per asset ID.

The moving 'latest' tag replaces and deletes assets, destroying GitHub's old
counters. This tracks counts from first observation, not historical all-time.
"""
import argparse
import base64
import datetime
import json
import os
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


class ApiError(Exception):
    def __init__(self, status, detail):
        self.status = status
        super().__init__(f"GitHub HTTP {status}: {detail}")


def api(repo, method, path, payload=None, missing=False):
    token = os.getenv("GH_TOKEN") or os.getenv("GITHUB_TOKEN")
    if not token:
        raise RuntimeError("GitHub token missing")
    body = None if payload is None else json.dumps(payload).encode()
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "Launchstation-download-metrics",
    }
    if body is not None:
        headers["Content-Type"] = "application/json"
    for attempt in range(4):
        try:
            request = Request(f"https://api.github.com/repos/{repo}{path}", body, headers, method=method)
            with urlopen(request, timeout=35) as response:
                data = response.read()
                return json.loads(data) if data else {}
        except HTTPError as error:
            if missing and error.code == 404:
                return None
            if error.code in (429, 500, 502, 503, 504) and attempt < 3:
                time.sleep(attempt + 1)
                continue
            raise ApiError(error.code, error.read().decode("utf-8", "replace")[:250]) from error
        except (URLError, TimeoutError):
            if attempt == 3:
                raise
            time.sleep(attempt + 1)


def observed_assets(repo):
    found = {}
    for page in range(1, 101):
        releases = api(repo, "GET", f"/releases?per_page=100&page={page}")
        if page == 1:
            # Fetch the fixed tag explicitly too: drafts may be omitted from
            # a release listing even when the authenticated caller can view
            # the release by its tag. Failure to observe latest must fail
            # closed before any --clobber in the publishing workflow.
            latest = api(repo, "GET", "/releases/tags/latest", missing=True)
            if latest is not None and not any(r["id"] == latest["id"] for r in releases):
                releases.insert(0, latest)
        for release in releases:
            # The moving latest can be left in draft after an interrupted
            # publication. Its assets still have counts we MUST preserve.
            if release["draft"] and release["tag_name"] != "latest":
                continue
            rid = release["id"]
            for apage in range(1, 101):
                assets = api(repo, "GET", f"/releases/{rid}/assets?per_page=100&page={apage}")
                for asset in assets:
                    key = str(int(asset["id"]))
                    count = int(asset["download_count"])
                    if count < 0:
                        raise ValueError("Negative asset download count")
                    value = {"downloads": count, "name": asset["name"], "release": release["tag_name"]}
                    if key not in found or count > found[key]["downloads"]:
                        found[key] = value
                if len(assets) < 100:
                    break
            else:
                raise RuntimeError("Unbounded asset pages")
        if len(releases) < 100:
            return found
    raise RuntimeError("Unbounded release pages")


def merge(previous, current, timestamp):
    if previous is not None and (previous.get("schema") != 1 or not isinstance(previous.get("assets"), dict)):
        raise ValueError("Unexpected ledger schema")
    values = dict(previous["assets"]) if previous else {}
    changed = previous is None
    for key, sample in current.items():
        existing = values.get(key)
        if existing is None or sample["downloads"] > int(existing["downloads"]):
            values[key] = sample
            changed = True
    if not changed:
        return previous, False
    total = sum(int(a["downloads"]) for a in values.values())
    if previous and total < previous["total_downloads"]:
        raise ValueError("Counter decreased")
    return {
        "schema": 1,
        "tracking_since": previous["tracking_since"] if previous else timestamp,
        "updated_at": timestamp,
        "scope": "Observed downloads since tracking began; earlier deleted assets excluded",
        "total_downloads": total,
        "assets": dict(sorted(values.items(), key=lambda item: int(item[0]))),
    }, True


def branch_exists(repo, branch):
    return api(repo, "GET", f"/git/ref/heads/{quote(branch, safe='')}", missing=True) is not None


def read(repo, branch):
    path = f"/contents/metrics/downloads.json?ref={quote(branch, safe='')}"
    data = api(repo, "GET", path, missing=True)
    if data is None:
        return None, None
    return json.loads(base64.b64decode(data["content"]).decode()), data["sha"]


def snapshot(repo, branch, base):
    # Call BEFORE clobber or deletion: a failed snapshot blocks release edits.
    # Never recreate the branch or seed ledger on a publish retry: a missing
    # history is a data-loss emergency, not a fresh counter.
    if not branch_exists(repo, branch):
        raise RuntimeError(f"Metrics branch {branch} is missing; restore the ledger before publishing")
    current = observed_assets(repo)
    now = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")
    for attempt in range(6):
        previous, sha = read(repo, branch)
        if previous is None:
            raise RuntimeError("Metrics ledger is missing; restore its saved history before publishing")
        ledger, changed = merge(previous, current, now)
        if not changed:
            print(f"Tracked downloads: {ledger['total_downloads']} (unchanged)")
            return
        data = {
            "message": "metrics: preserve observed release asset downloads",
            "branch": branch,
            "content": base64.b64encode((json.dumps(ledger, indent=2) + "\n").encode()).decode(),
        }
        if sha:
            data["sha"] = sha
        try:
            api(repo, "PUT", "/contents/metrics/downloads.json", data)
            print(f"Tracked downloads: {ledger['total_downloads']} ({len(ledger['assets'])} asset IDs)")
            return
        except ApiError as error:
            if error.status not in (409, 422) or attempt == 5:
                raise
            time.sleep(1)
    raise RuntimeError("Could not commit snapshot")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=os.getenv("GITHUB_REPOSITORY", "NathanNeurotic/Launchstation"))
    parser.add_argument("--branch", default="download-metrics")
    parser.add_argument("--base", default="master")
    args = parser.parse_args()
    try:
        snapshot(args.repo, args.branch, args.base)
    except Exception as error:
        print(f"Download metrics snapshot failed: {error}", file=sys.stderr)
        sys.exit(1)
