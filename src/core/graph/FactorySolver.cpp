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

    // Weight of plain port flow relative to unconsumed excess in the waste stage.
    constexpr double kFlowWasteWeight = 1e-3;
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
}

FactorySolver::FactorySolver(const std::string &solver_name) {
    operations_research::MPSolver::OptimizationProblemType problem_type;
    if (!operations_research::MPSolver::ParseSolverType(solver_name, &problem_type)) {
        throw std::invalid_argument("Unknown solver type: " + solver_name);
    }

    if (!operations_research::MPSolver::SupportsProblemType(problem_type)) {
        throw std::runtime_error("Problem type not supported for solver: " + solver_name);
    }

    m_solver = std::make_unique<operations_research::MPSolver>("FactorySolver", problem_type);
}


FactorySolver::SolverResult FactorySolver::solve(FactoryGraph &factory_graph) {
    m_portVariables.clear();
    m_connectionVariables.clear();
    m_excessVariables.clear();
    m_balanceVariables.clear();
    m_constraints.clear();
    m_solution.clear();
    m_solver->Clear(); // Clear any previous state in the solver

    absl::Time t_start, t_end_setup, t_end_solve, t_end_update;

    try {
        t_start = absl::Now();
        createAllVariables(factory_graph);
        addAllConstraints(factory_graph);
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning); // Suppress solver output
        t_end_setup = absl::Now();
        const auto result_status = solveLexicographic(factory_graph);
        t_end_solve = absl::Now();
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

        const SolverResultStatus result = convertSolverStatus(result_status);
        m_lastSolverStatus = toString(result);

        if (result == SolverResultStatus::SUCCESS) {
            updateFactoryGraph(factory_graph);
        } else {
            const auto &ports = factory_graph.getPorts();
            for (const auto &port: ports) {
                Port *graph_port = factory_graph.getPort(port.id);
                graph_port->rate = 0; // Update the port rate in the factory graph
                graph_port->excess_rate = 0;
            }
            const auto &connections = factory_graph.getConnections();
            for (const auto &conn: connections) {
                factory_graph.getConnection(conn.id)->rate = 0; // Update the connection rate in the factory graph
            }
            LOG(ERROR) << "Solver failed with status: " << m_lastSolverStatus;
        }
        t_end_update = absl::Now();
        m_lastSolveTime = absl::ToDoubleMilliseconds(t_end_update - t_start);

        if (result == SolverResultStatus::SUCCESS) {
            VLOG(1) << "Solver finished in " << absl::ToDoubleMilliseconds(t_end_update - t_start) << "ms";
        } else {
            LOG(WARNING) << "Solver failed to find optimal solution. Status: " << toString(result);
        }

        return {
            result,
            absl::ToDoubleMilliseconds(t_end_update - t_start),
            absl::ToDoubleMilliseconds(t_end_setup - t_start),
            absl::ToDoubleMilliseconds(t_end_solve - t_end_setup),
            absl::ToDoubleMilliseconds(t_end_update - t_end_solve)
        };
    } catch (const std::exception &e) {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
        LOG(ERROR) << "Exception during solving: " << e.what();
        NotificationManager::instance().addNotification(
            "Solver Error",
            "An error occurred during solving: " + std::string(e.what()),
            NotificationType::Error
        );
        return {
            SolverResultStatus::ERROR_,
            absl::ToDoubleMilliseconds(t_end_update - t_start),
            absl::ToDoubleMilliseconds(t_end_setup - t_start),
            absl::ToDoubleMilliseconds(t_end_solve - t_end_setup),
            absl::ToDoubleMilliseconds(t_end_update - t_end_solve)
        };
    }
}


FactorySolver::ConstraintKind FactorySolver::classifyConstraint(const FactoryGraph &factory_graph, const Port &port) {
    // - Input ports: the constraint is a ceiling ("at most this much may flow in").
    // - Raw producers (miners, extractors, ...) consume nothing but the "nothing" resource; a constraint on them
    //   describes how much is available (a capacity).
    // - Any other output port: the constraint describes how much of the product is wanted (a target).
    if (factory_graph.isInputPort(port.id)) {
        return ConstraintKind::LIMIT;
    }
    const Node *node = factory_graph.getNode(port.node_id);
    if (!node) {
        return ConstraintKind::TARGET;
    }
    const bool is_raw_source = std::all_of(node->input_ports.begin(), node->input_ports.end(),
        [&](const uint64_t input_id) {
            const Port *input = factory_graph.getPort(input_id);
            return input && input->resource_key == "nothing";
        });
    return is_raw_source ? ConstraintKind::LIMIT : ConstraintKind::TARGET;
}


