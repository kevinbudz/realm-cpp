#include "realm/util/Telemetry.hpp"
#include "realm/net/Transport.hpp"
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

void destroy(Socket socket) {
  closesocket(socket);
}

int last_error() {
  return WSAGetLastError();
}

bool pending(int error) {
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

bool interrupted(int error) {
  return error == WSAEINTR;
}

struct SocketSystem {
  SocketSystem() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) {
      throw std::runtime_error("WSAStartup failed");
    }
  }

  ~SocketSystem() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket invalid = -1;

void destroy(Socket socket) {
  ::close(socket);
}

int last_error() {
  return errno;
}

bool pending(int error) {
  return error == EWOULDBLOCK || error == EAGAIN || error == EINPROGRESS;
}

bool interrupted(int error) {
  return error == EINTR;
}
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
  struct Endpoint {
    sockaddr_storage address{};
    int family{};
    int type{};
    int protocol{};
    int length{};
  };

  std::deque<Endpoint> endpoints;
  Socket socket{invalid};
  ConnectionState state{ConnectionState::closed};
  std::string error;
  Framer framer;
  std::deque<Bytes> output;
  std::size_t offset{};
  std::size_t queued{};
  std::chrono::steady_clock::time_point deadline;

  void close() {
    if (socket != invalid) {
      destroy(socket);
    }
    socket = invalid;
    state = ConnectionState::closed;
    endpoints.clear();
    output.clear();
    offset = queued = 0;
    framer.reset();
  }

  bool start_next() {
    if (socket != invalid) {
      destroy(socket);
    }
    socket = invalid;
    while (!endpoints.empty()) {
      const auto address = endpoints.front();
      endpoints.pop_front();
      const auto attempt_socket = ::socket(address.family, address.type, address.protocol);
      if (attempt_socket == invalid) {
        continue;
      }
#ifdef _WIN32
      u_long enabled = 1;
      if (ioctlsocket(attempt_socket, FIONBIO, &enabled)) {
        destroy(attempt_socket);
        continue;
      }
#else
      if (attempt_socket >= FD_SETSIZE) {
        destroy(attempt_socket);
        error = "Socket descriptor exceeds select capacity";
        continue;
      }
      const int flags = fcntl(attempt_socket, F_GETFL, 0);
      if (flags < 0 || fcntl(attempt_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        destroy(attempt_socket);
        continue;
      }
#endif
      const int enabled_nodelay = 1;
      setsockopt(attempt_socket, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&enabled_nodelay), sizeof(enabled_nodelay));
#ifdef SO_NOSIGPIPE
      setsockopt(attempt_socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled_nodelay,
                 sizeof(enabled_nodelay));
#endif
      const int status = ::connect(
          attempt_socket, reinterpret_cast<const sockaddr*>(&address.address), address.length);
      if (!status || pending(last_error())) {
        this->socket = attempt_socket;
        state = status ? ConnectionState::connecting : ConnectionState::connected;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        return true;
      }
      error = error_text(last_error());
      destroy(attempt_socket);
    }
    state = ConnectionState::closed;
    return false;
  }

  void fail(std::string message) {
    close();
    error = std::move(message);
  }

  ~Impl() { close(); }
};

Transport::Transport() : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
  static SocketSystem system;
#endif
}

Transport::~Transport() = default;

ConnectionState Transport::state() const {
  return impl_->state;
}

const std::string& Transport::error() const {
  return impl_->error;
}

std::size_t Transport::queued_bytes() const {
  return impl_->queued;
}

std::size_t Transport::pending_bytes() const {
  return impl_->framer.pending_bytes();
}

void Transport::close() {
  impl_->close();
}

