#include "FactorySolver.h"
#include "FactoryGraph.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <absl/log/globals.h>

#include "services/NotificationManager.h"

namespace {
    using operations_research::MPConstraint;
    using operations_research::MPSolver;
    using operations_research::MPVariable;

    // Values with a smaller magnitude are reported as exactly zero.
    constexpr double kZeroEpsilon = 1e-9;

    // Slack allowed when locking in the optimum of a stage, so that numerical noise does not render later
    // stages infeasible.
    double lockTolerance(const double value) {
        return 1e-9 * std::max(1.0, std::abs(value));
    }

    double cleanValue(const double value) {
        return std::abs(value) < kZeroEpsilon ? 0.0 : value;
    }

    int statusSeverity(const FactorySolver::SolverResultStatus status) {
        switch (status) {
            case FactorySolver::SolverResultStatus::SUCCESS: return 0;
            case FactorySolver::SolverResultStatus::UNBOUNDED: return 1;
            case FactorySolver::SolverResultStatus::INFEASIBLE: return 2;
            case FactorySolver::SolverResultStatus::ERROR_: return 3;
            default: return 3;
        }
    }
}

FactorySolver::FactorySolver(const std::string &solver_name) {
    if (!operations_research::MPSolver::ParseSolverType(solver_name, &m_problemType)) {
        throw std::invalid_argument("Unknown solver type: " + solver_name);
    }

    if (!operations_research::MPSolver::SupportsProblemType(m_problemType)) {
        throw std::runtime_error("Problem type not supported for solver: " + solver_name);
    }

    m_solver = std::make_unique<operations_research::MPSolver>("FactorySolver", m_problemType);
}

void FactorySolver::clearComponentSolverState() {
    m_portVariables.clear();
    m_connectionVariables.clear();
    m_excessVariables.clear();
    m_nonSinkBalanceVariables.clear();
    m_balanceVariables.clear();
    m_constraints.clear();
    m_solution.clear();
    m_solver->Clear();
}

std::vector<FactorySolver::ComponentData> FactorySolver::partitionComponents(const FactoryGraph &factory_graph) {
    std::vector<ComponentData> components;
    const auto &nodes = factory_graph.getNodes();
    if (nodes.empty()) {
        return components;
    }

    std::vector<uint64_t> all_node_ids;
    all_node_ids.reserve(nodes.size());
    std::unordered_set<uint64_t> valid_nodes;
    valid_nodes.reserve(nodes.size());
    for (const auto &node : nodes) {
        all_node_ids.push_back(node.id);
        valid_nodes.insert(node.id);
    }
    std::sort(all_node_ids.begin(), all_node_ids.end());

    std::unordered_map<uint64_t, std::vector<uint64_t>> undir_adj;
    for (uint64_t nid : all_node_ids) {
        undir_adj[nid] = {};
    }

    for (const auto &conn : factory_graph.getConnections()) {
        const Port *from_port = factory_graph.getPort(conn.from_port);
        const Port *to_port = factory_graph.getPort(conn.to_port);
        if (!from_port || !to_port) continue;
        uint64_t u = from_port->node_id;
        uint64_t v = to_port->node_id;
        if (!valid_nodes.count(u) || !valid_nodes.count(v)) continue;
        if (u != v) {
            undir_adj[u].push_back(v);
            undir_adj[v].push_back(u);
        }
    }

    std::unordered_set<uint64_t> visited;
    visited.reserve(nodes.size());

    for (uint64_t root : all_node_ids) {
        if (visited.count(root)) continue;

        ComponentData comp;
        std::queue<uint64_t> q;
        visited.insert(root);
        q.push(root);

        while (!q.empty()) {
            uint64_t u = q.front();
            q.pop();
            comp.nodes.push_back(u);
            comp.node_set.insert(u);

            for (uint64_t v : undir_adj[u]) {
                if (!visited.count(v)) {
                    visited.insert(v);
                    q.push(v);
                }
            }
        }

        std::sort(comp.nodes.begin(), comp.nodes.end());

        for (uint64_t nid : comp.nodes) {
            const Node *node = factory_graph.getNode(nid);
            if (!node) continue;
            for (uint64_t pid : node->input_ports) {
                if (!factory_graph.getPort(pid)) continue;
                comp.input_ports.insert(pid);
                if (comp.port_set.insert(pid).second) {
                    comp.ports.push_back(pid);
                }
            }
            for (uint64_t pid : node->output_ports) {
                if (!factory_graph.getPort(pid)) continue;
                comp.output_ports.insert(pid);
                if (comp.port_set.insert(pid).second) {
                    comp.ports.push_back(pid);
                }
            }
        }
        std::sort(comp.ports.begin(), comp.ports.end());

        components.push_back(std::move(comp));
    }

    // Assign connections to their component
    for (const auto &conn : factory_graph.getConnections()) {
        for (auto &comp : components) {
            if (comp.port_set.count(conn.from_port) && comp.port_set.count(conn.to_port)) {
                comp.connections.push_back(conn.id);
                comp.port_outgoing[conn.from_port].push_back(conn.id);
                comp.port_incoming[conn.to_port].push_back(conn.id);
                break;
            }
        }
    }

    for (auto &comp : components) {
        std::sort(comp.connections.begin(), comp.connections.end());
        for (auto &[pid, cids] : comp.port_outgoing) {
            std::sort(cids.begin(), cids.end());
        }
        for (auto &[pid, cids] : comp.port_incoming) {
            std::sort(cids.begin(), cids.end());
        }
    }

    return components;
}

