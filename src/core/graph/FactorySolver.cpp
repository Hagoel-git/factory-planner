#include "FactorySolver.h"
#include "FactoryGraph.h"
#include <queue>
#include <absl/log/globals.h>

#include "services/NotificationManager.h"

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
    m_constraints.clear();
    m_solver->Clear(); // Clear any previous state in the solver

    absl::Time t_start, t_end_setup, t_end_solve, t_end_update;

    try {
        t_start = absl::Now();
        createAllVariables(factory_graph);
        addObjectiveFunction(factory_graph);
        addAllConstraints(factory_graph);
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning); // Suppress solver output
        t_end_setup = absl::Now();
        const auto result_status = m_solver->Solve();
        t_end_solve = absl::Now();
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

        const SolverResultStatus result = convertSolverStatus(result_status);

        if (result == SolverResultStatus::SUCCESS) {
            updateFactoryGraph(factory_graph);
        } else {
            const auto &ports = factory_graph.getPorts();
            for (const auto &port: ports) {
                factory_graph.getPort(port.id)->rate = 0; // Update the port rate in the factory graph
            }
            const auto &connections = factory_graph.getConnections();
            for (const auto &conn: connections) {
                factory_graph.getConnection(conn.id)->rate = 0; // Update the connection rate in the factory graph
            }
            LOG(ERROR) << "Solver failed with status: " << m_lastSolverStatus;
        }
        t_end_update = absl::Now();

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
        LOG(ERROR) << "Exception during solving: " << e.what();
        NotificationManager::instance().addNotification(
            "Solver Error",
            "An error occurred during solving: " + std::string(e.what()),
            NotificationType::Error
        );
        return {
            SolverResultStatus::ERROR,
            absl::ToDoubleMilliseconds(t_end_update - t_start),
            absl::ToDoubleMilliseconds(t_end_setup - t_start),
            absl::ToDoubleMilliseconds(t_end_solve - t_end_setup),
            absl::ToDoubleMilliseconds(t_end_update - t_end_solve)
        };
    }
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

void FactorySolver::addObjectiveFunction(const FactoryGraph &factory_graph) {
    // Find all ports that are reachable from constrained ports
    std::unordered_set<uint64_t> reachable_ports = findReachablePorts(factory_graph);

    // Identify all ports that have outgoing connections
    std::unordered_set<uint64_t> ports_with_outputs;
    for (const auto &conn: factory_graph.getConnections()) {
        ports_with_outputs.insert(conn.from_port);
    }

    const auto &ports = factory_graph.getPorts();
    operations_research::MPObjective *objective = m_solver->MutableObjective();
    bool has_objective_terms = false;

    // Maximize output of leaf ports that are reachable from constrained ports
    for (const auto &port: ports) {
        if (!ports_with_outputs.count(port.id) && reachable_ports.count(port.id)) {
            objective->SetCoefficient(m_portVariables[port.id], 1.0);
            has_objective_terms = true;
        }
    }

    // If no reachable leaf ports found, add a dummy objective to avoid unbounded problem
    if (!has_objective_terms) {
        // Just minimize the sum of all variables (or set a trivial objective)
        for (const auto &port: ports) {
            objective->SetCoefficient(m_portVariables.at(port.id), 0.0001); // Small coefficient
        }
        objective->SetMinimization(); // Minimize instead of maximize
    } else {
        objective->SetMaximization();
    }
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
        if (port.user_constraint >= 0) {
            operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(-operations_research::MPSolver::infinity(), port.user_constraint);
            constraint->SetCoefficient(m_portVariables.at(port.id), 1.0);
            m_constraints.push_back(constraint);
        }
    }

    addConnectionConstraints(factory_graph);
}

void FactorySolver::addRecipeConstraints(const Node &node, const Recipe &recipe) {
    for (int i = 0; i < recipe.input_ports.size(); ++i) {
        operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(node.input_ports[i]), recipe.output_ports[0].amount * (node.production_multiplier / 100.0));
        constraint->SetCoefficient(m_portVariables.at(node.output_ports[0]), -recipe.input_ports[i].amount);
        m_constraints.push_back(constraint);
    }
    for (int i = 1; i < recipe.output_ports.size(); ++i) {
        operations_research::MPConstraint *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(node.output_ports[0]), recipe.output_ports[i].amount);
        constraint->SetCoefficient(m_portVariables.at(node.output_ports[i]), -recipe.output_ports[0].amount);
        m_constraints.push_back(constraint);
    }
}

void FactorySolver::addConnectionConstraints(const FactoryGraph &factory_graph) {
    const auto &connections = factory_graph.getConnections();

    // Build maps for port -> connections
    std::unordered_map<uint64_t, std::vector<uint64_t> > port_outgoing;
    std::unordered_map<uint64_t, std::vector<uint64_t> > port_incoming;

    for (const auto &conn: connections) {
        port_outgoing[conn.from_port].push_back(conn.id);
        port_incoming[conn.to_port].push_back(conn.id);
    }

    // Create constraints linking ports to their connection flows

    // For each port with outgoing connections: port = sum(outgoing_connections)
    for (const auto &[port_id, conn_ids]: port_outgoing) {
        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), 1.0);

        for (uint64_t conn_id: conn_ids) {
            constraint->SetCoefficient(m_connectionVariables[conn_id], -1.0);
        }
        m_constraints.push_back(constraint);
    }

    // For each port with incoming connections: port = sum(incoming_connections)
    for (const auto &[port_id, conn_ids]: port_incoming) {
        auto *constraint = m_solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(m_portVariables.at(port_id), -1.0);

        for (uint64_t conn_id: conn_ids) {
            constraint->SetCoefficient(m_connectionVariables.at(conn_id), 1.0);
        }
        m_constraints.push_back(constraint);
    }
}

void FactorySolver::updateFactoryGraph(FactoryGraph &factory_graph) const {
    // Output the results to factory_graph
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        const double value = m_portVariables.at(port.id)->solution_value();
        factory_graph.getPort(port.id)->rate = value;
    }

    const auto &connections = factory_graph.getConnections();
    for (const auto &conn: connections) {
        const double value = m_connectionVariables.at(conn.id)->solution_value();
        factory_graph.getConnection(conn.id)->rate = value;
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
            return SolverResultStatus::ERROR;
    }
}