void Transport::connect(std::string_view host, std::uint16_t port) {
  auto& transport = *impl_;
  transport.close();
  transport.error.clear();

  const std::string hostname(host);
  const std::string service = std::to_string(port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* results{};
  const auto code = getaddrinfo(hostname.c_str(), service.c_str(), &hints, &results);
  if (code) {
    throw std::runtime_error("Cannot resolve game host: " + std::string(gai_strerror(code)));
  }

  struct Guard {
    addrinfo* addresses;

    ~Guard() { freeaddrinfo(addresses); }
  } guard{results};

  for (auto* address = results; address; address = address->ai_next) {
    if (address->ai_addrlen > sizeof(sockaddr_storage)) {
      continue;
    }
    Impl::Endpoint endpoint;
    endpoint.family = address->ai_family;
    endpoint.type = address->ai_socktype;
    endpoint.protocol = address->ai_protocol;
    endpoint.length = static_cast<int>(address->ai_addrlen);
    std::memcpy(&endpoint.address, address->ai_addr, address->ai_addrlen);
    transport.endpoints.push_back(endpoint);
  }
  if (transport.start_next()) {
    return;
  }
  throw std::runtime_error("Cannot connect to game server: " + transport.error);
}

void Transport::send(const Packet& packet) {
  auto& transport = *impl_;
  if (transport.state == ConnectionState::closed) {
    throw std::runtime_error("Cannot send on closed connection");
  }
  auto bytes = frame(packet.id, packet.body);
  if (transport.queued + bytes.size() > 4 * max_packet_length) {
    throw ProtocolError("Outgoing queue exceeds limit");
  }
  transport.queued += bytes.size();
  transport.output.push_back(std::move(bytes));
}

std::vector<Packet> Transport::poll() {
  REALM_ZONE("network.poll");
  auto& transport = *impl_;
  std::vector<Packet> packets;
  if (transport.state == ConnectionState::closed) {
    return packets;
  }
  if (transport.state == ConnectionState::connecting) {
    fd_set writable;
    fd_set errors;
    FD_ZERO(&writable);
    FD_ZERO(&errors);
    FD_SET(transport.socket, &writable);
    FD_SET(transport.socket, &errors);
    timeval timeout{};
    const auto ready =
        select(static_cast<int>(transport.socket + 1), nullptr, &writable, &errors, &timeout);
    if (ready < 0) {
      if (!interrupted(last_error())) {
        transport.fail(error_text(last_error()));
      }
      return packets;
    }
    if (ready) {
      int socket_error{};
#ifdef _WIN32
      int length = sizeof(socket_error);
#else
      socklen_t length = sizeof(socket_error);
#endif
      if (getsockopt(transport.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error),
                     &length) ||
          socket_error) {
        transport.error = error_text(socket_error ? socket_error : last_error());
        if (!transport.start_next()) {
          transport.fail(transport.error);
        }
        return packets;
      }
      transport.state = ConnectionState::connected;
      transport.endpoints.clear();
      transport.error.clear();
    } else if (std::chrono::steady_clock::now() > transport.deadline) {
      if (!transport.start_next()) {
        transport.fail("Game connection timed out");
      }
      return packets;
    } else {
      return packets;
    }
  }

  std::size_t sent{};
  while (!transport.output.empty() && sent < max_packet_length) {
    const auto& bytes = transport.output.front();
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif
    const auto count =
        ::send(transport.socket, reinterpret_cast<const char*>(bytes.data() + transport.offset),
               static_cast<int>(bytes.size() - transport.offset), send_flags);
    if (count < 0) {
      if (interrupted(last_error())) {
        continue;
      }
      if (!pending(last_error())) {
        transport.fail(error_text(last_error()));
      }
      break;
    }
    if (!count) {
      break;
    }
    const auto amount = static_cast<std::size_t>(count);
    telemetry::add(telemetry::Counter::packet_tx_bytes, amount);
    transport.offset += amount;
    transport.queued -= amount;
    sent += amount;
    if (transport.offset == bytes.size()) {
      transport.output.pop_front();
      transport.offset = 0;
    }
  }

  if (transport.state == ConnectionState::closed) {
    return packets;
  }
  std::array<std::uint8_t, 65536> buffer{};
  std::size_t received{};
  while (received < 4 * max_packet_length) {
    const auto count = ::recv(transport.socket, reinterpret_cast<char*>(buffer.data()),
                              static_cast<int>(buffer.size()), 0);
    if (count < 0) {
      if (interrupted(last_error())) {
        continue;
      }
      if (!pending(last_error())) {
        transport.fail(error_text(last_error()));
      }
      break;
    }
    if (!count) {
      transport.fail("Game server disconnected");
      break;
    }
    const auto amount = static_cast<std::size_t>(count);
    telemetry::add(telemetry::Counter::packet_rx_bytes, amount);
    received += amount;
    try {
      auto next = transport.framer.feed(std::span(buffer).first(amount));
      packets.insert(packets.end(), std::make_move_iterator(next.begin()),
                     std::make_move_iterator(next.end()));
    } catch (const std::exception& e) {
      transport.fail(e.what());
      throw;
    }
  }
  telemetry::set(telemetry::Counter::outgoing_queue_bytes, transport.queued);
  return packets;
}
}
