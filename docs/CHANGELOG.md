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
- Fixed application crashes when renaming machines or recipes that have historical aliases in the Game Data Manager.
- Fixed an unhandled filesystem exception when scanning for game data packages in missing or inaccessible directories.
- Fixed a file descriptor leak in texture loading when encountering invalid or empty files.
- Fixed crashes (`std::bad_optional_access`) on path resolution failures across settings, recent files, and session managers.
- Fixed container iterator invalidation causing port leaks and dangling connections when deleting nodes with multiple input or output ports.
- Fixed internal connection index corruption and accidental connection deletion when removing ports with duplicate or self-referential connections.
- Fixed application crashes caused by null pointer dereferences when invalid or rejected connections are created during node addition or pasting.
- Fixed duplicate connection restoration and graph corruption when undoing node deletions that have internal self-connections between input and output ports.
- Fixed pasted nodes losing their positions in the canvas editor when redoing a paste operation.
- Fixed game data parser silently failing without error details when encountering corrupted or invalid JSON in game data files.
- Fixed new editor creation proceeding with empty game data if loading the game data file failed.
- Fixed duplicated path prefixes when loading projects or creating new projects with scanned game data packages.
- Fixed immediate data loss when choosing "Close All" from the menu by safely checking for unsaved changes across all open editor tabs.
- Fixed leaked ports and orphaned connections when changing a node's recipe by cleaning up existing ports before creating new ones.
- Fixed broken resource and machine icons after moving a game data file to another package by copying associated icon assets to the destination package folder.
- Fixed empty resource key on connections deserialized from saved project files, ensuring correct link labels and flow displays.
- Improved connection removal performance from $O(E)$ to $O(1)$ by using an ID-based index lookup.
- Fixed crashes (null pointer dereference) in project saving and loading operations when executing in headless mode or without an active node editor context.
- Fixed Save As operations failing to register the newly saved project in the Recent Files history.
- Fixed premature teardown of the native file dialog system caused by background dialog threads, which caused subsequent file and folder pickers across the application to fail.
- Fixed window title collision in Settings Editor that conflicted with Game Data Manager.
- Fixed active editor tab desynchronization when closing a tab situated before the currently active tab.
- Fixed project-dependent menu items (Save, Save As, Undo, Redo, Cut, Copy, Paste, Select All, Show Flow, Fit View) remaining active when no projects are open.
- Removed debug crash button from the debug interface.
- Fixed potential crashes and out-of-bounds memory accesses in the solver when evaluating recipes with mismatched node port counts or missing recipe/machine definitions.
- Fixed division by zero when calculating node machine counts for nodes with zero or negative clock speed or recipes with zero execution time.
- Fixed stack overflow crashes on deep linear graph chains by converting recursive graph traversal and SCC depth analysis in the solver to an iterative stack algorithm.
- Improved connection validation performance by removing an unnecessary deep copy of the resource definition registry.
- Fixed index-skipping bug when deleting ports or machine definitions in Game Data Editor loops.
- Fixed redundant map lookups in Game Data Editor and Factory Node Editor by reusing iterators.
- Fixed `fontSize` setting discarded when specified as an integer in configuration JSON by accepting any valid number format.
- Hardened `SettingsManager`, `SessionManager`, and `RecentFiles` against corrupted files by implementing atomic file writes (temporary file and rename).
- Optimized missing texture handling in `TextureManager` by caching missing texture IDs to eliminate continuous per-frame disk polling.
- Cleaned up header hygiene by eliminating leaked global `using json` and `namespace ed` from `GameDataManager.h` and `ProjectIo.h`.
- Changed `scanForGameData` header function from `static` to `inline` in `GameDataScanner.h` to prevent duplicate instances across translation units.
- Fixed initial port context in `FactoryNodeEditor` incorrectly defaulting to `0` rather than unselected (`-1`), which erroneously scoped the recipe creation popup to port `0`.
- Fixed undefined behavior and MSVC debug assertions on non-ASCII characters in `naturalLess` by casting characters to `unsigned char` before passing to `std::isdigit` and `std::tolower`.
- Fixed hardcoded `/min` rate unit in output port excess rate tooltips by respecting the dataset's configured time unit.
- Fixed missing space in paste failure notification message when copied nodes contain invalid recipes or machines.
- Fixed platform-dependent 64-bit integer format specifiers (`%lu` instead of `PRIu64`) across `FactoryNodeEditor` debug overlays and `NotificationManager`.
- Removed redundant ternary operator in `FactoryNodeEditor::drawNodes` machine count formatting.
- Removed leftover debug newline output to standard console on node deletion.
- Cleaned up redundant double semicolon in `FactoryNodeEditor` node movement logic.
- Optimized canvas rendering performance by avoiding per-frame value copies of `Resource` objects during pin drawing in `FactoryNodeEditor`.
- Optimized `StringUtils::slugify` performance by declaring regular expressions as `static const` to eliminate repeated regex compilation.
- Optimized `TextureUtils::loadTextureFromFile` by streaming directly via `stbi_load`, eliminating manual file reading and redundant memory allocations, and hardened texture loading against null pointers, zero/negative dimensions, and OpenGL resource leaks.
- Fixed uninitialized primitive member fields in core graph primitives (`Port::id`, `Port::node_id`, `Node::id`, `Connection::id`, `Connection::from_port`, `Connection::to_port`) by adding in-class default member initializers.
- Improved header self-sufficiency across `Port.h` and `CopyBuffer.h` by adding missing required `#include` statements (`<string>`, `<functional>`, `<cstddef>`, `<imgui.h>`).
- Fixed state loss and ghost node corruption in `RemoveNodeCommand` by adding an execution guard and preserving snapshot memento data across repeated undo/redo cycles.
- Expanded test suite coverage with comprehensive automated unit tests for concrete commands (`AddNodeCommand`, `RemoveNodeCommand`, `PasteCommand`, `AddConnectionCommand`, `RemoveConnectionCommand`) across single and multiple undo/redo cycles in `test_undo_redo_system.cpp`, multi-port node removal with fan-out connections in `test_factory_graph.cpp`, and core primitive default initializers and texture loading edge cases.

---

## v0.9-alpha - 2026-Jan-16
- Initial release