void FactorySolver::analyzeComponent(const FactoryGraph &factory_graph, ComponentData &comp) {
    // Directed node adjacency within K
    std::unordered_map<uint64_t, std::vector<uint64_t>> node_rev;
    for (uint64_t nid : comp.nodes) {
        node_rev[nid] = {};
    }
    for (uint64_t cid : comp.connections) {
        const Connection *conn = factory_graph.getConnection(cid);
        if (!conn) continue;
        const Port *from_p = factory_graph.getPort(conn->from_port);
        const Port *to_p = factory_graph.getPort(conn->to_port);
        if (!from_p || !to_p) continue;
        if (from_p->node_id != to_p->node_id) {
            node_rev[to_p->node_id].push_back(from_p->node_id);
        }
    }

    auto backwardBFS = [&](const std::unordered_set<uint64_t> &seeds) {
        std::unordered_set<uint64_t> visited = seeds;
        std::queue<uint64_t> q;
        for (uint64_t s : seeds) {
            q.push(s);
        }
        while (!q.empty()) {
            uint64_t curr = q.front();
            q.pop();
            auto it = node_rev.find(curr);
            if (it == node_rev.end()) continue;
            for (uint64_t pred : it->second) {
                if (visited.insert(pred).second) {
                    q.push(pred);
                }
            }
        }
        return visited;
    };

    // Identify B_K^raw, B_K^imp, X_K, S_K
    for (uint64_t nid : comp.nodes) {
        const Node *node = factory_graph.getNode(nid);
        if (!node) continue;

        const bool is_raw_node = std::all_of(
            node->input_ports.begin(), node->input_ports.end(),
            [&](const uint64_t pid) {
                const Port *p = factory_graph.getPort(pid);
                return !p || p->resource_key == "nothing";
            });
        if (is_raw_node) {
            for (uint64_t pid : node->output_ports) {
                comp.raw_ports.insert(pid);
            }
        }

        const bool is_sink_node = std::all_of(
            node->output_ports.begin(), node->output_ports.end(),
            [&](const uint64_t pid) {
                const Port *p = factory_graph.getPort(pid);
                return !p || p->resource_key == "nothing";
            });
        if (is_sink_node) {
            comp.sink_nodes.insert(nid);
        }
    }

    for (uint64_t pid : comp.input_ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p || p->resource_key == "nothing") continue;
        if (comp.port_incoming.find(pid) == comp.port_incoming.end()) {
            comp.imp_ports.insert(pid);
        }
    }

    for (uint64_t pid : comp.output_ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p || p->resource_key == "nothing") continue;
        if (comp.port_outgoing.find(pid) == comp.port_outgoing.end()) {
            comp.export_ports.insert(pid);
        }
    }

    // Step 1 of AUTO Resolution: Non-boundary ports & explicit modes
    for (uint64_t pid : comp.ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p) continue;
        if (p->constraint_mode != ConstraintMode::AUTO) {
            comp.resolved_modes[pid] = p->constraint_mode;
        } else if (comp.input_ports.count(pid) && !comp.imp_ports.count(pid)) {
            comp.resolved_modes[pid] = ConstraintMode::LIMIT;
        } else if (comp.output_ports.count(pid) && !comp.raw_ports.count(pid)) {
            comp.resolved_modes[pid] = ConstraintMode::TARGET;
        }
    }

    // Step 2 of AUTO Resolution: Compute S+_K
    const std::unordered_set<uint64_t> R_sink = backwardBFS(comp.sink_nodes);
    std::unordered_set<uint64_t> B_seeds;
    for (uint64_t pid : comp.export_ports) {
        if (const Port *p = factory_graph.getPort(pid)) {
            B_seeds.insert(p->node_id);
        }
    }
    for (uint64_t pid : comp.ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p) continue;
        auto mode_it = comp.resolved_modes.find(pid);
        if (p->user_constraint > 0.0 && mode_it != comp.resolved_modes.end() &&
            mode_it->second == ConstraintMode::TARGET) {
            B_seeds.insert(p->node_id);
        }
        if (p->constraint_mode == ConstraintMode::PUSH &&
            (p->user_constraint > 0.0 || p->user_constraint < 0.0)) {
            B_seeds.insert(p->node_id);
        }
    }
    for (uint64_t nid : comp.nodes) {
        if (!R_sink.count(nid)) {
            B_seeds.insert(nid);
        }
    }
    const std::unordered_set<uint64_t> rev_B = backwardBFS(B_seeds);
    for (uint64_t nid : R_sink) {
        if (!rev_B.count(nid)) {
            comp.sink_dedicated_nodes.insert(nid);
        }
    }

    // Forward port BFS helper for Step 3 and Z_K
    auto forwardPortBFS = [&](const std::vector<uint64_t> &seed_ports,
                              const bool restrict_to_non_target_serving) {
        std::unordered_set<uint64_t> visited;
        std::queue<uint64_t> q;
        for (uint64_t sp : seed_ports) {
            if (visited.insert(sp).second) {
                q.push(sp);
            }
        }
        while (!q.empty()) {
            uint64_t curr_p = q.front();
            q.pop();

            if (comp.output_ports.count(curr_p)) {
                auto out_it = comp.port_outgoing.find(curr_p);
                if (out_it != comp.port_outgoing.end()) {
                    for (uint64_t cid : out_it->second) {
                        const Connection *conn = factory_graph.getConnection(cid);
                        if (conn && visited.insert(conn->to_port).second) {
                            q.push(conn->to_port);
                        }
                    }
                }
            } else if (comp.input_ports.count(curr_p)) {
                const Port *p = factory_graph.getPort(curr_p);
                if (!p) continue;
                if (restrict_to_non_target_serving && comp.target_serving_nodes.count(p->node_id)) {
                    continue;
                }
                const Node *node = factory_graph.getNode(p->node_id);
                if (!node) continue;
                for (uint64_t out_pid : node->output_ports) {
                    if (visited.insert(out_pid).second) {
                        q.push(out_pid);
                    }
                }
            }
        }
        return visited;
    };

    // Step 3 of AUTO Resolution: Boundary supply ports (B_K^raw U B_K^imp)
    bool has_effective_targets = false;
    for (uint64_t pid : comp.ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p || p->user_constraint <= 0.0) continue;
        auto mode_it = comp.resolved_modes.find(pid);
        if (mode_it != comp.resolved_modes.end() && mode_it->second == ConstraintMode::TARGET) {
            has_effective_targets = true;
            break;
        }
    }

    if (has_effective_targets) {
        for (uint64_t pid : comp.ports) {
            const Port *p = factory_graph.getPort(pid);
            if (!p || p->constraint_mode != ConstraintMode::AUTO) continue;
            if (comp.raw_ports.count(pid) || comp.imp_ports.count(pid)) {
                comp.resolved_modes[pid] = ConstraintMode::LIMIT;
            }
        }
    } else {
        // Precompute ports reachable from non-sink-dedicated nodes (N_K \ S+_K)
        std::vector<uint64_t> non_sink_seed_ports;
        for (uint64_t nid : comp.nodes) {
            if (comp.sink_dedicated_nodes.count(nid)) continue;
            const Node *node = factory_graph.getNode(nid);
            if (!node) continue;
            for (uint64_t out_pid : node->output_ports) {
                non_sink_seed_ports.push_back(out_pid);
            }
        }
        const std::unordered_set<uint64_t> ports_from_non_sink =
            forwardPortBFS(non_sink_seed_ports, /*restrict_to_non_target_serving=*/false);

        const bool has_non_sink_nodes = comp.sink_dedicated_nodes.size() < comp.nodes.size();

        for (uint64_t pid : comp.ports) {
            const Port *p = factory_graph.getPort(pid);
            if (!p || p->constraint_mode != ConstraintMode::AUTO) continue;
            if (!comp.raw_ports.count(pid) && !comp.imp_ports.count(pid)) continue;

            bool demote_to_limit = false;
            if (has_non_sink_nodes && comp.sink_dedicated_nodes.count(p->node_id)) {
                const std::unordered_set<uint64_t> reached_from_p =
                    forwardPortBFS({pid}, /*restrict_to_non_target_serving=*/false);

                bool reached_any_sink = false;
                bool all_reached_sinks_qualify = true;

                for (uint64_t sink_nid : comp.sink_nodes) {
                    const Node *sink_node = factory_graph.getNode(sink_nid);
                    if (!sink_node) continue;

                    bool sink_reached = false;
                    bool reached_port_overlaps_main = false;
                    bool sink_has_other_main_port = false;

                    for (uint64_t in_pid : sink_node->input_ports) {
                        const Port *in_p = factory_graph.getPort(in_pid);
                        if (!in_p || in_p->resource_key == "nothing") continue;

                        const bool from_p = reached_from_p.count(in_pid) > 0;
                        const bool from_main = ports_from_non_sink.count(in_pid) > 0;

                        if (from_p) {
                            sink_reached = true;
                            if (from_main) {
                                reached_port_overlaps_main = true;
                            }
                        } else if (from_main) {
                            sink_has_other_main_port = true;
                        }
                    }

                    if (sink_reached) {
                        reached_any_sink = true;
                        if (reached_port_overlaps_main || !sink_has_other_main_port) {
                            all_reached_sinks_qualify = false;
                            break;
                        }
                    }
                }

                if (reached_any_sink && all_reached_sinks_qualify) {
                    demote_to_limit = true;
                }
            }

            comp.resolved_modes[pid] = demote_to_limit ? ConstraintMode::LIMIT : ConstraintMode::PUSH;
        }
    }

    // Active goals G_K
    for (uint64_t pid : comp.ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p || p->user_constraint <= 0.0) continue;
        const ConstraintMode mode = comp.resolved_modes[pid];
        if (mode == ConstraintMode::TARGET || mode == ConstraintMode::PUSH) {
            comp.active_goals.push_back(pid);
        }
    }

    // Target-serving subgraph T_K = BackwardBFS({v(p) | p in G_K and mode(p) == TARGET}) \ S+_K
    std::unordered_set<uint64_t> target_node_seeds;
    for (uint64_t pid : comp.active_goals) {
        if (comp.resolved_modes[pid] == ConstraintMode::TARGET) {
            if (const Port *p = factory_graph.getPort(pid)) {
                target_node_seeds.insert(p->node_id);
            }
        }
    }
    const std::unordered_set<uint64_t> rev_targets = backwardBFS(target_node_seeds);
    for (uint64_t nid : rev_targets) {
        if (!comp.sink_dedicated_nodes.count(nid)) {
            comp.target_serving_nodes.insert(nid);
        }
    }

    // Push-driven no-overflow port set Z_K
    std::vector<uint64_t> rated_push_seeds;
    std::vector<uint64_t> unrated_push_seeds;
    for (uint64_t pid : comp.ports) {
        const Port *p = factory_graph.getPort(pid);
        if (!p) continue;
        if (comp.resolved_modes[pid] == ConstraintMode::PUSH && p->user_constraint > 0.0) {
            rated_push_seeds.push_back(pid);
        } else if (p->constraint_mode == ConstraintMode::PUSH && p->user_constraint < 0.0) {
            unrated_push_seeds.push_back(pid);
        }
    }

    const std::unordered_set<uint64_t> cl_rated =
        forwardPortBFS(rated_push_seeds, /*restrict_to_non_target_serving=*/false);
    const std::unordered_set<uint64_t> cl_unrated =
        forwardPortBFS(unrated_push_seeds, /*restrict_to_non_target_serving=*/true);

    for (uint64_t pid : comp.output_ports) {
        if (comp.port_outgoing.find(pid) == comp.port_outgoing.end()) continue;
        if (cl_rated.count(pid) || cl_unrated.count(pid)) {
            comp.no_overflow_ports.insert(pid);
        }
    }
}

