# Antigravity Agent Guidelines — Factory Planner

This document provides workspace rules and operational guidelines for Antigravity AI coding agents working on the `factory-planner` repository. All agents must read and adhere to these guidelines before making changes. The development plan and its decisions live in `docs/ROADMAP.md`.

---

## 1. Project Summary

**Factory Planner** is a high-performance, node-based factory optimization tool designed to plan and calculate precise production lines for factory simulation games (such as Satisfactory). It translates factory graph topologies into linear programming problems to compute optimal flow rates, machine counts, and bottleneck distributions.

- **Language Standard:** C++20 (`set(CMAKE_CXX_STANDARD 20)`)
- **Build System:** CMake 3.30+
- **GUI & Graphics:** Dear ImGui (docking branch), GLFW 3.4 (via `FetchContent`), OpenGL 3.2 Core Profile (GLSL 150)
- **Graph Interaction:** `imgui-node-editor` (vendored in `external/imgui-node-editor`)
- **LP Solver:** Google OR-Tools (GLOP linear solver, prebuilt binaries in `external/or-tools`)
- **Serialization:** nlohmann/json (`external/nlohmann/json.hpp`)
- **Logging & Utilities:** Google Abseil (`absl::log`, `absl::log_sink`, `absl::flags`, `absl::failure_signal_handler`), Native File Dialog Extended (`nfd`), `stb_image` (vendored inside `external/imgui-node-editor/external/stb_image`)
- **Spatial Indexing:** pvigier Quadtree (`external/pvigier/Quadtree.h`) for viewport culling in the node editor
- **Testing Framework:** GoogleTest (`gtest_main`, via CMake `FetchContent`)

### `external/` layout
- `external/imgui`, `external/nativefiledialog-extended`: git submodules (`git submodule update --init --recursive`).
- `external/imgui-node-editor`: vendored directly in git (not a submodule) and **heavily modified locally**. Do not "restore" it to upstream. Change it only when the task explicitly covers it.
- `external/nlohmann`, `external/pvigier`: vendored single headers.
- `external/or-tools`: prebuilt OR-Tools archive, **not tracked in git**. It must exist locally to build.

---

## 2. Build and Test Commands

### Linux

Prerequisites: CMake 3.30+, a C++20 compiler (GCC 12+ or Clang 16+), pkg-config, OpenGL development libraries (`libgl1-mesa-dev`), X11/Wayland development libraries, DBus, RE2, `xvfb` (for headless tests), and prebuilt OR-Tools extracted into `external/or-tools`. See `.github/workflows/ci.yml` for the exact package list.

