#include "FactorySolver.h"

#include <queue>

FactorySolver::FactorySolver(const std::string &solver_name) {
    operations_research::MPSolver::OptimizationProblemType problem_type;
    if (!operations_research::MPSolver::ParseSolverType(solver_name, &problem_type)) {
        throw std::invalid_argument("Unknown solver type: " + solver_name);
    }

    if (!operations_research::MPSolver::SupportsProblemType(problem_type)) {
        throw std::runtime_error("Problem type not supported for solver: " + solver_name);
    }

    solver_ = std::make_unique<operations_research::MPSolver>("FactorySolver", problem_type);
}


FactorySolver::SolverResult FactorySolver::solve(FactoryGraph &factory_graph) {
    variables.clear();
    constraints.clear();
    solver_->Clear(); // Clear any previous state in the solver

    try {
        createAllVariables(factory_graph);
        addObjectiveFunction(factory_graph);
        addAllConstraints(factory_graph);

        const auto result_status = solver_->Solve();
        last_solve_time = absl::ToDoubleMilliseconds(solver_->DurationSinceConstruction());

        const SolverResult result = convertSolverStatus(result_status);
        last_solver_status = std::to_string(result_status);
        std::cout << last_solve_time << std::endl;

        if (result == SolverResult::SUCCESS) {
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
            std::cerr << "Solver failed with status: " << last_solver_status << std::endl;
        }

        return result;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return SolverResult::ERROR;
    }
}


void FactorySolver::createAllVariables(const FactoryGraph &factory_graph) {
    const auto &ports = factory_graph.getPorts();
    const auto &connections = factory_graph.getConnections();

    // Reserve space for port variables + connection variables
    variables.reserve(ports.size() + connections.size());

    // Create variables for each port in the factory graph
    for (const auto &port: ports) {
        const std::string var_name = "Port_" + std::to_string(port.id);
        operations_research::MPVariable *var = solver_->MakeNumVar(0.0, infinity, var_name);
        variables[port.id] = var;
    }

    // Create variables for each connection (using negative IDs to avoid conflicts)
    for (const auto &conn: connections) {
        const int connection_var_id = -(static_cast<int>(conn.id) + 1); // -1, -2, -3, ...
        const std::string var_name = "Conn_" + std::to_string(conn.id);
        operations_research::MPVariable *var = solver_->MakeNumVar(0.0, infinity, var_name);
        variables[connection_var_id] = var;
    }
}

void FactorySolver::addObjectiveFunction(const FactoryGraph &factory_graph) {
    // Find all ports that are reachable from constrained ports
    std::unordered_set<int> reachable_ports = findReachablePorts(factory_graph);

    // Identify all ports that have outgoing connections
    std::unordered_set<int> ports_with_outputs;
    for (const auto &conn: factory_graph.getConnections()) {
        ports_with_outputs.insert(conn.from_port);
    }

    const auto &ports = factory_graph.getPorts();
    operations_research::MPObjective *objective = solver_->MutableObjective();
    bool has_objective_terms = false;

    // Maximize output of leaf ports that are reachable from constrained ports
    for (const auto &port: ports) {
        if (!ports_with_outputs.count(port.id) && reachable_ports.count(port.id)) {
            objective->SetCoefficient(variables[port.id], 1.0);
            has_objective_terms = true;
        }
    }

    // If no reachable leaf ports found, add a dummy objective to avoid unbounded problem
    if (!has_objective_terms) {
        // Just minimize the sum of all variables (or set a trivial objective)
        for (const auto &port: ports) {
            objective->SetCoefficient(variables[port.id], 0.0001); // Small coefficient
        }
        objective->SetMinimization(); // Minimize instead of maximize
    } else {
        objective->SetMaximization();
    }
}

