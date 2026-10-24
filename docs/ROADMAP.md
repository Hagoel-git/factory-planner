# Factory Planner — Road to v1.0-Beta

This is the handbook for development after v0.10-alpha. It records the decisions already made, the order of work, and a **ready-to-paste prompt for every step**.

It lives in the repo so that every agent conversation can read it. Prompts refer to it as `docs/ROADMAP.md`.

---

## 1. Decisions already made

| # | Decision | Why |
|---|---|---|
| D1 | **Strictly sequential**: one task at a time, merged before the next starts | You prefer quality over speed |
| D2 | **The in-app GameData editor is deleted** | It's slow and nobody would use it for 50k-recipe games; game data comes from importers and hand edits instead |
| D3 | **No Python** (or other extra runtimes) in the repo | You don't want venvs and library version conflicts. Committed tooling is C++ |
| D4 | Old GameData and project formats **don't need backward compatibility** | There are no users yet |
| D5 | GameData v4 is a **custom schema with formulas**; **GTNH is the stress test** | Goal: support as many games as possible |
| D6 | **Two kinds of formulas**: *pre-solve* (constants per node → recipe amounts, enabled ports, crafting time; the solver stays linear) and *post-solve* (solved values → displayed metrics such as power or cost, plus graph-wide totals) | Your GTNH examples (tier-gated byproducts, crop manager) change what the solver works with |
| D7 | Formula language: **a small expression language written in C++**, no new dependency | No extra runtime; good error messages |
| D8 | `gamedata_tool` (a C++ command-line tool for validating and importing data) **gets its own design conversation** later | You want to discuss it separately |
| D9 | GameData v4 comes **before** the node-editor rework | v4 decides what a node shows; the canvas is designed around that |
| D10 | The node-editor rework is **Opus-led** and preceded by a premium **UI/UX audit** | It needs real decisions, not mechanical pruning |
| D11 | **Auto-layout** ships first as a standalone "Auto-arrange" command, then Generate reuses it | Useful on its own; Generate needs it anyway |

---

## 2. Milestones

| Version | Contents (steps below) | Done when |
|---|---|---|
| **v0.11-alpha** | 0, 1, 2 | Tests run in CI on every push; GameData editor removed; golden tests in place; audit findings fixed or logged |
| **v0.12-alpha** | 3 | GameData v4 + expression engine; Satisfactory and Mindustry converted; solver golden tests still pass |
| **v0.13-alpha** | 4 | `gamedata_tool`; at least one new game imported (ideally GTNH as the scale test); Data Browser |
| **v0.14-alpha** | 5, 6 | UI/UX decisions doc; node editor reworked (app-owned state, link waypoints, link styles, no default snapping, smooth `showFlow`) |
| **v0.15-alpha** | 7 | Auto-arrange + Generate |
| **v1.0-Beta** | 8 + polish | See the criteria below |

**v1.0-Beta criteria** (proposal; adjust as you like):
- GameData v4 and the project file format are versioned and considered stable.
- At least 3 games supported; a "how to add a game" guide exists.
- No known crash or data-loss bugs; CI green on Windows and Linux.
- README, GIFs, `docs/solver.md` and `docs/gamedata-v4.md` are up to date.

---

## 3. How to run each task (the routine)

1. **Start a new conversation per task.** Long conversations waste quota and lose focus. Use `/compact` if one grows too long.
2. **Pick the model** given for the step. For Opus and Argon tasks, start the prompt with `/plan` so the agent proposes a plan before writing code and you approve it.
3. **Branch first (you do this)**: `git switch dev && git pull && git switch -c <branch-name-given-in-step>`.
4. **Paste the prompt.** Every prompt tells the agent to read this file and the rules file (`AGENTS.md`).
5. **Agents never touch git** (AGENTS.md Hard Rule 5). They leave changes uncommitted and end with a handover report and a suggested commit message. You review `git status` / `git diff`, then commit and push yourself.
6. **Run CI before merging.** `ci.yml` runs automatically for pull requests into `dev`/`main` and for pushes to them. For a feature branch without a PR, start it manually: GitHub → Actions → CI → "Run workflow" → pick the branch. Merge into `dev` only when CI is green. Merge `dev → main` only at a milestone, then tag the release.
7. **Write things down.** If the agent made a mistake you had to correct, use `/learn` (or add a line to the rules file) so the next agent doesn't repeat it.
8. **Record what each step produced** (a document, decisions) under `docs/design/` or `docs/audits/`. Later prompts refer to those files, which is how one conversation hands over to the next.