- **Configure:**
  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(pwd)/external/or-tools" \
    -Dortools_DIR="$(pwd)/external/or-tools/lib/cmake/ortools" \
    -DCMAKE_INSTALL_RPATH="\$ORIGIN/lib" \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
  ```
- **Build:**
  ```bash
  cmake --build build --config Release --parallel
  ```
- **Run Tests (local / interactive terminal):**
  ```bash
  export LD_LIBRARY_PATH="$(pwd)/external/or-tools/lib:$LD_LIBRARY_PATH"
  ctest --test-dir build -C Release --output-on-failure
  ```
- **Run Tests (headless / CI):**
  ```bash
  export LD_LIBRARY_PATH="$(pwd)/external/or-tools/lib:$LD_LIBRARY_PATH"
  xvfb-run --auto-servernum ctest --test-dir build -C Release --output-on-failure
  ```
- **Run a Single Test:**
  ```bash
  export LD_LIBRARY_PATH="$(pwd)/external/or-tools/lib:$LD_LIBRARY_PATH"
  ./build/tests/factory_planner_tests --gtest_filter="SuiteName.TestName"
  ```
  (Prefix with `xvfb-run --auto-servernum` when no display is available.)
- **Run Executable:**
  ```bash
  ./build/bin/factory_planner
  ```

### Windows

Prerequisites: Visual Studio 2022 (MSVC v143+), CMake 3.30+, and prebuilt OR-Tools extracted into `external/or-tools`.

- **Configure (Visual Studio generator):**
  ```cmd
  cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%cd%\external\or-tools" -Dortools_DIR="%cd%\external\or-tools\lib\cmake\ortools"
  ```
- **Configure (NMake generator, from the x64 Native Tools Command Prompt, run in the repository root):**
  ```cmd
  cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%cd%\external\or-tools" -Dortools_DIR="%cd%\external\or-tools\lib\cmake\ortools"
  ```
- **Build:**
  ```cmd
  cmake --build build --config Release --target factory_planner --parallel
  cmake --build build --config Release --target factory_planner_tests --parallel
  ```
- **Run Tests (PowerShell):**
  ```powershell
  $env:PATH = "${pwd}\external\or-tools\bin;${pwd}\build\tests\Release;$env:PATH"
  ctest --test-dir build -C Release --output-on-failure
  ```
- **Run a Single Test (PowerShell):**
  ```powershell
  .\build\tests\Release\factory_planner_tests.exe --gtest_filter="SuiteName.TestName"
  ```
- **Run Executable:**
  ```cmd
  .\build\bin\factory_planner.exe
  ```

---

## 3. Architecture Map

The codebase is organized into modules under `src/`:

### `src/app/` (Application Lifecycle & Shell)
- `src/app/main.cpp`: Application entry point; initializes Abseil logging/flags, GLFW and the OpenGL context, and runs the main application shell.
- `src/app/Application.h` / `Application.cpp`: Coordinates the ImGui docking window shell, main menu bar, project tabs (open/save/close flows, unsaved-changes dialogs, autosave).

### `src/core/data/` (Game Data Definitions & Management)
- `src/core/data/GameData.h`: Core data structures for resources, machines, and recipes loaded from game data files.
- `src/core/data/GameDataManager.h` / `GameDataManager.cpp`: Loads game data files and provides lookups for resources, machines, recipes and ID aliases.
- `src/core/data/GameDataScanner.h`: Discovers game directories and `.gd` game data files (`<game_data>/<game>/game_datas/*.gd`) on disk.

### `src/core/graph/` (Factory Graph Topology & Solver)
- `src/core/graph/Port.h`: An input or output pin on a node, holding the user constraint, calculated rate, and excess rate.
- `src/core/graph/Node.h`: A recipe/machine placement on the canvas with clock speed, production multiplier, and machine count.
- `src/core/graph/Connection.h`: A directed link from an output port to an input port with a calculated flow rate.
- `src/core/graph/FactoryGraph.h` / `FactoryGraph.cpp`: Owns the graph state (nodes, ports, connections), ID generation, mutations, and lookups.
- `src/core/graph/FactorySolver.h` / `FactorySolver.cpp`: Translates the graph into an OR-Tools linear program solved lexicographically in 3 phases (target water-filling, per-depth throughput, waste minimization with fair splitting). See `docs/solver.md`.

### `src/editor/` (User Interface & Canvas)
- `src/editor/factory_planner/FactoryNodeEditor.h` / `FactoryNodeEditor.cpp`: The node canvas for editing production lines (rendering, wiring, interactions, quadtree culling).
- `src/editor/settings_editor/SettingsEditor.h` / `SettingsEditor.cpp`: Settings window for theme, font, grid, and autosave interval.

### `src/services/` (Cross-Cutting Application Services)
- `src/services/ProjectIo.h` / `ProjectIo.cpp`: Saves and loads `.fpp` project files (JSON content) using atomic temp-file writes.
- `src/services/UndoRedoSystem.h` / `UndoRedoSystem.cpp`: Command pattern implementation for reversible graph actions (add/remove node, connect, paste, ...).
- `src/services/SettingsManager.h` / `SettingsManager.cpp`: Persistent application settings (`app_settings.json`).
- `src/services/SessionManager.h` / `SessionManager.cpp`: Persists session state across restarts (`session.json`, reopening previous tabs).
- `src/services/RecentFiles.h` / `RecentFiles.cpp`: Tracks recently opened projects (`recent.json`).
- `src/services/TextureManager.h` / `TextureManager.cpp`: Loads and caches GPU textures for resource and machine icons.
- `src/services/NotificationManager.h` / `NotificationManager.cpp`: Queues and renders non-blocking toast notifications.

### `src/common/` (Shared Primitives & Utilities)
- `src/common/CopyBuffer.h`: Data structures for copied nodes and links (clipboard).
- `src/common/FilesystemUtils.h`: Filesystem helpers (executable directory, canonical paths, atomic file operations).
- `src/common/FramePacer.h`: Dynamic frame rate throttling (active animation vs. idle power saving).
- `src/common/IdUtils.h`: Converts between internal 64-bit graph IDs and `imgui-node-editor` handles (`NodeId`, `PinId`, `LinkId`) using fixed offsets. IDs themselves are generated by `FactoryGraph`.
- `src/common/StringUtils.h`: Natural sort string comparison and slugification utilities.
- `src/common/TextureUtils.h` / `TextureUtils.cpp`: Loads image files into OpenGL textures using `stb_image`.

### `tests/` (GoogleTest suites, single binary `factory_planner_tests`)
- `tests/test_factory_graph.cpp`: Graph mutations, ports/connections, game data loading and project serialization.
- `tests/test_factory_solver.cpp`: LP formulation, LIMIT/TARGET semantics, lexicographic phases, cycles, machine counts.
- `tests/test_undo_redo_system.cpp`: Undo/redo commands and graph rollback.
- `tests/test_frame_pacer.cpp`: Frame pacing and idle throttling.
- `tests/test_application.cpp`: Application shell and tab/dialog flows. **Creates a GLFW window: needs a display (or `xvfb-run`).**

### `data/` (Runtime Data & Assets, copied next to the executable at build time)
- `data/game_data/<game>/game_datas/*.gd`: Game data files (JSON).
- `data/game_data/<game>/icons/`: Resource and machine icons.
- `data/assets/fonts/`, `data/assets/icons/`: Application fonts and fallback icons.

### Runtime files
`app_settings.json`, `session.json` and `recent.json` are written to the executable directory (fallback: current working directory), i.e. `build/bin/` during development. Never commit them.

---

## 4. Code & Process Conventions

### Naming Conventions
- **Types / Classes / Structs:** `PascalCase` (e.g. `FactorySolver`, `GameDataManager`, `Node`, `Port`, `Connection`).
- **Functions & Methods:** `camelCase` (e.g. `solve()`, `addNode()`, `classifyConstraint()`).
- **Variables & Struct Members:** `snake_case` for graph/data domain models and JSON keys (e.g. `machine_count`, `from_port`, `resource_key`); `camelCase` for local variables and application/service structs (e.g. `AppSettings`, `CopyBuffer`, `nodeCount`). Follow the style of the file you are editing.
- **Class Private Member Variables:** Prefixed with `m_` (e.g. `m_solver`, `m_portVariables`, `m_constraints`).
- **Constants & Enums:** `kCamelCase` or `UPPER_SNAKE_CASE` for constants (e.g. `kExcessWasteWeight`, `kZeroEpsilon`, `NODE_ID_OFFSET`); scoped enums use a `PascalCase` type with `UPPER_SNAKE_CASE` or `PascalCase` enumerators (e.g. `ConstraintKind::LIMIT`, `SolverResultStatus::SUCCESS`).

### Header Guards
- New headers use classic `#ifndef FILENAME_H` / `#define FILENAME_H` / `#endif` guards matching the uppercase file name.
- Some existing headers use `#pragma once` (`FramePacer.h`, `FactoryNodeEditor.h`) or prefixed guards (`FACTORY_PLANNER_*`). Do not convert existing headers unless the task asks for it.
- Keep headers self-contained: each header includes everything it references.

### Logging
- Use Abseil logging macros (`LOG(INFO)`, `LOG(WARNING)`, `LOG(ERROR)`, `VLOG(1)`, `VLOG(2)`).
- Never use `std::cout`, `printf`, or `<iostream>` for output in production code. The only exception is the existing `std::cerr` fallback in `main.cpp` used before logging is initialized. `snprintf` into buffers for UI text is fine.

### Changelog Policy
- `docs/CHANGELOG.md` is the **user-facing** changelog used for release notes, following [Keep a Changelog](https://keepachangelog.com/).
- Add entries **only for changes a user of the app would notice**: new features, changed behavior, bug fixes, removed functionality. Put them under `## Unreleased` at the top, in `### Added`, `### Changed`, `### Fixed`, or `### Removed`.
- Internal changes (CI, agent rules, docs, refactors without behavior change, tests) do **not** get changelog entries. They are described in the commit message.

### Commit Messages (suggested by the agent, committed by the human)
- Use Conventional Commits format matching repository history:
  - Format: `<type>(<scope>): <subject>` or `<type>: <subject>`
  - Types: `feat`, `fix`, `refactor`, `test`, `chore`, `docs`, `ci`
  - Scopes: `tests`, `ci`, `solver`, `graph`, `editor`, `app`, `data`, etc.
  - Multi-line messages include bullet points explaining what changed and why.

---

## 5. Definition of Done

Every task is complete only when:
1. **Clean compilation:** The project builds without new compiler or linker warnings (Linux locally; Windows is verified by CI).
2. **All tests pass:** 100% of tests pass via `ctest`, without failures or flaky timeouts.
3. **Behavior tested:** New, modified, or fixed behavior is covered by automated tests in `tests/`.
4. **Changelog (user-visible changes only):** An entry under `## Unreleased` in `docs/CHANGELOG.md` if, and only if, a user would notice the change (see Changelog Policy).
5. **Handover report:** The final message lists the files changed, how they were verified (commands + results), anything left open, and a suggested commit message. Changes are left **uncommitted**.

---

## 6. Hard Rules

1. **No Python or extra runtimes:** Committed tooling, import scripts, and build tasks must be written in C++. No Python scripts, virtualenvs, or non-C++ dependencies in the repository (Roadmap Decision D3).
2. **No new dependencies without approval:** Use the libraries already integrated (OR-Tools, ImGui, GLFW, nlohmann/json, Abseil, GoogleTest, stb_image, NFD, Quadtree). Do not add new external libraries without explicit discussion.
3. **Do not modify the solver objective without a test:** Any change to optimization criteria, lexicographic phases, tolerances, or objective weights in `FactorySolver` must be justified, guarded by dedicated tests in `tests/test_factory_solver.cpp`, and reflected in `docs/solver.md`.
4. **Strictly sequential execution:** Follow `docs/ROADMAP.md`. Work on one task at a time, keep scope tightly bounded, and never perform refactorings that the task does not ask for. Report unrelated problems instead of fixing them.
5. **The human owns git:** Never run commands that change repository history, the index, or branch state: no `git commit`, `push`, `pull`, `merge`, `rebase`, `reset`, `checkout`/`switch`, `branch`, `stash`, `tag`, `add`, `rm`, `mv`, `restore`, `clean`, `cherry-pick`, `revert`, or `submodule update`. Leave all changes uncommitted in the working tree for human review. Read-only commands (`git status`, `diff`, `log`, `show`, `blame`, `ls-files`) are allowed.
