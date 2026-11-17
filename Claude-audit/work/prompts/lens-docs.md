# Lens: documentation

Your files are prose: READMEs, guides, wiki pages, notes. Read each file. Long generated or tabular files may be skimmed; say so in `coverage`. Questions:

- Stale: does it describe files, options, menus, build steps or features that no longer exist? Check each concrete claim you rely on against the repo (Grep for the file name, option or function) and cite both the doc line and what you found.
- Duplicate: the same content in several places (README, docs folder, wiki, docs-site sources, translated copies that are not maintained). Cite every copy.
- Clutter: agent handoff notes, investigation logs, plans, audits, status reports, changelogs of internal work and similar process files that are committed but are not user or developer documentation; generated output committed next to its source.
- Wrong: instructions that would fail or mislead (wrong path, wrong command).

Group findings: one finding per kind of problem per document set, with every location (`"whole": true` for whole files). Do not write one finding per file. Do not use earlier review or audit notes found in the repo as a source of findings about the code; judge them only as documents (are they clutter?).

Rate honestly: documentation findings are rank 1 to 3 unless a very large amount of text can go.
