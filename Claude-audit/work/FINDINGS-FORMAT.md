# Findings file format

Every auditing agent writes ONE JSON file: `findings/<Repo>/<chunk-id>.json` (UTF-8, valid JSON, no comments, no trailing commas). Write it with the Write tool, then validate it:

```
python -I scripts/validate_findings.py . <Repo> <chunk-id>      (run from the work root)
```

Fix every reported problem and run it again until it prints `OK`. The validator checks that every cited path exists at the audited commit and that every line range is inside the file, so cite real lines.

## Shape

```json
{
  "repo": "RiptOPL",
  "chunk": "ript-core-07",
  "coverage": [
    {"path": "src/app/opl.cpp", "status": "read_full", "note": ""},
    {"path": "include/opl.h", "status": "read_partial", "note": "lines 1-400 read; 401-520 is a generated table, skimmed"}
  ],
  "module_summary": "Two to six sentences: what these files do and how they fit into the program.",
  "reusable_helpers": [
    {"name": "copyFileBlocks", "path": "src/core/util.cpp", "line": 120, "what": "copies a file in 64 KB blocks with a progress callback"}
  ],
  "findings": [
    {
      "n": 1,
      "title": "guiFooBar is never called",
      "category": 1,
      "locations": [
        {"path": "src/ui/gui.cpp", "start": 120, "end": 168},
        {"path": "include/gui.h", "start": 44, "end": 44}
      ],
      "problem": "guiFooBar has no caller: the only matches for the name in the repo are its definition and its prototype.",
      "fix": "Delete the function and its prototype.",
      "justification": "",
      "benefit": "~50 lines removed; code is already dropped by --gc-sections, so no ELF change (INFERRED)",
      "benefit_rank": 2,
      "lines_removed": 50,
      "effort": "S",
      "risk": "Low",
      "confidence": "High",
      "basis": "READ",
      "evidence": "grep -rnw guiFooBar over src include modules ee_core Makefile *.S exports.tab: gui.cpp:120 (definition), gui.h:44 (prototype), nothing else."
    }
  ]
}
```

## Field rules

- `coverage`: one entry for EVERY file assigned to you. `status` is `read_full` (you read every line of your assigned range), `read_partial`, `skimmed` or `not_read`; anything other than `read_full` needs a `note` saying exactly what was not read and why. Be truthful: this list becomes the report's "files not read" section.
- `module_summary`: orientation for the duplicate-code analysis across chunks.
- `reusable_helpers`: up to about 12 general-purpose helpers defined in your files that other code is likely to have re-implemented (file copy, path join/normalise, string trim/compare, config parse, hex/number format, CRC/hash, endian swap, list/sort, retry or delay loops, device or network init, memory-card write helpers, text drawing helpers). Leave empty if none.
- `findings[].n`: 1, 2, 3 ... unique inside the file.
- `title`: short and specific (name the function/file/option). Max 150 characters.
- `category`: integer 1-8 (see AUDIT-RULES.md). Pick the main one.
- `locations`: repo-relative paths with forward slashes and tight line ranges (`start`..`end`, 1-based, inclusive). For a whole file, directory or binary asset use `{"path": "docs/demo.mp4", "whole": true}`. To cite another audited repo add `"repo": "Launchstation"` to that location. List every place where the same problem occurs instead of writing several findings.
- `problem`: one or two sentences. Put the essential proof in it (for dead code: where the name does and does not appear). Max 800 characters.
- `fix`: the simplest change, in words. No patches, no code blocks. Max 800 characters. If the simplest fix is deletion, say "Delete ...".
- `justification`: empty string unless the fix ADDS anything (check, helper, option, test, wrapper...). Then it is mandatory: the concrete problem and why this is the simplest net win.
- `benefit`: bytes / RAM / ms / lines removed / card writes saved, with MEASURED or INFERRED and the arithmetic. Max 320 characters.
- `benefit_rank`: integer 1-5 on the scale in AUDIT-RULES.md. Be honest; most findings are 1-3.
- `lines_removed`: integer estimate of net source lines removed (0 if not applicable, negative if the fix adds lines).
- `effort`: `S`, `M` or `L`. `risk`: `Low`, `Med` or `High`. `confidence`: `High`, `Med` or `Low`. `basis`: `READ` or `INFERRED`.
- `evidence`: what you searched and read to establish the finding (commands and results in brief). For internal use by the verifier. Max 1200 characters.
- Never include tokens, keys, passwords, long hex strings or key material anywhere. Shorten commit ids to 10 characters.

A chunk with nothing worth reporting is fine: write `"findings": []` and still fill `coverage` and `module_summary`.

# Verification file format

A verifier writes `findings/<Repo>/<chunk-id>.verify.json` and validates it with the same script plus `--verify`:

```json
{
  "repo": "RiptOPL",
  "chunk": "ript-core-07",
  "verdicts": [
    {"n": 1, "verdict": "confirmed", "note": "Re-read gui.cpp:120-168 and gui.h:44; grep -rnw guiFooBar over the whole repo gives only those two lines."},
    {"n": 2, "verdict": "corrected", "note": "Real, but the buffer is 4 KB not 64 KB and lines were off by 12.",
     "corrected": {"locations": [{"path": "src/core/io.cpp", "start": 212, "end": 219}], "benefit": "4 KB .bss (INFERRED: char buf[4096])", "benefit_rank": 2}},
    {"n": 3, "verdict": "refuted", "note": "The function is referenced from the dispatch table at src/ui/menu.cpp:88."}
  ]
}
```

- One verdict for EVERY finding number in the findings file.
- `confirmed`: every claim checked and correct as written (including line numbers and ratings).
- `corrected`: the problem is real but something was wrong (lines, numbers, rating, wording, fix that adds without justification). Put only the fields that change in `corrected`; they replace the original fields.
- `refuted`: the claim is wrong, not reproducible from the cited code, already handled, forbidden by AUDIT-RULES.md, or pure speculation. Say why in `note`.
