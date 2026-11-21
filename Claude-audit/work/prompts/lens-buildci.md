# Lens: build files, workflows and repo root

Your files are the build definition, the CI workflows and the files in the repo root. Read every line, with these questions.

- What is the real build? Which file drives it, which configurations and variants exist, and which of them does CI build or release? Everything outside that is a candidate: unused targets, variables, flags, variants, include paths, libraries and packaging steps, and legacy build systems kept next to the real one.
- Flags: optimisation level, section garbage collection (`-ffunction-sections -fdata-sections -Wl,--gc-sections`), LTO, strip, debug info, duplicated or contradictory flags, defines that no source tests, sources or libraries listed that nothing needs.
- Workflows: jobs or steps that do the same work twice; matrix entries that duplicate each other; workflows that never trigger or always skip; steps that cannot fail (`|| true`, `continue-on-error`); artefacts that nobody uses; set-up repeated where one step would do; long fixed sleeps; scripts that are called but do not exist; the same logic copied into several workflow files.
- Read-only GitHub queries are allowed as evidence: `gh workflow list -R NathanNeurotic/<Repo> --all` and `gh run list -R NathanNeurotic/<Repo> --limit 40 --json workflowName,conclusion,createdAt,updatedAt,event`. Do not read pull requests or issues.
- Root files: stray files, duplicated or stale top-level docs, editor or agent configuration that nothing uses, ignore and attribute files that do not match what is tracked (see `evidence/<Repo>/repo-clutter.md`).
- Secrets: never quote a value. Report only "secret-like value at <path>".

Do not list stale branches or tags; a separate step lists them.

Rate by CI minutes and maintenance saved, and by bytes of shipped binary when a flag changes size (INFERRED unless the build evidence shows it).
