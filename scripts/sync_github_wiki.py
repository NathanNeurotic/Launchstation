#!/usr/bin/env python3
"""Safely synchronize Launchstation's *standalone* GitHub Wiki.

This script runs against a freshly cloned .wiki.git repository, preserving all
unmanaged pages and wiki history. It never deletes files or forces pushes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import sys
from urllib.parse import urlsplit

MANAGED = re.compile(
    r"\A<!-- LAUNCHSTATION-WIKI-SYNC source=([^\s]+) sha256=([a-f0-9]{64}) -->\n\n"
)
MARKDOWN_LINK = re.compile(r"(?<!!)(\[[^\]]+\]\()([^\)\s]+)(\))")
HTML_LINK = re.compile(r'(\bhref=")([^"]+)(")')
SKIP_HTML = {"index", "wiki"}
WIKI_ALIASES = {"index": "Home", "wiki": "Home", "credits": "Credits"}


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def wiki_slug(filename: str) -> str:
    stem = filename.rsplit(".", 1)[0]
    return WIKI_ALIASES.get(stem, stem)


def render_link(target: str, source: Path, repo: str, commit: str) -> str:
    if target.startswith(("#", "http://", "https://", "mailto:", "tel:")):
        return target
    path, separator, fragment = target.partition("#")
    if not path:
        return target
    if path.endswith(".html"):
        return wiki_slug(path.rsplit("/", 1)[-1]) + (separator + fragment if separator else "")
    if path.endswith(".md"):
        repo_path = posixpath.normpath(posixpath.join(source.parent.as_posix(), path))
        if repo_path.startswith("docs/wiki/"):
            slug = wiki_slug(repo_path.rsplit("/", 1)[-1])
        elif repo_path == "docs/THIRD_PARTY_CREDITS.md":
            slug = "Third-Party-Credits"
        else:
            slug = f"https://github.com/{repo}/blob/{commit}/{repo_path}"
        return slug + (separator + fragment if separator else "")
    # Non-Markdown relative links, including non-HTML files, must resolve
    # against the *source* repository, not against .wiki.git.
    if path.startswith(("../", "./")):
        repo_path = posixpath.normpath(posixpath.join(source.parent.as_posix(), path))
        if repo_path.startswith("../"):
            raise ValueError(f"Invalid relative source link: {target} in {source}")
        return f"https://github.com/{repo}/blob/{commit}/{repo_path}" + (
            separator + fragment if separator else ""
        )
    return target


def rewrite_markdown(content: str, source: Path, repo: str, commit: str) -> str:
    return MARKDOWN_LINK.sub(
        lambda match: match[1] + render_link(match[2], source, repo, commit) + match[3],
        content,
    )


def rewrite_html(content: str, source: Path, repo: str, commit: str) -> str:
    return HTML_LINK.sub(
        lambda match: match[1] + render_link(match[2], source, repo, commit) + match[3],
        content,
    )


def add_plan(plans: dict[str, tuple[str, str]], name: str, content: str, source: str):
    if not name.endswith(".md") or "/" in name:
        raise ValueError(f"Invalid flat Wiki filename {name}")
    if name in plans:
        raise ValueError(f"Wiki page collision: {name}")
    if not content.strip():
        raise ValueError(f"Empty source for {name}")
    plans[name] = (content.rstrip() + "\n", source)


def source_pages(repo: str, commit: str) -> dict[str, tuple[str, str]]:
    plans: dict[str, tuple[str, str]] = {}
    guide_dir = Path("docs/wiki")
    content_dir = Path("docs-site/content")
    manifest = json.loads(Path("docs-site/manifest.json").read_text(encoding="utf-8"))
    if not guide_dir.is_dir() or not content_dir.is_dir():
        raise ValueError("Documentation source folders are absent")
    guide_files = sorted(guide_dir.glob("*.md"))
    if not any(path.name == "Home.md" for path in guide_files):
        raise ValueError("docs/wiki/Home.md is required")

    for path in guide_files:
        content = rewrite_markdown(path.read_text(encoding="utf-8"), path, repo, commit)
        add_plan(plans, path.name, content, path.as_posix())

    for path in sorted(content_dir.glob("*.html")):
        if path.stem in SKIP_HTML:
            continue
        page = wiki_slug(path.name) + ".md"
        content = rewrite_html(path.read_text(encoding="utf-8"), path, repo, commit)
        add_plan(plans, page, content, path.as_posix())

    credits = Path("docs/THIRD_PARTY_CREDITS.md")
    content = rewrite_markdown(credits.read_text(encoding="utf-8"), credits, repo, commit)
    add_plan(plans, "Third-Party-Credits.md", content, credits.as_posix())

    sidebar = [
        "# LAUNCHSTATION",
        "",
        "- [Wiki Home](Home)",
        "- [Current Features](Current-Features)",
        "- [MMCE Recovery](MMCE-Recovery)",
        "- [Release Recovery](Releases-and-Recovery)",
        "- [Full Third-Party Credits](Third-Party-Credits)",
        "",
    ]
    for group in manifest["sections"]:
        sidebar.extend((f"### {group['name']}", ""))
        for slug, title, *_ in group["pages"]:
            if slug in SKIP_HTML:
                continue
            page = wiki_slug(slug + ".html")
            if page + ".md" not in plans:
                raise ValueError(f"Manifest page not published: {slug}")
            sidebar.append(f"- [{title}]({page})")
        sidebar.append("")
    add_plan(plans, "_Sidebar.md", "\n".join(sidebar), "docs-site/manifest.json")

    footer = (
        "[LAUNCHSTATION repository](https://github.com/" + repo + ") · "
        "[Live documentation](https://nathanneurotic.github.io/Launchstation/) · "
        "[Release downloads](https://github.com/" + repo + "/releases/tag/latest)\n\n"
        "Maintained from [documentation sources](https://github.com/" + repo
        + "/tree/" + commit + "/docs/wiki). Do not edit generated pages directly; "
        "propose changes in the source repository.\n"
    )
    add_plan(plans, "_Footer.md", footer, "docs/wiki")
    return plans


def apply_plan(wiki: Path, plans: dict[str, tuple[str, str]]):
    if not (wiki / ".git").exists():
        raise ValueError("Destination is not an already-cloned GitHub Wiki git repository")
    pending = {}
    archives = {}
    for name, (body, source) in plans.items():
        header = f"<!-- LAUNCHSTATION-WIKI-SYNC source={source} sha256={sha256(body)} -->\n\n"
        desired = header + body
        destination = wiki / name
        if destination.exists():
            current = destination.read_text(encoding="utf-8")
            if current == desired:
                continue
            tracked = MANAGED.match(current)
            if tracked:
                old_body = current[tracked.end():]
                if sha256(old_body) != tracked[2]:
                    raise ValueError(
                        f"{name}: manual edits to a previously synchronized page detected. "
                        "Stop and incorporate those edits into authored documentation first."
                    )
            else:
                suffix = hashlib.sha256(current.encode("utf-8")).hexdigest()[:12]
                archive_name = f"Legacy-{Path(name).stem}-{suffix}.md"
                if archive_name in plans:
                    raise ValueError(f"Unexpected archive collision: {archive_name}")
                archive_path = wiki / archive_name
                if archive_path.exists() and archive_path.read_text(encoding="utf-8") != current:
                    raise ValueError(f"Existing wiki archive conflicts: {archive_name}")
                if not archive_path.exists():
                    archives[archive_name] = current
        pending[name] = desired
    # No modifications are made until all collisions and manual edits are checked.
    for name, body in archives.items():
        (wiki / name).write_text(body, encoding="utf-8")
    for name, body in pending.items():
        (wiki / name).write_text(body, encoding="utf-8")
    print(
        f"Wiki sync: {len(plans)} managed pages; "
        f"{len(pending)} added/updated, {len(archives)} existing pages archived; "
        "no unmanaged pages deleted"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wiki-dir", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--sha", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
        parser.error("Invalid owner/repository")
    if not re.fullmatch(r"[0-9a-f]{40}", args.sha):
        parser.error("Invalid source commit")
    try:
        apply_plan(Path(args.wiki_dir), source_pages(args.repo, args.sha))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"Wiki synchronization stopped safely: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
