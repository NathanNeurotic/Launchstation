# Lens: structure (dead code, duplication, overengineering, clutter, unfinished features)

Your job on this unit is to find code that can be deleted, merged or simplified: categories 1, 2, 3, 7 and 8, plus the size effect of what you find. A second reader covers bugs, performance and memory on the same lines, so do not spend time there. If you trip over an obvious defect, report it anyway.

Go through this list for every file.

## Dead code (1)

- Functions, static functions, macros, typedefs, enum values, struct fields, globals and config keys that nothing uses. Start with the leads, then check every definition you read that looks suspicious: a repo-wide Grep on the name is cheap.
- Things used only by other dead things. Follow the chain and report it as one finding.
- Prototypes and extern declarations without a definition; exports that nobody imports.
- Parameters and return values that nobody uses; variables that are written and never read; fields that are set and never read.
- Unreachable code: after return/goto/break, conditions that are always true or always false (compare with the defines the real build sets), switch cases that cannot occur, error handling for calls that cannot fail.
- `#if 0`, `#ifdef` on macros that no build defines, commented-out code, leftover debug prints, test hooks, benchmark code, "temporary" code.
- Whole files or directories that no build compiles and nothing loads.

## Duplication (2)

- Copy-paste inside a file and between files. Use the duplicate leads, and your own eyes as well: scripts miss copies with renamed variables.
- Functions that differ only by a constant, a type or a string; repeated blocks that an existing helper already implements (name the helper and where it is).
- The same table, string list, constant set or struct declared twice.
- Fill `reusable_helpers` in your output with the general helpers your files define. A later pass uses that list to find re-implementations elsewhere.

## Overengineering (3)

- Wrappers that only forward; abstraction layers, vtables, callback tables and registries with a single implementation or a single caller.
- Options, modes and flags that nobody can set or that always have the same value.
- Redundant fallbacks, retries and backups; the same value validated again at several layers; defensive checks for states that cannot occur; caches of cheap values.
- State machines, queues, logging or telemetry machinery that is bigger than its job.
- Compatibility code for things that no longer exist in the repo.

## Clutter (7) and unfinished features (8)

- Sizeable stale comments and banners that describe code that is gone; generated files that are committed; stray files.
- Stubs, TODO features, menu entries or options for things that are not implemented; an old code path that a newer feature has replaced but that is still there.

## Rating

Rate by what deleting or merging saves: source lines, and bytes of ELF or RAM where you can show them. Unused static functions are usually dropped by the compiler already, and unused non-static functions are dropped only when the build uses section garbage collection: say which case applies when you claim a size effect.