void FactorySolver::createAllVariables(const FactoryGraph &factory_graph) {
    const auto &ports = factory_graph.getPorts();
    const auto &connections = factory_graph.getConnections();

    m_portVariables.reserve(ports.size());
    m_connectionVariables.reserve(connections.size());

    // Create variables for each port in the factory graph
    for (const auto &port: ports) {
        const std::string var_name = "Port_" + std::to_string(port.id);
        operations_research::MPVariable *var = m_solver->MakeNumVar(0.0, operations_research::MPSolver::infinity(), var_name);
        m_portVariables[port.id] = var;
    }

    // Create variables for each connection
    for (const auto &conn: connections) {
        const std::string var_name = "Conn_" + std::to_string(conn.id);
        operations_research::MPVariable *var = m_solver->MakeNumVar(0.0, operations_research::MPSolver::infinity(), var_name);
        m_connectionVariables[conn.id] = var;
    }
}

FactorySolver::Terms FactorySolver::buildThroughputTerms(const FactoryGraph &factory_graph) {
    // Find all ports that are reachable from constrained ports
    std::unordered_set<uint64_t> reachable_ports = findReachablePorts(factory_graph);

    // Identify all ports that have outgoing connections
    std::unordered_set<uint64_t> ports_with_outputs;
    for (const auto &conn: factory_graph.getConnections()) {
        ports_with_outputs.insert(conn.from_port);
    }

    // Leaf products are output ports nothing consumes. Nodes without any outputs are pure consumers; their inputs
    // are the end of the line instead.
    Terms terms;
    for (const auto &node: factory_graph.getNodes()) {
        for (const uint64_t port_id: node.output_ports) {
            if (!ports_with_outputs.count(port_id) && reachable_ports.count(port_id)) {
                terms.emplace_back(m_portVariables.at(port_id), 1.0);
            }
        }
        if (node.output_ports.empty()) {
            for (const uint64_t port_id: node.input_ports) {
                if (reachable_ports.count(port_id)) {
                    terms.emplace_back(m_portVariables.at(port_id), 1.0);
                }
            }
        }
    }
    return terms;
}

// Helper function to find all ports reachable from constrained ports
std::unordered_set<uint64_t> FactorySolver::findReachablePorts(const FactoryGraph &factory_graph) {
    std::unordered_set<uint64_t> reachable;
    std::queue<uint64_t> to_visit;

    // 1. Start from constrained ports
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        if (port.user_constraint >= 0) {
            to_visit.push(port.id);
            reachable.insert(port.id);
        }
    }

    // 2. Build Wire Connections
    std::unordered_map<uint64_t, std::vector<uint64_t>> forward_connections;
    for (const auto &conn: factory_graph.getConnections()) {
        forward_connections[conn.from_port].push_back(conn.to_port);
    }

    std::unordered_map<uint64_t, std::vector<uint64_t>> internal_connections;
    for (const auto &node : factory_graph.getNodes()) {
        // Link all inputs to all outputs for reachability
        for (uint64_t input_id : node.input_ports) {
            for (uint64_t output_id : node.output_ports) {
                internal_connections[input_id].push_back(output_id);
            }
        }
    }

    // 4. BFS
    while (!to_visit.empty()) {
        uint64_t current_port = to_visit.front();
        to_visit.pop();

        // Traverse Wires
        if (forward_connections.count(current_port)) {
            for (uint64_t next_port: forward_connections[current_port]) {
                if (!reachable.count(next_port)) {
                    reachable.insert(next_port);
                    to_visit.push(next_port);
                }
            }
        }

        // Traverse Nodes
        if (internal_connections.count(current_port)) {
            for (uint64_t next_port : internal_connections[current_port]) {
                if (!reachable.count(next_port)) {
                    reachable.insert(next_port);
                    to_visit.push(next_port);
                }
            }
        }
    }

    return reachable;
}