std::unordered_map<uint64_t, ConstraintMode> FactorySolver::resolveConstraintModes(
    const FactoryGraph &factory_graph) {
    std::unordered_map<uint64_t, ConstraintMode> result;
    auto components = partitionComponents(factory_graph);
    for (auto &comp : components) {
        analyzeComponent(factory_graph, comp);
        for (const auto &[pid, mode] : comp.resolved_modes) {
            result[pid] = mode;
        }
    }
    return result;
}

FactorySolver::ConstraintKind FactorySolver::classifyConstraint(
    const FactoryGraph &factory_graph, const Port &port) {
    const auto modes = resolveConstraintModes(factory_graph);
    auto it = modes.find(port.id);
    if (it != modes.end()) {
        switch (it->second) {
            case ConstraintMode::LIMIT: return ConstraintKind::LIMIT;
            case ConstraintMode::TARGET: return ConstraintKind::TARGET;
            case ConstraintMode::PUSH: return ConstraintKind::PUSH;
            case ConstraintMode::AUTO: break;
        }
    }
    if (factory_graph.isInputPort(port.id)) {
        return ConstraintKind::LIMIT;
    }
    const Node *node = factory_graph.getNode(port.node_id);
    if (!node) {
        return ConstraintKind::TARGET;
    }
    const bool is_raw_source = std::all_of(
        node->input_ports.begin(), node->input_ports.end(),
        [&](const uint64_t input_id) {
            const Port *input = factory_graph.getPort(input_id);
            return input && input->resource_key == "nothing";
        });
    return is_raw_source ? ConstraintKind::LIMIT : ConstraintKind::TARGET;
}

ConstraintMode FactorySolver::getResolvedConstraintMode(const uint64_t port_id) const {
    auto it = m_resolvedModes.find(port_id);
    return it != m_resolvedModes.end() ? it->second : ConstraintMode::AUTO;
}

double FactorySolver::getGoalSaturation(const uint64_t port_id) const {
    auto it = m_goalSaturations.find(port_id);
    return it != m_goalSaturations.end() ? it->second : -1.0;
}

bool FactorySolver::isGoalBottlenecked(const uint64_t port_id) const {
    auto it = m_goalSaturations.find(port_id);
    return it != m_goalSaturations.end() && it->second < 1.0 - 1e-6;
}

