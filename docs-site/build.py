#!/usr/bin/env python3
"""Build the Launchstation static documentation site and optional offline ZIP."""
from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import sys
import zipfile
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MANIFEST = ROOT / "manifest.json"
CONTENT = ROOT / "content"
STATIC = ROOT / "static"

class TextExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self.parts = []
    def handle_data(self, data):
        data = " ".join(data.split())
        if data:
            self.parts.append(data)
    def text(self):
        return " ".join(self.parts)

def plain(fragment: str) -> str:
    p = TextExtractor()
    p.feed(fragment)
    return p.text()

def page_map(manifest):
    out = {}
    for section in manifest["sections"]:
        for slug, title, icon in section["pages"]:
            out[slug] = {"title": title, "icon": icon, "section": section["name"]}
    return out

def nav_html(manifest, active):
    rows = [
        '<div class="sidebar-brand-sub">PLAYSTATION 2 // Launchstation</div>'
    ]
    for section in manifest["sections"]:
        rows.append(f'<h4>{html.escape(section["name"])}</h4>')
        for slug, title, icon in section["pages"]:
            cls = ' class="active"' if slug == active else ""
            href = "index.html" if slug == "index" else f"{slug}.html"
            rows.append(
                f'<a href="{href}"{cls}><span class="icon icon-{html.escape(icon)}"></span>'
                f'{html.escape(title)}</a>'
            )
    return "\n    ".join(rows)

def topbar(manifest):
    s = manifest["site"]
    return f'''<header class="topbar">
  <button class="menu-btn" aria-label="Menu">☰</button>
  <a class="brand" href="index.html">
    <span class="psbtns"><i class="tri">&#9651;</i><i class="cir">&#9711;</i><i class="cro">&#10005;</i><i class="squ">&#9633;</i></span>
    {html.escape(s["brand"])} <small>DOCS</small></a>
  <div class="topbar-actions">
    <a class="topbar-btn primary" href="releases.html"><span class="icon icon-releases"></span>Downloads</a>
    <a class="topbar-btn" href="{html.escape(s["repo"])}" target="_blank" rel="noopener"><span class="icon icon-github"></span>GitHub</a>
  </div>
  <div class="search">
    <span class="ico"></span>
    <input id="q" type="search" placeholder="Search devices, Neutrino, cloud, settings…  ( / )" autocomplete="off">
    <kbd>/</kbd>
    <div id="results"></div>
  </div>
</header>'''

def render(manifest, slug, meta, fragment):
    s = manifest["site"]
    title = meta["title"]
    description = plain(fragment)[:260]
    canonical = s["pages_url"] + ("index.html" if slug == "index" else f"{slug}.html")
    return f'''<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)} · {html.escape(s["title"])}</title>
<meta name="description" content="{html.escape(description, quote=True)}">
<link rel="icon" type="image/svg+xml" href="assets/favicon.svg">
<link rel="canonical" href="{html.escape(canonical, quote=True)}">
<meta property="og:site_name" content="{html.escape(s["title"], quote=True)}">
<meta property="og:type" content="website">
<meta property="og:title" content="{html.escape(title, quote=True)} · {html.escape(s["title"], quote=True)}">
<meta property="og:description" content="{html.escape(description, quote=True)}">
<meta name="twitter:card" content="summary">
<link rel="stylesheet" href="assets/style.css">
</head><body data-base="">
{topbar(manifest)}
<div class="layout">
  <nav class="sidebar">
    {nav_html(manifest, slug)}
  </nav>
  <main class="content">
{fragment}
  </main>
  <aside class="toc"><div class="toc-h">On this page</div><nav id="toc-nav"></nav></aside>
</div>
<footer>
  <p><b>Launchstation Documentation</b> — PlayStation 2 dashboard and file manager maintained by Ripto / NathanNeurotic. This documentation is generated from the same repository as the software.</p>
  <p class="prov-inline">Launchstation descends from the LaunchELF/uLaunchELF/wLaunchELF family and incorporates work by the wider PS2 homebrew community. See <a href="credits.html">Credits &amp; Licenses</a>.</p>
</footer>
<script src="data/search-index.js"></script>
<script src="assets/app.js"></script>
<script src="assets/orbs.js"></script>
</body></html>
'''