void FactorySolver::addAllConstraints(const FactoryGraph &factory_graph) {
    // Add constraints for each node in the factory graph
    const auto &nodes = factory_graph.getNodes();
    for (const auto &node: nodes) {
        Recipe recipe = factory_graph.getGameData().recipes.find(node.selected_recipe_key)->second;
        addRecipeConstraints(node, recipe);
    }

    // Add user-defined constraints for each port
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        if (port.user_constraint < 0) {
            continue;
        }
        MPVariable *rate = m_portVariables.at(port.id);
        const std::string suffix = std::to_string(port.id);


        if (classifyConstraint(factory_graph, port) == ConstraintKind::LIMIT) {
            // Capacity: 0 <= rate <= limit
            rate->SetUB(port.user_constraint);
        } else {
            // Target: rate <= target
            rate->SetUB(port.user_constraint);
        }

    }

    addConnectionConstraints(factory_graph);
}

void FactorySolver::addRecipeConstraints(const Node &node, const Recipe &recipe) {
    if (!recipe.output_ports.empty() && recipe.output_ports[0].amount > 0.0) {
        for (int i = 0; i < recipe.input_ports.size(); ++i) {
            operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
            // Input = Output * (input_amount / output_amount) * (production_multiplier / 100)
            constraint->SetCoefficient(m_portVariables.at(node.input_ports[i]), recipe.output_ports[0].amount * (node.production_multiplier / 100.0));
            constraint->SetCoefficient(m_portVariables.at(node.output_ports[0]), -recipe.input_ports[i].amount);
            m_constraints.push_back(constraint);
        }
        for (int i = 1; i < recipe.output_ports.size(); ++i) {
            operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
            // Output_i = Output_0 * (output_i_amount / output_0_amount)
            constraint->SetCoefficient(m_portVariables.at(node.output_ports[0]), recipe.output_ports[i].amount);
            constraint->SetCoefficient(m_portVariables.at(node.output_ports[i]), -recipe.output_ports[0].amount);
            m_constraints.push_back(constraint);
        }
    } else if (!recipe.input_ports.empty() && recipe.input_ports[0].amount > 0.0) {
        for (int i = 1; i < recipe.input_ports.size(); ++i) {
            operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
            // Input_i = Input_0 * (input_i_amount / input_0_amount)
            constraint->SetCoefficient(m_portVariables.at(node.input_ports[i]), recipe.input_ports[0].amount);
            constraint->SetCoefficient(m_portVariables.at(node.input_ports[0]), -recipe.input_ports[i].amount);
            m_constraints.push_back(constraint);
        }
        for (int i = 0; i < recipe.output_ports.size(); ++i) {
            operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
            // Output = Input * (output_amount / input_amount) * (production_multiplier / 100)
            constraint->SetCoefficient(m_portVariables.at(node.output_ports[i]), recipe.input_ports[0].amount);
            constraint->SetCoefficient(m_portVariables.at(node.input_ports[0]), -recipe.output_ports[i].amount * (node.production_multiplier / 100.0));
            m_constraints.push_back(constraint);
        }
    } else {
        for (auto pid : node.input_ports) {
            auto* c = m_solver->MakeRowConstraint(0.0, 0.0);
            c->SetCoefficient(m_portVariables.at(pid), 1.0);
        }
        for (auto pid : node.output_ports) {
            auto* c = m_solver->MakeRowConstraint(0.0, 0.0);
            c->SetCoefficient(m_portVariables.at(pid), 1.0);
        }
    }
}

void FactorySolver::addConnectionConstraints(const FactoryGraph &factory_graph) {
    const auto &connections = factory_graph.getConnections();

    // Build maps for port -> connections (ordered, so the model is built deterministically)
    std::map<uint64_t, std::vector<uint64_t> > port_outgoing;
    std::map<uint64_t, std::vector<uint64_t> > port_incoming;

    for (const auto &conn: connections) {
        port_outgoing[conn.from_port].push_back(conn.id);
        port_incoming[conn.to_port].push_back(conn.id);
    }

    // For splits/merges: hi >= flow_c >= lo for every branch c. Minimizing (hi - lo) later spreads the flow evenly
    // over branches that are not otherwise determined, instead of starving some of them.
    auto addBalance = [&](const uint64_t port_id, const std::vector<uint64_t> &conn_ids) {
        if (conn_ids.size() < 2) {
            return;
        }
        const std::string suffix = std::to_string(port_id);
        MPVariable *hi = m_solver->MakeNumVar(0.0, MPSolver::infinity(), "BalanceMax_" + suffix);
        MPVariable *lo = m_solver->MakeNumVar(0.0, MPSolver::infinity(), "BalanceMin_" + suffix);
        for (const uint64_t conn_id: conn_ids) {
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
        m_balanceVariables.emplace_back(hi, lo);
    };

    // Create constraints linking ports to their connection flows

    // For each port with outgoing connections: port = sum(outgoing_connections) + excess
    // The excess is production that nothing downstream consumes (e.g. surplus byproducts).
    for (const auto &[port_id, conn_ids]: port_outgoing) {
        MPVariable *excess = m_solver->MakeNumVar(0.0, MPSolver::infinity(), "Excess_" + std::to_string(port_id));
        m_excessVariables[port_id] = excess;

        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), 1.0);
        constraint->SetCoefficient(excess, -1.0);

        for (uint64_t conn_id: conn_ids) {
            constraint->SetCoefficient(m_connectionVariables.at(conn_id), -1.0);
        }
        m_constraints.push_back(constraint);
        addBalance(port_id, conn_ids);
    }

    // For each port with incoming connections: port = sum(incoming_connections)
    for (const auto &[port_id, conn_ids]: port_incoming) {
        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), -1.0);

        for (uint64_t conn_id: conn_ids) {
            constraint->SetCoefficient(m_connectionVariables.at(conn_id), 1.0);
        }
        m_constraints.push_back(constraint);
        addBalance(port_id, conn_ids);
    }
}


