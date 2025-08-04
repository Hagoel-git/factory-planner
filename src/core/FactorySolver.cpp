//
// Created by hagoel on 8/4/25.
//

#include "FactorySolver.h"

void FactorySolver::solve(const FactoryGraph &factory_graph) {
    createAllVariables(factory_graph);
    addObjectiveFunction(factory_graph);
    addAllConstraints(factory_graph);


    std::cout << "Number of variables = " << solver->NumVariables() << std::endl;
    std::cout << "Number of constraints = " << solver->NumConstraints() << std::endl;

    const operations_research::MPSolver::ResultStatus result_status = solver->Solve();

    if (result_status != operations_research::MPSolver::OPTIMAL) {
        std::cerr << "The problem does not have an optimal solution." << std::endl;
        return;
    }
    std::cout << "Problem solved in " << solver->wall_time() << " milliseconds" << std::endl;
    std::cout << "Solution: " << std::endl;
    std::cout << "Objective value = " << solver->Objective().Value() << std::endl;
    for (const auto &var: variables) {
        std::cout << var->name() << " = " << var->solution_value()
                    << " (reduced cost: " << var->reduced_cost() << ")" << std::endl;
    }
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
    operations_research::MPObjective *objective = solver->MutableObjective();
    bool hasProducerLimits = false;
    for (const auto &node: factory_graph.getNodes()) {
        if (node.type == NodeType::PRODUCER) {
            hasProducerLimits = true;
            break;
        }
    }

    //if (hasProducerLimits) {
        objective->SetMaximization();
    //} else {
        //objective->SetMinimization();
    //}

    objective->SetCoefficient(variables[0], 1.0); // todo: automatically set the objective based on the factory graph
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
    for (const auto &connection: connections) {
        addConnectionConstraints(connection);
    }

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

void FactorySolver::addConnectionConstraints(const Connection &connection) {
    operations_research::MPConstraint *constraint = solver->MakeRowConstraint(0.0, 0.0);
    constraint->SetCoefficient(variables[connection.from_port], 1.0);
    constraint->SetCoefficient(variables[connection.to_port], -1.0);
    constraints.push_back(constraint);
}