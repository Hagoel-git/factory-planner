#ifndef CONNECTION_H
#define CONNECTION_H
#include <string>
#include <utility>

struct Connection {
    double rate = 0.0;
    int id;
    int from_port; // Port number on the originating node
    int to_port; // Port number on the destination node
    std::string resource_key;

    Connection() = default;

    Connection(int id,int from_port, int to_port, std::string resource_key)
        : id(id), from_port(from_port), to_port(to_port), resource_key(std::move(resource_key)) {
    }

    friend bool operator==(const Connection &lhs, const Connection &rhs) {
        return lhs.id == rhs.id;
    }

    friend bool operator!=(const Connection &lhs, const Connection &rhs) {
        return !(lhs == rhs);
    }

};
namespace std {
    template <>
    struct hash<Connection> {
        std::size_t operator()(const Connection& c) const noexcept {
            return std::hash<int>{}(c.id);
        }
    };
}
#endif //CONNECTION_H