std::unordered_map<uint64_t, int> FactorySolver::computeNodeDepths(const FactoryGraph &factory_graph) const {
    std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
    std::unordered_set<uint64_t> all_nodes;
    
    for (const auto& node : factory_graph.getNodes()) {
        all_nodes.insert(node.id);
        adj[node.id] = {};
    }
    
    std::unordered_map<uint64_t, uint64_t> port_to_node;
    for (const auto& port : factory_graph.getPorts()) {
        port_to_node[port.id] = port.node_id;
    }
    
    for (const auto& conn : factory_graph.getConnections()) {
        uint64_t from_node = port_to_node[conn.from_port];
        uint64_t to_node = port_to_node[conn.to_port];
        if (from_node != to_node) {
            adj[from_node].push_back(to_node);
        }
    }
    
    std::vector<uint64_t> order;
    std::unordered_set<uint64_t> visited;
    
    std::function<void(uint64_t)> dfs1 = [&](uint64_t u) {
        visited.insert(u);
        for (uint64_t v : adj[u]) {
            if (!visited.count(v)) dfs1(v);
        }
        order.push_back(u);
    };
    
    for (uint64_t u : all_nodes) {
        if (!visited.count(u)) dfs1(u);
    }
    
    std::unordered_map<uint64_t, std::vector<uint64_t>> rev_adj;
    for (const auto& kv : adj) {
        for (uint64_t v : kv.second) rev_adj[v].push_back(kv.first);
    }
    
    visited.clear();
    std::vector<std::vector<uint64_t>> sccs;
    std::unordered_map<uint64_t, int> node_to_scc;
    
    std::function<void(uint64_t, std::vector<uint64_t>&)> dfs2 = [&](uint64_t u, std::vector<uint64_t>& comp) {
        visited.insert(u);
        comp.push_back(u);
        node_to_scc[u] = sccs.size();
        for (uint64_t v : rev_adj[u]) {
            if (!visited.count(v)) dfs2(v, comp);
        }
    };
    
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (!visited.count(*it)) {
            std::vector<uint64_t> comp;
            dfs2(*it, comp);
            sccs.push_back(comp);
        }
    }
    
    std::unordered_map<uint64_t, int> memo;
    std::function<int(int)> get_scc_depth = [&](int scc_id) {
        if (memo.count(scc_id)) return memo[scc_id];
        int max_d = 0;
        for (uint64_t u : sccs[scc_id]) {
            for (uint64_t v : rev_adj[u]) {
                int v_scc = node_to_scc[v];
                if (v_scc != scc_id) {
                    max_d = std::max(max_d, 1 + get_scc_depth(v_scc));
                }
            }
        }
        return memo[scc_id] = max_d;
    };
    
    std::unordered_map<uint64_t, int> depths;
    for (int i = 0; i < (int)sccs.size(); ++i) {
        int d = get_scc_depth(i);
        for (uint64_t u : sccs[i]) depths[u] = d;
    }
    
    return depths;
}