FactorySolver::SolverResult FactorySolver::solve(FactoryGraph &factory_graph) {
    clearComponentSolverState();
    m_resolvedModes.clear();
    m_goalSaturations.clear();

    absl::Time t_start = absl::Now();
    double setup_ms = 0.0;
    double solve_ms = 0.0;
    double update_ms = 0.0;

    try {
        absl::Time t_part_start = absl::Now();
        auto components = partitionComponents(factory_graph);

        // Zero any orphan ports/connections not attached to valid nodes
        std::unordered_set<uint64_t> covered_ports;
        std::unordered_set<uint64_t> covered_conns;
        for (auto &comp : components) {
            analyzeComponent(factory_graph, comp);
            for (const auto &[pid, mode] : comp.resolved_modes) {
                m_resolvedModes[pid] = mode;
            }
            covered_ports.insert(comp.ports.begin(), comp.ports.end());
            covered_conns.insert(comp.connections.begin(), comp.connections.end());
        }
        setup_ms += absl::ToDoubleMilliseconds(absl::Now() - t_part_start);

        absl::Time t_orphan_start = absl::Now();
        for (const auto &port : factory_graph.getPorts()) {
            if (!covered_ports.count(port.id)) {
                if (Port *gp = factory_graph.getPort(port.id)) {
                    gp->rate = 0.0;
                    gp->excess_rate = 0.0;
                }
            }
        }
        for (const auto &conn : factory_graph.getConnections()) {
            if (!covered_conns.count(conn.id)) {
                if (Connection *gc = factory_graph.getConnection(conn.id)) {
                    gc->rate = 0.0;
                }
            }
        }
        update_ms += absl::ToDoubleMilliseconds(absl::Now() - t_orphan_start);

        SolverResultStatus overall_result = SolverResultStatus::SUCCESS;
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);

        for (auto &comp : components) {
            absl::Time t_comp_setup_start = absl::Now();
            clearComponentSolverState();

            bool invalid_constraint = false;
            for (uint64_t pid : comp.ports) {
                const Port *p = factory_graph.getPort(pid);
                if (p && !std::isfinite(p->user_constraint)) {
                    invalid_constraint = true;
                    break;
                }
            }

            if (!invalid_constraint) {
                createComponentVariables(factory_graph, comp);
                addComponentConstraints(factory_graph, comp);
            }
            absl::Time t_comp_setup_end = absl::Now();
            setup_ms += absl::ToDoubleMilliseconds(t_comp_setup_end - t_comp_setup_start);

            MPSolver::ResultStatus comp_mp_status = MPSolver::ABNORMAL;
            if (!invalid_constraint) {
                comp_mp_status = solveComponentLexicographic(factory_graph, comp);
            }
            absl::Time t_comp_solve_end = absl::Now();
            solve_ms += absl::ToDoubleMilliseconds(t_comp_solve_end - t_comp_setup_end);

            const SolverResultStatus comp_result = convertSolverStatus(comp_mp_status);
            if (comp_result == SolverResultStatus::SUCCESS) {
                updateComponentGraph(factory_graph, comp);
            } else {
                zeroComponentGraph(factory_graph, comp);
                if (statusSeverity(comp_result) > statusSeverity(overall_result)) {
                    overall_result = comp_result;
                }
                LOG(ERROR) << "Component solver failed with status: " << toString(comp_result);
            }
            absl::Time t_comp_update_end = absl::Now();
            update_ms += absl::ToDoubleMilliseconds(t_comp_update_end - t_comp_solve_end);
        }

        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

        m_lastSolverStatus = toString(overall_result);
        const double total_ms = absl::ToDoubleMilliseconds(absl::Now() - t_start);
        m_lastSolveTime = total_ms;

        if (overall_result == SolverResultStatus::SUCCESS) {
            VLOG(1) << "Solver finished in " << total_ms << "ms";
        } else {
            LOG(WARNING) << "Solver failed to find optimal solution. Status: " << m_lastSolverStatus;
        }

        return {
            overall_result,
            total_ms,
            setup_ms,
            solve_ms,
            update_ms
        };
    } catch (const std::exception &e) {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(ERROR) << "Exception during solving: " << e.what();
        NotificationManager::instance().addNotification(
            "Solver Error",
            "An error occurred during solving: " + std::string(e.what()),
            NotificationType::Error
        );
        for (const auto &port : factory_graph.getPorts()) {
            if (Port *graph_port = factory_graph.getPort(port.id)) {
                graph_port->rate = 0.0;
                graph_port->excess_rate = 0.0;
            }
        }
        for (const auto &conn : factory_graph.getConnections()) {
            if (Connection *graph_conn = factory_graph.getConnection(conn.id)) {
                graph_conn->rate = 0.0;
            }
        }
        for (const auto &node : factory_graph.getNodes()) {
            if (Node *graph_node = factory_graph.getNode(node.id)) {
                graph_node->machine_count = 0.0;
            }
        }
        m_lastSolverStatus = toString(SolverResultStatus::ERROR_);
        const double total_ms = absl::ToDoubleMilliseconds(absl::Now() - t_start);
        m_lastSolveTime = total_ms;
        return {
            SolverResultStatus::ERROR_,
            total_ms,
            setup_ms,
            solve_ms,
            update_ms
        };
    }
}

void FactorySolver::createComponentVariables(const FactoryGraph &factory_graph, const ComponentData &comp) {
    m_portVariables.reserve(comp.ports.size());
    m_connectionVariables.reserve(comp.connections.size());

    for (uint64_t pid : comp.ports) {
        if (!factory_graph.getPort(pid)) continue;
        const std::string var_name = "Port_" + std::to_string(pid);
        MPVariable *var = m_solver->MakeNumVar(0.0, MPSolver::infinity(), var_name);
        m_portVariables[pid] = var;
    }

    for (uint64_t cid : comp.connections) {
        if (!factory_graph.getConnection(cid)) continue;
        const std::string var_name = "Conn_" + std::to_string(cid);
        MPVariable *var = m_solver->MakeNumVar(0.0, MPSolver::infinity(), var_name);
        m_connectionVariables[cid] = var;
    }
}

void FactorySolver::addComponentConstraints(const FactoryGraph &factory_graph, ComponentData &comp) {
    for (uint64_t nid : comp.nodes) {
        const Node *node = factory_graph.getNode(nid);
        if (!node) continue;
        auto recipe_it = factory_graph.getGameData().recipes.find(node->selected_recipe_key);
        if (recipe_it == factory_graph.getGameData().recipes.end()) {
            // Missing recipe: clamp all ports on this node to 0
            for (uint64_t pid : node->input_ports) {
                if (auto it = m_portVariables.find(pid); it != m_portVariables.end()) {
                    it->second->SetUB(0.0);
                    auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                    c->SetCoefficient(it->second, 1.0);
                    m_constraints.push_back(c);
                }
            }
            for (uint64_t pid : node->output_ports) {
                if (auto it = m_portVariables.find(pid); it != m_portVariables.end()) {
                    it->second->SetUB(0.0);
                    auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                    c->SetCoefficient(it->second, 1.0);
                    m_constraints.push_back(c);
                }
            }
            continue;
        }
        addRecipeConstraints(*node, recipe_it->second);
    }

    // Add user-defined capacity/goal ceilings (all active numeric constraints enforce R_p <= c)
    for (uint64_t pid : comp.ports) {
        const Port *port = factory_graph.getPort(pid);
        if (!port || port->user_constraint < 0.0) {
            continue;
        }
        if (auto var_it = m_portVariables.find(pid); var_it != m_portVariables.end()) {
            var_it->second->SetUB(std::min(var_it->second->ub(), port->user_constraint));
        }
    }

    addConnectionConstraints(factory_graph, comp);
}

