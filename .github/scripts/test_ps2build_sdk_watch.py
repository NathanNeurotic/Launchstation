"""ps2build SDK Watch: build jobs stay off the release API, and the weekly bump pins the right things."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import ps2build_sdk_watch as watch  # noqa: E402

WORKFLOWS = ROOT / ".github/workflows"
MIRROR = "/repos/NathanNeurotic/ps2build-mirror"

PINS = """# A comment that must survive.
PS2BUILD_SDK_MIRROR=NathanNeurotic/ps2build-mirror
PS2BUILD_SDK_TAG=sdk-old
PS2BUILD_SDK_SUMS_SHA256={old_sums}
PS2BUILD_COMPONENTS="ps2build=v1 sdk-core=v1 sdk-world=v1"
# Another comment.
PS2BUILD_LLVM_TOOLCHAIN=v0.1
PS2BUILD_EXTRAS="ogg vorbis"
OGG_VERSION=v1
VORBIS_VERSION=v1
PS2BUILD_SDK_HOLD="{hold}"
"""


def http_error(url, code):
    return urllib.error.HTTPError(url, code, "test", {}, io.BytesIO(b""))


def sums_for(tag):
    return f"{'0' * 64}  ps2build-sdk-{tag}-linux-amd64-base.tar.zst\n".encode()


def mirror_pin(tag, components, llvm, ogg, vorbis, sums=None):
    sums = sums or hashlib.sha256(sums_for(tag)).hexdigest()
    return (f"PS2BUILD_SDK_MIRROR=NathanNeurotic/ps2build-mirror\nPS2BUILD_SDK_TAG={tag}\n"
            f"PS2BUILD_SDK_SUMS_SHA256={sums}\nPS2BUILD_COMPONENTS=\"{components}\"\n"
            f"PS2BUILD_LLVM_TOOLCHAIN={llvm}\nOGG_VERSION={ogg}\nVORBIS_VERSION={vorbis}\n").encode()


class FakeMirror:
    """GitHub's release API for the mirror: releases by tag, a release list and asset downloads."""

    def __init__(self):
        self.files, self.releases, self.requests = {}, [], []

    def add(self, tag, pin, published, prerelease=False):
        ids = (len(self.files) + 1, len(self.files) + 2)
        self.files[f"{MIRROR}/releases/assets/{ids[0]}"] = pin
        self.files[f"{MIRROR}/releases/assets/{ids[1]}"] = sums_for(tag)
        release = {"tag_name": tag, "draft": False, "prerelease": prerelease, "published_at": published,
                   "assets": [{"name": "pin.env", "id": ids[0]}, {"name": "SHA256SUMS", "id": ids[1]}]}
        self.releases.append(release)
        self.files[f"{MIRROR}/releases/tags/{tag}"] = json.dumps(release).encode()

    def get(self, path, accept="application/vnd.github+json"):
        self.requests.append(path)
        if path.startswith(f"{MIRROR}/releases?"):
            return json.dumps(self.releases if path.endswith("page=1") else []).encode()
        if path not in self.files:
            raise http_error(path, 404)
        return self.files[path]


class WatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.mirror = FakeMirror()
        self.mirror.add("sdk-old", mirror_pin("sdk-old", "ps2build=v1 sdk-core=v1 sdk-world=v1", "v0.1", "v1", "v1"),
                        "2026-10-01T00:00:00Z")
        self.old_sums = hashlib.sha256(sums_for("sdk-old")).hexdigest()
        self.newest = {"ps2build": "v1", "sdk-core": "v1", "sdk-world": "v1", "llvm-toolchain": "v0.1",
                       "ogg": "v1", "vorbis": "v1"}
        self.asked = []

    def official(self, name):
        self.asked.append(name)
        value = self.newest[name]
        if isinstance(value, Exception):
            raise value
        return value

    def run_watch(self, hold="", pins=None):
        pin = self.dir / "ps2build-sdk.env"
        pin.write_text(pins or PINS.format(old_sums=self.old_sums, hold=hold))
        out = self.dir / "out"
        with mock.patch.object(watch, "latest_official", self.official), \
                mock.patch.object(watch, "mirror_get", self.mirror.get):
            code = watch.main(["--pin", str(pin), "--out", str(out)])
        read = lambda name: (out / name).read_text() if (out / name).exists() else None  # noqa: E731
        return code, read("status").strip(), read("ps2build-sdk.env"), read("body.md")

    def test_repository_pin_file_pins_every_version(self):
        # A bare name or "latest" would send every build job back to the release API.
        values = watch.read_pin((ROOT / ".github/ps2build-sdk.env").read_text())
        self.assertEqual(watch.problems(values), [])
        names = [name for name, _ in watch.entries(values)]
        self.assertIn("llvm-toolchain", names)
        self.assertEqual(watch.main(["--check"]), 0)

    def test_problems_name_unpinned_versions_and_a_half_mirror_pin(self):
        values = watch.read_pin('PS2BUILD_COMPONENTS="cmake ninja=v1"\nPS2BUILD_LLVM_TOOLCHAIN=latest\n'
                                'PS2BUILD_EXTRAS="ogg"\nPS2BUILD_SDK_TAG=sdk-x\n')
        found = watch.problems(values)
        self.assertIn("cmake is not pinned to a release", found)
        self.assertIn("llvm-toolchain is not pinned to a release", found)
        self.assertIn("ogg is not pinned to a release", found)
        self.assertTrue(any("set together" in problem for problem in found))

    def test_nothing_newer_is_current_and_writes_nothing_else(self):
        code, status, env, body = self.run_watch()
        self.assertEqual((code, status, env, body), (0, "current", None, None))
        self.assertEqual(sorted(self.asked), sorted(self.newest))  # one request per pinned name

    def test_bump_pins_new_versions_and_the_mirror_release_that_holds_them(self):
        self.newest.update({"sdk-world": "v2", "llvm-toolchain": "v0.2"})
        self.mirror.add("sdk-new", mirror_pin("sdk-new", "ps2build=v1 sdk-core=v1 sdk-world=v2", "v0.2", "v1", "v1"),
                        "2026-10-20T00:00:00Z")
        code, status, env, body = self.run_watch()
        self.assertEqual((code, status), (0, "changed"))
        values = watch.read_pin(env)
        self.assertEqual(values["PS2BUILD_COMPONENTS"], "ps2build=v1 sdk-core=v1 sdk-world=v2")
        self.assertEqual(values["PS2BUILD_LLVM_TOOLCHAIN"], "v0.2")
        self.assertEqual(values["PS2BUILD_SDK_TAG"], "sdk-new")
        self.assertEqual(values["PS2BUILD_SDK_SUMS_SHA256"], hashlib.sha256(sums_for("sdk-new")).hexdigest())
        self.assertEqual(watch.problems(values), [])
        self.assertIn("# A comment that must survive.\n", env)
        self.assertIn('PS2BUILD_COMPONENTS="ps2build=v1 sdk-core=v1 sdk-world=v2"\n# Another comment.\n', env)
        self.assertIn("| sdk-world | `v1` | `v2` |", body)
        self.assertIn("release `sdk-new`", body)

    def test_no_mirror_release_for_the_new_versions_leaves_the_mirror_pin_empty(self):
        self.newest["sdk-world"] = "v2"
        code, status, env, body = self.run_watch()
        values = watch.read_pin(env)
        self.assertEqual((status, values["PS2BUILD_SDK_TAG"], values["PS2BUILD_SDK_SUMS_SHA256"]), ("changed", "", ""))
        self.assertEqual(watch.problems(values), [])
        self.assertIn("has no release with exactly these versions yet", body)

    def test_a_later_run_pins_the_mirror_release_once_it_exists(self):
        pins = PINS.format(old_sums="", hold="").replace("PS2BUILD_SDK_TAG=sdk-old", "PS2BUILD_SDK_TAG=")
        self.mirror.add("sdk-later", mirror_pin("sdk-later", "ps2build=v1 sdk-core=v1 sdk-world=v1", "v0.1", "v1", "v1"),
                        "2026-10-25T00:00:00Z")
        code, status, env, body = self.run_watch(pins=pins)
        self.assertEqual((status, watch.read_pin(env)["PS2BUILD_SDK_TAG"]), ("changed", "sdk-later"))
        self.assertIn("The pinned SDK versions are still the newest", body)

    def test_held_names_are_never_requested_or_changed(self):
        self.newest.update({"sdk-world": "v2", "llvm-toolchain": "v0.2", "ps2build": "v2"})
        code, status, env, body = self.run_watch(hold="sdk-world llvm-toolchain")
        self.assertNotIn("sdk-world", self.asked)
        self.assertNotIn("llvm-toolchain", self.asked)
        values = watch.read_pin(env)
        self.assertEqual(values["PS2BUILD_COMPONENTS"], "ps2build=v2 sdk-core=v1 sdk-world=v1")
        self.assertEqual(values["PS2BUILD_LLVM_TOOLCHAIN"], "v0.1")
        self.assertIn("`llvm-toolchain`, `sdk-world`", body)

    def test_an_official_outage_waits_for_the_next_run(self):
        self.newest["sdk-core"] = http_error("https://official.test", 502)
        self.assertEqual(self.run_watch()[:3], (0, "unknown", None))

    def test_a_package_gone_upstream_fails_the_run(self):
        self.newest["sdk-core"] = http_error("https://official.test", 404)
        self.assertEqual(self.run_watch()[:3], (1, "unknown", None))

    def test_a_mirror_release_whose_sums_do_not_match_its_pin_is_not_pinned(self):
        self.newest["sdk-world"] = "v2"
        self.mirror.add("sdk-forged", mirror_pin("sdk-forged", "ps2build=v1 sdk-core=v1 sdk-world=v2", "v0.1", "v1",
                                                 "v1", sums="1" * 64), "2026-10-20T00:00:00Z")
        self.assertEqual(watch.read_pin(self.run_watch()[2])["PS2BUILD_SDK_TAG"], "")

    def test_prereleases_and_source_backups_are_never_pinned(self):
        self.newest["sdk-world"] = "v2"
        self.mirror.add("sdk-pre", mirror_pin("sdk-pre", "ps2build=v1 sdk-core=v1 sdk-world=v2", "v0.1", "v1", "v1"),
                        "2026-10-20T00:00:00Z", prerelease=True)
        self.mirror.add("src-x", mirror_pin("src-x", "ps2build=v1 sdk-core=v1 sdk-world=v2", "v0.1", "v1", "v1"),
                        "2026-10-21T00:00:00Z")
        self.assertEqual(watch.read_pin(self.run_watch()[2])["PS2BUILD_SDK_TAG"], "")

    def test_an_unreadable_mirror_keeps_the_current_pin(self):
        def down(path, accept=None):
            raise urllib.error.URLError("mirror down")
        self.mirror.get = down
        self.assertEqual(self.run_watch()[:3], (0, "current", None))

    def test_rewrite_adds_a_missing_mirror_pin_after_the_mirror_line(self):
        text = "# c\nPS2BUILD_SDK_MIRROR=o/m\nPS2BUILD_COMPONENTS=\"a=v1\"\n"
        new = watch.rewrite(text, {"PS2BUILD_COMPONENTS": "a=v2", "PS2BUILD_SDK_TAG": "sdk-1"})
        self.assertEqual(new, "# c\nPS2BUILD_SDK_MIRROR=o/m\nPS2BUILD_SDK_TAG=sdk-1\nPS2BUILD_COMPONENTS=\"a=v2\"\n")

    def test_requests_identify_us_and_retry_only_outages(self):
        calls = []

        def urlopen(request, timeout):
            calls.append(request)
            if len(calls) < 3:
                raise http_error(request.full_url, 503)
            return io.BytesIO(b'{"tag_name": "v9"}')

        with mock.patch.object(watch.OPENER, "open", urlopen), mock.patch.object(watch, "sleep") as slept:
            self.assertEqual(watch.latest_official("sdk-core"), "v9")
        self.assertEqual(len(calls), 3)  # one try and two retries, never more
        self.assertEqual([c.args[0] for c in slept.call_args_list], list(watch.RETRY_DELAYS))
        self.assertTrue(calls[0].get_header("User-agent").startswith(watch.PROJECT + "-build/1.0 "))
        self.assertTrue(calls[0].full_url.endswith("/api/v1/repos/ps2/sdk-core/releases/latest"))

        calls.clear()

        def missing(request, timeout):
            calls.append(request)
            raise http_error(request.full_url, 404)

        with mock.patch.object(watch.OPENER, "open", missing), mock.patch.object(watch, "sleep"):
            with self.assertRaises(urllib.error.HTTPError):
                watch.latest_official("gone")
        self.assertEqual(len(calls), 1)

    def test_the_mirror_token_never_follows_a_redirect_to_another_host(self):
        # A mirror asset download answers with a redirect to GitHub's storage host.
        handler = watch.TokenStaysOnHost()
        asset = "https://api.github.com/repos/NathanNeurotic/ps2build-mirror/releases/assets/1"

        def redirect(to):
            request = urllib.request.Request(asset, headers={"Authorization": "Bearer t", "User-Agent": "u"})
            return handler.redirect_request(request, io.BytesIO(), 302, "Found", {}, to)

        storage = redirect("https://release-assets.githubusercontent.com/github-production-release-asset/1?sig=s")
        self.assertFalse(storage.has_header("Authorization"))
        self.assertEqual(storage.get_header("User-agent"), "u")
        self.assertFalse(redirect("https://api.github.com:8443/repos/x").has_header("Authorization"))
        self.assertEqual(redirect("https://api.github.com/repos/x").get_header("Authorization"), "Bearer t")
        with self.assertRaises(ValueError):
            redirect("http://api.github.com/repos/x")  # never from https down to http
        self.assertIn(watch.TokenStaysOnHost, [type(h) for h in watch.OPENER.handlers])

    def test_the_mirror_token_is_sent_only_over_https(self):
        opened = []

        def open_url(request, timeout):
            opened.append(request)
            return io.BytesIO(b"[]")

        with mock.patch.dict("os.environ", {"PS2BUILD_SDK_MIRROR_TOKEN": "t"}), \
                mock.patch.object(watch.OPENER, "open", open_url):
            watch.mirror_get("/repos/o/m/releases")
            self.assertEqual(opened[0].get_header("Authorization"), "Bearer t")
            opened.clear()
            with mock.patch.object(watch, "MIRROR_API", "http://api.github.com"):
                with self.assertRaises(ValueError):
                    watch.mirror_get("/repos/o/m/releases")
        self.assertEqual(opened, [])  # refused before any request

    def test_bad_tags_from_the_release_api_are_refused(self):
        with mock.patch.object(watch, "get", return_value=b'{"tag_name": "v1; rm -rf /"}'):
            with self.assertRaises(ValueError):
                watch.latest_official("sdk-core")


