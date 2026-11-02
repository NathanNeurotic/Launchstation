#!/usr/bin/env python3
"""ps2build SDK Watch: compare the pinned ps2build SDK with the newest official releases.

.github/ps2build-sdk.env pins every version of the SDK, so build jobs never ask git.techwritescode.dev
which release is newest. Unpinned, every build job asked once per package, 9 to 11 requests; with RiptOPL's
~70 jobs per push that came to 20k to 70k requests a day on tech's server. This script is now the only
place that asks.
.github/workflows/ps2build-sdk-watch.yml runs it once a week: one request for each pinned name (the SDK
components, llvm-toolchain and the extra packages) and none for the names in PS2BUILD_SDK_HOLD.

When something is newer, it writes the bumped pin file and the pull request that proposes it. It also
pins the NathanNeurotic/ps2build-mirror release that holds exactly the new versions (its tag, and the
SHA-256 of its SHA256SUMS), so the mirror fallback installs the same SDK as the official server. When the
mirror has no such release yet, both are left empty: the fallback then takes the newest mirror release,
and a later run pins the matching one once it exists.

Outputs, in --out:
  status            current, changed or unknown
  ps2build-sdk.env  the new pin file (changed only)
  title, body.md    the pull request (changed only)
  commit.txt        its commit message (changed only)
unknown means an official release could not be read. Nothing else is written, and the workflow does
nothing until its next run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# What differs between the repositories that carry a copy of this watch (RiptOPL, Launchstation and
# ssb-decomp-re). Keep everything below this block identical in all three.
PROJECT = "Launchstation"
PROJECT_URL = "https://github.com/NathanNeurotic/Launchstation"
DEFAULT_BRANCH = "master"
BUILDS = "Automated-Build (it publishes only from master)"
WARMED = "once (LLVM, the toolchain Launchstation builds with)"

ROOT = Path(__file__).resolve().parents[2]
PIN = ROOT / ".github/ps2build-sdk.env"
OFFICIAL = os.environ.get("PS2BUILD_SDK_OFFICIAL_URL") or "https://git.techwritescode.dev"
MIRROR_API = os.environ.get("PS2BUILD_SDK_MIRROR_API_URL") or "https://api.github.com"
# The identifier our SDK downloads send too, so tech can tell our requests apart in his logs.
USER_AGENT = f"{PROJECT}-build/1.0 (ps2build SDK Watch; +{PROJECT_URL})"
VERSION = re.compile(r"^[A-Za-z0-9._+-]+$")  # what tools/ps2build_sdk.sh accepts from the release API
ASSIGNMENT = re.compile(r"^([A-Z][A-Z0-9_]*)=(.*)$")
RETRY_DELAYS = (10, 30)  # one try plus two retries, never more: this runs against someone else's server
MIRROR_PAGES = 5         # releases listed newest first, 100 a page (the mirror also holds source backups)
MIRROR_CANDIDATES = 10   # SDK releases whose pin.env is read before giving up


def sleep(seconds):  # replaced by the tests
    time.sleep(seconds)


def extra_var(name):
    """ogg -> OGG_VERSION, as tools/ps2build_sdk.sh names an extra package's version."""
    return name.upper().replace("-", "_") + "_VERSION"


def read_pin(text):
    """The NAME=value assignments of a pin file, quotes removed. A later assignment wins, as in sh."""
    values = {}
    for line in text.splitlines():
        match = ASSIGNMENT.match(line.strip())
        if match:
            value = match.group(2).strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            values[match.group(1)] = value
    return values


def unpinned(version):
    return not version or version == "latest"


def entries(values):
    """[(name, version)] of every version a pin file sets. An unpinned version is "" or "latest"."""
    found = []
    for pair in values.get("PS2BUILD_COMPONENTS", "").split():
        name, _, version = pair.partition("=")
        found.append((name, version))
    found.append(("llvm-toolchain", values.get("PS2BUILD_LLVM_TOOLCHAIN", "latest")))
    for name in values.get("PS2BUILD_EXTRAS", "").split():
        found.append((name, values.get(extra_var(name), "latest")))
    return found