operations_research::MPSolver::ResultStatus FactorySolver::solveLexicographic(const FactoryGraph &factory_graph) {
    // 1. Stage 1: Water-filling for TARGETs
    std::vector<uint64_t> active_targets;
    for (const auto &port : factory_graph.getPorts()) {
        if (port.user_constraint >= 0 && classifyConstraint(factory_graph, port) == ConstraintKind::TARGET) {
            active_targets.push_back(port.id);
        }
    }

    if (!active_targets.empty()) {
        operations_research::MPVariable* S = m_solver->MakeNumVar(0.0, 1.0, "Saturation");
        std::unordered_map<uint64_t, operations_research::MPConstraint*> s_constraints;
        
        for (uint64_t port_id : active_targets) {
            double target = factory_graph.getPort(port_id)->user_constraint;
            auto* c = m_solver->MakeRowConstraint(0.0, operations_research::MPSolver::infinity());
            c->SetCoefficient(m_portVariables.at(port_id), 1.0);
            c->SetCoefficient(S, -target);
            s_constraints[port_id] = c;
        }
        
        while (!active_targets.empty()) {
            operations_research::MPObjective* obj = m_solver->MutableObjective();
            obj->Clear();
            obj->SetCoefficient(S, 1.0);
            obj->SetOptimizationDirection(true);
            
            if (m_solver->Solve() != operations_research::MPSolver::OPTIMAL) break;
            
            double s_val = S->solution_value();
            if (s_val >= 1.0 - kZeroEpsilon) {
                for (uint64_t port_id : active_targets) {
                    double target = factory_graph.getPort(port_id)->user_constraint;
                    m_portVariables.at(port_id)->SetLB(std::max(0.0, target));
                }
                break;
            }
            
            // OPTIMIZATION: Instead of N test solves, we do 1 solve maximizing sum(R_i)
            // to instantly reveal which targets are strictly bottlenecked.
            double old_lb = S->lb();
            double old_ub = S->ub();
            S->SetLB(s_val);
            S->SetUB(s_val);
            
            obj->Clear();
            for (uint64_t port_id : active_targets) {
                obj->SetCoefficient(m_portVariables.at(port_id), 1.0);
            }
            obj->SetOptimizationDirection(true);
            m_solver->Solve();
            
            std::vector<uint64_t> next_active;
            for (uint64_t port_id : active_targets) {
                double target = factory_graph.getPort(port_id)->user_constraint;
                double r_val = m_portVariables.at(port_id)->solution_value();
                
                // If it couldn't grow beyond the S constraint, it's a bottleneck
                if (r_val <= target * s_val + 1e-6) {
                    double locked_val = target * s_val;
                    auto* R_i = m_portVariables.at(port_id);
                    R_i->SetLB(std::max(R_i->lb(), locked_val - lockTolerance(locked_val)));
                    
                    auto* c = s_constraints[port_id];
                    c->SetBounds(-operations_research::MPSolver::infinity(), operations_research::MPSolver::infinity());
                    c->SetCoefficient(R_i, 0.0);
                    c->SetCoefficient(S, 0.0);
                } else {
                    next_active.push_back(port_id);
                }
            }
            S->SetLB(old_lb);
            S->SetUB(old_ub);
            if (next_active.size() == active_targets.size()) break;
            active_targets = next_active;
        }
    }

    // 2. Stage 2: Lexicographic Depth Priority
    auto node_depths = computeNodeDepths(factory_graph);
    std::unordered_map<uint64_t, uint64_t> port_to_node;
    for (const auto& port : factory_graph.getPorts()) port_to_node[port.id] = port.node_id;
    
    Terms all_throughput = buildThroughputTerms(factory_graph);
    std::map<int, Terms, std::greater<int>> depth_terms;
    
    for (const auto& term : all_throughput) {
        uint64_t port_id = 0;
        for (const auto& kv : m_portVariables) {
            if (kv.second == term.first) { port_id = kv.first; break; }
        }
        int depth = node_depths[port_to_node[port_id]];
        depth_terms[depth].push_back(term);
    }
    
    for (const auto& kv : depth_terms) {
        double obj_val = 0.0;
        if (solveStage(kv.second, true, obj_val) == operations_research::MPSolver::OPTIMAL) {
            lockStage(kv.second, true, obj_val);
        }
    }

    // 3. Stage 3: Waste Clean-up & Fair Splitting
    Terms waste_terms;
    std::map<uint64_t, operations_research::MPVariable*> ordered_excess(m_excessVariables.begin(), m_excessVariables.end());
    for (const auto &[id, var]: ordered_excess) {
        waste_terms.emplace_back(var, 1.0);
    }
    std::map<uint64_t, operations_research::MPVariable*> ordered_ports(m_portVariables.begin(), m_portVariables.end());
    for (const auto &[id, var]: ordered_ports) {
        waste_terms.emplace_back(var, kFlowWasteWeight);
    }
    
    for (const auto &[hi, lo]: m_balanceVariables) {
        waste_terms.emplace_back(hi, 1e-4);
        waste_terms.emplace_back(lo, -1e-4);
    }
    
    double obj_val = 0.0;
    auto final_status = solveStage(waste_terms, false, obj_val);
    
    if (final_status == operations_research::MPSolver::OPTIMAL) {
        storeSolution();
    }
    return final_status;
}