class WorkflowTests(unittest.TestCase):
    """The watch warms the same cache the build restores, only on master, and never publishes."""

    def test_the_build_resolves_the_pin_file_and_shares_the_watch_cache_key(self):
        watch_text = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text()
        build = (WORKFLOWS / "compile.yml").read_text()
        key = "ps2build-sdk-${{ runner.os }}-llvm-${{ steps.sdk.outputs.key }}"
        for text in (watch_text, build):
            self.assertIn('sh scripts/ps2build_sdk.sh --resolve --llvm "$RUNNER_TEMP/ps2build-sdk.env"', text)
            self.assertIn("path: ~/.local/share/ps2build", text)
            self.assertIn(key, text)
        self.assertIn("workflow_dispatch:", build)  # the watch starts it on its branch
        self.assertNotIn("PS2BUILD_SDK_PIN:", build)

    def test_the_watch_never_publishes_and_pushes_only_its_own_branch(self):
        text = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text()
        self.assertIn('gh workflow run "$@" --repo "$R" --ref "$BRANCH"', text)
        self.assertIn("\n          dispatch compile.yml\n", text)
        self.assertNotIn("HEAD:master", text)
        self.assertIn("github.ref == 'refs/heads/master'", text)
        self.assertIn("--draft", text)
        # A run the watch starts on its branch must not publish: compile.yml publishes from master only.
        build = (WORKFLOWS / "compile.yml").read_text()
        self.assertIn("github.ref == 'refs/heads/master'", build.split("Publish rolling release", 1)[1].split("run:", 1)[0])

    def test_someone_elses_commits_keep_the_watch_off_the_branch_and_its_pull_request(self):
        # The guard runs first, so a branch that already holds the new pins gets no pull request edit either.
        step = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text().split("- name: Push the new pins", 1)[1]
        guard, same_pins = step.index('grep -qvxF "$BOT"'), step.index("already proposes these pins")
        guard_line = next(line for line in step.splitlines() if 'grep -qvxF "$BOT"' in line)
        self.assertIn("--format='%ae%n%ce'", guard_line)  # an amended commit keeps the watch as its author
        self.assertLess(guard, same_pins)
        self.assertIn("exit 0", step[guard:same_pins])

    def test_the_pull_request_waits_for_the_warm_cache(self):
        # A pull request for pins the cache does not hold yet would send each of its build jobs to the
        # official server for the new SDK. A failed warm leaves the update for the next run.
        text = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text()
        pr = text.split("\n  pr:\n", 1)[1].split("runs-on:", 1)[0]
        self.assertIn("needs: [check, warm]", pr)
        for status in ("always()", "cancelled()", "failure()"):
            self.assertNotIn(status, pr)
        # warm fails when the installer fell back to an older mirror SDK, which it does not cache.
        warm = text.split("\n  warm:\n", 1)[1].split("\n  pr:\n", 1)[0]
        stale = warm.split("if: ${{ env.PS2BUILD_SDK_STALE == 'true' }}", 1)
        self.assertEqual(len(stale), 2)
        self.assertIn("exit 1", stale[1].split("- name:", 1)[0])

    def test_the_pull_request_waits_until_the_cache_holds_the_new_sdk(self):
        # actions/cache/save only warns when its upload fails, and a cache step only warns while the cache service is
        # unavailable. So after saving, warm looks the exact key up and fails unless the cache holds it; each build
        # job of the pull request would ask the official server for the new SDK otherwise.
        text = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text()
        steps = text.split("\n  warm:\n", 1)[1].split("\n  pr:\n", 1)[0].split("\n      - ")
        save = next(i for i, step in enumerate(steps) if "uses: actions/cache/save@" in step)
        lookup, gate = steps[save + 1], steps[save + 2]

        def field(step, name):
            return [line.strip() for line in step.splitlines() if line.strip().startswith(name + ":")]

        self.assertEqual(field(lookup, "uses"), ["uses: actions/cache/restore@v4"])
        self.assertEqual(field(lookup, "lookup-only"), ["lookup-only: true"])
        self.assertEqual(field(lookup, "restore-keys"), [])  # the exact key only
        self.assertEqual(field(lookup, "if"), [])  # after a cache hit too, when nothing was saved
        for name in ("path", "key"):  # the cache version hashes the path, so it must match as well
            self.assertEqual(field(lookup, name), field(steps[save], name))
        lookup_id = field(lookup, "id")[0].split(":", 1)[1].strip()
        self.assertEqual(field(gate, "if"), ["if: ${{ steps.%s.outputs.cache-hit != 'true' }}" % lookup_id])
        self.assertIn("exit 1", gate)

    def test_the_proposal_stops_when_the_pins_changed_after_the_check(self):
        # check reads the pins at github.sha, the same commit in every job of a run. pr checks out the newest
        # master and copies check's pin file over it, which would undo a change to the pins made in between (a
        # hold, say). So pr proposes nothing then, and the next run starts from the new pins.
        text = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text()
        check = text.split("\n  check:\n", 1)[1].split("\n  warm:\n", 1)[0]
        pr = text.split("\n  pr:\n", 1)[1]
        self.assertEqual([line for line in check.splitlines() if line.strip().startswith("ref:")], [])
        self.assertIn("\n      CHECKED: ${{ github.sha }}", pr)
        guard = pr.index('if ! git diff --quiet "$CHECKED" HEAD -- .github/ps2build-sdk.env; then')
        copy = pr.index('cp "$W/ps2build-sdk.env" .github/ps2build-sdk.env')
        self.assertLess(guard, copy)
        self.assertIn("exit 0", pr[guard:copy])

    @unittest.skipIf(os.name != "posix" or not shutil.which("bash"), "runs the workflow step with bash and a fake gh")
    def test_each_run_starts_the_builds_the_proposal_has_not_run(self):
        # A push made with GITHUB_TOKEN starts no workflow, so the watch starts the builds on its branch. A dispatch
        # that failed was never retried once the branch held the pins, so each run starts every build that has no
        # run on the branch head yet, and tries them all before it fails.
        step = (WORKFLOWS / "ps2build-sdk-watch.yml").read_text().split("- name: Push the new pins", 1)[1]
        script = textwrap.dedent(step.split("run: |\n", 1)[1])
        self.assertNotIn("PUSHED", script)  # not only the run that pushed
        self.assertIn('HEAD_SHA="$OLD"', script.split("already proposes these pins", 1)[1].split("\nelse\n", 1)[0])
        self.assertIn('HEAD_SHA="$(git rev-parse HEAD)"', script.split("git push --force-with-lease", 1)[1])
        block = script[script.index("\nFAILED=\n"):]
        names = [line.split()[1] for line in block.splitlines() if line.startswith("dispatch ")]
        self.assertTrue(names)
        fake_gh = """#!/bin/sh
echo "$*" >> "$LOG"
if [ "$1 $2" = "run list" ]; then
  [ -z "$LIST_FAILS" ] || exit 1
  while [ $# -gt 0 ]; do
    if [ "$1" = --workflow ]; then workflow="$2"; fi
    shift
  done
  if grep -qxF "$workflow" "$RAN"; then echo 1; else echo 0; fi
elif [ "$1 $2" = "workflow run" ]; then
  for name in $FAILS; do
    if [ "$name" = "$3" ]; then exit 1; fi
  done
fi
exit 0
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "gh").write_text(fake_gh)
            (root / "gh").chmod(0o755)

            def run(ran=(), fails=(), list_fails=""):
                (root / "ran").write_text("".join(name + "\n" for name in ran))
                (root / "log").write_text("")
                env = {**os.environ, "PATH": f"{root}{os.pathsep}{os.environ['PATH']}", "R": "o/r", "BRANCH": "b",
                       "HEAD_SHA": "abc123", "RAN": str(root / "ran"), "LOG": str(root / "log"),
                       "FAILS": " ".join(fails), "LIST_FAILS": list_fails}
                code = subprocess.run(["bash", "-eu", "-c", block], env=env, capture_output=True).returncode
                calls = (root / "log").read_text().splitlines()
                for call in calls:
                    if call.startswith("run list "):
                        self.assertIn("--commit abc123", call)  # runs on the branch head only
                return code, [call.split()[2] for call in calls if call.startswith("workflow run ")]

            self.assertEqual(run(), (0, names))  # after a push, every build starts
            self.assertEqual(run(ran=names), (0, []))  # a later run, with every build run on the branch head
            self.assertEqual(run(ran=names[:1]), (0, names[1:]))  # starts only the missing ones
            self.assertEqual(run(fails=names[:1]), (1, names))  # fails, but only after trying every build
            self.assertEqual(run(list_fails="1"), (0, names))  # a run it cannot see counts as missing


if __name__ == "__main__":
    unittest.main()
