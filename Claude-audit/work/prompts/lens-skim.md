# Lens: vendored or generated content (structural review, not a line-by-line read)

Your unit is third-party library code, generated data or language tables. Do not read it line by line. Establish the following, with evidence.

- Use: which of these files does the build compile, pack or load (read the build files), and which are only carried along? Files that are never compiled or loaded are category 1 or 7.
- Configuration (libraries): which features are switched on in the config header or build flags, and which of them does the product use? Name features that could be switched off and what they cost. Use `evidence/<Repo>/BUILD-EVIDENCE.md` and the map data in `evidence/<Repo>/raw/` when present; otherwise mark the number INFERRED.
- Origin: is this copy modified from upstream (look for ORIGIN, README or VERSION files, local-change markers, `git -C src/<Repo> log --oneline -8 -- <dir>`)? Is the same library available as a package of the PS2 SDK the repo already builds with, or present a second time in this repo or in another audited repo?
- Generated data (byte arrays of images, fonts, sounds, modules, tables): are the generator and the source asset in the repo, is the array referenced, is the same data embedded twice, is the format wasteful (for example raw RGBA where the code already supports a smaller format)? Give sizes from the file or the map.
- Language files: string ids that the code never uses, ids that the code uses but that are missing, languages that are far behind. Use Grep and small read-only one-liners; give counts and a few examples, not every line.

In `coverage` use status `skimmed` with a note saying what you checked. Prefer "stop compiling it", "trim the config", "drop the private copy", "delete the unused file" over any rewrite.