void FactorySolver::addRecipeConstraints(const Node &node, const Recipe &recipe) {
    if (node.production_multiplier <= 0.0 || node.clock_speed <= 0.0) {
        for (auto pid : node.input_ports) {
            auto it = m_portVariables.find(pid);
            if (it != m_portVariables.end()) {
                auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                c->SetCoefficient(it->second, 1.0);
                m_constraints.push_back(c);
            }
        }
        for (auto pid : node.output_ports) {
            auto it = m_portVariables.find(pid);
            if (it != m_portVariables.end()) {
                auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                c->SetCoefficient(it->second, 1.0);
                m_constraints.push_back(c);
            }
        }
        return;
    }

    if (!recipe.output_ports.empty() && recipe.output_ports[0].amount > 0.0 && !node.output_ports.empty()) {
        auto out0_it = m_portVariables.find(node.output_ports[0]);
        if (out0_it != m_portVariables.end()) {
            MPVariable *out0_var = out0_it->second;

            size_t input_count = std::min(recipe.input_ports.size(), node.input_ports.size());
            for (size_t i = 0; i < input_count; ++i) {
                auto in_it = m_portVariables.find(node.input_ports[i]);
                if (in_it == m_portVariables.end()) continue;
                MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
                // Input = Output * (input_amount / output_amount) * (production_multiplier / 100)
                constraint->SetCoefficient(in_it->second, recipe.output_ports[0].amount * (node.production_multiplier / 100.0));
                constraint->SetCoefficient(out0_var, -recipe.input_ports[i].amount);
                m_constraints.push_back(constraint);
            }
            size_t output_count = std::min(recipe.output_ports.size(), node.output_ports.size());
            for (size_t i = 1; i < output_count; ++i) {
                auto out_it = m_portVariables.find(node.output_ports[i]);
                if (out_it == m_portVariables.end()) continue;
                MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
                // Output_i = Output_0 * (output_i_amount / output_0_amount)
                constraint->SetCoefficient(out0_var, recipe.output_ports[i].amount);
                constraint->SetCoefficient(out_it->second, -recipe.output_ports[0].amount);
                m_constraints.push_back(constraint);
            }
        }
    } else if (!recipe.input_ports.empty() && recipe.input_ports[0].amount > 0.0 && !node.input_ports.empty()) {
        auto in0_it = m_portVariables.find(node.input_ports[0]);
        if (in0_it != m_portVariables.end()) {
            MPVariable *in0_var = in0_it->second;

            size_t input_count = std::min(recipe.input_ports.size(), node.input_ports.size());
            for (size_t i = 1; i < input_count; ++i) {
                auto in_it = m_portVariables.find(node.input_ports[i]);
                if (in_it == m_portVariables.end()) continue;
                MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
                // Input_i = Input_0 * (input_i_amount / input_0_amount)
                constraint->SetCoefficient(in_it->second, recipe.input_ports[0].amount);
                constraint->SetCoefficient(in0_var, -recipe.input_ports[i].amount);
                m_constraints.push_back(constraint);
            }
            size_t output_count = std::min(recipe.output_ports.size(), node.output_ports.size());
            for (size_t i = 0; i < output_count; ++i) {
                auto out_it = m_portVariables.find(node.output_ports[i]);
                if (out_it == m_portVariables.end()) continue;
                MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
                // Output = Input * (output_amount / input_amount) * (production_multiplier / 100)
                constraint->SetCoefficient(out_it->second, recipe.input_ports[0].amount);
                constraint->SetCoefficient(in0_var, -recipe.output_ports[i].amount * (node.production_multiplier / 100.0));
                m_constraints.push_back(constraint);
            }
        }
    } else {
        for (auto pid : node.input_ports) {
            auto it = m_portVariables.find(pid);
            if (it != m_portVariables.end()) {
                auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                c->SetCoefficient(it->second, 1.0);
                m_constraints.push_back(c);
            }
        }
        for (auto pid : node.output_ports) {
            auto it = m_portVariables.find(pid);
            if (it != m_portVariables.end()) {
                auto *c = m_solver->MakeRowConstraint(0.0, 0.0);
                c->SetCoefficient(it->second, 1.0);
                m_constraints.push_back(c);
            }
        }
    }
}

void FactorySolver::addConnectionConstraints(const FactoryGraph &factory_graph, ComponentData &comp) {
    auto addBalance = [&](const std::string &prefix,
                          const uint64_t port_id,
                          const std::vector<uint64_t> &conn_ids,
                          std::vector<std::pair<MPVariable *, MPVariable *>> &target_list) {
        if (conn_ids.size() < 2) {
            return;
        }
        const std::string suffix = std::to_string(port_id);
        MPVariable *hi = m_solver->MakeNumVar(0.0, MPSolver::infinity(), prefix + "Max_" + suffix);
        MPVariable *lo = m_solver->MakeNumVar(0.0, MPSolver::infinity(), prefix + "Min_" + suffix);
        for (const uint64_t conn_id : conn_ids) {
            MPVariable *flow = m_connectionVariables.at(conn_id);

            MPConstraint *upper = m_solver->MakeRowConstraint(-MPSolver::infinity(), 0.0);
            upper->SetCoefficient(flow, 1.0);
            upper->SetCoefficient(hi, -1.0);
            m_constraints.push_back(upper);

            MPConstraint *lower = m_solver->MakeRowConstraint(-MPSolver::infinity(), 0.0);
            lower->SetCoefficient(lo, 1.0);
            lower->SetCoefficient(flow, -1.0);
            m_constraints.push_back(lower);
        }
        target_list.emplace_back(hi, lo);
    };

    // Outgoing connections per output port: R_p = sum(F_c) + E_p
    for (const auto &[port_id, conn_ids] : comp.port_outgoing) {
        MPVariable *excess = m_solver->MakeNumVar(0.0, MPSolver::infinity(), "Excess_" + std::to_string(port_id));
        m_excessVariables[port_id] = excess;

        const Port *port = factory_graph.getPort(port_id);
        const bool in_Z_K = comp.no_overflow_ports.count(port_id) > 0;
        const bool in_T_K = port && comp.target_serving_nodes.count(port->node_id) > 0;

        // No-Overflow Rules (Section 3.3):
        // Rule 1: E_p = 0 on all wired output ports in Z_K (push-driven paths).
        // Rule 2: E_p = 0 on all wired output ports of nodes outside T_K.
        if (in_Z_K || !in_T_K) {
            excess->SetUB(0.0);
        }

        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), 1.0);
        constraint->SetCoefficient(excess, -1.0);

        for (uint64_t conn_id : conn_ids) {
            constraint->SetCoefficient(m_connectionVariables.at(conn_id), -1.0);
        }
        m_constraints.push_back(constraint);

        // Stage 2c splitter classification
        if (conn_ids.size() >= 2) {
            std::vector<uint64_t> sink_conns;
            std::vector<uint64_t> non_sink_conns;
            std::vector<uint64_t> non_sink_non_target_conns;
            for (uint64_t conn_id : conn_ids) {
                const Connection *conn = factory_graph.getConnection(conn_id);
                const Port *to_p = conn ? factory_graph.getPort(conn->to_port) : nullptr;
                if (to_p && comp.sink_dedicated_nodes.count(to_p->node_id)) {
                    sink_conns.push_back(conn_id);
                } else {
                    non_sink_conns.push_back(conn_id);
                    if (!to_p || !comp.target_serving_nodes.count(to_p->node_id)) {
                        non_sink_non_target_conns.push_back(conn_id);
                    }
                }
            }
            if (!sink_conns.empty() && !non_sink_conns.empty()) {
                comp.mixed_splitter_sink_conns.insert(
                    comp.mixed_splitter_sink_conns.end(), sink_conns.begin(), sink_conns.end());
            }
            if (non_sink_non_target_conns.size() >= 2) {
                addBalance("NonSinkBalance", port_id, non_sink_non_target_conns, m_nonSinkBalanceVariables);
            }
        }

        addBalance("Balance", port_id, conn_ids, m_balanceVariables);
    }

    // Incoming connections per input port: R_p = sum(F_c)
    for (const auto &[port_id, conn_ids] : comp.port_incoming) {
        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), -1.0);

        for (uint64_t conn_id : conn_ids) {
            constraint->SetCoefficient(m_connectionVariables.at(conn_id), 1.0);
        }
        m_constraints.push_back(constraint);
        addBalance("Balance", port_id, conn_ids, m_balanceVariables);
    }
}

