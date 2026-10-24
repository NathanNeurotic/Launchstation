# Launchstation changelog

## Rolling

- **Dashboard languages:** navigation, Settings labels, common actions and transfer progress use the nine built-in language tables. External LNG overrides also apply to these labels. User filenames, paths and application metadata keep their original text; some extended help remains in English.
- **Safer copying:** missing-file checks can confirm ambiguous legacy-driver results through a completed parent-directory listing. Physical memory-card creation uses MCMAN-compatible flags. Replacements still keep the original until the new copy has been written, closed and verified, with rollback/recovery on publication failure.
- **Clearer file operations:** copy/delete popups give filenames their own full-width row, show remaining MB or completed selected items, and finish with a full progress bar. Paste failures include a readable reason, stage and numeric error.
- **Navigation and rendering:** wrapped list selections stay visible in the new viewport. Carousel rendering omits fully offscreen cards while retaining visible geometry and order; affected-console performance still needs confirmation.
- **Neutrino options:** menu audio stays active until launch is committed, with ordinary ELF muting and failure recovery preserved.
- **Retained improvements:** concise release notes, short-transfer handling, storage preflight, MASS display names, verified software-clock saves, lazy storage loading and power-off service initialization remain available.

## Earlier changes

See the repository's merged pull requests for earlier development history.