def problems(values):
    """Why a pin file would make build jobs ask the release API, or install a mismatched fallback."""
    found = [f"{name} is not pinned to a release" for name, version in entries(values) if unpinned(version)]
    for name, version in entries(values):
        if not unpinned(version) and not VERSION.match(version):
            found.append(f"{name}={version} is not a release tag")
    tag, sums = values.get("PS2BUILD_SDK_TAG", ""), values.get("PS2BUILD_SDK_SUMS_SHA256", "")
    if bool(tag) != bool(sums):
        found.append("PS2BUILD_SDK_TAG and PS2BUILD_SDK_SUMS_SHA256 must be set together, or both left empty")
    if sums and not re.fullmatch(r"[0-9a-f]{64}", sums):
        found.append("PS2BUILD_SDK_SUMS_SHA256 is not a SHA-256")
    if tag and not VERSION.match(tag):
        found.append("PS2BUILD_SDK_TAG is not a release tag")
    return found


class TokenStaysOnHost(urllib.request.HTTPRedirectHandler):
    """Follows redirects, but sends the Authorization header only to the host it was meant for.

    urllib copies every header to the redirected request, and a mirror asset download is a redirect to
    GitHub's storage host, which has no use for the mirror token. A redirect that changes the scheme, the
    host or the port drops the header, and a redirect from https to anything else is refused."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is None:
            return None
        old, target = urllib.parse.urlsplit(req.full_url), urllib.parse.urlsplit(new.full_url)
        if old.scheme == "https" and target.scheme != "https":
            fp.close()
            raise ValueError(f"{req.full_url}: refusing a redirect from https to {target.scheme or 'no scheme'}")
        if (target.scheme, target.hostname, target.port) != (old.scheme, old.hostname, old.port):
            new.remove_header("Authorization")
        return new


OPENER = urllib.request.build_opener(TokenStaysOnHost)


def get(url, headers=None):
    """GET url and return the body. Retries a network error, a 429 or a 5xx twice; a 4xx is final."""
    for attempt, delay in enumerate((*RETRY_DELAYS, None)):
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
        try:
            with OPENER.open(request, timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if delay is None or (error.code < 500 and error.code != 429):
                raise
            reason = f"HTTP {error.code}"
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            if delay is None:
                raise
            reason = str(error)
        print(f"{url}: {reason}; retrying in {delay} s (attempt {attempt + 2} of {len(RETRY_DELAYS) + 1})", flush=True)
        sleep(delay)
    raise AssertionError("unreachable")


def latest_official(name):
    """The tag of the newest official release of ps2/<name>."""
    body = get(f"{OFFICIAL}/api/v1/repos/ps2/{urllib.parse.quote(name, safe='')}/releases/latest",
               {"Accept": "application/json"})
    tag = json.loads(body).get("tag_name") or ""
    if not VERSION.match(tag):
        raise ValueError(f"the newest official {name} release has an unexpected tag {tag!r}")
    return tag


def mirror_get(path, accept="application/vnd.github+json"):
    """A GitHub API request for the mirror. PS2BUILD_SDK_MIRROR_TOKEN (read-only) reads a private mirror."""
    headers = {"Accept": accept, "X-GitHub-Api-Version": "2022-11-28"}
    token = os.environ.get("PS2BUILD_SDK_MIRROR_TOKEN")
    if token:
        if urllib.parse.urlsplit(MIRROR_API).scheme != "https":
            raise ValueError(f"{MIRROR_API} is not an https URL, and the mirror token is sent only over https")
        headers["Authorization"] = "Bearer " + token
    return get(MIRROR_API + path, headers)


def mirror_versions(pin):
    """{name: version} of a mirror release's pin.env (which carries no PS2BUILD_EXTRAS line)."""
    versions = {}
    for pair in pin.get("PS2BUILD_COMPONENTS", "").split():
        name, _, version = pair.partition("=")
        versions[name] = version
    versions["llvm-toolchain"] = pin.get("PS2BUILD_LLVM_TOOLCHAIN", "")
    return versions


