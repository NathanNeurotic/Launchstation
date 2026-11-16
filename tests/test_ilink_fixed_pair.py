"""Launchstation's own i.LINK pair is RiptOPL's fixed build, and an i.LINK Neutrino launch prefers an
install that carries the same pair (ship/neutrino-ilink/README.txt)."""
import hashlib
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
yaml = (root / "ps2.yaml").read_text(encoding="utf-8")
blobs = (root / "src/embed_blobs.S").read_text(encoding="utf-8")
iso = (root / "src/neutrino_iso.c").read_text(encoding="utf-8")
kit = (root / "scripts/package_kit.py").read_text(encoding="utf-8")

# The dashboard embeds the shipped files, not the SDK pair.
assert 'EMBED_FILE(iLinkman_irx, "ship/neutrino-ilink/iLinkman.irx")' in blobs
assert 'EMBED_FILE(IEEE1394_bd_irx, "ship/neutrino-ilink/IEEE1394_bd_mini.irx")' in blobs
main = yaml[yaml.index("  - name: wLaunchELF"):]
assert "      - iLinkman\n" not in main and "IEEE1394_bd_mini:IEEE1394_bd" not in main

# Those files are the digests the kit verifies for neutrino/modules/.
for name in ("iLinkman.irx", "IEEE1394_bd_mini.irx"):
    digest = hashlib.sha256((root / "ship/neutrino-ilink" / name).read_bytes()).hexdigest()
    assert re.search(r'"modules/%s":\s*"%s"' % (re.escape(name), digest), kit), name

# i.LINK launches order installs with the fixed pair first, and say so when the chosen one lacks it.
assert 'if (!strcmp(bsd, "ilink"))\n\t\tpreferFixedIlinkPair(&candidates);' in iso
assert iso.index("preferFixedIlinkPair(&candidates);") < iso.index("ret = preparePopstarterCandidateLaunch(")
assert '!strcmp(bsd, "ilink") && !neutrinoIlinkPairIsOurs(selected)' in iso
assert iso.index("neutrinoIlinkPairIsOurs(selected)") < iso.index('CleanUpForKeepIopExec("Preparing Neutrino")')

print("PASS: Launchstation runs the fixed i.LINK pair and prefers a Neutrino install that carries it")
