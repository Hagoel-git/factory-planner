//
// Created by hagoel on 8/1/25.
//

#ifndef NODETYPE_H
#define NODETYPE_H

enum class NodeType {
    PRODUCER,
    PROCESSOR,
    CONSUMER,
    MISC,
};

inline const char *toString(NodeType type) {
    switch (type) {
        case NodeType::PRODUCER: return "Producer";
        case NodeType::PROCESSOR: return "Processor";
        case NodeType::CONSUMER: return "Consumer";
        case NodeType::MISC: return "Miscellaneous";
        default: return "Unknown";
    }
}

#endif //NODETYPE_H