def holds_exactly(pin, wanted):
    """The mirror release's pin.env names every wanted version."""
    have = mirror_versions(pin)
    return all((have.get(name) if name in have else pin.get(extra_var(name))) == version
               for name, version in wanted.items())


def checked_release(repo, release, wanted):
    """(tag, sums) when this mirror release holds exactly the wanted versions and its SHA256SUMS
    matches the hash its pin.env names; None otherwise."""
    tag = release.get("tag_name") or ""
    assets = {asset.get("name"): asset.get("id") for asset in release.get("assets", [])}
    # Build jobs source the pin file this tag goes into, and Git allows a tag such as sdk-$(id).
    if not tag.startswith("sdk-") or not VERSION.match(tag) or release.get("draft") or release.get("prerelease") \
            or not isinstance(assets.get("pin.env"), int) or not isinstance(assets.get("SHA256SUMS"), int):
        return None
    pin = read_pin(mirror_get(f"/repos/{repo}/releases/assets/{assets['pin.env']}",
                              "application/octet-stream").decode("utf-8", "replace"))
    sums = pin.get("PS2BUILD_SDK_SUMS_SHA256", "")
    if pin.get("PS2BUILD_SDK_TAG") != tag or not re.fullmatch(r"[0-9a-f]{64}", sums) or not holds_exactly(pin, wanted):
        return None
    data = mirror_get(f"/repos/{repo}/releases/assets/{assets['SHA256SUMS']}", "application/octet-stream")
    if hashlib.sha256(data).hexdigest() != sums:
        print(f"warning: ps2build-mirror release {tag}: SHA256SUMS does not match its pin.env; not pinning it", flush=True)
        return None
    return tag, sums


def mirror_release(repo, wanted, current_tag=""):
    """(tag, sums) of the newest ps2build-mirror SDK release holding exactly the wanted versions, or None.

    The release pinned now (current_tag) is checked first, by its tag. Raises OSError or ValueError when
    the mirror cannot be read, so the caller can tell "no such release" from "could not look"."""
    if current_tag:
        try:
            release = json.loads(mirror_get(f"/repos/{repo}/releases/tags/{urllib.parse.quote(current_tag, safe='')}"))
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
        else:
            found = checked_release(repo, release, wanted)
            if found:
                return found
    candidates = []
    for page in range(1, MIRROR_PAGES + 1):
        # Explicit page numbers: GitHub's Link header can name the repository by numeric id instead.
        releases = json.loads(mirror_get(f"/repos/{repo}/releases?per_page=100&page={page}"))
        candidates += [release for release in releases
                       if (release.get("tag_name") or "").startswith("sdk-")
                       and not release.get("draft") and not release.get("prerelease")]
        if len(releases) < 100:
            break
    candidates.sort(key=lambda release: release.get("published_at") or "", reverse=True)
    for release in candidates[:MIRROR_CANDIDATES]:
        if release.get("tag_name") == current_tag:
            continue
        found = checked_release(repo, release, wanted)
        if found:
            return found
    return None


def assignment(name, value, quoted):
    return f'{name}="{value}"' if quoted or any(c.isspace() for c in value) else f"{name}={value}"


def rewrite(text, updates):
    """The pin file with each NAME=value in updates replaced in place (comments and order kept).
    A name the file does not assign yet is added after PS2BUILD_SDK_MIRROR, else at the end."""
    lines = text.splitlines()
    seen = set()
    for index, line in enumerate(lines):
        match = ASSIGNMENT.match(line)
        if match and match.group(1) in updates:
            lines[index] = assignment(match.group(1), updates[match.group(1)], match.group(2).startswith('"'))
            seen.add(match.group(1))
    missing = [assignment(name, value, False) for name, value in updates.items() if name not in seen]
    if missing:
        at = next((index + 1 for index, line in enumerate(lines) if line.startswith("PS2BUILD_SDK_MIRROR=")), len(lines))
        lines[at:at] = missing
    return "\n".join(lines) + "\n"