**Saving premium quota:**
- Before an Opus or Argon task, have Flash gather the context first (for example a summary of the files involved), and paste that summary in.
- Ask the premium model for **decisions + the core code**. Leave follow-up polish (renames, extra tests, changelog) to a Flash conversation.
- If a premium task runs out of quota midway, ask it first: *"Write a handoff note to `docs/handoff/<step>.md`: what's done, what's left, open decisions."* A new conversation can continue from that file.

---

## 4. Steps and prompts

### Step 0 — Agent infrastructure · Flash · branch `chore/agent-infra` · ✅ done

The rules file is `AGENTS.md` in the repository root. After the first attempt, a review fixed: the git rule, the changelog policy, factual errors in `AGENTS.md` and `docs/solver.md`, the obsolete class comment in `FactorySolver.h`, and CI permissions/concurrency/timeouts. The prompt is kept below for reference.

> **Prompt:**
> ```
> Read docs/ROADMAP.md (sections 1 and 3) first.
>
> Task: set up agent infrastructure for this repo. Do the following:
>
> 1. Create a workspace rules file for Antigravity agents. Use the agy-customizations skill
>    to find the correct location and format. It must contain:
>    - Project summary (C++20, CMake 3.30+, ImGui docking, vendored imgui-node-editor in
>      external/, OR-Tools GLOP solver, nlohmann json, GoogleTest).
>    - Exact build and test commands for Linux and Windows (read readme.md,
>      .vscode/tasks.json and .github/workflows/release.yml for the real ones).
>    - Architecture map: src/core (data, graph, solver), src/editor, src/services, src/app,
>      with one line per important file describing its responsibility.
>    - Conventions you observe in the code (naming, header guards, logging, changelog
>      format in docs/CHANGELOG.md, commit message style from git log).
>    - Definition of done: builds without new warnings, all ctest tests pass, new
>      behaviour has tests, a CHANGELOG entry under "Unreleased" ONLY for user-visible
>      changes (the changelog is user-facing release notes).
>    - Hard rules: no Python or other new runtimes, no new dependencies without asking,
>      don't change the solver objective without a test, do one task at a time and don't
>      expand scope, never run git commands that change state (the human commits).
> 2. Add a GitHub Actions workflow .github/workflows/ci.yml that runs on push and
>    pull_request to dev and main: build + ctest on Linux and Windows. Reuse the
>    steps from release.yml, but don't package or release. Keep release.yml as it is.
> 3. FactorySolver.h refers to docs/solver_redesign_spec.md, which doesn't exist. Write
>    docs/solver.md describing the solver as implemented (variables, LIMIT/TARGET
>    semantics, the lexicographic stages, how machine_count is derived) from
>    FactorySolver.cpp and tests/test_factory_solver.cpp. Update the reference.
>
> Verify: configure, build and run ctest locally. Show me the rules file content and
> the test summary at the end.
> ```

**Review checklist:**
- Are the commands in the rules file correct? Try one yourself.
- Is CI green? Start it manually on the branch (Actions → CI → Run workflow) or open a PR into `dev`.
- Does `docs/solver.md` match how you understand the solver?

---

### Step 1 — Delete the in-app GameData editor · Flash · branch `refactor/remove-gamedata-editor`

> **Prompt:**
> ```
> Read docs/ROADMAP.md and the workspace rules file first.
>
> Task: completely remove the in-app Game Data Editor (decision D2).
>
> 1. Delete src/editor/game_data_editor/ and remove all wiring (Application.h/.cpp,
>    menus, windows, settings, session state, CMake).
> 2. In GameDataManager, remove every API that only the editor used (creating, renaming
>    or deleting resources/machines/recipes, saving game data files, icon setters, draft
>    state). KEEP: scanning game data packages, loading, alias resolution, lookups used by
>    the graph, solver, node editor, ProjectIo and UndoRedoSystem. Before deleting a
>    function, grep for its callers and list the removals with that evidence.
> 3. Remove or adapt the tests in tests/ that cover the removed code. Don't delete tests
>    for code that still exists.
> 4. Remove assets, settings keys and menu entries that only the editor used.
> 5. Add a CHANGELOG entry under "Unreleased".
>
> Don't refactor anything else. Verify: clean build, no new warnings, all tests pass.
> Report the net line count change (git diff --stat).
> ```