operations_research::MPSolver::ResultStatus FactorySolver::solveStage(const Terms &terms, const bool maximize,
                                                                      double &objective_value) {
    operations_research::MPObjective *objective = m_solver->MutableObjective();
    objective->Clear();
    for (const auto &[var, coefficient]: terms) {
        objective->SetCoefficient(var, objective->GetCoefficient(var) + coefficient);
    }
    objective->SetOptimizationDirection(maximize);

    const MPSolver::ResultStatus status = m_solver->Solve();
    if (status == MPSolver::OPTIMAL) {
        objective_value = objective->Value();
    }
    return status;
}

void FactorySolver::lockStage(const Terms &terms, const bool maximize, const double objective_value) {
    if (terms.empty()) {
        return;
    }
    const double tolerance = lockTolerance(objective_value);
    MPConstraint *constraint = maximize
        ? m_solver->MakeRowConstraint(objective_value - tolerance, MPSolver::infinity())
        : m_solver->MakeRowConstraint(-MPSolver::infinity(), objective_value + tolerance);
    for (const auto &[var, coefficient]: terms) {
        constraint->SetCoefficient(var, constraint->GetCoefficient(var) + coefficient);
    }
    m_constraints.push_back(constraint);
}

void FactorySolver::storeSolution() {
    const auto &variables = m_solver->variables();
    m_solution.assign(variables.size(), 0.0);
    for (const MPVariable *var: variables) {
        m_solution[var->index()] = var->solution_value();
    }
}

double FactorySolver::solutionValue(const MPVariable *var) const {
    const int index = var->index();
    return index >= 0 && static_cast<size_t>(index) < m_solution.size() ? cleanValue(m_solution[index]) : 0.0;
}

void FactorySolver::updateFactoryGraph(FactoryGraph &factory_graph) const {
    // Output the results to factory_graph
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        Port *graph_port = factory_graph.getPort(port.id);
        graph_port->rate = solutionValue(m_portVariables.at(port.id));

        // Unconsumed production on connected outputs; otherwise the overshoot above a target.
        double excess = 0.0;
        if (const auto it = m_excessVariables.find(port.id); it != m_excessVariables.end()) {
            excess = solutionValue(it->second);
        
            
        }
        graph_port->excess_rate = excess;
    }

    const auto &connections = factory_graph.getConnections();
    for (const auto &conn: connections) {
        factory_graph.getConnection(conn.id)->rate = solutionValue(m_connectionVariables.at(conn.id));
    }

    // calculate machine counts and power usage for each node
    const auto &nodes = factory_graph.getNodes();
    int time_unit = 1;
    if (const std::string &unit = factory_graph.getGameData().time_unit; unit == "minutes") {
        time_unit = 60;
    } else if (unit == "hours") {
        time_unit = 3600;
    }
    for (const auto &node: nodes) {
        const Recipe &recipe = factory_graph.getGameData().recipes.find(node.selected_recipe_key)->second;
        const Machine &machine = factory_graph.getGameData().machines.find(node.machine_key)->second;
        double machine_count;

        double base_speed = machine.base_crafting_speed;
        double clock_speed_multiplier = node.clock_speed / 100.0;
        double production_multiplier = node.production_multiplier / 100.0;

        if (recipe.input_ports.size() == 1 && recipe.input_ports.at(0).resource_key == "nothing") {
            double output_per_machine = (recipe.output_ports.at(0).amount * production_multiplier) / recipe.time_seconds * base_speed * time_unit * clock_speed_multiplier;
            machine_count = factory_graph.getPort(node.output_ports.at(0))->rate / output_per_machine;
        } else {
            double input_per_machine = recipe.input_ports.at(0).amount / recipe.time_seconds * base_speed * time_unit * clock_speed_multiplier;
            machine_count = factory_graph.getPort(node.input_ports.at(0))->rate / input_per_machine;
        }
        node.machine_count = machine_count;
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