def release_link(name, tag):
    return f"{OFFICIAL}/ps2/{urllib.parse.quote(name, safe='')}/releases/tag/{urllib.parse.quote(tag, safe='')}"


def describe(changes, holds, mirror, mirror_note):
    """title, body.md and commit.txt for the pull request."""
    names = ", ".join(f"{name} {new}" for name, _, new in changes)
    if changes:
        title = "CI: ps2build SDK " + (names if len(changes) <= 3 else f"update ({len(changes)} packages)")
    else:
        title = "CI: pin the ps2build-mirror fallback release"
    body = ["The weekly ps2build SDK Watch found newer official releases than `.github/ps2build-sdk.env` pins."
            if changes else "The pinned SDK versions are still the newest, but the mirror fallback pin changes.", ""]
    if changes:
        body += ["| Package | Pinned now | Newest | Release |", "|---|---|---|---|"]
        body += [f"| {name} | `{old or 'unpinned'}` | `{new}` | [notes]({release_link(name, new)}) |"
                 for name, old, new in changes]
        body.append("")
    body += [mirror_note, ""]
    if holds:
        body += ["Held in `PS2BUILD_SDK_HOLD`, not checked: " + ", ".join(f"`{name}`" for name in sorted(holds)) + ".", ""]
    body += [
        "Before marking this ready:",
        f"- the build checks on this branch must pass. The watch started {BUILDS} here itself, because a push "
        "made by GitHub Actions starts no workflow on its own.",
        "- read the upstream release notes linked above.",
        "",
        f"The watch already installed and cached this SDK on {DEFAULT_BRANCH}, {WARMED}, so the checks here and on "
        f"{DEFAULT_BRANCH} after the merge restore it from the GitHub cache instead of downloading it from "
        "git.techwritescode.dev again.",
        "",
        f"To stay on a release, add its name to `PS2BUILD_SDK_HOLD` in `.github/ps2build-sdk.env` on {DEFAULT_BRANCH}: "
        "the next run leaves it alone. Closing this pull request without that makes the next run propose the "
        "update again.",
        "",
        "Kept up to date by `.github/workflows/ps2build-sdk-watch.yml`, which never merges anything.",
    ]
    commit = [title, ""]
    commit += [f"- {name}: {old or 'unpinned'} -> {new}" for name, old, new in changes]
    if mirror:
        commit += ["", f"Mirror fallback: {mirror[0]}"]
    commit += ["", "Automatic update by the ps2build SDK Watch."]
    return title, "\n".join(body) + "\n", "\n".join(commit) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pin", type=Path, default=PIN)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="only check, offline, that the pin file pins every version (no network)")
    args = parser.parse_args(argv)

    text = args.pin.read_text(encoding="utf-8")
    values = read_pin(text)
    if args.check:
        found = problems(values)
        for problem in found:
            print(f"{args.pin}: {problem}", file=sys.stderr)
        return 1 if found else 0
    if args.out is None:
        parser.error("--out is required")
    pinned = entries(values)
    holds = set(values.get("PS2BUILD_SDK_HOLD", "").split())
    for name in sorted(holds - {name for name, _ in pinned}):
        print(f"warning: PS2BUILD_SDK_HOLD names {name}, which the pin file does not use", flush=True)
    args.out.mkdir(parents=True, exist_ok=True)
    status = args.out / "status"

    try:
        newest = {name: latest_official(name) for name, _ in pinned if name not in holds}
    except urllib.error.HTTPError as error:
        if error.code >= 500 or error.code == 429:
            print(f"::warning::{OFFICIAL} answered HTTP {error.code}; trying again next run.")
            status.write_text("unknown\n", encoding="utf-8")
            return 0
        # A 404 here is not an outage: a package was renamed or removed upstream. Fail, so a person looks.
        print(f"::error::{error.url}: HTTP {error.code}. Has a pinned package moved or gone upstream?")
        status.write_text("unknown\n", encoding="utf-8")
        return 1
    except (OSError, ValueError) as error:  # network errors are OSErrors; ValueError covers bad JSON or tags
        print(f"::warning::Could not read the newest official ps2build releases ({error}); trying again next run.")
        status.write_text("unknown\n", encoding="utf-8")
        return 0
    wanted = {name: newest.get(name, version) for name, version in pinned}
    changes = [(name, version, wanted[name]) for name, version in pinned if wanted[name] != version]
    for name in sorted(holds):
        if name in wanted and unpinned(wanted[name]):
            print(f"warning: {name} is held but not pinned, so every build job asks the release API for it", flush=True)

    repo = values.get("PS2BUILD_SDK_MIRROR") or "NathanNeurotic/ps2build-mirror"
    tag, sums = values.get("PS2BUILD_SDK_TAG", ""), values.get("PS2BUILD_SDK_SUMS_SHA256", "")
    try:
        mirror = mirror_release(repo, wanted, tag)
    except (OSError, ValueError) as error:
        print(f"warning: could not read the {repo} releases ({error})", flush=True)
        mirror, looked = None, False
    else:
        looked = True
    if mirror:
        tag, sums = mirror
        mirror_note = (f"Mirror fallback: pinned to [{repo}](https://github.com/{repo}) release `{tag}`, which holds "
                       "exactly these versions; its `SHA256SUMS` matches the pinned hash.")
    elif looked or changes:
        # No release holds these versions (or the mirror could not be read while the versions change): the
        # fallback takes the newest mirror release, and says so when its versions differ.
        tag, sums = "", ""
        mirror_note = (f"Mirror fallback: [{repo}](https://github.com/{repo}) has no release with exactly these "
                       "versions yet" + ("" if looked else " (the watch could not read its releases)") + ", so "
                       "`PS2BUILD_SDK_TAG` is left empty. Until a later run pins one, a build whose official install "
                       "fails takes the newest mirror release, warns that it differs, and does not cache it.")
    else:
        mirror_note = "Mirror fallback: unchanged (the watch could not read the mirror's releases)."

    components = [pair.partition("=")[0] for pair in values.get("PS2BUILD_COMPONENTS", "").split()]
    extras = values.get("PS2BUILD_EXTRAS", "").split()
    updates = {"PS2BUILD_COMPONENTS": " ".join(f"{name}={wanted[name]}" for name in components),
               "PS2BUILD_LLVM_TOOLCHAIN": wanted["llvm-toolchain"],
               **{extra_var(name): wanted[name] for name in extras},
               "PS2BUILD_SDK_TAG": tag, "PS2BUILD_SDK_SUMS_SHA256": sums}
    new_text = rewrite(text, updates)
    if new_text == text:
        print("The pinned ps2build SDK is the newest official one." if not holds else
              "The pinned ps2build SDK is the newest official one, apart from the held names.")
        status.write_text("current\n", encoding="utf-8")
        return 0

    title, body, commit = describe(changes, holds, mirror, mirror_note)
    (args.out / "ps2build-sdk.env").write_text(new_text, encoding="utf-8")
    (args.out / "title").write_text(title + "\n", encoding="utf-8")
    (args.out / "body.md").write_text(body, encoding="utf-8")
    (args.out / "commit.txt").write_text(commit, encoding="utf-8")
    status.write_text("changed\n", encoding="utf-8")
    print(title)
    for name, old, new in changes:
        print(f"  {name}: {old or 'unpinned'} -> {new}")
    print(mirror_note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