FactorySolver::Terms FactorySolver::buildMachineActivityTerms(
    const FactoryGraph &factory_graph,
    const std::vector<uint64_t> &candidate_nodes) const {
    Terms terms;
    double U = 1.0;
    if (const std::string &unit = factory_graph.getGameData().time_unit; unit == "minutes") {
        U = 60.0;
    } else if (unit == "hours") {
        U = 3600.0;
    }

    const auto &recipes = factory_graph.getGameData().recipes;
    const auto &machines = factory_graph.getGameData().machines;

    for (uint64_t nid : candidate_nodes) {
        const Node *node = factory_graph.getNode(nid);
        if (!node) continue;
        if (node->clock_speed <= 0.0 || node->production_multiplier <= 0.0) continue;

        auto rec_it = recipes.find(node->selected_recipe_key);
        if (rec_it == recipes.end()) continue;
        const Recipe &recipe = rec_it->second;

        double B = 1.0;
        auto mach_it = machines.find(node->machine_key);
        if (mach_it != machines.end() && mach_it->second.base_crafting_speed > 0.0) {
            B = mach_it->second.base_crafting_speed;
        }

        const double C = node->clock_speed / 100.0;
        const double P = node->production_multiplier / 100.0;

        const bool all_inputs_nothing = std::all_of(
            recipe.input_ports.begin(),
            recipe.input_ports.end(),
            [](const RecipePort &p) { return p.resource_key == "nothing"; });

        uint64_t ref_port_id = 0;
        double ref_amount = 0.0;
        bool found_ref = false;

        if (all_inputs_nothing) {
            const size_t output_count = std::min(recipe.output_ports.size(), node->output_ports.size());
            for (size_t i = 0; i < output_count; ++i) {
                if (recipe.output_ports[i].amount > 0.0) {
                    ref_port_id = node->output_ports[i];
                    ref_amount = recipe.output_ports[i].amount * P;
                    found_ref = true;
                    break;
                }
            }
        } else {
            const size_t input_count = std::min(recipe.input_ports.size(), node->input_ports.size());
            for (size_t i = 0; i < input_count; ++i) {
                if (recipe.input_ports[i].resource_key != "nothing" && recipe.input_ports[i].amount > 0.0) {
                    ref_port_id = node->input_ports[i];
                    ref_amount = recipe.input_ports[i].amount;
                    found_ref = true;
                    break;
                }
            }
            if (!found_ref) {
                const size_t output_count = std::min(recipe.output_ports.size(), node->output_ports.size());
                for (size_t i = 0; i < output_count; ++i) {
                    if (recipe.output_ports[i].amount > 0.0) {
                        ref_port_id = node->output_ports[i];
                        ref_amount = recipe.output_ports[i].amount * P;
                        found_ref = true;
                        break;
                    }
                }
            }
        }

        if (!found_ref || ref_amount <= 0.0) continue;

        auto var_it = m_portVariables.find(ref_port_id);
        if (var_it == m_portVariables.end()) continue;

        double w_n = 0.0;
        if (recipe.time_seconds > 0.0) {
            w_n = recipe.time_seconds / (ref_amount * B * U * C);
        } else {
            w_n = 1.0 / ref_amount;
        }

        if (w_n > 0.0) {
            terms.emplace_back(var_it->second, w_n);
        }
    }

    return terms;
}

