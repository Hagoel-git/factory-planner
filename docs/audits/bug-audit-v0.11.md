# Bug Audit — v0.11

Findings for Roadmap Step 2b. Each entry: severity, location, problem, reproduction idea, status (`open` / `fix` / `won't fix` / `fixed`).

## Candidates noted during the Phase 0 review (not yet confirmed)

These came up while documenting the solver. They have **not** been reproduced yet. Step 2b run 4 (solver lens) must confirm or reject each one.

1. **Stale machine counts after a failed solve.** Severity: wrong result (UI). `src/core/graph/FactorySolver.cpp` (non-SUCCESS branch of `solve()`, ~L74-91): port and connection rates are reset to 0, but `Node::machine_count` keeps its value from the previous successful solve. Repro idea: solve a valid graph, then make it infeasible → nodes still show the old machine counts next to zero rates. Status: confirmed (see finding #15 below).
2. **Raw-producer detection differs between classification and machine count.** Severity: wrong result (edge case). `classifyConstraint` treats a node as a raw source if *all* inputs are `"nothing"` (including nodes with *no* inputs), while `updateFactoryGraph` (~L868) requires *exactly one* `"nothing"` input. Repro idea: a recipe with zero inputs, or two `"nothing"` inputs → the constraint is a LIMIT, but the machine count goes through a different branch. Status: rejected / won't fix (mathematically equivalent; zero-input recipes are handled consistently as raw sources and nothing-input ratios cancel out).
3. **Constrained leaf ports are maximized again in Phase 2.** Severity: unclear, possibly intended. `buildThroughputTerms` (~L184-188) doesn't exclude ports with a user constraint. Check whether this can push a TARGET past the fair water-filling result of Phase 1 (it is upper-bounded by its constraint, so probably harmless). Status: rejected / won't fix (harmless; leaf ports are upper-bounded by their user constraint in `addAllConstraints` and cannot exceed capacity).
4. **Quadratic lookup in Phase 2.** Severity: performance. `solveLexicographic` (~L715-718) linearly scans `m_portVariables` for every throughput term to find its port ID. That's O(N·M) on large graphs; a reverse map would avoid it. Status: confirmed performance issue (O(N·M) reverse scan; not a correctness bug).



## Run 1 — Memory, lifetime, pointer/iterator invalidation

Scope: `src/core/graph/*`, `src/services/UndoRedoSystem.*`

1. **Unconditional dereference of global `imgui-node-editor` context in `UndoRedoSystem` commands crashes headless execution.**
   - **Severity:** crash
   - **Location:** `src/services/UndoRedoSystem.cpp:51, 103, 122, 160, 230, 234, 238, 242, 278, 279, 385`
   - **What goes wrong:** `AddNodeCommand`, `RemoveNodeCommand`, `MoveNodeCommand`, and `PasteCommand` call `ax::NodeEditor` global functions (`ed::SetNodePosition`, `ed::GetNodePosition`, `ed::ClearSelection`, `ed::ScreenToCanvas`, `ed::SelectNode`) unconditionally without checking whether an active editor context exists (`ed::GetCurrentEditor() != nullptr`). In `imgui_node_editor_api.cpp`, these functions unconditionally dereference the global `s_Editor` pointer (`s_Editor->SetNodePosition(...)`), which defaults to `nullptr`. When commands are executed, undone, or redone outside of an active GUI session (e.g. headless unit tests, future CLI tooling like `gamedata_tool`, or batch simulation scripts), `s_Editor` is null, causing an immediate SIGSEGV null pointer dereference. In contrast, `src/services/ProjectIo.cpp:214` explicitly guards editor calls with `ed::GetCurrentEditor() != nullptr`.
   - **Reproduction scenario / test idea:**
     In a test without initializing an `ax::NodeEditor` context:
     ```cpp
     TEST(UndoRedoHeadlessTest, AddNodeCommandWithoutEditorContextCrashes) {
         ax::NodeEditor::SetCurrentEditor(nullptr);
         GameData gameData;
         FactoryGraph graph(gameData);
         AddNodeCommand cmd("Miner", "mine_ore", (uint64_t)-1, ImVec2(0.0f, 0.0f));
         cmd.execute(graph); // Crashes with SIGSEGV dereferencing s_Editor
     }
     ```
   - **Confidence:** 100%
   - **Status:** fixed

2. **`FactoryGraph::restoreNode` does not advance `m_nextNodeId` or `m_nextPortId`, risking ID collision and map corruption.**
   - **Severity:** wrong result / crash (graph state & index corruption)
   - **Location:** `src/core/graph/FactoryGraph.cpp:19-28`
   - **What goes wrong:** `FactoryGraph::restoreNode` inserts a restored `Node` and its `Port`s into `m_nodes` and `m_ports` without updating `m_nextNodeId` or `m_nextPortId` when `node.id >= m_nextNodeId` or `port.id >= m_nextPortId`. In contrast, `FactoryGraph::restoreConnection` (lines 173-175) and `FactoryGraph::deserializeNode`/`processPorts` explicitly check and advance the ID counters: `if (id >= m_nextId) m_nextId = id + 1;`. If `restoreNode` is called with node/port IDs greater than or equal to the graph's current counters (such as when reconstructing nodes from external sources or in graph tests like `tests/test_factory_graph.cpp:795`), future calls to `addNode()` or `addPort()` will generate duplicate IDs. When an ID collision occurs, `addNodeToIndex(id, index)` overwrites the mapping in `m_nodeIdToIndex`. Calling `getNode(id)` then resolves to the wrong node, and `removeNode(id)` leaves the duplicate node orphaned in `m_nodes` without an index map entry. Subsequent swap-and-pop operations then record corrupted indices, leading to out-of-bounds container accesses or lookups returning incorrect elements.
   - **Reproduction scenario / test idea:**
     ```cpp
     TEST(FactoryGraphTest, RestoreNodeDoesNotAdvanceNextIdLeadingToCollision) {
         FactoryGraph graph(testGameData);
         Node n("Restored Node", 0);
         Port p(0, 0, "iron_ore");
         graph.restoreNode(n, {p}); // Restores node ID 0, port ID 0; m_nextNodeId remains 0
         uint64_t newId = graph.addNode("New Miner", "mine_ore"); // Generates ID 0 again!
         EXPECT_NE(newId, 0); // Fails: duplicate ID 0 is allocated, corrupting m_nodeIdToIndex
     }
     ```
   - **Confidence:** 100%
   - **Status:** fixed



## Lens: Undo/redo symmetry: every command fully restores state, including IDs, positions, constraints

1. **Uncommitted live port constraint edit in Pin Context Menu bypasses undo and leaves dirty state desynchronized.**
   - Severity: wrong result / data loss
   - Location: `src/editor/factory_planner/FactoryNodeEditor.cpp:1396-1416`
   - Problem: In `FactoryNodeEditor::handlePopups()` for `"Pin Context Menu"`, when typing in `##rate`, `m_graph->setPortConstraint(port->id, m_contextPinCurrentConstraint)` and `m_solver->solve(*m_graph)` are executed live on every keystroke. However, `SetPortConstraintCommand` is only constructed and dispatched when `ImGui::IsItemDeactivatedAfterEdit()` or Enter fires. If the user dismisses the popup by pressing Escape or clicking outside onto the canvas/window, the popup closes without deactivation, and lines 1408–1416 never execute. The modified constraint remains in `m_graph`, no undo command is recorded, `m_isDirty` remains `false`, and pressing Ctrl+Z reverts an unrelated earlier action rather than restoring the original constraint.
   - Reproduction / test idea: Open the Pin Context Menu on an unconstrained port (`user_constraint == -1.0`). Type "50.0". Press Escape or click outside the popup. Verify that `port->user_constraint == 50.0`, `m_undoRedoManager.canUndo()` has no `SetPortConstraintCommand`, and `m_isDirty == false`.
   - Confidence: Very High (100%).
   - Status: false positive.

2. **Function-local static variables in Node Context Menu cross-pollute edits across nodes/tabs and bypass undo on cancel.**
   - Severity: wrong result / data loss
   - Location: `src/editor/factory_planner/FactoryNodeEditor.cpp:1306-1346`
   - Problem: `s_startClockSpeed` and `s_startProductionMultiplier` are declared as function-local `static double` variables inside `FactoryNodeEditor::handlePopups()`, persisting across all frames, nodes, and editor tabs. When dragging or editing the sliders, `node->clock_speed` and `node->production_multiplier` are mutated directly on lines 1312 and 1333. If the user dismisses the popup (Escape or clicking away), `IsItemDeactivatedAfterEdit()` never triggers: no command is dispatched, the solver is not run, `m_isDirty` is not set, and the node retains the new values with stale machine counts and rates, completely untracked by the undo stack. Furthermore, `node->clock_speed` is mutated on line 1312 *before* `IsItemActivated()` on line 1315 reads `node->clock_speed`, causing `s_startClockSpeed` to capture the already-mutated value on the first active drag frame, which makes undo restore the wrong value.
   - Reproduction / test idea: Open the Node Context Menu for Node A (clock speed 100%). Drag the slider to 150%. Press Escape to cancel. Verify that `node->clock_speed` is 150%, the solver has not run, `m_undoRedoManager` has no `ChangeClockSpeedCommand`, and `m_isDirty == false`. Then open Node B (clock speed 200%) and edit it; verify that `s_startClockSpeed` may retain Node A's value and corrupt undo on Node B.
   - Confidence: Very High (100%).
   - Status: false positive.

3. **`FactoryNodeEditor::cut()` does not deselect or delete items in `imgui-node-editor`, corrupting subsequent undo operations.**
   - Severity: wrong result / minor
   - Location: `src/editor/factory_planner/FactoryNodeEditor.cpp:272-285`
   - Problem: In `FactoryNodeEditor::cut()`, selected nodes are copied to clipboard and removed from `FactoryGraph` via `CompositeCommand("Cut Nodes")` containing `RemoveNodeCommand`s. Unlike `ed::BeginDelete()`, `cut()` never calls `ed::ClearSelection()`, `ed::DeselectNode()`, or `ed::AcceptDeletedItem()`. The cut node IDs remain in `imgui-node-editor`'s internal selection list. If the user immediately presses Delete or Backspace before clicking on the canvas, `ed::BeginDelete()` queries the still-selected cut node IDs, generates no-op `RemoveNodeCommand`s for nodes no longer in `FactoryGraph`, and pushes a dummy `CompositeCommand("Delete operation")` onto the undo stack. When the user attempts to undo the cut with Ctrl+Z, the top command is the dummy delete, requiring a double-undo to restore the cut nodes.
   - Reproduction / test idea: Select a node. Press Ctrl+X (cut). Without clicking on the canvas, press Delete. Check `m_undoRedoManager.getUndoStackSize()`, which will show 2 commands instead of 1 (`Cut Nodes` followed by `Delete operation`). Press Ctrl+Z once: verify that the cut node is NOT restored until a second Ctrl+Z is pressed.
   - Confidence: High (95%).
   - Status: fixed.

4. **`PasteCommand` on redo drops ports if node port count differs from clipboard.**
   - Severity: crash / wrong result
   - Location: `src/services/UndoRedoSystem.cpp:292-322, 374-387`
   - Problem: In `PasteCommand::execute()`, ports and constraints are saved into `pastedPorts` by iterating up to `min(oldNode.input_ports.size(), newNode->input_ports.size())` and `min(oldNode.output_ports.size(), newNode->output_ports.size())`. If `newNode` (instantiated from the recipe) has more ports than `oldNode` in the clipboard, only the first `inputCount` and `outputCount` ports are inserted into `pastedPorts`. On redo, `graph.restoreNode(node, ports)` is called with only the ports present in `pastedPorts`. However, `node.input_ports` and `node.output_ports` in `pastedNodes` still hold all port IDs created during the initial paste. Any port beyond `inputCount`/`outputCount` is missing from `FactoryGraph::m_ports`. Subsequent calls to `graph.getPort(port_id)` return `nullptr`, causing null pointer dereferences or missing terms during solver and rendering loops.
   - Reproduction / test idea: Construct a `CopyBuffer` where `oldNode.input_ports` has 1 port but `newNode` (from the recipe) has 2 ports. Call `PasteCommand::execute(graph)`, then `undo(graph)`, then `redo(graph)`. Verify that `graph.getPort(node.input_ports[1]) == nullptr`.
   - Confidence: High (90%).
   - Status: fixed.

5. **`UndoRedoSystem` node positioning commands rely on implicit thread-local editor context.**
   - Severity: crash / minor
   - Location: `src/services/UndoRedoSystem.cpp:51, 103, 122, 160, 230, 234, 278, 386`, `src/editor/factory_planner/FactoryNodeEditor.cpp:451-497`
   - Problem: `MoveNodeCommand::execute/undo`, `AddNodeCommand::execute/redo`, `RemoveNodeCommand::execute/undo`, and `PasteCommand::execute/redo` directly invoke `ed::SetNodePosition` and `ed::GetNodePosition`. These functions in `imgui-node-editor` operate directly on the thread-local static pointer `s_Editor` with no null checks. `FactoryNodeEditor::undo()` and `FactoryNodeEditor::redo()` do not call `ed::SetCurrentEditor(m_context)`. If `editor.undo()` or `editor.redo()` is called from any code path where `ed::SetCurrentEditor` was not explicitly called beforehand by the caller (such as headless tests, background triggers, or outside `Application::undoActiveEditor()`), `s_Editor` is either `nullptr` (causing an immediate SIGSEGV) or points to another tab's editor context (silently displacing nodes in the wrong project tab).
   - Reproduction / test idea: In a test or runner, ensure `ed::SetCurrentEditor(nullptr)`. Call `AddNodeCommand` or `MoveNodeCommand`, then invoke `undo(*graph)` or `editor.undo()`. Observe a SIGSEGV inside `ax::NodeEditor::SetNodePosition`.
   - Confidence: Very High (100%).
   - Status: fixed (duplicate of Run 1 Finding 1; guarded by ed::GetCurrentEditor() checks and verified in HeadlessUndoRedoTest).

6. **"Preferred Machine" toggles in Node Context Menu bypass undo and dirty tracking.**
   - Severity: wrong result / minor
   - Location: `src/editor/factory_planner/FactoryNodeEditor.cpp:1290-1294`
   - Problem: In `FactoryNodeEditor::handlePopups()`, clicking the "P" button to set/unset a machine as preferred calls `m_graph->setMachinePreferred(node->machine_key, !isPref)` directly. Preferred machine configurations are persistent project-level graph state serialized to `.fpp` files (`j["preferred_machines"]`). This change does not create an undo command and does not mark the editor dirty (`m_isDirty`). The user cannot undo the preference change with Ctrl+Z (which instead reverts an earlier unrelated command), and closing the project without other changes will silently lose the user's preference change without an unsaved-changes warning.
   - Reproduction / test idea: Open the Node Context Menu. Click the "P" button next to Machine selector to toggle machine preference. Verify that `m_undoRedoManager.canUndo()` does not record the action, and `m_isDirty` remains `false`. Pressing Ctrl+Z undoes a previous action while leaving the preferred machine state toggled.
   - Confidence: Very High (100%).
   - Status: won't fix for undo (intended UX: tool preferences/defaults should not pollute canvas undo stack); fixed for dirty tracking (set m_isDirty = true).

7. **Zero-pixel node drag pushes empty `CompositeCommand` to undo stack and marks project dirty.**
   - Severity: minor
   - Location: `src/editor/factory_planner/FactoryNodeEditor.cpp:1146-1158`
   - Problem: When the user clicks or begins dragging a selected node, `m_draggingNodes` is set to `true`. When the mouse button is released, if `m_draggedNodeOriginalPos == m_draggedNodeNewPos` (e.g. click without movement or delta below mouse drag threshold), the loop over `selectedNodes` skips adding `MoveNodeCommand`s because `m_draggedNodeOriginalPos != m_draggedNodeNewPos` is false. However, the outer `CompositeCommand("Move Nodes Command")` is still executed via `executeCommand(std::move(cmd))`. This pushes an empty command onto the undo stack and unconditionally marks the document dirty (`m_isDirty = true`). The user must press Ctrl+Z an extra time to undo the prior real action.
   - Reproduction / test idea: Select a node on the canvas. Click and release it without moving the mouse. Verify that `m_undoRedoManager.getUndoStackSize()` increases by 1, and `m_isDirty == true`. Press Ctrl+Z once: verify that no node moves or graph state changes, and a second Ctrl+Z is required to undo the previous action.
   - Confidence: High (95%).
   - Status: false positive.

## Lens 3: File I/O robustness (`src/services/ProjectIo.*`, `SessionManager`, `SettingsManager`, `RecentFiles`)

5. **`SettingsManager::loadAppSettings()` accepts non-positive window dimensions, causing fatal crash on application startup.**
   - **Severity:** crash
   - **Location:** `src/services/SettingsManager.cpp:102-107` (called by `src/app/main.cpp:140-144`)
   - **What goes wrong:** `loadAppSettings()` deserializes `windowWidth` and `windowHeight` from `app_settings.json` without verifying that they are positive integers. If the settings file is corrupted or modified to contain `0` or negative values (e.g. from an aborted display configuration or manual edit), `main.cpp:140` passes them directly to `glfwCreateWindow(initWidth, initHeight, ...)`. GLFW requires width and height to be strictly greater than 0; passing non-positive values causes `glfwCreateWindow` to fail and return `nullptr`. `main.cpp:142` then calls `LOG(FATAL) << "Failed to create GLFW window"`, terminating the application with `SIGABRT`. The application cannot start until `app_settings.json` is manually deleted or edited.
   - **Reproduction / failing test idea:** Write `{"windowWidth": 0, "windowHeight": 0}` into `app_settings.json` and run the application binary. Assert that `SettingsManager::loadAppSettings()` validates/clamps window dimensions to minimum safe values (e.g. at least 640x480).
   - **Confidence:** 100%
   - **Status:** fixed

6. **`SettingsManager::loadAppSettings()` permits negative `maxRecentFiles`, causing uncaught `std::length_error` crash on project open.**
   - **Severity:** crash
   - **Location:** `src/services/SettingsManager.cpp:99-101` (call path into `src/services/RecentFiles.cpp:33-36` via `src/app/Application.cpp:1402`)
   - **What goes wrong:** `loadAppSettings()` does not validate or clamp `maxRecentFiles`. If `app_settings.json` contains a negative number (e.g. `"maxRecentFiles": -1`), `m_settings.maxRecentFiles` stores `-1`. When any project is opened or created, `Application::createNewEditor` calls `RecentFiles::instance().addFile(location)`. In `RecentFiles::addFile`, `if (static_cast<int>(m_files.size()) > maxFiles)` evaluates to true, and `m_files.resize(maxFiles)` is executed. Passing a negative `int` converts to `size_t` (e.g. `18446744073709551615ULL`), throwing `std::length_error`. Neither `RecentFiles::addFile` nor `Application` wraps this in a `try/catch` block, causing an immediate crash when opening/creating any project.
   - **Reproduction / failing test idea:** Set `"maxRecentFiles": -1` in `app_settings.json` (or set via `AppSettings`), then call `RecentFiles::instance().addFile("test.fpp")`. The call throws uncaught `std::length_error`.
   - **Confidence:** 100%
   - **Status:** fixed

7. **`SettingsManager::loadAppSettings()` allows out-of-bounds `navigationButtonIndex`, triggering ImGui assert / out-of-bounds memory read.**
   - **Severity:** crash
   - **Location:** `src/services/SettingsManager.cpp:93-95` (call path into `src/editor/factory_planner/FactoryNodeEditor.cpp:48` and `external/imgui-node-editor/imgui_node_editor.cpp:3381`)
   - **What goes wrong:** `loadAppSettings()` does not validate `navigationButtonIndex` (`0 <= index < 5`, expected 1 or 2). If `app_settings.json` contains an invalid button index like `-1` or `10`, `FactoryNodeEditor` configures `cfg.NavigateButtonIndex = navigationButtonIndex`. When rendering the editor canvas, `imgui-node-editor` calls `ImGui::IsMouseDragging(navBtn, 0.0f)`. In `external/imgui/imgui.cpp:10461`, ImGui asserts `button >= 0 && button < IM_COUNTOF(g.IO.MouseDown)`. In Debug builds this aborts immediately via assertion; in Release builds it performs an out-of-bounds read on the `g.IO.MouseDown` array.
   - **Reproduction / failing test idea:** Put `"navigationButtonIndex": -1` into `app_settings.json` and open an editor tab. The canvas render triggers `IM_ASSERT(button >= 0 && button < 5)` in ImGui. Assert that `loadAppSettings()` clamps or resets `navigationButtonIndex` to a valid button (1 or 2).
   - **Confidence:** 100%
   - **Status:** fixed

8. **`ProjectIO::loadProject()` returns `true` on JSON files missing `"graph_data"`, leading to silent file overwrite by autosave.**
   - **Severity:** data loss
   - **Location:** `src/services/ProjectIo.cpp:165-285`
   - **What goes wrong:** `loadProject()` wraps graph deserialization in `if (projectData.contains("graph_data")) { ... }`. If an `.fpp` file is valid JSON but does not contain a `"graph_data"` key (e.g. `{ "version": 1 }`, `{ "editor_settings": {} }`, an empty object `{}`, or another JSON file opened as a project), graph loading is completely bypassed. `loadProject()` does not report an error, leaves the graph unchanged, and returns `true`. The calling `FactoryNodeEditor` marks `m_loadedSuccessfully = true`. As soon as autosave triggers (or the user presses Ctrl+S), `FactoryNodeEditor::save()` executes `ProjectIO::saveProject()`, overwriting the project file on disk with an empty graph and permanently destroying the file's original contents.
   - **Reproduction / failing test idea:**
     ```cpp
     TEST_F(FactoryGraphTest, ProjectIOLoadProjectFailsWhenGraphDataMissing) {
         std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "test_no_graph_data.fpp";
         std::ofstream ofs(tempPath);
         ofs << "{\n  \"version\": 1\n}\n";
         ofs.close();

         FactoryGraph targetGraph(testGameData);
         EXPECT_FALSE(ProjectIO::loadProject(tempPath.string(), targetGraph));
         std::filesystem::remove(tempPath);
     }
     ```
     Currently `ProjectIO::loadProject()` returns `true`.
   - **Confidence:** 100%
   - **Status:** fixed

9. **`ProjectIO::saveProject()` does not flush stream buffer before checking `ofs.good()`, risking atomic replacement with truncated file on disk full.**
   - **Severity:** data loss
   - **Location:** `src/services/ProjectIo.cpp:71-89`
   - **What goes wrong:** In `ProjectIO::saveProject()`, data is written via `ofs << jsonStr;`, immediately followed by `if (!ofs.good())`. Unlike `SessionManager.cpp:93`, `SettingsManager.cpp:151`, and `RecentFiles.cpp:101`, `ProjectIO::saveProject()` does not call `ofs.flush()` or `ofs.close()`. Any unwritten bytes remaining in `std::ofstream`'s user-space buffer are only flushed when `ofs` is destroyed at line 83. In C++, `<fstream>` destructors swallow flush/close errors and do not throw. Because `ofs.good()` passed prior to the flush, line 89 proceeds to `std::filesystem::rename(tmp, path, ec)`. The truncated/corrupted temporary file replaces the user's existing valid `.fpp` file, causing permanent data loss.
   - **Reproduction / failing test idea:** Verify that `ProjectIO::saveProject()` flushes and closes `ofs` and verifies stream state before executing `rename()`. A simulated write failure during buffer flush demonstrates that `rename()` is reached despite incomplete write.
   - **Confidence:** 95%
   - **Status:** fixed

10. **`SessionManager`, `SettingsManager`, and `RecentFiles` pass ANSI-encoded paths to `nlohmann::json`, throwing `json::type_error` on Windows for non-ASCII paths.**
    - **Severity:** data loss
    - **Location:** `src/services/SessionManager.cpp:83`, `src/services/SettingsManager.cpp:121-122`, `src/services/RecentFiles.cpp:86`
    - **What goes wrong:** On Windows, `std::filesystem::path::string()` converts UTF-16 path strings to the system ANSI code page (e.g. Windows-1252). Non-ASCII characters (e.g. accents `é`, umlauts `ü`, Cyrillic, etc.) are represented as single bytes in the range `0x80`–`0xFF`. When stored into `nlohmann::json` and serialized with `.dump()`, `nlohmann::json` validates UTF-8 and throws `json::type_error` (code 316: invalid UTF-8 byte) upon encountering non-UTF-8 byte sequences. The `catch` blocks in `saveSession()`, `saveAppSettings()`, and `saveRecentFiles()` delete the `.tmp` file and abort. As a result:
      - `session.json` is never saved when any open project contains non-ASCII characters, losing all open tabs across application restarts.
      - `recent.json` is never updated when accessing projects in non-ASCII paths.
      - `app_settings.json` is never saved if `gameDataPath` or `defaultProjectPath` contains non-ASCII characters.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST(SessionManagerTest, NonAsciiPathDoesNotThrowInSave) {
          SessionState state;
          // Byte 0xE9 ('é' in CP1252) followed by ASCII is invalid UTF-8
          state.openProjectPaths.push_back(std::string("C:\\Users\\Ren\xE9\\project.fpp"));
          SessionManager::instance().setSessionState(state);
          // Currently throws json::type_error in j.dump(2), caught and save aborted
          SessionManager::instance().save();
          // Assert session file was successfully written and can be reloaded
      }
      ```
    - **Confidence:** 95%
    - **Status:** fixed

11. **`ProjectIO` interface takes `const std::string &path` instead of `std::filesystem::path`, breaking Unicode path handling on Windows.**
    - **Severity:** wrong result
    - **Location:** `src/services/ProjectIo.h:17-18`, `src/services/ProjectIo.cpp:60, 130` (called from `src/editor/factory_planner/FactoryNodeEditor.cpp:67, 196, 208`)
    - **What goes wrong:** While `SessionManager`, `RecentFiles`, `SettingsManager`, and `FactoryNodeEditor` use `std::filesystem::path`, `ProjectIO::saveProject` and `loadProject` accept narrow `const std::string &path`. In `FactoryNodeEditor`, calls pass `m_projectFilePath.string()`. On Windows, `path.string()` converts paths to the current ANSI code page. If a project path contains characters not representable in the active ANSI code page (e.g. Asian or Cyrillic characters on a Western Windows locale, or emojis), `path.string()` replaces those characters with `?`. Inside `ProjectIO`, `std::ofstream ofs(tmp)` and `std::ifstream ifs(path)` attempt to open the corrupted path containing `?`, failing to save or load the project.
    - **Reproduction / failing test idea:** On Windows, create a directory with characters outside the current system code page (e.g. `фабрика` on a Western European locale) and attempt to save or load a project. `ProjectIO` fails with file open errors. Passing `std::filesystem::path` directly or using `u8path` / wide streams resolves the issue.
    - **Confidence:** 90%
    - **Status:** fixed

12. **`ProjectIO::saveProject()` leaves an orphaned `.tmp` file on disk if an exception occurs during serialization.**
    - **Severity:** minor
    - **Location:** `src/services/ProjectIo.cpp:60-115`
    - **What goes wrong:** In `ProjectIO::saveProject()`, `std::ofstream ofs(tmp, std::ios::binary)` creates the file `<path>.tmp` at line 60. At line 70, `projectData.dump(2)` is invoked. If `dump(2)` throws `json::type_error` (e.g. if any node name contains invalid UTF-8 bytes) or `std::bad_alloc`, control jumps to `catch (const std::exception& e)` at line 107. Unlike `SessionManager.cpp:111`, `SettingsManager.cpp:171`, and `RecentFiles.cpp:119`, which all call `std::filesystem::remove(tmp, ec)` in their `catch` handlers, `ProjectIO::saveProject()` has no cleanup in its `catch` block. The 0-byte temporary file remains permanently on disk. In addition, lines 80 and 97 call `std::filesystem::remove(tmp)` without `std::error_code`, which can throw `std::filesystem_error` and mask the original error.
    - **Reproduction / failing test idea:** Trigger a JSON serialization error or bad alloc after `tmp` has been opened at line 60. Verify that `saveProject()` returns `false` and that `<path>.tmp` still exists on disk.
    - **Confidence:** 95%
    - **Status:** won't fix

13. **`ProjectIO::saveProject()` fails when saving to a new or non-existent directory.**
    - **Severity:** minor
    - **Location:** `src/services/ProjectIo.cpp:60-68`
    - **What goes wrong:** `SessionManager.cpp:78`, `SettingsManager.cpp:142`, and `RecentFiles.cpp:92` all invoke `std::filesystem::create_directories(parent_path, dirEc)` before opening temporary files. `ProjectIO::saveProject()` does not create parent directories. If `saveProject()` is called with a path in a directory that does not yet exist (such as saving to a subfolder via Save As or saving to `defaultProjectPath` before the folder has been created on a fresh setup), `std::ofstream ofs(tmp, ...)` fails to open, returning `false` with "Failed to create temporary file for saving project".
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(FactoryGraphTest, ProjectIOSaveCreatesParentDirectories) {
          std::filesystem::path nested = std::filesystem::temp_directory_path() / "test_nested_dir" / "project.fpp";
          std::error_code ec;
          std::filesystem::remove_all(nested.parent_path(), ec);
          EXPECT_TRUE(ProjectIO::saveProject(nested.string(), *graph));
          std::filesystem::remove_all(nested.parent_path(), ec);
      }
      ```
      Currently fails because parent directories are not created.
    - **Confidence:** 95%
    - **Status:** open

14. **`RecentFiles` and `SessionManager` persist relative paths, resolving to incorrect files when working directory changes.**
    - **Severity:** minor
    - **Location:** `src/services/RecentFiles.cpp:26-39`, `src/services/SessionManager.cpp:80-85`
    - **What goes wrong:** When a project is opened via a relative path (such as `./my_project.fpp` or from command line), `RecentFiles::addFile` and `SessionManager::saveSession` store the uncanonicalized relative path in `recent.json` and `session.json`. If the user subsequently launches the application from a different working directory (e.g. from a desktop shortcut, terminal in another folder, or script), those relative paths are resolved against the new current working directory. The Recent Files menu and session restoration either report that the file does not exist or silently open an unrelated file with the same relative name in the new working directory.
    - **Reproduction / failing test idea:**
      1. Open or add `"project.fpp"` (relative) to `RecentFiles`.
      2. Call `RecentFiles::instance().save()`.
      3. Change current path (`std::filesystem::current_path(temp_dir)`).
      4. Inspect `RecentFiles::instance().getFiles()[0]`: points to `<temp_dir>/project.fpp` instead of the original project file.
    - **Confidence:** 90%
    - **Status:** won't fix


## Lens 4: Solver correctness and edge cases (`src/core/graph/FactorySolver.*`)

15. **Stale machine counts after a failed solve (`solve()` non-SUCCESS path resets ports/connections but leaves `Node::machine_count` untouched).**
    - **Severity:** wrong result
    - **Location:** `src/core/graph/FactorySolver.cpp:72-91`
    - **What goes wrong:** When `solve(factory_graph)` completes with a non-SUCCESS status (e.g. `INFEASIBLE`, `UNBOUNDED`, or `ERROR_`), lines 75-89 iterate over ports and connections and reset their rates and excess rates to 0.0. However, the `else` branch never iterates over `factory_graph.getNodes()` and never resets `Node::machine_count`. As a result, every node retains its `machine_count` from the previous successful solve. The user sees a canvas where all wire flows and port rates are 0.0 with an error notification, but nodes display stale, non-zero machine counts.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(FactorySolverTest, StaleMachineCountsResetOnFailedSolve) {
          uint64_t n_miner = addNode("mine_ore");
          uint64_t n_smelter = addNode("smelt_ingot");
          Port* p_miner_out = getPort(n_miner, "iron_ore", false);
          Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
          Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
          addConnection(p_miner_out, p_smelter_in);
          graph->setPortConstraint(p_smelter_out->id, 30.0);

          auto result = solver->solve(*graph);
          ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
          EXPECT_GT(graph->getNode(n_smelter)->machine_count, 0.0);

          // Force a solver failure by setting an infeasible numerical condition or corrupting input
          // (or test non-SUCCESS reset logic)
          // When solve() returns != SUCCESS:
          EXPECT_DOUBLE_EQ(graph->getNode(n_smelter)->machine_count, 0.0);
          EXPECT_DOUBLE_EQ(graph->getNode(n_miner)->machine_count, 0.0);
      }
      ```
      Currently, when `result.status != SUCCESS`, `node->machine_count` retains its previous value (e.g. 1.0).
    - **Confidence:** 100%
    - **Status:** fix

16. **Phase 1 water-filling sum-maximization heuristic falsely classifies competing targets as bottlenecked, breaking fair max-min satisfaction.**
    - **Severity:** wrong result
    - **Location:** `src/core/graph/FactorySolver.cpp:655-703`
    - **What goes wrong:** In Phase 1 (`solveLexicographic`), when active targets cannot all reach full saturation ($s_{val} < 1.0 - \text{kZeroEpsilon}$), the solver must identify which targets are strictly bottlenecked at $s_{val}$ and which targets have headroom to expand further. Instead of testing target expansion independently, line 655 uses a shortcut: it clamps $S = s_{val}$ and executes a single LP solve maximizing $\sum_{i \in \text{active}} R_i$. When multiple unbottlenecked targets share upstream capacity (especially with different recipe conversion ratios or on degenerate simplex facets), the simplex algorithm (GLOP) selects an extreme point of the optimal set where one target absorbs all available surplus while competing targets remain exactly at their lower bound $s_{val} \cdot \text{target}_i$. Line 683 checks `if (r_val <= target * s_val + 1e-6)`. Because the competing unbottlenecked target remained at its lower bound in that single solve, it is falsely classified as strictly bottlenecked, locked at $s_{val} \cdot \text{target}_i$, and removed from `next_active`. In subsequent iterations, only the favored target continues to receive resources. This violates proportional max-min water-filling and unfairly starves competing targets.
    - **Reproduction / failing test idea:**
      Construct a graph with 3 targets:
      - Target 3 is on an independent chain bottlenecked at saturation $S = 0.20$ (e.g. 20 available input, 100 target demand).
      - Target 1 and Target 2 share a common raw source producing 90 ore/s.
      - Target 1 recipe: 1 ore -> 1 item (target: 100 items/s).
      - Target 2 recipe: 2 ore -> 1 item (target: 100 items/s).
      At $S = 0.20$, Target 1 uses 20 ore (produces 20 items), Target 2 uses 40 ore (produces 20 items). 30 ore remains unallocated at the miner.
      The initial solve caps $S = 0.20$ due to Target 3. $S$ is clamped to 0.20, and $\max (R_1 + R_2 + R_3)$ is solved.
      Because 1 ore yields 1.0 unit of $R_1$ but only 0.5 units of $R_2$, GLOP allocates all 30 surplus ore to $R_1$ ($R_1 = 50$, $R_2 = 20$).
      Line 683 observes $R_2 \le 20 + 10^{-6}$, declares Target 2 bottlenecked, and permanently locks it at 20.
      Target 1 is left alone in `next_active` and takes all remaining capacity in the next round ($R_1 = 50, R_2 = 20$).
      Under fair max-min water-filling, the 30 surplus ore should have been shared proportionally between Target 1 and Target 2, achieving $S = 0.30$ ($R_1 = 30, R_2 = 30$).
    - **Confidence:** 100%
    - **Status:** fixed

17. **Phase 1 full-saturation clamp forces hard lower bound without `lockTolerance`, causing solver `INFEASIBLE` failures on floating-point precision differences.**
    - **Severity:** wrong result
    - **Location:** `src/core/graph/FactorySolver.cpp:643-652`
    - **What goes wrong:** In Phase 1, when saturation satisfies $s_{val} \ge 1.0 - \text{kZeroEpsilon}$ ($1.0 - 10^{-9}$), line 650 executes `var_it->second->SetLB(std::max(0.0, target));`. Unlike line 686 (which locks bottlenecked targets with `locked_val - lockTolerance(locked_val)`) and line 786 (which locks stage objectives with `objective_value - tolerance`), line 650 does not apply `lockTolerance`. If upstream supply or a chain of fractional conversion ratios (such as $1/3$) yields a maximum throughput of $target - 10^{-10}$ (where $s_{val} = 1.0 - 10^{-10} \ge 1.0 - 10^{-9}$), line 650 forces `LB = target` while `addAllConstraints` previously set `UB = target`. In subsequent Phase 2 or Phase 3 solves, GLOP attempts to solve an LP requiring $R_i \ge target$ where upstream flow can at most supply $target - 10^{-10}$. GLOP returns `MPSolver::INFEASIBLE`, causing `solve()` to fail and wipe all port/connection rates to 0.0 on a factory that was 99.9999999% satisfied.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(FactorySolverTest, NumericalToleranceNearFullSaturationDoesNotInfeasible) {
          uint64_t n_miner = addNode("mine_ore");
          uint64_t n_smelter = addNode("smelt_ingot");
          Port* p_miner_out = getPort(n_miner, "iron_ore", false);
          Port* p_smelter_in = getPort(n_smelter, "iron_ore", true);
          Port* p_smelter_out = getPort(n_smelter, "iron_ingot", false);
          addConnection(p_miner_out, p_smelter_in);

          // Supply is 99.9999999995 (differs from 100.0 by 5e-10, within kZeroEpsilon = 1e-9)
          graph->setPortConstraint(p_miner_out->id, 100.0 - 5e-10);
          graph->setPortConstraint(p_smelter_out->id, 100.0);

          auto result = solver->solve(*graph);
          // Currently fails with INFEASIBLE because SetLB(100.0) exceeds upstream limit of 100.0 - 5e-10
          EXPECT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
          EXPECT_NEAR(p_smelter_out->rate, 100.0, 1e-6);
      }
      ```
    - **Confidence:** 100%
    - **Status:** won't fix (false positive — verified by test: GLOP's primal feasibility tolerance absorbs sub-
10
−
8
10
−8
 differences, so INFEASIBLE never occurs).