**Review checklist:** the app starts, opening and saving projects works, game data still loads, and no editor menu entries are left.

---

### Step 2a — Golden tests · Flash · branch `test/golden-tests`

> **Prompt:**
> ```
> Read docs/ROADMAP.md, the rules file and docs/solver.md first.
>
> Task: add golden (regression) tests that will protect the upcoming GameData v4
> rework. Don't change production code unless a test reveals a real bug; if it does,
> stop and report it to me instead of fixing it.
>
> 1. Project round-trip: build graphs programmatically (multiple nodes, multi-port
>    recipes, fan-out/fan-in, constraints, clock speed and production multiplier set),
>    save with ProjectIo, load again, and assert the graphs are equal (nodes, ports,
>    connections, constraints, parameters, positions).
> 2. Solver goldens: 5–8 realistic Satisfactory chains using the real data file in
>    data/game_data/satisfactory (e.g. iron plates; reinforced iron plates; a chain with
>    byproducts; a split/merge; a constrained middle stage; a 0% clock node). For each,
>    assert the port rates and machine counts with a tolerance. Compute expected values
>    by hand from the recipe data and write the arithmetic in comments; don't just copy
>    what the solver outputs.
> 3. Put them in tests/test_golden.cpp (or similar) and register them in tests/CMakeLists.txt.
>
> Verify: all tests pass locally.
> ```

### Step 2b — Scoped bug audit · Flash · one conversation per lens, in sequence

Run these **one after another**. Each one only writes findings; it doesn't fix anything. Skip the GameData code, because it gets rewritten in step 3.

> **Prompt template** (replace `<LENS>` and `<FILES>`):
> ```
> Read docs/ROADMAP.md and the rules file first.
>
> Task: bug audit, read-only. Don't modify source code.
> Scope: <FILES>
> Lens: <LENS>
>
> For each finding, write: severity (crash / data loss / wrong result / minor), file:line,
> what goes wrong, a concrete reproduction scenario or a failing test idea, and how
> confident you are. Don't report style issues or speculative "could be cleaner" items.
> Verify each finding by reading the actual call paths; if you can't build a concrete
> scenario, drop it.
> Append the results to docs/audits/bug-audit-v0.11.md under a heading for this lens.
> ```

| Run | `<LENS>` | `<FILES>` |
|---|---|---|
| 1 | Memory, lifetime, pointer/iterator invalidation | `src/core/graph/*`, `src/services/UndoRedoSystem.*` |
| 2 | Undo/redo symmetry: every command fully restores state, including IDs, positions, constraints | `src/services/UndoRedoSystem.*`, `src/editor/factory_planner/*` |
| 3 | File I/O robustness: corrupted or partial files, paths, encodings, atomic writes | `src/services/ProjectIo.*`, `SessionManager`, `SettingsManager`, `RecentFiles` |
| 4 | Solver correctness and edge cases: cycles, zero amounts, disconnected parts, infeasible constraints | `src/core/graph/FactorySolver.*` |
| 5 | App shell: multiple windows and tabs, the close/save flow, autosave, state that leaks between frames | `src/app/*` |

**Then:** go through `docs/audits/bug-audit-v0.11.md` yourself and mark each entry *fix* / *won't fix*. Fix them in Flash conversations, a few at a time:

> ```
> Read docs/ROADMAP.md, the rules file and docs/audits/bug-audit-v0.11.md.
> Fix findings #<n>, #<m> (marked "fix"). For each: first write a failing test that
> reproduces it, then fix it, then confirm the test passes. Mark the findings as fixed in
> the audit file. Add a CHANGELOG entry.
> ```

**→ Release v0.11-alpha.**

---

### Step 3 — GameData v4

#### 3a. Mechanics research · Flash (research agent) · no branch needed (writes a document)