operations_research::MPSolver::ResultStatus FactorySolver::solveComponentLexicographic(
    const FactoryGraph &factory_graph, const ComponentData &comp) {
    // Stage 1: Max-Min Water-Filling on Active Goals (G_K)
    std::vector<uint64_t> active_goals = comp.active_goals;
    if (!active_goals.empty()) {
        MPVariable *S = m_solver->MakeNumVar(0.0, 1.0, "Saturation");
        std::unordered_map<uint64_t, MPConstraint *> s_constraints;

        for (uint64_t port_id : active_goals) {
            const Port *port = factory_graph.getPort(port_id);
            if (!port) continue;
            auto var_it = m_portVariables.find(port_id);
            if (var_it == m_portVariables.end()) continue;
            const double goal = port->user_constraint;
            auto *c = m_solver->MakeRowConstraint(0.0, MPSolver::infinity());
            c->SetCoefficient(var_it->second, 1.0);
            c->SetCoefficient(S, -goal);
            s_constraints[port_id] = c;
        }

        while (!active_goals.empty()) {
            operations_research::MPObjective *obj = m_solver->MutableObjective();
            obj->Clear();
            obj->SetCoefficient(S, 1.0);
            obj->SetOptimizationDirection(true);

            const auto s_status = m_solver->Solve();
            if (s_status != MPSolver::OPTIMAL) {
                return s_status;
            }
            storeSolution();

            const double s_val = S->solution_value();
            if (s_val >= 1.0 - kZeroEpsilon) {
                for (uint64_t port_id : active_goals) {
                    const Port *port = factory_graph.getPort(port_id);
                    if (!port) continue;
                    auto var_it = m_portVariables.find(port_id);
                    if (var_it == m_portVariables.end()) continue;
                    const double goal = port->user_constraint;
                    var_it->second->SetLB(std::max(var_it->second->lb(), std::max(0.0, goal)));
                }
                break;
            }
            if (active_goals.size() == 1) {
                for (uint64_t port_id : active_goals) {
                    const Port *port = factory_graph.getPort(port_id);
                    if (!port) continue;
                    auto var_it = m_portVariables.find(port_id);
                    if (var_it == m_portVariables.end()) continue;
                    const double target_val = port->user_constraint * s_val;
                    const double locked_val = target_val - 0.1 * lockTolerance(target_val);
                    var_it->second->SetLB(std::max(var_it->second->lb(), std::max(0.0, locked_val)));
                }
                break;
            }

            const double old_lb = S->lb();
            const double old_ub = S->ub();
            const double s_tol = 0.1 * lockTolerance(s_val);
            S->SetLB(std::max(old_lb, s_val - s_tol));
            S->SetUB(std::min(old_ub, s_val + s_tol));

            obj->Clear();
            for (uint64_t port_id : active_goals) {
                auto var_it = m_portVariables.find(port_id);
                if (var_it != m_portVariables.end()) {
                    obj->SetCoefficient(var_it->second, 1.0);
                }
            }
            obj->SetOptimizationDirection(true);
            const auto sum_status = m_solver->Solve();
            if (sum_status != MPSolver::OPTIMAL) {
                return sum_status;
            }
            storeSolution();

            std::vector<uint64_t> next_active;
            std::vector<uint64_t> suspects;
            for (uint64_t port_id : active_goals) {
                const Port *port = factory_graph.getPort(port_id);
                if (!port) continue;
                auto var_it = m_portVariables.find(port_id);
                if (var_it == m_portVariables.end()) continue;

                const double goal = port->user_constraint;
                const double r_val = var_it->second->solution_value();

                if (r_val > goal * s_val + 1e-6) {
                    next_active.push_back(port_id);
                } else {
                    suspects.push_back(port_id);
                }
            }

            for (uint64_t port_id : suspects) {
                const Port *port = factory_graph.getPort(port_id);
                if (!port) continue;
                auto var_it = m_portVariables.find(port_id);
                if (var_it == m_portVariables.end()) continue;

                const double goal = port->user_constraint;
                auto *R_i = var_it->second;

                obj->Clear();
                obj->SetCoefficient(R_i, 1.0);
                obj->SetOptimizationDirection(true);

                bool can_grow = false;
                const auto test_status = m_solver->Solve();
                if (test_status == MPSolver::OPTIMAL) {
                    storeSolution();
                    const double test_r_val = R_i->solution_value();
                    if (test_r_val > goal * s_val + 1e-6) {
                        can_grow = true;
                    }
                } else if (test_status != MPSolver::FEASIBLE) {
                    return test_status;
                }

                if (!can_grow) {
                    const double locked_val = goal * s_val;
                    R_i->SetLB(std::max(R_i->lb(), locked_val - 0.1 * lockTolerance(locked_val)));

                    auto c_it = s_constraints.find(port_id);
                    if (c_it != s_constraints.end()) {
                        auto *c = c_it->second;
                        c->SetBounds(-MPSolver::infinity(), MPSolver::infinity());
                        c->SetCoefficient(R_i, 0.0);
                        c->SetCoefficient(S, 0.0);
                    }
                } else {
                    next_active.push_back(port_id);
                }
            }

            S->SetLB(old_lb);
            S->SetUB(old_ub);
            if (next_active.size() == active_goals.size()) {
                for (uint64_t port_id : active_goals) {
                    const Port *port = factory_graph.getPort(port_id);
                    if (!port) continue;
                    auto var_it = m_portVariables.find(port_id);
                    if (var_it == m_portVariables.end()) continue;
                    const double locked_val = port->user_constraint * s_val;
                    var_it->second->SetLB(std::max(var_it->second->lb(), locked_val - 0.1 * lockTolerance(locked_val)));
                }
                break;
            }
            active_goals = next_active;
        }

        // Decouple all remaining S constraints so S is inert in subsequent stages
        for (auto &[port_id, c] : s_constraints) {
            c->SetBounds(-MPSolver::infinity(), MPSolver::infinity());
            if (auto var_it = m_portVariables.find(port_id); var_it != m_portVariables.end()) {
                c->SetCoefficient(var_it->second, 0.0);
            }
            c->SetCoefficient(S, 0.0);
        }
    }

    bool has_solved = !comp.active_goals.empty();
    int stage_step = 1;

    auto runMinimizeAndLock = [&](const Terms &terms) -> MPSolver::ResultStatus {
        if (terms.empty()) {
            return MPSolver::OPTIMAL;
        }
        double obj_val = 0.0;
        const auto status = solveStage(terms, /*maximize=*/false, obj_val);
        has_solved = true;
        if (status == MPSolver::OPTIMAL) {
            lockStage(terms, /*maximize=*/false, obj_val, stage_step++);
        }
        return status;
    };

    // Stage 2a: Minimal Target-Serving Machine Activity (T_K)
    if (!comp.target_serving_nodes.empty()) {
        std::vector<uint64_t> t_nodes;
        t_nodes.reserve(comp.target_serving_nodes.size());
        for (uint64_t nid : comp.nodes) {
            if (comp.target_serving_nodes.count(nid)) {
                t_nodes.push_back(nid);
            }
        }
        const Terms stage2a_terms = buildMachineActivityTerms(factory_graph, t_nodes);
        const auto status = runMinimizeAndLock(stage2a_terms);
        if (status != MPSolver::OPTIMAL) return status;
    }

    // Stage 2b: Maximal Wired Consumption (Minimal Unconsumed Excess E_p)
    if (!m_excessVariables.empty()) {
        Terms stage2b_terms;
        std::map<uint64_t, MPVariable *> ordered_excess(m_excessVariables.begin(), m_excessVariables.end());
        stage2b_terms.reserve(ordered_excess.size());
        for (const auto &[pid, excess_var] : ordered_excess) {
            if (excess_var->ub() > 0.0) {
                stage2b_terms.emplace_back(excess_var, 1.0);
            }
        }
        const auto status = runMinimizeAndLock(stage2b_terms);
        if (status != MPSolver::OPTIMAL) return status;
    }

    // Stage 2c.1: Mixed-Splitter Sink-Branch Minimization
    if (!comp.mixed_splitter_sink_conns.empty()) {
        Terms stage2c1_terms;
        stage2c1_terms.reserve(comp.mixed_splitter_sink_conns.size());
        for (uint64_t cid : comp.mixed_splitter_sink_conns) {
            if (auto it = m_connectionVariables.find(cid); it != m_connectionVariables.end()) {
                stage2c1_terms.emplace_back(it->second, 1.0);
            }
        }
        const auto status = runMinimizeAndLock(stage2c1_terms);
        if (status != MPSolver::OPTIMAL) return status;
    }

    // Stage 2c.2: Non-Sink Splitter Fairness
    if (!m_nonSinkBalanceVariables.empty()) {
        Terms stage2c2_terms;
        stage2c2_terms.reserve(m_nonSinkBalanceVariables.size() * 2);
        for (const auto &[hi, lo] : m_nonSinkBalanceVariables) {
            stage2c2_terms.emplace_back(hi, 1.0);
            stage2c2_terms.emplace_back(lo, -1.0);
        }
        const auto status = runMinimizeAndLock(stage2c2_terms);
        if (status != MPSolver::OPTIMAL) return status;
    }

    // Stage 3a: Minimal Remaining Non-Sink Machine Activity (N_K \ (T_K U S+_K))
    {
        std::vector<uint64_t> residual_nodes;
        for (uint64_t nid : comp.nodes) {
            if (!comp.target_serving_nodes.count(nid) && !comp.sink_dedicated_nodes.count(nid)) {
                residual_nodes.push_back(nid);
            }
        }
        if (!residual_nodes.empty()) {
            const Terms stage3a_terms = buildMachineActivityTerms(factory_graph, residual_nodes);
            const auto status = runMinimizeAndLock(stage3a_terms);
            if (status != MPSolver::OPTIMAL) return status;
        }
    }

    // Stage 3b: Minimal Sink-Dedicated Port Flow (S+_K)
    if (!comp.sink_dedicated_nodes.empty()) {
        Terms stage3b_terms;
        for (uint64_t pid : comp.ports) {
            const Port *p = factory_graph.getPort(pid);
            if (p && comp.sink_dedicated_nodes.count(p->node_id)) {
                if (auto it = m_portVariables.find(pid); it != m_portVariables.end()) {
                    stage3b_terms.emplace_back(it->second, 1.0);
                }
            }
        }
        const auto status = runMinimizeAndLock(stage3b_terms);
        if (status != MPSolver::OPTIMAL) return status;
    }

    // Stage 4: Terminal Fair Branch Splitting across all splitters and mergers
    Terms stage4_terms;
    stage4_terms.reserve(m_balanceVariables.size() * 2);
    for (const auto &[hi, lo] : m_balanceVariables) {
        stage4_terms.emplace_back(hi, 1.0);
        stage4_terms.emplace_back(lo, -1.0);
    }

    if (!stage4_terms.empty() || !has_solved) {
        double final_obj_val = 0.0;
        const auto final_status = solveStage(stage4_terms, /*maximize=*/false, final_obj_val);
        if (final_status != MPSolver::OPTIMAL) {
            return final_status;
        }
    }
    return MPSolver::OPTIMAL;
}

operations_research::MPSolver::ResultStatus FactorySolver::solveStage(
    const Terms &terms, const bool maximize, double &objective_value) {
    operations_research::MPObjective *objective = m_solver->MutableObjective();
    objective->Clear();
    for (const auto &[var, coefficient] : terms) {
        objective->SetCoefficient(var, objective->GetCoefficient(var) + coefficient);
    }
    objective->SetOptimizationDirection(maximize);

    const MPSolver::ResultStatus status = m_solver->Solve();
    if (status == MPSolver::OPTIMAL) {
        objective_value = objective->Value();
        storeSolution();
    }
    return status;
}

