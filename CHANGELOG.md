# Launchstation changelog

## Rolling

- **Infinite list navigation wrap-around:** dashboard lists now cycle at boundaries (up from first selects last, down from last selects first), including File Browser views, app/library lists, Files/Devices pickers, Tools, Settings and HDD Manager.
- **Safer file replacement:** an existing destination is retained until its replacement has been written and checked. Failed publication rolls back; recovery files survive interrupted operations.
- **Copy failure details:** compact stage/error, device, byte-count and build information make failed transfers diagnosable. A failed source-folder listing is no longer reported as an empty successful copy.
- **Cross-device copying:** positive short reads and writes complete the remaining bytes. Slot-2 card preparation happens before opening source files when leaving MX4SIO.
- **Clearer file operations:** the dashboard shows the filename, remaining megabytes and a progress bar while copying, with progress updates during deletion and verification.
- **Release-page clarity:** the public release page now includes a concise Rolling changelog summary and links directly to full release notes and changelog history.
- **Consistent storage names:** Home and the application library use MASS labels for mass-storage applications.
- **Software clock:** setting a time now offers to save it immediately and waits for verified storage completion. Save current clock retains later runtime. The clock resumes the saved checkpoint after restart; elapsed time while unplugged is unavailable.
- **Storage loading:** returned mount waits redraw more frequently while retaining the existing device discovery windows. MMCE returns each probed port's bus state to the normal card/controller drivers.
- **Power-off:** setup starts only the power-off service, without loading unused DEV9 drivers, and detects failed initialization.

## Earlier changes

See the repository's merged pull requests for earlier development history.