> **Prompt:**
> ```
> Read docs/ROADMAP.md (decisions D5–D7) first, then src/core/data/GameData.h and
> src/core/graph/FactorySolver.cpp to understand the current data model.
>
> Task: research production mechanics across factory games, to feed the design of a
> generic, formula-driven game data schema. Games: Satisfactory (incl. clock speed,
> somersloop amplification, power shards), Factorio (incl. Space Age: modules, beacons,
> quality, spoilage, productivity research), Dyson Sphere Program (proliferator modes),
> Mindustry (optional boosters, power), GregTech: New Horizons (voltage tiers, overclocking
> rules, tier-gated byproducts, chance outputs, non-consumed catalysts/circuits,
> programmed circuits, multiblocks, crop sticks/crop manager), Captain of Industry
> (workers, maintenance, electricity, computing), Foundry, Techtonica, Create (stress
> units, RPM), Shapez 2.
>
> Produce docs/design/mechanics-matrix.md containing:
> 1. A table: mechanic | games | a short description with the concrete math | category:
>    PRE-SOLVE (changes recipe amounts, ports or time per node, keeping the LP linear),
>    POST-SOLVE (displayed metric only), or NON-LINEAR (can't be expressed in an LP;
>    say why, and whether an approximation exists).
> 2. For each game: the 3–5 mechanics that are must-haves for a useful planner.
> 3. A list of the node parameters, machine attributes and recipe attributes a schema
>    would need to cover all PRE-SOLVE mechanics.
> 4. Data availability: where each game's recipe data can be extracted from (game files,
>    official dumps, mods, wikis) and in what format.
> Cite sources (wiki URLs). Be concrete with the formulas; avoid vague descriptions.
> ```

#### 3b. Schema design · **Opus** · `/plan` · branch `design/gamedata-v4`

> **Prompt:**
> ```
> /plan
> Read docs/ROADMAP.md (all decisions), docs/design/mechanics-matrix.md,
> src/core/data/GameData.h, src/core/data/GameDataManager.cpp,
> src/core/graph/FactorySolver.cpp, src/core/graph/Node.h, and one existing data file
> under data/game_data/.
>
> Task: design GameData v4. This is a design discussion with me, not an implementation.
> The output is docs/design/gamedata-v4-spec.md.
>
> The spec must define:
> - The file structure (JSON): game metadata, units/time base, resources (items, fluids,
>   display units), machines (attributes, e.g. tier, speed, power), recipes (ports,
>   time, allowed machines), node parameter definitions (type, range, default, UI hint),
>   pre-solve formulas (amounts, conditional ports, crafting time), post-solve metric
>   formulas (display location: node body / tooltip / totals panel; how totals are
>   aggregated). Also a strategy for splitting very large datasets (GTNH has 50k+ recipes).
> - The expression language (decision D7): grammar, types, variables available in
>   pre-solve vs post-solve context, built-in functions (min, max, pow, clamp, lookup
>   tables, if/else), error reporting, and determinism. Keep it small.
> - How the solver consumes it: what gets evaluated per node before solving, why the
>   LP stays linear, what replaces Node::clock_speed / production_multiplier.
> - What the project file stores per node (parameter values, machine, recipe).
> - Validation rules (what a validator must reject, with example error messages).
> - Worked examples in the schema: Satisfactory constructor with clock speed + power;
>   GTNH macerator with byproducts only at HV+; GTNH crop manager driven by parameters;
>   DSP proliferator (extra products vs speed); Factorio assembler with modules + a beacon.
> - Explicit non-goals for v4.
>
> Ask me about real trade-offs before deciding them. Don't write C++ yet.
> ```

#### 3c. Adversarial review · **Argon** · same branch

> **Prompt:**
> ```
> Read docs/ROADMAP.md, docs/design/mechanics-matrix.md and docs/design/gamedata-v4-spec.md.
>
> Task: review the spec adversarially. Don't rewrite it.
> 1. Try to express each must-have mechanic from the matrix in the schema. For each, write
>    the schema snippet or explain why it can't be done or is awkward.
> 2. Find ambiguities, contradictions and under-specified parts in the expression language
>    and the validation rules.
> 3. Point out anything that would make the solver non-linear, or make loading 50k recipes slow.
> 4. Judge the UX cost for people writing game data by hand.
> Write the results to docs/design/gamedata-v4-review.md, ranked by severity, each with a
> concrete suggested fix.
> ```
Then, in a short follow-up (Opus if the issues are deep, otherwise Flash): *"Apply the accepted items from gamedata-v4-review.md to the spec."* You decide which items are accepted.

#### 3d. Core implementation · **Opus** · `/plan` · branch `feat/gamedata-v4-core`

