# Changelog

This document holds the user-facing changelog that is also used in release notes.

## Unreleased [WIP] - YYYY-MM-DD
### Changed
- Improved solver logic. This fixes machines slowing down on target production.
- The "Open Recent" list is now updated when saving a file, ensuring the most recently active projects stay at the top.
- Improved Recent Files reliability by saving the list immediately upon modification.
- New projects are now saved to disk immediately upon creation.
- Added a safety confirmation dialog when closing tabs or quitting the application with unsaved changes.
- Added visual "unsaved changes" indicator (dot) to editor tabs.
- Save loading now preserves original IDs for nodes and ports instead of remapping them.

### Fixed
- Fixed high CPU usage when the application is running in the background.
- Fixed a bug where creating a new project would unintentionally clear the data in the Game Data Manager window.
- Fixed wrong tooltip styling when hovering over node icons in Light theme.
- Fixed potential crashes when loading save files containing invalid connections or duplicate IDs.

---

## v0.9-alpha - 2026-Jan-16
- Initial release