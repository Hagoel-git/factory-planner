# Factory Solver

`FactorySolver` (`src/core/graph/FactorySolver.h`, `src/core/graph/FactorySolver.cpp`) computes the steady-state flow rates, machine counts, craft rates, and post-solve metrics of a factory graph by combining **GameData v4 expression evaluation** (`NodeEvaluator`) with a **Linear Program (LP)** solved per weakly connected component using Google OR-Tools' **GLOP** simplex solver.

This document describes the **v4 two-phase evaluation and 4-stage lexicographic LP formulation** implemented by `FactorySolver`: pre-solve expression evaluation, connected-component partitioning over active elements, port constraint modes (`AUTO`, `LIMIT`, `TARGET`, `PUSH`), the sink-dedicated and target-serving subgraphs, no-overflow rules, the 4-stage lexicographic optimization sequence, and post-solve metric evaluation.

---

## 1. Two-Phase Evaluation Model & Canonical Rate Units

Under GameData v4 (`docs/design/gamedata-v4-spec.md` §7), all user- and profile-defined formulas are functions of fixed parameters (`param.*`, `global.*`, `machine.*`, `recipe.*`, `port.*`, `metric.*`), never of the unknown LP flow variables. Evaluation is therefore split into three stages around a purely linear LP:

1. **Pre-Solve Evaluation (`NodeEvaluator::evaluateNode`):**
   - Resolves project-wide global parameters (`resolveProjectGlobals(pkg, graph.getGlobals())`).
   - For each node $n$, evaluates its assigned machine's `profile` against `node.params`, resolved globals, and custom attributes:
     - **Effective craft duration:** $t'_n = \text{eval.craft\_time\_s} > 0$ (in seconds).
     - **Effective port stoichiometry & activity:** For every recipe port (`in:i`, `out:i`) and machine extra port (`xin:<id>`, `xout:<id>`), evaluates whether the port is `active` (`when` predicate is true and evaluated amount $> 0$), its resolved `resource_key` (static `resource` or `resource_from_param`), and its effective amount per craft $a_p = \text{amount\_per\_craft}$.
   - **Error / Blocked Clamping:** If a node is unresolved (`node.is_unresolved`), lacks a valid recipe or machine, or its `NodeEval` status is `EVAL_ERROR` or `BLOCKED` (including $t'_n \le 0$ or no active ports with $a_p > 0$), the node is excluded from the LP and its ports, incident wires, `machine_count`, `crafts`, and metrics are clamped to `0`.
   - **Wire States (`Connection::state`):** Each connection $c = (p_{\text{out}} \to p_{\text{in}})$ is classified by `FactoryGraph::updateWireStates()`:
     - `WireState::OK`: Both endpoint nodes and ports are resolved and `active`, and both ports currently resolve to the same `resource_key`.
     - `WireState::INACTIVE`: At least one endpoint port is currently inactive (`!port.active`), while resources do not conflict. The wire remains in the project and graph, but is excluded from the LP ($F_c = 0$).
     - `WireState::INVALID`: Both endpoint ports are active, but their resolved `resource_key`s differ (e.g. after changing a `resource_from_param` selector), or an endpoint is unresolved. Excluded from the LP ($F_c = 0$).

2. **Canonical LP Rate Unit (`units.time_base`):**
   - Every solver variable ($R_p$, $F_c$, $E_p$) and every stored `Port::user_constraint` is expressed in **items per `units.time_base`** (`"s"`, i.e., items per second).
   - Project `rate_unit` (`"s"`, `"m"`, `"h"`) is a display/input conversion applied only at the UI and test helper boundary (`GameData::toDisplayRate` / `fromDisplayRate`). Changing `rate_unit` never mutates stored `user_constraint` values or re-triggers the LP.

---

## 2. Graph Elements & Connected Component Partitioning

Before building LP variables or resolving `AUTO` constraint modes, `FactorySolver::solve` refreshes pre-solve evaluations and partitions the active graph into **weakly connected components** $\mathcal{K}$ over the undirected projection of nodes and **`WireState::OK`** connections (including inactive/error nodes only for failure isolation bookkeeping, while only `EvalStatus::OK` nodes and active ports enter the LP constraints).

Each component $K = (\mathcal{N}_K, \mathcal{P}_K, \mathcal{W}_K) \in \mathcal{K}$ is analyzed and solved in its own independent `MPSolver` pass:
- **Active Nodes ($\mathcal{N}_K$):** Each active node $n \in \mathcal{N}_K$ runs a selected `Recipe` (`selected_recipe_key`) on a `Machine` (`machine_key`) with schema-defined parameters `node.params`. Its solved `machine_count` ($M_n$) and `crafts` ($\text{crafts}_n$) are written back after solving.
- **Active Ports ($\mathcal{P}_K = \mathcal{P}^{\text{in}}_K \cup \mathcal{P}^{\text{out}}_K$):** Input and output pins on active nodes in $\mathcal{N}_K$ for which `port.active && port.amount_per_craft > 0` (spanning both recipe ports `in:i`/`out:i` and active machine extra ports `xin:<id>`/`xout:<id>`). Inactive ports (`!port.active` or `amount_per_craft <= 0`) are clamped to $R_p = 0, E_p = 0$ and ignored by all structural sets and LP constraints.
  - Each port $p$ has a `slot_key`, a `user_constraint` ($-1$ when unconstrained, $\ge 0$ when active), a `constraint_mode` (`ConstraintMode::{AUTO, LIMIT, TARGET, PUSH}`), a solved `rate` $R_p$, and (for wired output ports) an unconsumed `excess_rate` $E_p$.
- **Active Connections ($\mathcal{W}_K$):** Directed edges $c = (p_{\text{out}} \to p_{\text{in}})$ in $K$ with `c.state == WireState::OK` between active ports of the same resource, each carrying a solved `rate` $F_c$.
- **Component Failure Isolation:** If one component $K \in \mathcal{K}$ fails to solve (`INFEASIBLE`, `UNBOUNDED`, or `ERROR`), only the nodes, ports, connections, and metrics belonging to $K$ are zeroed; all other valid components retain their solved rates, machine counts, and metrics.

### Structural Subsets within Component $K$

For each component $K$, let $W^+(p)$ and $W^-(p)$ denote the outgoing and incoming `WireState::OK` connections of active port $p$, and $\nu(p) \in \mathcal{N}_K$ the node owning $p$:

- **Raw Sources ($\mathcal{B}^{\text{raw}}_K$, Rule R1):** Active output ports of nodes that have **zero active input ports** ($\mathcal{P}^{\text{in}}(n) = \emptyset$) and at least one active output port ($\mathcal{P}^{\text{out}}(n) \neq \emptyset$). (In GameData v4, extraction recipes simply have `in = []`; there is no `"nothing"` sentinel resource.)
- **Unwired Imports ($\mathcal{B}^{\text{imp}}_K$):** Active input ports with no incoming `OK` connections ($W^-(p) = \emptyset$).
- **Exports ($\mathcal{X}_K$):** Active output ports with no outgoing `OK` connections ($W^+(p) = \emptyset$).
- **Sinks ($\mathcal{S}_K$, Rule R1):** Active nodes $n \in \mathcal{N}_K$ that have at least one active input port ($\mathcal{P}^{\text{in}}(n) \neq \emptyset$) and **zero active output ports** ($\mathcal{P}^{\text{out}}(n) = \emptyset$, e.g. disposal recipes with `out = []`).
- **Sink-Dedicated Subgraph ($\mathcal{S}^+_K$):** The set of active nodes $n \in \mathcal{N}_K$ that have at least one directed path (along `OK` wires) to $\mathcal{S}_K$, and for which **every** directed path starting at $n$ terminates at a sink in $\mathcal{S}_K$ without ever visiting an export port in $\mathcal{X}_K$, a pre-resolved active `TARGET` port ($g > 0$), or an explicit `PUSH` port. Computed once before Step 3 of `AUTO` resolution via backward reachability:
  1. $R_{\text{sink}} = \text{BackwardBFS}(\mathcal{S}_K)$
  2. $B = \{\nu(p) \mid p \in \mathcal{X}_K \cup G^{\text{pre}}_K \cup P^{\text{push,expl}}_K\} \cup (\mathcal{N}_K \setminus R_{\text{sink}})$
  3. $\mathcal{S}^+_K = R_{\text{sink}} \setminus \text{BackwardBFS}(B)$

---

## 3. Port Constraint Modes & `AUTO` Resolution

Every port $p$ carries a `ConstraintMode`:

| Mode | Semantics |
| :--- | :--- |
| **`AUTO`** | Default. Resolved per component $K$ into `LIMIT`, `TARGET`, or `PUSH` (see below). |
| **`LIMIT`** | Hard capacity ceiling: $R_p \le c$. Never actively pulls or pushes flow. |
| **`TARGET`** | Demand pull goal: enforces $R_p \le g_p$ and maximizes $R_p \to g_p$ in Stage 1 by pulling from upstream. |
| **`PUSH`** | Supply push goal:<li>**Rated (`user_constraint >= 0`):** enforces $R_p \le g_p$, maximizes $R_p \to g_p$ in Stage 1, and forces $E_q = 0$ on all downstream wired output ports $q \in \mathrm{Cl}(p)$.</li><li>**Unrated (`user_constraint < 0`):** imposes no numeric rate ceiling or Stage 1 goal, but forces $E_q = 0$ on downstream non-target-serving wired output ports $q \in \mathrm{Cl}_{\neg\mathcal{T}}(p)$.</li> |

### 3-Step `AUTO` Resolution within Component $K$

For any active port $p \in \mathcal{P}_K$ with `constraint_mode == ConstraintMode::AUTO`:

1. **Step 1 — Non-Boundary Ports:**
   - Wired input ports ($p \in \mathcal{P}^{\text{in}}_K \setminus \mathcal{B}^{\text{imp}}_K$) resolve to **`LIMIT`**.
   - Non-raw output ports ($p \in \mathcal{P}^{\text{out}}_K \setminus \mathcal{B}^{\text{raw}}_K$) resolve to **`TARGET`**.
2. **Step 2 — Sink-Dedicated Subgraph:** Compute $\mathcal{S}^+_K$ using the pre-resolved `TARGET` ($g > 0$) and explicit `PUSH` ports.
3. **Step 3 — Boundary Supply Ports ($p \in \mathcal{B}^{\text{raw}}_K \cup \mathcal{B}^{\text{imp}}_K$):**
   - **Demand-Driven Component** ($K$ has $\ge 1$ effective `TARGET` port with `user_constraint > 0`; a `TARGET` with `user_constraint == 0` is only a zero cap and does not switch $K$ into demand mode): every `AUTO` boundary port resolves to **`LIMIT`**.
   - **Supply-Driven Component** ($K$ has $0$ effective `TARGET`s):
     - *Auxiliary sink-supplier demotion:* If $\mathcal{N}_K \setminus \mathcal{S}^+_K \neq \emptyset$ and $\nu(p) \in \mathcal{S}^+_K$, $p$ is demoted to **`LIMIT`** if and only if every sink $s \in \mathcal{S}_K$ reachable from $p$ is reached exclusively through sink input port(s) that have no directed path from $\mathcal{N}_K \setminus \mathcal{S}^+_K$, while $s$ has at least one other input port that *does* have a directed path from $\mathcal{N}_K \setminus \mathcal{S}^+_K$.
     - All other `AUTO` boundary supply ports resolve to **`PUSH`**.

---

## 4. Variables & Hard Constraints

### LP Variables (per Component $K$)

All variables are non-negative ($\ge 0$) and expressed in items per `units.time_base` (`items/s`):

| Variable | Created for | Meaning |
| :--- | :--- | :--- |
| $R_p \ge 0$ | Every active port $p \in \mathcal{P}_K$ | Flow rate through port $p$ |
| $F_c \ge 0$ | Every `WireState::OK` connection $c \in \mathcal{W}_K$ | Flow rate along wire $c$ |
| $E_p \ge 0$ | Every wired active output port $p \in \mathcal{P}^{\text{out}}_K$ ($W^+(p) \neq \emptyset$) | Unconsumed production on port $p$ |
| $S \in [0, 1]$ | Stage 1 (when $G_K \neq \emptyset$) | Common water-filling saturation fraction |
| $hi^{\text{ns}}_p, lo^{\text{ns}}_p \ge 0$ | Every active output port $p$ with $\ge 2$ outgoing `OK` wires to $(\mathcal{N}_K \setminus \mathcal{S}^+_K) \setminus \mathcal{T}_K$ | Max and min branch flow across non-sink, non-target-serving outgoing wires of $p$ |
| $hi_p, lo_p \ge 0$ | Every active port $p$ with $\ge 2$ outgoing or $\ge 2$ incoming `OK` wires | Max and min branch flow across all `OK` wires of $p$ |

### Hard Constraints

1. **Pre-Evaluated Recipe & Extra-Port Stoichiometry:**
   For each active node $n \in \mathcal{N}_K$, let $p_{\text{ref}} = \text{ref}(n)$ be its **reference port**, chosen as the first active input port with $a_p > 0$, or if $n$ has no active input ports (a raw source), the first active output port with $a_p > 0$.
   - All active ports $p \in \mathcal{P}(n)$ (both recipe ports `in:i`/`out:i` and active machine extra ports `xin:<id>`/`xout:<id>`) are locked in exact linear proportion to $p_{\text{ref}}$ using their pre-evaluated amounts per craft $a_p = p.\text{amount\_per\_craft} > 0$, normalized by $c_{\max} = \max(a_{\text{ref}}, a_p)$ so every row's largest coefficient is $1$:
     $$\frac{a_{\text{ref}}}{c_{\max}} \cdot R_p - \frac{a_p}{c_{\max}} \cdot R_{\text{ref}} = 0 \qquad \forall p \in \mathcal{P}(n) \setminus \{p_{\text{ref}}\}$$
   - Because $a_p$ already incorporates any profile `input_amount` / `output_amount` scaling (such as `port.chance`, productivity bonuses, or fuel consumption per craft), the LP constraint is purely linear in $R_p$.
2. **User Rate Ceilings:** Every active port $p \in \mathcal{P}_K$ with `user_constraint >= 0` (regardless of whether its resolved mode is `LIMIT`, `TARGET`, or `PUSH`) enforces:
   $$R_p \le p.\text{user\_constraint}$$
3. **Wire Flow Conservation (over `WireState::OK` wires):**
   - Wired active output port ($W^+(p) \neq \emptyset$): $R_p - E_p - \sum_{c \in W^+(p)} F_c = 0$
   - Wired active input port ($W^-(p) \neq \emptyset$): $\sum_{c \in W^-(p)} F_c - R_p = 0$
4. **No-Overflow Rules ($E_p = 0$):**
   Let:
   - $G_K = \{p \in \mathcal{P}_K \mid p.\text{user\_constraint} > 0 \land \text{mode}(p) \in \{\text{TARGET}, \text{PUSH}\}\}$ be the **active goals** of $K$.
   - $\mathcal{T}_K = \text{BackwardBFS}(\{\nu(p) \mid p \in G_K \land \text{mode}(p) = \text{TARGET}\}) \setminus \mathcal{S}^+_K$ be the **target-serving subgraph** of $K$.
   - $Z_K = \{p \in \mathcal{P}^{\text{out}}_K \mid W^+(p) \neq \emptyset \land p \in \mathrm{Cl}(P^{\text{push,rated}}_K) \cup \mathrm{Cl}_{\neg\mathcal{T}}(P^{\text{push,unrated}}_K)\}$ be the **push-driven no-overflow port set**, where $\mathrm{Cl}$ is forward reachability over `OK` wires and active node input$\to$output links, and $\mathrm{Cl}_{\neg\mathcal{T}}$ restricts internal node traversal to $n \in \mathcal{N}_K \setminus \mathcal{T}_K$.

   Then $E_p$ is fixed to zero (`excess->SetUB(0.0)`) on every wired active output port $p$ satisfying:
   - **Rule 1:** $p \in Z_K$ (push-driven paths cannot dump flow into unconsumed wire excess), OR
   - **Rule 2:** $\nu(p) \notin \mathcal{T}_K$ (nodes outside the target-serving subgraph cannot generate unconsumed wire excess).

   Consequently, $E_p > 0$ is permitted **only** on target-serving nodes ($\nu(p) \in \mathcal{T}_K$) not subject to a push constraint ($p \notin Z_K$), allowing co-product/byproduct shedding when required to satisfy a `TARGET`.

---

## 5. The 4-Stage Lexicographic Solve

Each component $K$ is solved through a fixed sequence of LP stages.

### Numerical Conditioning, Tolerances & GLOP Stability

Because all factory stoichiometry, wire conservation, and branch-balance equations are homogeneous in flow rates while user constraints span many orders of magnitude (from $< 10^{-5}$ items/s up to $> 10^{9}$ W), `FactorySolver` conditions each component LP as follows:
- **Component Rate Scaling ($\sigma_K$):** Before creating variables for component $K$, `m_rateScale` ($\sigma_K$) is set to the geometric mean of all finite positive `user_constraint` values in $K$ (or $1$ if none). All LP flow variables solve in scaled coordinates $R'_p = R_p / \sigma_K$, $F'_c = F_c / \sigma_K$, $E'_p = E_p / \sigma_K$ with scaled user ceilings $c'_p = c_p / \sigma_K$, keeping typical variable magnitudes near $O(1)$ where GLOP's primal/dual feasibility tolerances ($10^{-8}$) and absolute residual check ($10^{-6}$) are well-conditioned. Post-solve values snap scaled simplex noise $|v'| < 10^{-13}$ to $0$, then unscale by $\sigma_K$ and snap unscaled $|v| < 10^{-9}$ to $0$.
- **GLOP Preprocessor Tolerance:** For `GLOP_LINEAR_PROGRAMMING`, `preprocessor_zero_tolerance` is set to $10^{-14}$ (tightened from GLOP's default $10^{-9}$). GLOP's default $10^{-9}$ relative bound-snapping tolerance conflicts with its absolute $10^{-6}$ postsolve residual check on `= 0` equality rows (`IMPRECISE` / `ABNORMAL`) and drops small matrix entries in wide-range constraints; $10^{-14}$ prevents both while preserving presolve speed.
- **Stage-Lock Tolerances & Row Normalization:** After each intermediate stage $k \in \{1, 2, \dots\}$ achieves optimum $z^\star$, `lockStage` locks that stage's objective within $k \cdot \tau(z^\star)$ where $\tau(z^\star) = \max(10^{-12}, 10^{-8} \cdot |z^\star|)$, normalizing multi-term lock rows by their maximum absolute coefficient. When minimizing a sum of strictly positive-weighted non-negative variables (such as Stage 2b excess or Stage 3b sink flow) achieves $z^\star \le 10^{-13}$ with all lower bounds at $0$, each variable in the stage is fixed directly to $0$ (`ub = 0`) so later stages cannot exploit primal feasibility slack to leak spurious excess after unscaling. Stage 1 bottlenecked goal lower bounds use the tighter $\tau_1(v) = \max(10^{-12}, 10^{-9} \cdot |v|)$ because Stage 2a minimizes directly against Stage 1 lower bounds.
- **Defensive `ABNORMAL` Recovery (`solveCurrentModel`):** If GLOP returns `MPSolver::ABNORMAL` (numerical imprecision) on any stage—never on genuine `INFEASIBLE` or `UNBOUNDED`—the solver retries from a fresh basis (`INCREMENTALITY_OFF`), and if still non-optimal, retries with `INCREMENTALITY_OFF` and `PRESOLVE_OFF`.

### Stage 1: Max-Min Water-Filling on Active Goals ($G_K$)

If $G_K \neq \emptyset$, let $g'_p = g_p / \sigma_K$ be each goal's scaled rate and $s_{\text{scale}}$ the geometric mean of $\{g'_p \mid p \in G_K\}$. Introduce $S \in [0, s_{\text{scale}}]$ representing $s_{\text{scale}}$ times the common saturation fraction, with row-normalized constraints $\frac{1}{m_p} R'_p - \frac{g'_p / s_{\text{scale}}}{m_p} S \ge 0$ ($m_p = \max(1, g'_p / s_{\text{scale}})$) for all $p \in G_K$, and iteratively maximize $S$ (recovering $S^\star = \mathrm{clamp}(S / s_{\text{scale}}, 0, 1)$):
- If $S^\star \ge 1 - 10^{-12}$ and $(1 - S^\star) g'_p \le 10^{-12}$ for every remaining $p \in G_{\text{active}}$, all remaining goals are locked at $R'_p \ge g'_p$ and the loop terminates.
- If $|G_{\text{active}}| = 1$, the single remaining goal is locked at $R'_p \ge g'_p S^\star - \tau_1(g'_p S^\star)$ and the loop terminates.
- Otherwise, $S$'s lower bound is temporarily raised to $(S^\star - 10^{-9}) \cdot s_{\text{scale}}$ (without tightening its upper bound, since $S^\star$ is already the LP maximum) and a **Hybrid Filter** identifies bottlenecked goals using relative growth threshold $\varepsilon_p = \max(10^{-12}, 10^{-6} \cdot g'_p)$:
  1. **Sum solve:** Maximize $\sum_{p \in G_{\text{active}}} R'_p$. Any goal with $R'_p > g'_p S^\star + \varepsilon_p$ has headroom and stays active.
  2. **Suspect verification:** Each goal that did not grow in the sum solve is maximized individually. If it still cannot exceed $g'_p S^\star + \varepsilon_p$, it is a genuine bottleneck: its lower bound is locked at $g'_p S^\star - \tau_1(g'_p S^\star)$ and its $S$-constraint is decoupled.
- When all goals are locked, all $S$-constraints are decoupled so $S$ is inert in Stages 2–4.

### Stage 2: Upstream Conservation & Overflow Routing

- **Stage 2a — Minimal Target-Serving Machine Activity:**
  $$\min \sum_{n \in \mathcal{T}_K} w_n \cdot R_{\text{ref}(n)}$$
  where $\text{ref}(n)$ is the node's reference port (first active input port with $a_p > 0$, or first active output port with $a_p > 0$ for raw sources), and $w_n$ is the machine-count weight per unit reference flow using the pre-evaluated effective craft duration $t'_n = \text{eval.craft\_time\_s}$:
  $$w_n = \frac{t'_n}{a_{\text{ref}(n)}}$$
  Note that $w_n \cdot R_{\text{ref}(n)} = M_n$ is the exact solved machine count of node $n$. Only the total weighted sum is locked via `lockStage`, preserving freedom for Stage 4 to balance parallel equal-efficiency branches.
- **Stage 2b — Maximal Wired Consumption (Minimal Unconsumed Excess):**
  $$\min \sum_{p \in \mathcal{P}^{\text{out}}_K,\, W^+(p) \neq \emptyset} E_p$$
  Forces all produced flow on wired output ports into connected downstream wires (including sinks) up to their capacity before leaving any excess $E_p > 0$.
- **Stage 2c — Non-Sink Routing Priority & Fair Splitting:**
  - **Stage 2c.1 (Mixed-Splitter Sink-Branch Minimization):** For every mixed splitter port $p$ (having both sink-dedicated outgoing wires $c \in W^+_{\text{sink}}(p)$ to $\mathcal{S}^+_K$ and non-sink outgoing wires $c \in W^+_{\text{ns}}(p)$ to $\mathcal{N}_K \setminus \mathcal{S}^+_K$), minimize total flow into the sink-dedicated branches:
    $$\min \sum_{c \in W^{\text{mix-sink}}_K} F_c$$
  - **Stage 2c.2 (Non-Sink Splitter Fairness):** For every output port $p$ with $\ge 2$ outgoing wires $W^+_{\text{ns}\setminus\mathcal{T}}(p)$ to non-sink nodes outside $\mathcal{T}_K$ ($(\mathcal{N}_K \setminus \mathcal{S}^+_K) \setminus \mathcal{T}_K$), minimize the spread across those branches:
    $$\min \sum_{p:\, |W^+_{\text{ns}\setminus\mathcal{T}}(p)| \ge 2} \left(hi^{\text{ns}}_p - lo^{\text{ns}}_p\right)$$

### Stage 3: Residual & Sink Subgraph Minimization

- **Stage 3a — Minimal Remaining Non-Sink Machine Activity:**
  $$\min \sum_{n \in \mathcal{N}_K \setminus (\mathcal{T}_K \cup \mathcal{S}^+_K)} w_n \cdot R_{\text{ref}(n)}$$
  Drives unconstrained nodes, unused branches, and unconstrained amplifying cycles to zero while respecting all locked upstream pushes and targets.
- **Stage 3b — Minimal Sink-Dedicated Port Flow:**
  $$\min \sum_{p \in \mathcal{P}_K:\, \nu(p) \in \mathcal{S}^+_K} R_p$$
  Ensures sink-dedicated subgraphs run only as much as forced by Stage 1 goals or Stage 2b overflow absorption.

### Stage 4: Terminal Fair Branch Splitting

$$\min \sum_{p:\, |W^+(p)| \ge 2 \text{ or } |W^-(p)| \ge 2} \left(hi_p - lo_p\right)$$
Balances any remaining degrees of freedom across all splitters and mergers (including equal-efficiency upstream suppliers and parallel sink lines).

---

## 6. Post-Processing, Post-Solve Metrics & Diagnostics

After solving each component $K$:
1. **Zero Snapping & Flow Write-Back:**
   - Any solved value with scaled magnitude $|v'| < 10^{-13}$ or unscaled magnitude $|v| < 10^{-9}$ is snapped to `0.0`.
   - Unscaled port rates $R_p = \sigma_K R'_p$, wire rates $F_c = \sigma_K F'_c$, and unconsumed excess rates $E_p = \sigma_K E'_p$ (all in items/s) are written back to `FactoryGraph`.
2. **Craft Rate & Machine Count (§7.2):**
   - For each active node $n \in \mathcal{N}_K$ with reference port $p_{\text{ref}}$ ($a_{\text{ref}} > 0$) and effective craft time $t'_n > 0$:
     $$\text{crafts}_n = \frac{R_{p_{\text{ref}}}}{a_{\text{ref}}}, \qquad M_n = \text{crafts}_n \cdot t'_n = w_n \cdot R_{p_{\text{ref}}}$$
   - Written to `node.crafts` and `node.machine_count`.
3. **Post-Solve Metrics & Graph Totals (§7.3):**
   - For each active node $n \in \mathcal{N}_K$, `NodeEvaluator::evaluateNodeMetrics` evaluates every metric in `pkg.metrics` in declaration order using the solved `solved.machines` ($M_n$) and `solved.crafts` ($\text{crafts}_n$), supporting `"per_machine"`, `"per_craft"`, and `"value"` metric kinds as well as inter-metric references `metric.<earlier_id>`.
   - After all components finish, `NodeEvaluator::aggregateMetricTotals` aggregates the per-node metrics across all solved nodes in the graph according to each metric's `total` rule (`"sum"` with optional `split_sign: true` for `positive`/`negative`/`net`, `"max"`, `"min"`, or `"none"`).
   - Exposed on `FactorySolver` via `getNodeMetrics(node_id)`, `getAllNodeMetrics()`, and `getTotals()`.
4. **Diagnostics:**
   - Diagnostic getters on `FactorySolver` expose the per-port resolved `ConstraintMode` (`getResolvedConstraintMode`) and per-goal saturation ratio $R_p / g_p \in [0, 1]$ (`getGoalSaturation`, `isGoalBottlenecked`).
