//
// Created by hagoel on 8/1/25.
//

#ifndef CONNECTION_H
#define CONNECTION_H

struct Connection {
    int from_port;  // Port number on the originating node
    int to_port;    // Port number on the destination node
    int resource_id;

    bool is_bottleneck = false;

    Connection(int from_port, int to_port, int resource_id)
        : from_port(from_port), to_port(to_port), resource_id(resource_id) {}
};

#endif //CONNECTION_H
