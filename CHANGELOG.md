# Launchstation changelog

## Rolling

- **Dashboard languages:** navigation, Settings labels, common actions and transfer progress use the nine built-in language tables. External LNG overrides also apply to these labels. User filenames, paths and application metadata keep their original text; some extended help remains in English.
- **File-copy compatibility:** restore upstream wLaunchELF_R3Z direct overwrite behavior. Remove the custom destination-stat, staging, verification, backup and rename transaction so ordinary paste does not depend on those additional operations.
- **Clearer file operations:** copy/delete popups give filenames their own full-width row, show remaining MB or completed selected items, and finish with a full progress bar. Paste failures include a readable reason, stage and numeric error.
- **Navigation and rendering:** wrapped list selections stay visible in the new viewport. Carousel rendering omits fully offscreen cards while retaining visible geometry and order; affected-console performance still needs confirmation.
- **Neutrino options:** menu audio stays active until launch is committed, with ordinary ELF muting and failure recovery preserved.
- **Retained improvements:** concise release notes, short-transfer handling, storage preflight, MASS display names, verified software-clock saves, lazy storage loading and power-off service initialization remain available.

## Earlier changes

See the repository's merged pull requests for earlier development history.
