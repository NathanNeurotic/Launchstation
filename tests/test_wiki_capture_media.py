"""The generated wiki must render captured media from the pinned source revision."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wiki_sync', root / 'scripts/sync_github_wiki.py')
wiki = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wiki)
repo, commit = 'owner/project', 'a' * 40
raw = f'https://raw.githubusercontent.com/{repo}/{commit}/docs-site/static/assets/ui/'
html = '<img src="assets/ui/home.png"><video poster="assets/ui/home.png"><source src="assets/ui/tour.mp4"></video><a href="assets/ui/tour.gif">GIF</a><a href="themes.html#presets">Themes</a>'
rendered = wiki.rewrite_html(html, Path('docs-site/content/ui-captures.html'), repo, commit)
for name in ['home.png', 'tour.mp4', 'tour.gif']:
    assert raw + name in rendered
assert 'href="themes#presets"' in rendered
assert 'src="assets/' not in rendered and 'poster="assets/' not in rendered
md = '![Home](../../docs-site/static/assets/ui/home.png)\n[Guide](Theme-and-Wallpaper-Guide.md)'
rendered = wiki.rewrite_markdown(md, Path('docs/wiki/Home.md'), repo, commit)
assert f'![Home]({raw}home.png)' in rendered
assert '[Guide](Theme-and-Wallpaper-Guide)' in rendered
external = '<img src="https://example.com/image.png">'
assert wiki.rewrite_html(external, Path('docs-site/content/ui-captures.html'), repo, commit) == external
print('PASS: wiki images, GIF/MP4 links and posters use pinned raw assets; guide links and external URLs preserved')
