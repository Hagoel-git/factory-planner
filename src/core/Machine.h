//
// Created by hagoel on 8/5/25.
//

#ifndef MACHINE_H
#define MACHINE_H
#include <string>

struct Machine {
    std::string name;
    double base_power_usage; // Base power usage in MW
    int id;
    int max_somersloop_slots; // Maximum number of somersloop slots available for this machine

    Machine() = default;
};

#endif //MACHINE_H