> **Prompt:**
> ```
> /plan
> Read docs/ROADMAP.md, the rules file and docs/design/gamedata-v4-spec.md (final).
>
> Task: implement the GameData v4 core per the spec. This step does NOT touch the solver
> or the UI.
> - The new data model replaces the structs in src/core/data/GameData.h.
> - Loader + validator with error messages that say exactly what's wrong and where
>   (file, JSON path), as specified.
> - The expression engine (lexer, parser, AST, evaluator) as a separate unit under
>   src/core/expr/ with no ImGui dependency.
> - Thorough unit tests: expression parsing, evaluation and errors; loader happy path;
>   every validation rule.
> Keep the old code compiling if feasible; if not, list exactly what breaks and the
> plan for step 3e.
> ```

#### 3e. Solver and graph integration · **Opus** · `/plan` · branch `feat/gamedata-v4-solver`

> **Prompt:**
> ```
> /plan
> Read docs/ROADMAP.md, the rules file, docs/design/gamedata-v4-spec.md and docs/solver.md.
>
> Task: integrate GameData v4 into FactoryGraph, FactorySolver, Node, ProjectIo and
> UndoRedoSystem per the spec.
> - Evaluate the pre-solve formulas per node; feed the resulting coefficients and active
>   ports into the LP.
> - Replace Node::clock_speed / production_multiplier with schema-defined node parameters.
> - Machine or parameter changes that change the active ports must be undoable, and must
>   handle existing connections safely.
> - Evaluate the post-solve metrics after solving (per node + graph totals), exposed
>   through an API that the UI step will use.
> - Update docs/solver.md.
> The golden tests (tests/test_golden*) are the acceptance criteria: port them to v4
> data, with the same expected numbers.
> ```

#### 3f–3h. UI, data conversion, project format · Flash · one conversation each

> **3f UI:** `Read docs/ROADMAP.md, the rules file and docs/design/gamedata-v4-spec.md. Implement the node UI for v4: parameter widgets generated from the parameter definitions (respecting type, range and UI hints), post-solve metrics shown where the spec's display location says, and a graph totals panel. Keep the visuals simple and consistent with the current style; the visual redesign comes later (step 6). Undo/redo must work for parameter edits.`
>
> **3g Data:** `Read docs/ROADMAP.md, the rules file and the v4 spec. Convert data/game_data/satisfactory and data/game_data/mindustry to v4, including clock speed and power formulas for Satisfactory as in the spec examples. Do the conversion as a one-off (don't commit any script) and validate with the v4 loader in a test. Delete the old files.`
>
> **3h Format:** `Read docs/ROADMAP.md and the rules file. Add an explicit format version to the project file format. Reject unknown or newer versions with a clear error. Add a test.`

**→ Release v0.12-alpha.**

---

### Step 4 — `gamedata_tool` + importers · conversation to come (decision D8)

You'll start this design conversation yourself. Starter prompt:

> ```
> /plan
> Read docs/ROADMAP.md (D3, D8), docs/design/gamedata-v4-spec.md and
> docs/design/mechanics-matrix.md (data availability section).
> Let's design gamedata_tool: a C++ command-line tool built from the same GameData library
> as the app (validate, stats, importers per game). Discuss with me the commands, the
> importer architecture, which game to import first, and the in-app read-only Data Browser
> with hot reload. Output: docs/design/gamedata-tool.md.
> ```

Things to bring into that conversation: a GTNH-sized import should also check the **recipe and machine picker UX** (search, filtering) and loading speed.

**→ Release v0.13-alpha.**

---

### Step 5 — Whole-app UI/UX audit · **Argon** (or Opus) · writes a document

First have Flash collect raw material (cheap):
> ```
> Read docs/ROADMAP.md and the rules file. Read-only task: list every user-facing
> feature, interaction, shortcut, menu, window, popup and setting in the app, with
> where each is implemented (file:line). Include node-editor library behaviours that are
> enabled (snapping, selection, link dragging, flow animation, context menus).
> Write the list to docs/audits/ui-inventory.md.
> ```

Then the premium audit:
> ```
> /plan
> Read docs/ROADMAP.md, docs/audits/ui-inventory.md and the v4 spec. Look at the code in
> src/editor/factory_planner/ and external/imgui-node-editor/ as needed.
>
> Task: a UI/UX audit of the whole app. Output: docs/design/ui-decisions.md with three
> sections: REMOVE / REVISE / ADD. Each item gets a rationale, priority and effort.
> Must include a node-editor section covering my requirements:
> - node position and size owned by our Node class (the library keeps no node state of its own),
> - link waypoints the user can add and drag, with the link passing through them,
> - several link render styles (bezier / straight / right-angle),
> - snapping off by default and configurable,
> - showFlow lag with many links or at extreme zoom-out,
> - which unused library features (groups, shortcut actions, config callbacks, crude_json,
>   examples/, docs/) should be pruned.
> Also judge picker UX at GTNH scale, discoverability, keyboard flow and readability of
> large graphs. Compare with tools like Satisfactory Tools, Factorio's FactoryPlanner, YAFC.
> Discuss the major items with me before finalizing them. End with a proposed split of
> step 6 into 3–5 tasks.
> ```

