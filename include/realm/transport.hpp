#pragma once
#include "realm/protocol.hpp"
#include <memory>
namespace realm::net {
enum class ConnectionState { closed, connecting, connected };
class Transport {
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    void connect(std::string_view host, std::uint16_t port);
    void close();
    void send(const Packet& packet);
    [[nodiscard]] std::vector<Packet> poll();
    [[nodiscard]] ConnectionState state() const;
    [[nodiscard]] const std::string& error() const;
    [[nodiscard]] std::size_t queued_bytes() const;
    [[nodiscard]] std::size_t pending_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
