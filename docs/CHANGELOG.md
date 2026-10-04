# Changelog

This document holds the user-facing changelog that is also used in release notes.

## Unreleased [WIP] - YYYY-MM-DD
### Changed
- The "Open Recent" list is now updated when saving a file, ensuring the most recently active projects stay at the top.
- Improved Recent Files reliability by saving the list immediately upon modification.
- New projects are now saved to disk immediately upon creation.
- Added a safety confirmation dialog when closing tabs or quitting the application with unsaved changes.
- Added visual "unsaved changes" indicator (*) to editor tabs.
- Save loading now preserves original IDs for nodes and ports instead of remapping them.
- Constraints on raw producers (e.g. miners) are now treated as capacities, while constraints on any other port are treated as targets the solver tries to meet.
- Surplus production that nothing downstream consumes (e.g. byproducts) is now allowed and reported as excess instead of throttling the whole production line.
- Flow is now split evenly between otherwise unconstrained branches of a split or merge.
- Massively improved solver performance on large graphs by optimizing the Target Water-Filling algorithm (from $O(N^2)$ to $O(1)$ solves per iteration).
- Fixed "Waste Compression" in the solver where byproducts were unnecessarily processed into denser items (pulling in extra raw resources) to numerically minimize excess item counts. Excess now stops at the earliest possible unconstrained port.
- UI: Unconsumed byproducts now highlight their output port in orange and display the exact excess amount in tooltips and the right-click context menu.

### Fixed
- Fixed an issue where tabs with unsaved changes could not be closed using the 'X' button or middle-click.
- Fixed context menu failing to open if the mouse moved slightly during a right-click, especially noticeable on high-DPI mice.
- Fixed the port context menu incorrectly displaying the constraint of a previously right-clicked port instead of the current one.
- Fixed the text input field in the port context menu failing to consistently auto-focus when opened for quick editing.
- Fixed high CPU usage when the application is running in the background.
- Fixed a bug where creating a new project would unintentionally clear the data in the Game Data Manager window.
- Fixed wrong tooltip styling when hovering over node icons in Light theme.
- Fixed potential crashes when loading save files containing invalid connections or duplicate IDs.
- Fixed the solver starving a constrained branch of a split (running it at 0) while sending all flow to an unconstrained sibling branch.

---

## v0.9-alpha - 2026-Jan-16
- Initial release