// Helper function to find all ports reachable from constrained ports
std::unordered_set<int> FactorySolver::findReachablePorts(const FactoryGraph &factory_graph) {
    std::unordered_set<int> reachable;
    std::queue<int> to_visit;

    // Start from all constrained ports
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        if (port.user_constraint >= 0) {
            to_visit.push(port.id);
            reachable.insert(port.id);
        }
    }

    // Build adjacency maps
    std::unordered_map<int, std::vector<int> > forward_connections; // from_port -> [to_ports]
    std::unordered_map<int, std::vector<int> > backward_connections; // to_port -> [from_ports]

    for (const auto &conn: factory_graph.getConnections()) {
        forward_connections[conn.from_port].push_back(conn.to_port);
        backward_connections[conn.to_port].push_back(conn.from_port);
    }

    // BFS to find all reachable ports (both forward and backward)
    while (!to_visit.empty()) {
        int current_port = to_visit.front();
        to_visit.pop();

        // Check forward connections
        if (forward_connections.count(current_port)) {
            for (int next_port: forward_connections[current_port]) {
                if (!reachable.count(next_port)) {
                    reachable.insert(next_port);
                    to_visit.push(next_port);
                }
            }
        }

        // Check backward connections
        if (backward_connections.count(current_port)) {
            for (int prev_port: backward_connections[current_port]) {
                if (!reachable.count(prev_port)) {
                    reachable.insert(prev_port);
                    to_visit.push(prev_port);
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
        Recipe recipe = factory_graph.getGameData().recipes[node.selected_recipe_id];
        addRecipeConstraints(node, recipe);
    }

    // Add user-defined constraints for each port
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        if (port.user_constraint >= 0) {
            operations_research::MPConstraint *constraint = solver_->MakeRowConstraint(-infinity, port.user_constraint);
            constraint->SetCoefficient(variables[port.id], 1.0);
            constraints.push_back(constraint);
        }
    }

    addConnectionConstraints(factory_graph);
}

void FactorySolver::addRecipeConstraints(const Node &node, const Recipe &recipe) {
    for (int i = 0; i < recipe.input_ports.size(); ++i) {
        operations_research::MPConstraint *constraint = solver_->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[node.input_ports[i]], recipe.output_ports[0].amount);
        constraint->SetCoefficient(variables[node.output_ports[0]], -recipe.input_ports[i].amount);
        constraints.push_back(constraint);
    }
    for (int i = 1; i < recipe.output_ports.size(); ++i) {
        operations_research::MPConstraint *constraint = solver_->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[node.output_ports[0]], recipe.output_ports[i].amount);
        constraint->SetCoefficient(variables[node.output_ports[i]], -recipe.output_ports[0].amount);
        constraints.push_back(constraint);
    }
}

void FactorySolver::addConnectionConstraints(const FactoryGraph &factory_graph) {
    const auto &connections = factory_graph.getConnections();

    // Build maps for port -> connections
    std::unordered_map<int, std::vector<int> > port_outgoing; // port_id -> [connection_var_ids]
    std::unordered_map<int, std::vector<int> > port_incoming; // port_id -> [connection_var_ids]

    for (const auto &conn: connections) {
        // Remove size_t i
        const int connection_var_id = -(static_cast<int>(conn.id) + 1); // Use conn.id, not index

        port_outgoing[conn.from_port].push_back(connection_var_id);
        port_incoming[conn.to_port].push_back(connection_var_id);
    }

    // Create constraints linking ports to their connection flows

    // For each port with outgoing connections: port = sum(outgoing_connections)
    for (const auto &[port_id, conn_var_ids]: port_outgoing) {
        auto *constraint = solver_->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[port_id], 1.0);

        for (int conn_var_id: conn_var_ids) {
            constraint->SetCoefficient(variables[conn_var_id], -1.0);
        }
        constraints.push_back(constraint);
    }

    // For each port with incoming connections: port = sum(incoming_connections)
    for (const auto &[port_id, conn_var_ids]: port_incoming) {
        auto *constraint = solver_->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[port_id], -1.0);

        for (int conn_var_id: conn_var_ids) {
            constraint->SetCoefficient(variables[conn_var_id], 1.0);
        }
        constraints.push_back(constraint);
    }
}

void FactorySolver::updateFactoryGraph(FactoryGraph &factory_graph) const {
    // Output the results to factory_graph
    const auto &ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        double value = variables.at(port.id)->solution_value();
        factory_graph.getPort(port.id)->rate = value; // Update the port rate in the factory graph
    }

    const auto &connections = factory_graph.getConnections();
    for (const auto &conn: connections) {
        const int connection_var_id = -(static_cast<int>(conn.id) + 1); // Use conn.id consistently
        double value = variables.at(connection_var_id)->solution_value();
        factory_graph.getConnection(conn.id)->rate = value;
    }

    // calculate machine counts and power usage for each node
    const auto &nodes = factory_graph.getNodes();
    for (const auto &node: nodes) {
        const Recipe &recipe = factory_graph.getGameData().recipes.at(node.selected_recipe_id);
        double machine_count = factory_graph.getPort(node.output_ports.at(0))->rate / (
                                   recipe.output_ports.at(0).amount / recipe.time_seconds);
        node.machine_count = machine_count;
    }
}

FactorySolver::SolverResult FactorySolver::convertSolverStatus(
    operations_research::MPSolver::ResultStatus status) const {
    switch (status) {
        case operations_research::MPSolver::OPTIMAL:
            return SolverResult::SUCCESS;
        case operations_research::MPSolver::INFEASIBLE:
            return SolverResult::INFEASIBLE;
        case operations_research::MPSolver::UNBOUNDED:
            return SolverResult::UNBOUNDED;
        default:
            return SolverResult::ERROR;
    }
}
