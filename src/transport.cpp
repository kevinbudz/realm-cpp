#include "realm/transport.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace realm::net {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void destroy(Socket socket) { closesocket(socket); }
int last_error() { return WSAGetLastError(); }
bool pending(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
bool interrupted(int error) { return error == WSAEINTR; }
struct SocketSystem { SocketSystem() { WSADATA data; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("WSAStartup failed"); } ~SocketSystem() { WSACleanup(); } };
#else
using Socket = int;
constexpr Socket invalid = -1;
void destroy(Socket socket) { ::close(socket); }
int last_error() { return errno; }
bool pending(int error) { return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS; }
bool interrupted(int error) { return error == EINTR; }
#endif
std::string error_text(int code) {
#ifdef _WIN32
    return "Socket error " + std::to_string(code);
#else
    return std::strerror(code);
#endif
}
}
struct Transport::Impl {
    struct Endpoint { sockaddr_storage address{}; int family{}, type{}, protocol{}, length{}; };
    std::deque<Endpoint> endpoints;
    Socket socket{invalid};
    ConnectionState state{ConnectionState::closed};
    std::string error;
    Framer framer;
    std::deque<Bytes> output;
    std::size_t offset{}, queued{};
    std::chrono::steady_clock::time_point deadline;
    void close() {
        if (socket != invalid) destroy(socket);
        socket = invalid; state = ConnectionState::closed; endpoints.clear(); output.clear(); offset = queued = 0; framer.reset();
    }
    bool start_next() {
        if (socket!=invalid) destroy(socket);
        socket=invalid;
        while (!endpoints.empty()) {
            const auto address=endpoints.front();endpoints.pop_front();
        const auto attempt_socket = ::socket(address.family, address.type, address.protocol);
        if (attempt_socket == invalid) continue;
#ifdef _WIN32
        u_long enabled = 1;
        if (ioctlsocket(attempt_socket, FIONBIO, &enabled)) { destroy(attempt_socket); continue; }
#else
        if (attempt_socket >= FD_SETSIZE) { destroy(attempt_socket); error="Socket descriptor exceeds select capacity"; continue; }
        const int flags = fcntl(attempt_socket, F_GETFL, 0);
        if (flags < 0 || fcntl(attempt_socket, F_SETFL, flags | O_NONBLOCK) < 0) { destroy(attempt_socket); continue; }
#endif
        const int enabled_nodelay = 1;
        setsockopt(attempt_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled_nodelay), sizeof(enabled_nodelay));
#ifdef SO_NOSIGPIPE
        setsockopt(attempt_socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled_nodelay, sizeof(enabled_nodelay));
#endif
        const int status = ::connect(attempt_socket, reinterpret_cast<const sockaddr*>(&address.address), address.length);
        if (!status || pending(last_error())) {
            this->socket = attempt_socket; state = status ? ConnectionState::connecting : ConnectionState::connected;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10); return true;
        }
        error = error_text(last_error()); destroy(attempt_socket);
        }
        state=ConnectionState::closed;return false;
    }
    void fail(std::string message) { close(); error = std::move(message); }
    ~Impl() { close(); }
};
Transport::Transport() : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    static SocketSystem system;
#endif
}
Transport::~Transport() = default;
ConnectionState Transport::state() const { return impl_->state; }
const std::string& Transport::error() const { return impl_->error; }
std::size_t Transport::queued_bytes() const { return impl_->queued; }
void Transport::close() { impl_->close(); }
void Transport::connect(std::string_view host, std::uint16_t port) {
    auto& p = *impl_; p.close(); p.error.clear();
    const std::string hostname(host), service = std::to_string(port);
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* results{};
    const auto code = getaddrinfo(hostname.c_str(), service.c_str(), &hints, &results);
    if (code) throw std::runtime_error("Cannot resolve game host: " + std::string(gai_strerror(code)));
    struct Guard { addrinfo* p; ~Guard() { freeaddrinfo(p); } } guard{results};
    for (auto* address = results; address; address = address->ai_next) {
        if (address->ai_addrlen > sizeof(sockaddr_storage)) continue;
        Impl::Endpoint endpoint;endpoint.family=address->ai_family;endpoint.type=address->ai_socktype;endpoint.protocol=address->ai_protocol;endpoint.length=static_cast<int>(address->ai_addrlen);
        std::memcpy(&endpoint.address,address->ai_addr,address->ai_addrlen);p.endpoints.push_back(endpoint);
    }
    if (p.start_next()) return;
    throw std::runtime_error("Cannot connect to game server: " + p.error);
}
void Transport::send(const Packet& packet) {
    auto& p = *impl_;
    if (p.state == ConnectionState::closed) throw std::runtime_error("Cannot send on closed connection");
    auto bytes = frame(packet.id, packet.body);
    if (p.queued + bytes.size() > 4 * max_packet_length) throw ProtocolError("Outgoing queue exceeds limit");
    p.queued += bytes.size(); p.output.push_back(std::move(bytes));
}
std::vector<Packet> Transport::poll() {
    auto& p = *impl_; std::vector<Packet> packets;
    if (p.state == ConnectionState::closed) return packets;
    if (p.state == ConnectionState::connecting) {
        fd_set writable, errors; FD_ZERO(&writable); FD_ZERO(&errors); FD_SET(p.socket,&writable); FD_SET(p.socket,&errors); timeval timeout{};
        const auto ready = select(static_cast<int>(p.socket + 1), nullptr, &writable, &errors, &timeout);
        if (ready < 0) { if (!interrupted(last_error())) p.fail(error_text(last_error())); return packets; }
        if (ready) {
            int socket_error{};
#ifdef _WIN32
            int length = sizeof(socket_error);
#else
            socklen_t length = sizeof(socket_error);
#endif
            if (getsockopt(p.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &length) || socket_error) {
                p.error=error_text(socket_error ? socket_error : last_error());
                if (!p.start_next()) p.fail(p.error);
                return packets;
            }
            p.state = ConnectionState::connected; p.endpoints.clear(); p.error.clear();
        } else if (std::chrono::steady_clock::now() > p.deadline) { if (!p.start_next()) p.fail("Game connection timed out"); return packets; }
        else return packets;
    }
    std::size_t sent{};
    while (!p.output.empty() && sent < max_packet_length) {
        const auto& bytes = p.output.front();
#ifdef MSG_NOSIGNAL
        constexpr int send_flags = MSG_NOSIGNAL;
#else
        constexpr int send_flags = 0;
#endif
        const auto count = ::send(p.socket, reinterpret_cast<const char*>(bytes.data() + p.offset), static_cast<int>(bytes.size() - p.offset), send_flags);
        if (count < 0) { if (interrupted(last_error())) continue; if (!pending(last_error())) p.fail(error_text(last_error())); break; }
        if (!count) break;
        const auto n = static_cast<std::size_t>(count); p.offset += n; p.queued -= n; sent += n;
        if (p.offset == bytes.size()) { p.output.pop_front(); p.offset = 0; }
    }
    if (p.state == ConnectionState::closed) return packets;
    std::array<std::uint8_t,65536> buffer{}; std::size_t received{};
    while (received < 4 * max_packet_length) {
        const auto count = ::recv(p.socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
        if (count < 0) { if (interrupted(last_error())) continue; if (!pending(last_error())) p.fail(error_text(last_error())); break; }
        if (!count) { p.fail("Game server disconnected"); break; }
        const auto n = static_cast<std::size_t>(count); received += n;
        try {
            auto next = p.framer.feed(std::span(buffer).first(n));
            packets.insert(packets.end(), std::make_move_iterator(next.begin()), std::make_move_iterator(next.end()));
        } catch (const std::exception& e) { p.fail(e.what()); throw; }
    }
    return packets;
}
}
