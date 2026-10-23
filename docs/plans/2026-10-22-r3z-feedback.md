# R3Z feedback follow-up

Approved scope, based on master 29e5aaf. Keep all work in one draft PR; do not request CodeRabbit or mark ready until the implementation, audit and validation are complete. Split Part 2 only if the PR would exceed 100 changed files. Preserve optional startup driver loading and specialized game-loader contracts.

- [ ] Investigate video transitions and add timed confirmation/rollback before saving a changed mode.
- [ ] Gate i.Link presentation and automatic probing by actual console capability, including PSX exclusion.
- [ ] Route Configure consistently to Settings within the dashboard, preserving classic configuration outside it.
- [ ] Align browser capacity/scrolling with ten rows where supported, preserving NTSC/PAL hints and safety margins.
- [ ] Use stable device names; show loading/errors when opening, retain diagnostic tools and lazy loading.
- [ ] Audit upstream 4.80_r3z game identification, RetroGEM output, MMCE switching and MX4SIO/source exclusion; patch confirmed gaps only.
- [ ] Clarify FTP server / remote access labels while preserving saved command aliases and all existing services.
- [ ] Retain opt-in driver auto-load settings and existing defaults.

Validation will distinguish source comparisons, executable host fixtures, target builds, CI/packages and real-console tests. Hardware-only acceptance remains explicit. No inference from a successful build to PAL, RetroGEM or MMCE acceptance.
