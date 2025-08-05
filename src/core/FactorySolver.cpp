//
// Created by hagoel on 8/4/25.
//

#include "FactorySolver.h"

void FactorySolver::solve(FactoryGraph &factory_graph) {
    createAllVariables(factory_graph);
    addObjectiveFunction(factory_graph);
    addAllConstraints(factory_graph);

    const operations_research::MPSolver::ResultStatus result_status = solver->Solve();

    if (result_status != operations_research::MPSolver::OPTIMAL) {
        std::cerr << "The problem does not have an optimal solution." << std::endl;
        return;
    }
    std::cout << solver->wall_time() << std::endl;

    // Output the results to factory_graph
    const auto &ports = factory_graph.getPorts();
    for (const auto &port : ports) {
        double value = variables[port.id]->solution_value();
        factory_graph.getPort(port.id)->rate = value; // Update the port rate in the factory graph
    }

    // calculate machine counts and power usage for each node
    const auto &nodes = factory_graph.getNodes();
    for (const auto &node : nodes) {
        const Recipe &recipe = factory_graph.getGameData().recipes.at(node.selected_recipe_id);
        double machine_count = ports[node.output_ports.at(0)].rate / (recipe.output_ports.at(0).amount / recipe.time * pow(60, factory_graph.getGameData().time_unit));
        node.machine_count = machine_count;

        double power_usage = factory_graph.getGameData().machines[node.machine_id].base_power_usage * machine_count;
        node.power_usage = power_usage;
    }

    // clean up
    variables.clear();
    constraints.clear();
    delete solver;
    solver = nullptr;
}

void FactorySolver::createAllVariables(const FactoryGraph &factory_graph) {
    // Create variables for each port in the factory graph
    const auto ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        std::string var_name = "Port_" + std::to_string(port.id);
        operations_research::MPVariable *var = solver->MakeNumVar(0.0, infinity, var_name);
        variables.push_back(var);
    }
}

void FactorySolver::addObjectiveFunction(const FactoryGraph &factory_graph) {
    // Step 1: Identify all ports that have no incoming connections
    std::unordered_set<int> ports_with_inputs;
    for (const auto &conn : factory_graph.getConnections()) {
        ports_with_inputs.insert(conn.to_port);
    }

    const auto &ports = factory_graph.getPorts();
    operations_research::MPObjective *objective = solver->MutableObjective();

    for (const auto &port : ports) {
        // Skip ports that have any incoming connection
        if (ports_with_inputs.count(port.id)) continue;

        // This port has no incoming connections and is an input — treat as raw
        objective->SetCoefficient(variables[port.id], 1.0);
    }

    objective->SetMaximization();
}

void FactorySolver::addAllConstraints(const FactoryGraph &factory_graph) {
    // Add constraints for each node in the factory graph
    const auto nodes = factory_graph.getNodes();
    for (const auto &node: nodes) {
        Recipe recipe = factory_graph.getGameData().recipes[node.selected_recipe_id];
        addRecipeConstraints(node, recipe);
    }

    // Add user-defined constraints for each port
    const auto ports = factory_graph.getPorts();
    for (const auto &port: ports) {
        if (port.user_constraint >= 0) {
            operations_research::MPConstraint *constraint = solver->MakeRowConstraint(-infinity, port.user_constraint);
            constraint->SetCoefficient(variables[port.id], 1.0);
            constraints.push_back(constraint);
        }
    }

    // Add constraints for each connection in the factory graph
    const auto connections = factory_graph.getConnections();
    addConnectionConstraints(factory_graph);
}

void FactorySolver::addRecipeConstraints(const Node &node, const Recipe &recipe) {
    for (int i = 0; i < recipe.getInputPortCount(); ++i) {
        operations_research::MPConstraint *constraint = solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[node.input_ports[i]], recipe.output_ports[0].amount);
        constraint->SetCoefficient(variables[node.output_ports[0]], -recipe.input_ports[i].amount);
        constraints.push_back(constraint);
    }
    for (int i = 1; i < recipe.getOutputPortCount(); ++i) {
        operations_research::MPConstraint *constraint = solver->MakeRowConstraint(0.0, 0.0);
        constraint->SetCoefficient(variables[node.output_ports[0]], recipe.output_ports[i].amount);
        constraint->SetCoefficient(variables[node.output_ports[i]], -recipe.output_ports[0].amount);
        constraints.push_back(constraint);
    }
}

void FactorySolver::addConnectionConstraints(const FactoryGraph &factory_graph) {
    std::unordered_map<int, std::vector<int>> from_map;
    std::unordered_map<int, std::vector<int>> to_map;

    for (const auto &conn : factory_graph.getConnections()) {
        from_map[conn.from_port].push_back(conn.to_port);
        to_map[conn.to_port].push_back(conn.from_port);
    }

    std::unordered_set<int> already_constrained;

    // Split constraints (1 → N)
    for (const auto &[port_id, targets] : from_map) {
        if (targets.size() > 1 && !already_constrained.count(port_id)) {
            auto constraint = solver->MakeRowConstraint(0.0, 0.0);
            constraint->SetCoefficient(variables[port_id], 1.0);
            for (int to_port_id : targets) {
                constraint->SetCoefficient(variables[to_port_id], -1.0);
            }
            constraints.push_back(constraint);
            already_constrained.insert(port_id);
        }
    }

    // Merge constraints (N → 1)
    for (const auto &[port_id, sources] : to_map) {
        if (sources.size() > 1 && !already_constrained.count(port_id)) {
            auto constraint = solver->MakeRowConstraint(0.0, 0.0);
            for (int from_port_id : sources) {
                constraint->SetCoefficient(variables[from_port_id], 1.0);
            }
            constraint->SetCoefficient(variables[port_id], -1.0);
            constraints.push_back(constraint);
            already_constrained.insert(port_id);
        }
    }

    // Basic 1-to-1 passthrough
    for (const auto &conn : factory_graph.getConnections()) {
        int from = conn.from_port;
        int to = conn.to_port;
        if (!already_constrained.count(from) && !already_constrained.count(to)) {
            auto constraint = solver->MakeRowConstraint(0.0, 0.0);
            constraint->SetCoefficient(variables[from], 1.0);
            constraint->SetCoefficient(variables[to], -1.0);
            constraints.push_back(constraint);
        }
    }
}

