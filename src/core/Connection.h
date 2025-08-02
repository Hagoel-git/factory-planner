//
// Created by hagoel on 8/1/25.
//

#ifndef CONNECTION_H
#define CONNECTION_H

struct Connection {
    int from;  // ID of the node this connection originates from
    int to;    // ID of the node this connection points to
    int resource_id;

    double flow_rate = 0.0;
    double max_flow = -1;       // Maximum flow capacity of this connection
    bool is_bottleneck = false;

    Connection(int from, int to, int resource)
        : from(from),
          to(to),
          resource_id(resource) {}
};

#endif //CONNECTION_H