void FactorySolver::lockStage(const Terms &terms, const bool maximize, const double objective_value, const int stage_step) {
    if (terms.empty()) {
        return;
    }
    const double tolerance = static_cast<double>(std::max(1, stage_step)) * lockTolerance(objective_value);
    if (terms.size() == 1 && terms[0].second > 0.0) {
        MPVariable *var = terms[0].first;
        const double coeff = terms[0].second;
        if (maximize) {
            const double bound = (objective_value - tolerance) / coeff;
            var->SetLB(std::min(var->ub(), std::max(var->lb(), bound)));
        } else {
            const double bound = (objective_value + tolerance) / coeff;
            var->SetUB(std::max(var->lb(), std::min(var->ub(), bound)));
        }
        return;
    }
    MPConstraint *constraint = maximize
        ? m_solver->MakeRowConstraint(objective_value - tolerance, MPSolver::infinity())
        : m_solver->MakeRowConstraint(-MPSolver::infinity(), objective_value + tolerance);
    for (const auto &[var, coefficient] : terms) {
        constraint->SetCoefficient(var, constraint->GetCoefficient(var) + coefficient);
    }
    m_constraints.push_back(constraint);
}

void FactorySolver::storeSolution() {
    const auto &variables = m_solver->variables();
    m_solution.assign(variables.size(), 0.0);
    for (const MPVariable *var : variables) {
        m_solution[var->index()] = var->solution_value();
    }
}

double FactorySolver::solutionValue(const MPVariable *var) const {
    const int index = var->index();
    return index >= 0 && static_cast<size_t>(index) < m_solution.size() ? cleanValue(m_solution[index]) : 0.0;
}

void FactorySolver::updateComponentGraph(FactoryGraph &factory_graph, const ComponentData &comp) {
    for (uint64_t pid : comp.ports) {
        Port *graph_port = factory_graph.getPort(pid);
        if (!graph_port) continue;
        if (auto var_it = m_portVariables.find(pid); var_it != m_portVariables.end()) {
            graph_port->rate = solutionValue(var_it->second);
        } else {
            graph_port->rate = 0.0;
        }

        double excess = 0.0;
        if (const auto it = m_excessVariables.find(pid); it != m_excessVariables.end()) {
            excess = solutionValue(it->second);
        }
        graph_port->excess_rate = excess;
    }

    for (uint64_t pid : comp.active_goals) {
        const Port *graph_port = factory_graph.getPort(pid);
        if (graph_port && graph_port->user_constraint > 0.0) {
            const double ratio = std::clamp(graph_port->rate / graph_port->user_constraint, 0.0, 1.0);
            m_goalSaturations[pid] = ratio;
        }
    }

    for (uint64_t cid : comp.connections) {
        Connection *graph_conn = factory_graph.getConnection(cid);
        if (!graph_conn) continue;
        if (auto var_it = m_connectionVariables.find(cid); var_it != m_connectionVariables.end()) {
            graph_conn->rate = solutionValue(var_it->second);
        } else {
            graph_conn->rate = 0.0;
        }
    }

    int time_unit = 1;
    if (const std::string &unit = factory_graph.getGameData().time_unit; unit == "minutes") {
        time_unit = 60;
    } else if (unit == "hours") {
        time_unit = 3600;
    }

    for (uint64_t nid : comp.nodes) {
        Node *node = factory_graph.getNode(nid);
        if (!node) continue;

        auto recipe_it = factory_graph.getGameData().recipes.find(node->selected_recipe_key);
        if (recipe_it == factory_graph.getGameData().recipes.end()) {
            node->machine_count = 0.0;
            continue;
        }
        auto machine_it = factory_graph.getGameData().machines.find(node->machine_key);
        if (machine_it == factory_graph.getGameData().machines.end()) {
            node->machine_count = 0.0;
            continue;
        }
        const Recipe &recipe = recipe_it->second;
        const Machine &machine = machine_it->second;

        if (node->clock_speed <= 0.0 || recipe.time_seconds <= 0.0 || node->production_multiplier <= 0.0) {
            node->machine_count = 0.0;
            continue;
        }

        double machine_count = 0.0;
        const double base_speed = machine.base_crafting_speed;
        const double clock_speed_multiplier = node->clock_speed / 100.0;
        const double production_multiplier = node->production_multiplier / 100.0;

        const bool all_inputs_nothing = std::all_of(
            recipe.input_ports.begin(),
            recipe.input_ports.end(),
            [](const RecipePort &p) { return p.resource_key == "nothing"; });

        if (all_inputs_nothing) {
            const size_t output_count = std::min(recipe.output_ports.size(), node->output_ports.size());
            for (size_t i = 0; i < output_count; ++i) {
                if (recipe.output_ports[i].amount <= 0.0) continue;
                const double output_per_machine =
                    (recipe.output_ports[i].amount * production_multiplier) / recipe.time_seconds *
                    base_speed * time_unit * clock_speed_multiplier;
                if (output_per_machine > 0.0) {
                    if (const Port *port = factory_graph.getPort(node->output_ports[i])) {
                        machine_count = port->rate / output_per_machine;
                        break;
                    }
                }
            }
        } else {
            const size_t input_count = std::min(recipe.input_ports.size(), node->input_ports.size());
            for (size_t i = 0; i < input_count; ++i) {
                if (recipe.input_ports[i].resource_key == "nothing" || recipe.input_ports[i].amount <= 0.0) continue;
                const double input_per_machine =
                    recipe.input_ports[i].amount / recipe.time_seconds *
                    base_speed * time_unit * clock_speed_multiplier;
                if (input_per_machine > 0.0) {
                    if (const Port *port = factory_graph.getPort(node->input_ports[i])) {
                        machine_count = port->rate / input_per_machine;
                        break;
                    }
                }
            }
            if (machine_count == 0.0) {
                const size_t output_count = std::min(recipe.output_ports.size(), node->output_ports.size());
                for (size_t i = 0; i < output_count; ++i) {
                    if (recipe.output_ports[i].amount <= 0.0) continue;
                    const double output_per_machine =
                        (recipe.output_ports[i].amount * production_multiplier) / recipe.time_seconds *
                        base_speed * time_unit * clock_speed_multiplier;
                    if (output_per_machine > 0.0) {
                        if (const Port *port = factory_graph.getPort(node->output_ports[i])) {
                            machine_count = port->rate / output_per_machine;
                            break;
                        }
                    }
                }
            }
        }
        node->machine_count = machine_count;
    }
}

void FactorySolver::zeroComponentGraph(FactoryGraph &factory_graph, const ComponentData &comp) {
    for (uint64_t pid : comp.ports) {
        if (Port *graph_port = factory_graph.getPort(pid)) {
            graph_port->rate = 0.0;
            graph_port->excess_rate = 0.0;
        }
    }
    for (uint64_t cid : comp.connections) {
        if (Connection *graph_conn = factory_graph.getConnection(cid)) {
            graph_conn->rate = 0.0;
        }
    }
    for (uint64_t nid : comp.nodes) {
        if (Node *graph_node = factory_graph.getNode(nid)) {
            graph_node->machine_count = 0.0;
        }
    }
}

FactorySolver::SolverResultStatus FactorySolver::convertSolverStatus(
    const operations_research::MPSolver::ResultStatus status) const {
    switch (status) {
        case operations_research::MPSolver::OPTIMAL:
            return SolverResultStatus::SUCCESS;
        case operations_research::MPSolver::INFEASIBLE:
            return SolverResultStatus::INFEASIBLE;
        case operations_research::MPSolver::UNBOUNDED:
            return SolverResultStatus::UNBOUNDED;
        default:
            return SolverResultStatus::ERROR_;
    }
}
