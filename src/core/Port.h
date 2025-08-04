//
// Created by hagoel on 8/4/25.
//

#ifndef PORT_H
#define PORT_H

struct Port {
    double rate = 0.0; // Rate of the port, e.g., how much resource it can handle per second
    int id;
    int resource_id; // ID of the resource associated with this port

    Port(int id, int resource_id)
        : id(id), resource_id(resource_id) {}
};
#endif //PORT_H
