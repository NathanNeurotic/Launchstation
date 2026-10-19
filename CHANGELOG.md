# Launchstation changelog

## Rolling

- **Safer file replacement:** an existing destination is retained until its replacement has been written and checked. Failed publication rolls back; recovery files survive interrupted operations.
- **Clearer file operations:** the dashboard shows the filename, remaining megabytes and a progress bar while copying, with progress updates during deletion and verification.
- **Consistent storage names:** Home and the application library use MASS labels for mass-storage applications.
- **Software clock:** setting a time now offers to save it immediately and waits for verified storage completion. Save current clock retains later runtime. The clock resumes the saved checkpoint after restart; elapsed time while unplugged is unavailable.
- **Storage loading:** returned mount waits redraw more frequently while retaining the existing device discovery windows. MMCE returns each probed port's bus state to the normal card/controller drivers.
- **Power-off:** setup starts only the power-off service, without loading unused DEV9 drivers, and detects failed initialization.
- **Quieter startup:** automatic driver status is hidden behind the Launchstation startup screen; manual device actions retain their status messages.

## Earlier changes

See the repository's merged pull requests for earlier development history.
