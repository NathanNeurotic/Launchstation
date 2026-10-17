"""Source-level regression contract for R3Z per-core opt-out switches.

The toolchain build covers C syntax and linking; this test checks dispatch
ordering, backward-compatible CNFs, and dashboard persistence wiring.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]

def source(path):
    return (root / path).read_text(encoding="utf-8")

def function(text, signature):
    start = text.index(signature)
    end = text.index("\n}", start) + 2
    return text[start:end]

header = source("include/launchelf.h")
config = source("src/config.c")
settings = source("src/dash_settings.c")
actions = source("src/main_actions.c")
docs = source("docs/LAUNCHELF.CNF")

# Default NO means enabled on older CNFs, and each bit is independently stored.
for feature, field in (
    ("EMBER", "disable_ember"),
    ("POPSTARTER", "disable_popstarter"),
    ("NEUTRINO", "disable_neutrino"),
):
    assert f"int {field};" in header
    assert f"setting->{field} = 0;" in config
    assert f"Disable_{feature} = %d" in config
    assert f'strcmp(name, "Disable_{feature}")' in config
    assert f"setting->{field} = (atoi(value) != 0);" in config
    assert f"Disable_{feature} = 0" in docs
    assert f"S_DISABLE_{feature}" in settings
    assert f'"Disable {feature.title() if feature != "POPSTARTER" else "POPStarter"} support"' in settings

# Dashboard choices participate in the existing classic CNF Save/Discard session.
for token in ("S_DISABLE_EMBER", "S_DISABLE_POPSTARTER", "S_DISABLE_NEUTRINO"):
    assert settings.count("case " + token + ":") == 2
assert "memcpy(setting, &snap_classic, sizeof(snap_classic));" in settings
assert "cfgSave(CFG_SAVE_CNF" in settings

# The image handler remains selected by the existing file format, with no
# unrelated ELF fallback when the chosen core is disabled.
for token in ("IsPopstarterVcdPath(path)", 'genCmpFileExt(path, "CUE") || IsEmberGamePath(path)', "IsNeutrinoIsoPath(path)"):
    assert token in actions
for path, symbol, field, first_io in (
    ("src/ember.c", "LaunchEmberGame", "disable_ember", "parseEmberGamePath(path,"),
    ("src/popstarter.c", "LaunchPopstarterVcd", "disable_popstarter", "prepareLaunch(path,"),
    ("src/neutrino_iso.c", "LaunchNeutrinoIso", "disable_neutrino", "validateIso(path,"),
):
    body = function(source(path), "int " + symbol + "(")
    guard = "if (setting != NULL && setting->" + field + ")"
    assert body.index(guard) < body.index(first_io), (symbol, "gate must precede image access")
    assert body.index(guard) < body.index("CleanUpFor"), (symbol, "gate must precede IOP shutdown")

print("R3Z launcher-disable controls: defaults, persistence, route guards OK")
