# Changelog

This document holds the user-facing changelog that is also used in release notes.

## Unreleased

### Fixed
- Improved tab and recent file disambiguation when multiple open projects share identical filenames and parent folder names by walking ancestor directory components up to the differing directory (e.g. `1 (instance1/projects/myproj)` vs `1 (instance2/projects/myproj)`).
- Fixed crash on application startup when `app_settings.json` contains non-positive window dimensions by enforcing minimum safe dimensions (640x480).
- Fixed crash (`std::length_error`) on project open when `maxRecentFiles` setting is negative.
- Fixed potential assertion and memory errors from out-of-bounds `navigationButtonIndex` in settings by validating to supported mouse buttons (Right or Middle).
- Fixed silent project file overwrite by autosave when opening JSON files missing `"graph_data"`.
- Fixed risk of project file truncation on full disk during save operations by flushing and closing output streams prior to atomic file replacement.
- Fixed session, recent files, and settings persistence failures when file paths contain non-ASCII or Unicode characters.
- Fixed project save and load failures on Windows when file paths contain Unicode characters outside the system ANSI code page.
- Fixed crash when undo/redo commands (`AddNodeCommand`, `RemoveNodeCommand`, `MoveNodeCommand`, `PasteCommand`) execute in headless mode or without an active canvas editor context.
- Fixed potential ID collisions and graph index corruption when restoring nodes by advancing next node and port ID counters in `FactoryGraph::restoreNode`.
- Fixed pasted nodes dropping ports on redo when a recipe has more ports than the clipboard source node.
- Fixed cut nodes remaining selected in the canvas, preventing subsequent delete operations from creating dummy undo entries.
- Fixed toggling preferred machines in the node context menu not marking the project dirty.

### Removed
- Removed the in-app Game Data Editor and the "Game Data Manager" menu entry.

## v0.10 - 2026-Oct-16
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
- Enabled docking layout persistence across application restarts, saving window layouts to `docking_layout.ini`.
- Added a "Reset Window Layout" option under the `Window` menu to restore default docking and clear awkward window splits.
- Disambiguated open tabs sharing the same project name by appending their parent folder name in the tab header (e.g. `Project (dirA)`).
- Hardened autosave to only write dirty projects with valid filesystem paths, and bounded interval settings between 1 and 60 minutes.
- Refined unsaved changes dialog to automatically redirect untitled or pathless tabs to the native Save As dialog.

### Fixed
- Fixed silent project file clobbering when loading corrupted or invalid project files; `FactoryNodeEditor` now verifies project loading status and prevents autosave or manual save from overwriting the file on disk.
- Fixed silent data loss when choosing "Apply to all remaining files" in the Unsaved Changes confirmation dialog: tabs that encounter save errors are preserved in the queue and application shutdown is safely aborted.
- Fixed modal deadlock when attempting to save untitled or pathless projects from the Unsaved Changes confirmation dialog by seamlessly redirecting to the native Save As dialog.
- Fixed `Application::saveActiveEditor()` silently failing on untitled projects without prompting, properly directing users to the Save As dialog.
- Fixed application crash (`std::out_of_range`) in node canvas rendering when encountering deleted or unrecognized resource keys on ports by safely using iterator lookups.
- Fixed application crash (`std::out_of_range`) and eliminated deep struct value copying in `FactoryGraph::changeMachine` when verifying recipe compatibility.
- Fixed spatial culling desynchronization where pending quadtree rebuild flags were overwritten rather than bitwise preserved across undo, redo, and command execution cycles.
- Fixed `GameDataEditor` new file dialog retaining stale error messages and dispatching duplicate error notifications upon successful file creation.
- Fixed layout distortion and misaligned chip wrapping in Game Data Editor recipe inputs/outputs when recipes have empty/blank ports.
- Fixed Debug Window displaying a blank "Last solver status:" and fixed 64-bit `printf` format specifier mismatch for copy buffer size.
- Pruned dead state, unused member variables, and dangling `<iostream>` headers across the codebase.
- Fixed frame rate throttling to 10 FPS (or 5 FPS on Wayland) during flow animations ("Z"), fit view navigation ("F"), and toast notification fades by tracking active animation timers and maintaining 60 FPS until animations complete.
- Fixed double-sleep frame pacing bug on Linux Wayland where `glfwWaitEventsTimeout(0.1)` and `std::this_thread::sleep_for(0.1s)` compounded to throttle idle frame rates down to 5 FPS.
- Improved idle power saving by immediately sleeping minimized (`GLFW_ICONIFIED`) windows and dropping back to idle mode the instant animations conclude.
- Fixed spurious "Warning Saving Project: Failed to parse editor settings" notification when saving newly created project files before their initial render frame.
- Fixed duplicate project name validation blocking opening or creating identically named project files located in different directories by comparing canonical filesystem paths.
- Fixed window ID collisions in Dear ImGui for tabs sharing identical project names by unique path-based window ID hashing.
- Fixed closing dock window groups ('X' button on docked tab group) only closing a single tab instead of all docked tabs in that window.
- Fixed potential data races and UI thread corruption from background native file dialog threads in `SettingsEditor` and `GameDataEditor`.
- Fixed cancelling icon file dialog wiping existing draft resource or machine icons.
- Fixed pasted nodes silently resetting machine selection, clock speed, and production multiplier to default values.
- Fixed solver Stage 3 waste minimization inadvertently preferring unconsumed output excess over routing flow to downstream links.
- Fixed solver allowing machines configured with 0% clock speed or 0% production multiplier to consume free input resources, and clamped production multiplier inputs to [0.0%, 10000.0%].
- Fixed Game Data Manager allowing resources, machines, or recipes to be renamed to empty slugs.
- Fixed pin flow rate limit input in node editor accepting non-finite (NaN/Inf) or negative values.
- Fixed 64-bit node ID truncation during project load and cleaned up dangling aliases when deleting renamed resources, machines, or recipes.
- Fixed crash (std::out_of_range) in node editor when pasting clipboard nodes lacking position entries.
- Fixed application crash (assertion failure in Quadtree) when placing or dragging nodes far from the canvas origin or when node positions are uninitialized.
- Fixed viewport drag freeze and view oscillation at extreme zoom-out caused by Dear ImGui local mouse coordinates breaching the internal -256,000 coordinate limit.
- Fixed outer editor window scrollbars erroneously appearing and conflicting with canvas navigation during extreme zoom-out.
- Fixed nodes disappearing and cancelling wire creation when dragging a new connection across the canvas.
- Fixed connecting wires disappearing when crossing the visible viewport between two off-screen nodes.
- Fixed ImGui style color stack underflow and table imbalance in Game Data Editor's recipe grid.
- Fixed uninitialized `ImTextureID` member variables in Resource and Machine definitions.
- Fixed typo in Game Data Editor machine creator calling `setResourceIcon` instead of `setMachineIcon`.
- Fixed an issue where tabs with unsaved changes failed to close from tab 'X', middle mouse click, or the dock node right-corner close button due to window focus stealing dismissing the unsaved changes modal dialog immediately upon appearance.
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
- Fixed Wayland frame pacer accumulator drift and 100% CPU spinning caused by frame pacing latency backlog, resolved the 40 FPS frame cap by properly checking frame deadlines after advancing the pacing target, added dynamic monitor refresh rate detection for high-refresh displays on Wayland, corrected the idle-to-active wakeup state transition check, and hardened `FramePacer` against non-finite or invalid FPS inputs.

---

## v0.9-alpha - 2026-Jan-16
- Initial release