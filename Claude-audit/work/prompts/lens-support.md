# Lens: tests, tools, scripts, packaging

These files support the product; they are not shipped to the console. Read every line, with these questions.

- Is it used? For every script, tool and test file: who runs it (a workflow, a build file, another script, the docs)? Search the whole repo for its file name. A file that nothing runs is category 1 or 7.
- Tests: what does each test really check? Report, grouped, with every location:
  - tests that only search source text for strings or patterns (they break on harmless edits and prove nothing about behaviour);
  - tests of code or features that no longer exist; tests that cannot fail; tests that are skipped or never collected;
  - several test files that cover the same thing; copy-pasted fixtures and helpers;
  - slow tests, and tests that need the network or hardware and therefore never run.
  Do not propose new tests. Say what could be deleted or merged and what real coverage would be lost, if any.
- Scripts and tools: one-off migration or debugging scripts that were kept; duplicated scripts (the same job twice, or the same script in several repos); dead options and code paths; heavy dependencies for small jobs; generated output that is committed.
- Packaging and release helpers: steps that repeat what another step does; files that are packed but that the product does not use.
- Real bugs in tools that produce shipped artefacts (packers, signers, asset converters, updaters) are category 6. Give the trigger.
- Agent and editor configuration, prompt files and agent notes kept in the repo are clutter candidates (category 7) when nothing uses them.

Rate by lines removed, CI minutes saved and maintenance removed. Most findings here are rank 1 to 3.