---

### Step 6 — Node-editor rework · **Opus** · one `/plan` conversation per task from the split

Suggested split (the audit may change it):
1. **App-owned node state** (`feat/ne-owned-state`): position and size live in `Node`; the library reads them from us; saving, undo and copy/paste use our fields.
2. **Link waypoints + link styles** (`feat/ne-waypoints`): add, drag and delete points; links pass through them; saved and undoable; style setting.
3. **Snapping + `showFlow` performance** (`feat/ne-flow-lod`): snapping configurable and off by default; flow animation hidden when zoomed far out and only for on-screen links; measure the improvement with a stress scene.
4. **Pruning** (`refactor/ne-prune`): remove what the audit marked; behaviour unchanged, tests green.

> **Prompt template:**
> ```
> /plan
> Read docs/ROADMAP.md, the rules file and docs/design/ui-decisions.md
> (node-editor section). Implement task <N>: "<title>" exactly as specified there.
> external/imgui-node-editor is vendored and already heavily modified; changing it is
> allowed. Don't start other tasks from the split. Add tests where the logic can run
> without a window; describe the manual checks for the rest.
> ```

**→ Release v0.14-alpha.**

---

### Step 7 — Auto-layout, then Generate

**7a. Algorithm design · Argon** → `docs/design/auto-layout-and-generate.md`
> ```
> /plan
> Read docs/ROADMAP.md, the v4 spec, docs/solver.md and docs/design/ui-decisions.md.
> Design (no code):
> (1) Auto-layout for production graphs: layered layout (Sugiyama-style: cycle handling,
>     layer assignment, crossing reduction, coordinates) that uses real node sizes and
>     creates link waypoints for clean routing. It must work on selections and whole graphs.
> (2) Generate: given available inputs (with limits) and desired outputs, choose recipes
>     and machine counts with an OR-Tools LP over v4 data. Cover the objective options
>     (minimize raw input / machines / power), alternate recipes, byproducts, and
>     infeasibility reporting. Then build the graph and place it with auto-layout.
> Include complexity notes and the test strategy.
> ```

**7b. Auto-arrange · Opus** (`feat/auto-layout`). **7c. Generate · Opus** (`feat/generate`). Both use the template: *"Implement part X of docs/design/auto-layout-and-generate.md."*

**→ Release v0.15-alpha.**

---

### Step 8 — Audits round 2 → v1.0-Beta backlog

- Re-run the step 2b bug-audit template over **all** code, including the new GameData, expression and node-editor code. Write to `docs/audits/bug-audit-v0.15.md`.
- Feature-suggestion audit (Flash): *"Compare Factory Planner (read docs/ROADMAP.md and the code) with Satisfactory Tools, Factorio's FactoryPlanner, Helmod and YAFC. List missing features that matter for planning, with value and effort. Write to docs/audits/feature-ideas.md."*
- **One premium task:** *"Read all files in docs/audits/. De-duplicate and rank them into a v1.0-Beta backlog in docs/design/beta-backlog.md, checked against the v1.0-Beta criteria in docs/ROADMAP.md."*

Work through the backlog, then **release v1.0-Beta**.

---

## 5. Premium quota budget (approximate)

| Step | Opus tasks | Argon tasks |
|---|---|---|
| 3 GameData v4 | 3–4 (design, core, solver, maybe review fixes) | 1 (review) |
| 4 gamedata_tool | 1 (design) | — |
| 5 UI/UX audit | — | 1 |
| 6 Node editor | 3–4 | 0–1 (review of the state-ownership PR) |
| 7 Layout + Generate | 2 | 1 |
| 8 Consolidation | — | 1 |
| **Total** | **~10–11** | **~4–5** |

At about 2 + 2 premium tasks per week, the premium work comes to roughly 5–6 weeks of quota. Everything else runs on Flash.