18. **`updateFactoryGraph` assumes multi-input recipe ingredient 0 has positive amount, setting `machine_count = 0.0` for catalyst or byproduct recipes.**
    - **Severity:** wrong result
    - **Location:** `src/core/graph/FactorySolver.cpp:878-885`
    - **What goes wrong:** In `updateFactoryGraph`, `machine_count` for multi-input recipes (Branch 2) is calculated solely using `recipe.input_ports.at(0)`:
      ```cpp
      double input_per_machine = recipe.input_ports.at(0).amount / recipe.time_seconds * base_speed * time_unit * clock_speed_multiplier;
      if (input_per_machine > 0.0) {
          const Port *port = factory_graph.getPort(node.input_ports.at(0));
          if (port) {
              machine_count = port->rate / input_per_machine;
          }
      }
      ```
      If recipe input 0 is a non-consumed catalyst (amount 0.0) or an auxiliary slot, `input_per_machine` evaluates to 0.0. The `if (input_per_machine > 0.0)` guard fails, and the loop never examines secondary inputs (`input_ports[1..N]`) or output ports. As a result, `node.machine_count` remains 0.0 even though the machine is actively running at full capacity and producing products.
      Additionally, if a recipe has multiple inputs where input 0 is `"nothing"` (`recipe.input_ports.size() > 1`), Branch 1 is bypassed (`size == 1` check fails), Branch 2 checks `node.input_ports.at(0)` (which has rate 0.0 because nothing connects to `"nothing"`), resulting in `machine_count = 0.0`.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(FactorySolverTest, MultiInputRecipeWithZeroAmountCatalystDerivesMachineCount) {
          Recipe cat_recipe;
          cat_recipe.name = "Catalytic Smelting";
          cat_recipe.produced_in_machines_keys.push_back("smelter");
          cat_recipe.input_ports.push_back(RecipePort{0.0, "nothing"}); // or non-consumed catalyst
          cat_recipe.input_ports.push_back(RecipePort{30.0, "iron_ore"});
          cat_recipe.output_ports.push_back(RecipePort{30.0, "iron_ingot"});
          cat_recipe.time_seconds = 1.0;
          testGameData.recipes["cat_recipe"] = cat_recipe;

          graph = std::make_unique<FactoryGraph>(testGameData);
          uint64_t n_miner = addNode("mine_ore");
          uint64_t n_cat = graph->addNode("Cat Smelter", "cat_recipe");
          addConnection(getPort(n_miner, "iron_ore", false), getPort(n_cat, "iron_ore", true));
          graph->setPortConstraint(getPort(n_cat, "iron_ingot", false)->id, 30.0);

          auto result = solver->solve(*graph);
          ASSERT_EQ(result.status, FactorySolver::SolverResultStatus::SUCCESS);
          // Currently evaluates to 0.0 because input_ports.at(0) amount is 0 or rate is 0
          EXPECT_NEAR(graph->getNode(n_cat)->machine_count, 1.0, 0.001);
      }
      ```
    - **Confidence:** 95%
    - **Status:** fix

19. **`solveLexicographic` masks Phase 1 and Phase 2 solver failures, returning `SUCCESS` when terminal Phase 3 solves.**
    - **Severity:** wrong result
    - **Location:** `src/core/graph/FactorySolver.cpp:640, 670, 755-760`
    - **What goes wrong:** In `solveLexicographic`, if Phase 1's main target maximization solve returns non-OPTIMAL (e.g. `MPSolver::INFEASIBLE` or `MPSolver::ABNORMAL` at line 640), execution simply executes `break;`. The auxiliary solve at line 670 does not check its return code at all. After breaking out of Phase 1, Phase 2 is skipped or runs without target locks, and terminal Phase 3 (`solveStage(waste_terms, false, obj_val)`) runs. Because Phase 3 minimizes unconstrained waste and flow without target lower bounds, it trivially succeeds by choosing all-zero rates (flow = 0, waste = 0 is globally optimal).
      `solveLexicographic` returns Phase 3's status (`MPSolver::OPTIMAL`), and `solve()` at line 69 converts this to `SolverResultStatus::SUCCESS`. The application falsely reports that the solve was successful, despite the fact that Phase 1 target optimization failed completely and zero targets were fulfilled.
    - **Reproduction / failing test idea:**
      If any solver numerical anomaly or constraint contradiction occurs during Phase 1, verify that `solveLexicographic` captures the failure status and returns it instead of allowing Phase 3 to overwrite the return status with `OPTIMAL`.
    - **Confidence:** 95%
    - **Status:** fix

20. **Unconnected output ports never register excess rate or excess waste in Phase 3.**
    - **Severity:** minor
    - **Location:** `src/core/graph/FactorySolver.cpp:420-432, 819-824`
    - **What goes wrong:** In `addConnectionConstraints`, `MPVariable *excess` is created exclusively for ports that have $\ge 1$ outgoing connection (`port_outgoing`). When a recipe produces byproducts on an output port with no outgoing connections (e.g. surplus Heavy Oil Residue or Polymer Resin that the player has not yet connected to a sink or storage):
      1. No excess variable $E_p$ is created for the port.
      2. In Phase 3, the unconsumed excess penalty ($1.0 \cdot E_p$) is not applied to that port (it only pays flow waste $10^{-3} \cdot R_p$, weakening the pressure to route byproducts downstream).
      3. In `updateFactoryGraph` (lines 819-824), `m_excessVariables.find(port.id)` fails, so `graph_port->excess_rate` is written as `0.0`.
      The UI shows `rate = 30.0` but `excess_rate = 0.0`, even though 100% of the produced resource is unconsumed byproduct waste.
      Line 818 includes the comment: `// Unconsumed production on connected outputs; otherwise the overshoot above a target.`, indicating that calculating excess/overshoot on unconnected outputs was intended but omitted.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(FactorySolverTest, UnconnectedByproductOutputReportsExcessRate) {
          // split_recipe produces output 0 (iron_plate, 20/s) and output 1 (screw, 10/s)
          // Connect only iron_plate to consumer; leave screw unconnected
          // When solving for 20 iron_plate/s:
          // getPort(n_split, "screw", false)->rate is 10.0
          // Currently excess_rate is 0.0 because the port has no outgoing connections;
          // it should report 10.0 excess rate.
      }
      ```
    - **Confidence:** 95%
    - **Status:** won't fix


## Lens 5: App shell: multiple windows and tabs, the close/save flow, autosave, state that leaks between frames (`src/app/*`)

21. **Quitting while window is minimized overwrites window dimensions with 0x0, causing fatal startup crash.**
    - **Severity:** crash
    - **Location:** `src/app/main.cpp:293-303` (call path into `src/app/main.cpp:140-144`)
    - **What goes wrong:** During application shutdown, `main.cpp` captures window dimensions to persist into `app_settings.json`:
      ```cpp
      int maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
      newSettings.windowMaximized = (maximized == GLFW_TRUE);
      if (!newSettings.windowMaximized) {
          int w, h;
          glfwGetWindowSize(window, &w, &h);
          newSettings.windowWidth = w;
          newSettings.windowHeight = h;
      }
      ```
      The code checks `!newSettings.windowMaximized`, but does not check if the window is iconified (minimized): `glfwGetWindowAttrib(window, GLFW_ICONIFIED)`. When minimized to the taskbar, `GLFW_MAXIMIZED` is false, and `glfwGetWindowSize(window, &w, &h)` returns `(0, 0)` (or negative coordinates/minimized icon dimensions depending on the OS/WM). `newSettings.windowWidth` and `windowHeight` are set to `0`. `Application::~Application()` then writes `{"windowWidth": 0, "windowHeight": 0}` into `app_settings.json`.
      On the next launch, `main.cpp:140` executes `glfwCreateWindow(0, 0, ...)`, which fails and returns `nullptr`. `main.cpp:142` calls `LOG(FATAL) << "Failed to create GLFW window"`, terminating with `SIGABRT`. The application cannot start until `app_settings.json` is manually deleted.
    - **Reproduction / failing test idea:**
      1. Launch Factory Planner.
      2. Minimize the application window to the taskbar.
      3. Right-click the application on the taskbar and select "Close window" (or send `SIGTERM` / close via system shell).
      4. Inspect `app_settings.json`: `windowWidth` and `windowHeight` are written as `0`.
      5. Launch the binary again: crashes with `LOG(FATAL) Failed to create GLFW window`.
      *Failing test idea:* Assert that `main.cpp` ignores `glfwGetWindowSize` if `glfwGetWindowAttrib(window, GLFW_ICONIFIED)` is non-zero, preserving existing valid dimensions.
    - **Confidence:** 100%
    - **Status:** open

22. **Detached native file dialog worker threads access destroyed mutexes and NFD runtime if user exits during dialog, causing crash.**
    - **Severity:** crash
    - **Location:** `src/app/Application.cpp:789-806, 1030-1052, 1139-1162`, `src/app/main.cpp:305-314`
    - **What goes wrong:** `drawNewProjectDialog` (`dirDialogState`), `drawOpenProjectDialog` (`openDialogState`), and `drawSaveAsDialog` (`saveDialogState`) execute native file pickers on detached threads (`std::thread([]{ ... }).detach()`). The main GLFW window remains interactive while desktop portal / system dialogs run. If the user closes the main window (e.g. clicks OS close button or presses `Ctrl+Q` when open tabs are clean), the main loop terminates immediately:
      `Application` is destructed, `main.cpp:312` calls `NFD_Quit()`, and `main()` returns 0.
      Upon return from `main()`, static objects `openDialogState`, `saveDialogState`, and `dirDialogState` are destructed. When the detached thread subsequently returns from `NFD_OpenDialogU8_With`, it executes:
      `std::lock_guard<std::mutex> lock(openDialogState.mutex);`
      Locking a destructed mutex and calling `NFD_FreePathU8()` after `NFD_Quit()` invokes undefined behavior, resulting in a segmentation fault (`SIGSEGV`) or abort during process exit.
    - **Reproduction / failing test idea:**
      1. Open "File -> Open Project" (`Ctrl+O`) so the native file dialog appears.
      2. Leave the file picker open and close the main application window (or send a close event).
      3. Dismiss or select a file in the file picker.
      4. The detached thread attempts to acquire the destroyed `openDialogState.mutex`, crashing the process with `SIGSEGV`.
    - **Confidence:** 100%
    - **Status:** open

23. **Canvas editing shortcuts (Cut, Copy, Paste, Undo, Redo, Select All) do not check `io.WantTextInput`, modifying graph while editing text fields.**
    - **Severity:** data loss
    - **Location:** `src/app/Application.cpp:596-658` (contrast with `src/app/Application.cpp:660-671`)
    - **What goes wrong:** In `handleShortcuts()`, line 661 guards single-key shortcuts with `if (io.WantTextInput) return;` so typing 'f' or 'z' into text fields does not trigger "Fit View" or "Show Flow".
      However, the `if (io.KeyCtrl)` block (lines 596-658) contains NO check for `io.WantTextInput`.
      When an editor tab is focused and the user is typing into any text input widget (such as the recipe search box, a pin rate constraint box, or Ctrl-clicking a machine clock speed `DragFloat`):
      - Pressing `Ctrl+Z` to undo a typing mistake in the input field executes `undoActiveEditor()`, reverting background graph actions (deleting nodes, rewiring connections) without user awareness.
      - Pressing `Ctrl+X` to cut text from the input field executes `cutActiveEditor()`, deleting all selected graph nodes on the canvas.
      - Pressing `Ctrl+V` to paste text executes `pasteActiveEditor()`, pasting a cluster of graph nodes onto the canvas.
      - Pressing `Ctrl+A` to select all text executes `selectAllActiveEditor()`, selecting all nodes in the factory graph.
      - Pressing `Ctrl+C` to copy text executes `copyActiveEditor()`, overwriting `m_copyBuffer` with graph nodes instead of copying text.
    - **Reproduction / failing test idea:**
      1. Open any project and select a node on the canvas.
      2. Click on the recipe search bar or open a port constraint input box (`ImGui::InputText`).
      3. Type some text into the box and press `Ctrl+Z` to undo typing.
      4. Observe that the last factory graph command is undone, altering the production line.
      5. Press `Ctrl+X` while typing: the selected canvas node is deleted from the graph.
      *Failing test idea:* In an active editor, focus an `ImGui::InputText` (setting `io.WantTextInput = true`), press `Ctrl+Z`, and assert that `editor->getUndoStackSize()` is unchanged.
    - **Confidence:** 100%
    - **Status:** open

24. **`m_isSaveAsCopy` state leaks into subsequent "Save As..." operations if canceled, silently preventing editor from switching files.**
    - **Severity:** data loss
    - **Location:** `src/app/Application.cpp:496, 623, 1075, 1115`
    - **What goes wrong:** When the user selects "File -> Save a Copy" (line 496), `m_isSaveAsCopy = true` and `m_showSaveDialog = true`.
      If the user cancels the file dialog (line 1115) or an error occurs (line 1103), `m_showSaveDialog` is set to `false`, but `m_isSaveAsCopy` is NEVER reset to `false`.
      Neither the "Save As..." menu item (line 492) nor the `Ctrl+Shift+S` shortcut (line 623) resets `m_isSaveAsCopy`.
      When the user subsequently triggers "Save As..." and selects a new file path, line 1075 evaluates `if (m_isSaveAsCopy)` as `true`. It calls:
      `saveActiveEditorAs(path, SaveAsMode::KeepCurrentFile);`
      instead of `SaveAsMode::SwitchToNewFile`!
      The editor saves the copy but remains attached to the OLD file path. The user continues modifying the graph believing they are working in the new file; subsequent saves (`Ctrl+S` or autosave) overwrite the original file on disk, destroying previous work.
    - **Reproduction / failing test idea:**
      ```cpp
      TEST_F(ApplicationLifecycleTest, SaveAsCopyCancellationDoesNotLeakIntoSaveAs) {
          Application app;
          fs::path originalFile = dirA / "original.fpp";
          app.createNewEditor("", originalFile, "original", false);
          ASSERT_EQ(app.getEditorCount(), 1);

          // 1. Trigger "Save a Copy"
          // Simulating line 496: m_isSaveAsCopy = true, m_showSaveDialog = true
          // 2. User cancels the dialog (saveDialogState.isCancelled = true)
          // 3. User triggers "Save As..." with a new path "new_file.fpp"
          fs::path newFile = dirA / "new_file.fpp";
          // Currently, saveActiveEditorAs is invoked with SaveAsMode::KeepCurrentFile due to stale m_isSaveAsCopy.
          // Assert that the editor's current project path switched to newFile:
          EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), newFile);
      }
      ```
    - **Confidence:** 100%
    - **Status:** open

25. **"Save As..." saves whichever tab is active when dialog completes, overwriting the wrong project if user switches tabs.**
    - **Severity:** data loss
    - **Location:** `src/app/Application.cpp:1083-1095`
    - **What goes wrong:** "Save As..." executes the native file dialog asynchronously on a detached thread (`drawSaveAsDialog()`, line 1139). While the file dialog is displayed, the main window remains interactive, allowing the user to click between tabs and change `m_activeEditor`.
      `Application` does not record which editor initiated the "Save As..." operation. When the dialog completes, line 1094 calls:
      `saveActiveEditorAs(path, SaveAsMode::SwitchToNewFile);`
      which invokes `m_editors[m_activeEditor]->saveAs(...)`.
      If the user switched from Tab A to Tab B while the file dialog was open:
      1. Tab B is saved into the file chosen for Tab A.
      2. Tab B's path and tab name are renamed to Tab A's target file.
      3. Tab A remains unsaved.
      Furthermore, if all tabs are closed while the dialog is open, line 1060 returns without clearing `saveDialogState.resultPath`, causing the next newly created project to be immediately overwritten with the stale path.
    - **Reproduction / failing test idea:**
      1. Open Tab 0 ("ProjectA.fpp") and Tab 1 ("ProjectB.fpp").
      2. Focus Tab 0 and open "File -> Save As...".
      3. While the file picker is open, click on Tab 1 in the tab bar (`m_activeEditor` becomes 1).
      4. In the file picker, select "ProjectA_v2.fpp" and click Save.
      5. Inspect the editors: Tab 1 ("ProjectB") was saved and renamed to "ProjectA_v2.fpp", while Tab 0 ("ProjectA") was not saved.
    - **Confidence:** 100%
    - **Status:** open

26. **"Save As..." silently destroys existing open tabs and their unsaved changes without confirmation if target path collides.**
    - **Severity:** data loss
    - **Location:** `src/app/Application.cpp:1085-1092`
    - **What goes wrong:** When saving an editor to a new path via "Save As...", `drawSaveAsDialog()` checks whether another open tab already points to that target path:
      ```cpp
      for (int i = 0; i < static_cast<int>(m_editors.size()); ++i) {
          if (m_editors[i] && m_editors[i].get() != currentEditor && !m_editors[i]->getProjectFilePath().empty()) {
              std::error_code edEc;
              if (std::filesystem::weakly_canonical(m_editors[i]->getProjectFilePath(), edEc) == targetPath) {
                  closeEditor(i);
                  break;
              }
          }
      }
      ```
      Line 1089 directly invokes `closeEditor(i)`. It does NOT check `m_editors[i]->isDirty()` and does NOT prompt the user with an "Unsaved Changes" dialog. Tab `i` is immediately erased from `m_editors` and deleted, permanently destroying all unsaved work in that tab. Line 1094 then overwrites the file on disk with the current tab's content.
    - **Reproduction / failing test idea:**
      1. Open Tab 0 ("factory_a.fpp") and Tab 1 ("factory_b.fpp").
      2. Make edits to Tab 0 so it has unsaved changes (`isDirty() == true`).
      3. Switch to Tab 1. Select "File -> Save As..." and choose `factory_a.fpp`.
      4. Observe that Tab 0 is instantly closed without prompting "Save changes to factory_a.fpp?", destroying Tab 0's unsaved changes, and Tab 1's graph overwrites `factory_a.fpp` on disk.
    - **Confidence:** 100%
    - **Status:** open

27. **Autosave continues firing on dirty tabs awaiting "Unsaved Changes" confirmation, permanently committing discarded changes to disk.**
    - **Severity:** data loss
    - **Location:** `src/app/Application.cpp:152-198`, `src/editor/factory_planner/FactoryNodeEditor.cpp:127-134`
    - **What goes wrong:** `Application::draw()` iterates over all open editors and calls `editor->draw()` every frame, even while the "Unsaved Changes" modal confirmation dialog (`m_showUnsavedChangesDialog`) is active.
      Inside `FactoryNodeEditor::draw()`:
      ```cpp
      if (std::chrono::steady_clock::now() > m_nextAutosaveTime && SettingsManager::instance().getSettings().autoSaveEnabled) {
          if (m_isDirty && !m_projectFilePath.empty()) {
              LOG(INFO) << "Auto-saved project: " << m_projectFilePath;
              save();
          }
          ...
      }
      ```
      Neither `Application` nor `FactoryNodeEditor` suppresses autosave when a tab is in `m_editorsToClose` or when confirmation is pending.
      If the user initiates a tab close or quit and leaves the "Unsaved Changes" dialog open while stepping away, the autosave timer elapses. Autosave executes `save()`, writing the unwanted changes to disk and clearing `m_isDirty`.
      When the user returns and clicks "Discard", the editor closes, but the changes were already written to disk. The user's pre-edit file is permanently overwritten.
    - **Reproduction / failing test idea:**
      1. Open an existing project file. Make changes so it is dirty.
      2. Request closing the tab (opening "Unsaved Changes" dialog).
      3. Fast-forward or elapse `m_nextAutosaveTime` past current time.
      4. Render a frame (calling `app.draw()`). Autosave triggers and writes dirty state to disk.
      5. Click "Discard".
      6. Inspect the file on disk: it contains the unwanted changes rather than the pre-edit state.
    - **Confidence:** 100%
    - **Status:** open

28. **Inactive tabs docked behind the active tab execute full `editor->draw()` every frame because `ImGui::Begin` return value is ignored.**
    - **Severity:** minor
    - **Location:** `src/app/Application.cpp:188-194`
    - **What goes wrong:** In Dear ImGui docking, when multiple tabs are docked into the same dock node, only the active tab is visible; for all inactive background tabs, `ImGui::Begin()` returns `false` to indicate the window contents are clipped and should not be submitted.
      `Application::draw()` ignores the return value of `ImGui::Begin()`:
      ```cpp
      ImGui::Begin(windowTitle.c_str(), &is_open, editorWindowFlags);
      ImGui::PopStyleVar();
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
          m_activeEditor = i;
      }
      editor->draw();
      ImGui::End();
      ```
      `editor->draw()` is invoked for every open tab on every frame. Inside `editor->draw()`, `ed::Begin(m_editorName.c_str())`, node iteration, quadtree culling, and full ImGui draw lists are generated for all obscured tabs. As the number of open tabs $T$ increases, rendering and CPU time scale with $O(T)$ instead of $O(1)$, degrading frame rates and power consumption even when idle.
    - **Reproduction / failing test idea:**
      Open 10 factory planner tabs docked in the central dockspace. Count calls to `FactoryNodeEditor::draw()` per frame. Currently, `draw()` is called 10 times every frame despite 9 tabs being completely obscured. Wrapping `editor->draw()` in `if (ImGui::Begin(...))` ensures only visible tabs submit drawing commands.
    - **Confidence:** 95%
    - **Status:** open

29. **"Close All" immediately closes all clean tabs before prompting for dirty tabs; clicking "Cancel" fails to restore closed tabs.**
    - **Severity:** minor
    - **Location:** `src/app/Application.cpp:1614-1622` (called from `src/app/Application.cpp:512`)
    - **What goes wrong:** When closing multiple editors (such as clicking "File -> Close All"), `handleCloseEditors()` iterates through clean tabs and destroys them immediately via `closeEditor(idx)`:
      ```cpp
      for (int idx : sortedIndices) {
          if (idx >= 0 && idx < static_cast<int>(m_editors.size()) && m_editors[idx]) {
              if (!m_editors[idx]->isDirty()) {
                  closeEditor(idx);
              }
          }
      }
      ```
      Only after all clean tabs are closed does it prompt the user for the remaining dirty tab(s).
      If the user clicked "Close All" by accident and clicks "Cancel" in the modal dialog (lines 284-290), the dialog closes, but all clean tabs were already permanently destroyed. In contrast, `requestQuit()` (lines 1181-1198) leaves all tabs open until quit confirmation succeeds.
    - **Reproduction / failing test idea:**
      1. Open Tab 0 (clean reference project) and Tab 1 (dirty).
      2. Select "File -> Close All".
      3. In the "Unsaved Changes" dialog for Tab 1, click "Cancel".
      4. Expected: the "Close All" action is cancelled, preserving both tabs. Actual: Tab 0 was already closed and destroyed; only Tab 1 remains.
    - **Confidence:** 95%
    - **Status:** open
