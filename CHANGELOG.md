# Launchstation changelog

## Rolling

- **Safer file replacement:** an existing destination is retained until its replacement has been written and checked. Failed publication rolls back; recovery files survive interrupted operations.
- **Clearer file operations:** the dashboard shows the filename, remaining megabytes and a progress bar while copying, with progress updates during deletion and verification.
- **Consistent storage names:** Home and the application library use MASS labels for mass-storage applications.
- **Software clock:** Settings offers a battery-independent software clock and an explicit Save current clock action. It resumes a saved checkpoint after restart; elapsed time while unplugged is unavailable.
- **Quieter startup:** automatic driver status is hidden behind the Launchstation startup screen; manual device actions retain their status messages.

## Earlier changes

See the repository's merged pull requests for earlier development history.
