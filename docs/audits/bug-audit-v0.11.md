# Bug Audit — v0.11

Findings for Roadmap Step 2b. Each entry: severity, location, problem, reproduction idea, status (`open` / `fix` / `won't fix` / `fixed`).

## Candidates noted during the Phase 0 review (not yet confirmed)

These came up while documenting the solver. They have **not** been reproduced yet. Step 2b run 4 (solver lens) must confirm or reject each one.

1. **Stale machine counts after a failed solve.** Severity: wrong result (UI). `src/core/graph/FactorySolver.cpp` (non-SUCCESS branch of `solve()`, ~L74-91): port and connection rates are reset to 0, but `Node::machine_count` keeps its value from the previous successful solve. Repro idea: solve a valid graph, then make it infeasible → nodes still show the old machine counts next to zero rates. Status: open.
2. **Raw-producer detection differs between classification and machine count.** Severity: wrong result (edge case). `classifyConstraint` treats a node as a raw source if *all* inputs are `"nothing"` (including nodes with *no* inputs), while `updateFactoryGraph` (~L868) requires *exactly one* `"nothing"` input. Repro idea: a recipe with zero inputs, or two `"nothing"` inputs → the constraint is a LIMIT, but the machine count goes through a different branch. Status: open (confirm the intended behaviour first).
3. **Constrained leaf ports are maximized again in Phase 2.** Severity: unclear, possibly intended. `buildThroughputTerms` (~L184-188) doesn't exclude ports with a user constraint. Check whether this can push a TARGET past the fair water-filling result of Phase 1 (it is upper-bounded by its constraint, so probably harmless). Status: open (verify intent).
4. **Quadratic lookup in Phase 2.** Severity: performance. `solveLexicographic` (~L715-718) linearly scans `m_portVariables` for every throughput term to find its port ID. That's O(N·M) on large graphs; a reverse map would avoid it. Status: open.