def section_index(fragment, slug, page_title, category):
    entries = [{"title": page_title, "cat": category, "url": "index.html" if slug == "index" else f"{slug}.html", "text": plain(fragment)}]
    pat = re.compile(r'<h([23])\s+[^>]*id="([^"]+)"[^>]*>(.*?)</h\1>', re.I | re.S)
    matches = list(pat.finditer(fragment))
    for i, m in enumerate(matches):
        anchor = m.group(2)
        heading = plain(m.group(3))
        end = matches[i + 1].start() if i + 1 < len(matches) else len(fragment)
        body = plain(fragment[m.end():end])
        entries.append({
            "title": heading,
            "cat": category,
            "url": ("index.html" if slug == "index" else f"{slug}.html") + "#" + anchor,
            "text": body[:2400],
        })
    return entries

def validate_output(out: Path, manifest, slugs):
    errors = []
    style = (out / "assets/style.css").read_text(encoding="utf-8")
    used_icons = {icon for section in manifest["sections"] for _, _, icon in section["pages"]}
    for icon in sorted(used_icons):
        if f".icon-{icon}" not in style:
            errors.append(f"missing stylesheet icon class: icon-{icon}")
    html_files = list(out.glob("*.html"))
    targets = {p.name for p in html_files}
    for p in html_files:
        text = p.read_text(encoding="utf-8")
        if "{{" in text or "}}" in text:
            errors.append(f"{p.name}: unresolved template token")
        if p.name != "credits.html" and ("wLaunchELF R3Z" in text or "wLaunchELF_R3Z" in text):
            errors.append(f"{p.name}: stale upstream branding outside credits")
        for href in re.findall(r'href="([^"]+)"', text):
            if href.startswith(("http://", "https://", "#", "mailto:")):
                continue
            dest = href.split("#", 1)[0].split("?", 1)[0]
            if not dest:
                continue
            if dest.endswith(".html") and dest not in targets:
                errors.append(f"{p.name}: broken internal link {href}")
    required = {"index"} | set(slugs)
    actual = {p.stem for p in html_files}
    missing = sorted(required - actual)
    if missing:
        errors.append("missing generated pages: " + ", ".join(missing))
    for asset in ("assets/style.css", "assets/app.js", "assets/orbs.js", "data/search-index.json", "data/search-index.js", ".nojekyll"):
        if not (out / asset).exists():
            errors.append(f"missing output asset: {asset}")
    if errors:
        raise SystemExit("\n".join("ERROR: " + e for e in errors))

def make_zip(out: Path, zip_path: Path):
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                z.write(p, Path("Launchstation-Documentation") / p.relative_to(out))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--zip", dest="zip_path")
    args = ap.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    pages = page_map(manifest)
    out = Path(args.out).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copytree(STATIC, out, dirs_exist_ok=True)
    (out / "data").mkdir(exist_ok=True)
    (out / ".nojekyll").write_text("", encoding="utf-8")

    search = []
    for slug, meta in pages.items():
        src = CONTENT / f"{slug}.html"
        if not src.exists():
            raise SystemExit(f"missing content fragment: {src}")
        fragment = src.read_text(encoding="utf-8")
        dst = out / ("index.html" if slug == "index" else f"{slug}.html")
        dst.write_text(render(manifest, slug, meta, fragment), encoding="utf-8")
        search.extend(section_index(fragment, slug, meta["title"], meta["section"].lower()))

    compact_index = json.dumps(search, ensure_ascii=False, separators=(",", ":"))
    (out / "data/search-index.json").write_text(compact_index, encoding="utf-8")
    (out / "data/search-index.js").write_text("window.WLE_DOC_INDEX=" + compact_index + ";\n", encoding="utf-8")
    validate_output(out, manifest, pages.keys())
    if args.zip_path:
        make_zip(out, Path(args.zip_path).resolve())
    print(f"Built {len(pages)} pages with {len(search)} search entries at {out}")
    if args.zip_path:
        print(f"Offline ZIP: {Path(args.zip_path).resolve()}")

if __name__ == "__main__":
    main()
