#include "realm/net/Http.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace realm::net {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void destroy(Socket socket) { closesocket(socket); }
bool pending() {
  const int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
struct SocketSystem {
  SocketSystem() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed");
  }
  ~SocketSystem() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket invalid = -1;
void destroy(Socket socket) { ::close(socket); }
bool pending() { return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR; }
#endif
constexpr std::size_t max_response = 8 * 1024 * 1024;
struct Connection {
  Socket socket{invalid};
  std::chrono::steady_clock::time_point deadline;
  ~Connection() {
    if (socket != invalid) destroy(socket);
  }
  // Waits for readiness until the request deadline.
  void wait(bool write) const {
    for (;;) {
      const auto left =
          std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now());
      if (left.count() <= 0) throw std::runtime_error("HTTP request timed out");
      fd_set set;
      FD_ZERO(&set);
      FD_SET(socket, &set);
      timeval timeout{static_cast<long>(left.count() / 1000000),
                      static_cast<decltype(timeval::tv_usec)>(left.count() % 1000000)};
      const auto ready =
          select(static_cast<int>(socket + 1), write ? nullptr : &set, write ? &set : nullptr, nullptr, &timeout);
      if (ready > 0) return;
      if (ready < 0 && !pending()) throw std::runtime_error("HTTP select failed");
    }
  }
};
bool nonblocking(Socket socket) {
#ifdef _WIN32
  u_long enabled = 1;
  return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
  if (socket >= FD_SETSIZE) return false;
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
std::string dechunk(std::string_view body) {
  std::string result;
  for (std::size_t at = 0;;) {
    const auto line = body.find("\r\n", at);
    if (line == std::string_view::npos) throw std::runtime_error("Truncated chunked HTTP body");
    const auto size = std::stoul(std::string(body.substr(at, line - at)), nullptr, 16);
    if (!size) return result;
    if (line + 2 + size > body.size()) throw std::runtime_error("Truncated chunked HTTP body");
    result.append(body.substr(line + 2, size));
    at = line + 4 + size;
  }
}
}
std::string url_encode(std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      result.push_back(static_cast<char>(c));
    else if (c == ' ')
      result.push_back('+');
    else {
      result.push_back('%');
      result.push_back(hex[c >> 4U]);
      result.push_back(hex[c & 15U]);
    }
  }
  return result;
}
namespace {
// Status code of a complete raw response, which must start with an HTTP/ status line.
int response_status(const std::string& response) {
  const auto end = response.find("\r\n\r\n");
  if (end == std::string::npos || response.compare(0, 5, "HTTP/") != 0)
    throw std::runtime_error("Malformed HTTP response");
  const auto space = response.find(' ');
  return space < end ? std::atoi(response.c_str() + space + 1) : 0;
}
// Entity body of a complete raw response, with chunked or Content-Length framing applied.
std::string response_body(const std::string& response) {
  const auto end = response.find("\r\n\r\n");
  const auto headers = lower(response.substr(0, end));
  std::string content = response.substr(end + 4);
  if (headers.find("transfer-encoding: chunked") != std::string::npos) return dechunk(content);
  if (const auto length = headers.find("\r\ncontent-length:"); length != std::string::npos)
    content.resize(std::min(content.size(), static_cast<std::size_t>(std::stoul(headers.substr(length + 17)))));
  return content;
}
// Sends one request on a fresh connection and returns the raw response once it is complete.
std::string exchange(const std::string& host, std::uint16_t port, const std::string& request,
                     std::chrono::milliseconds timeout) {
#ifdef _WIN32
  static SocketSystem system;
#endif
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* results{};
  if (const auto code = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results))
    throw std::runtime_error("Cannot resolve app host: " + std::string(gai_strerror(code)));
  struct Guard {
    addrinfo* p;
    ~Guard() { freeaddrinfo(p); }
  } guard{results};
  Connection connection;
  connection.deadline = std::chrono::steady_clock::now() + timeout;
  std::string error = "no address";
  for (auto* address = results; address && connection.socket == invalid; address = address->ai_next) {
    const auto socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket == invalid) continue;
    if (!nonblocking(socket)) {
      destroy(socket);
      continue;
    }
    Connection attempt;
    attempt.socket = socket;
    attempt.deadline = connection.deadline;
    if (::connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) != 0) {
      if (!pending()) {
        error = std::strerror(errno);
        continue;
      }
      try {
        attempt.wait(true);
      } catch (const std::exception& e) {
        error = e.what();
        continue;
      }
      int socket_error{};
#ifdef _WIN32
      int length = sizeof(socket_error);
#else
      socklen_t length = sizeof(socket_error);
#endif
      if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &length) || socket_error) {
        error = std::strerror(socket_error);
        continue;
      }
    }
    connection.socket = std::exchange(attempt.socket, invalid);
  }
  if (connection.socket == invalid)
    throw std::runtime_error("Cannot connect to app server " + host + ':' + std::to_string(port) + ": " + error);
#ifdef MSG_NOSIGNAL
  constexpr int send_flags = MSG_NOSIGNAL;
#else
  constexpr int send_flags = 0;
#endif
  for (std::size_t sent = 0; sent < request.size();) {
    connection.wait(true);
    const auto count =
        ::send(connection.socket, request.data() + sent, static_cast<int>(request.size() - sent), send_flags);
    if (count < 0) {
      if (pending()) continue;
      throw std::runtime_error("HTTP send failed");
    }
    sent += static_cast<std::size_t>(count);
  }
  std::string response;
  char buffer[16384];
  for (;;) {
    connection.wait(false);
    const auto count = ::recv(connection.socket, buffer, sizeof(buffer), 0);
    if (count < 0) {
      if (pending()) continue;
      throw std::runtime_error("HTTP receive failed");
    }
    if (!count) break;
    response.append(buffer, static_cast<std::size_t>(count));
    if (response.size() > max_response) throw std::runtime_error("HTTP response exceeds limit");
    // HttpListener can keep the socket open after a complete sized response.
    if (const auto end = response.find("\r\n\r\n"); end != std::string::npos) {
      const auto headers = lower(response.substr(0, end));
      const auto length = headers.find("\r\ncontent-length:");
      if (length != std::string::npos && response.size() - end - 4 >= std::stoul(headers.substr(length + 17))) break;
      if (headers.find("transfer-encoding: chunked") != std::string::npos &&
          response.find("\r\n0\r\n\r\n", end) != std::string::npos)
        break;
    }
  }
  return response;
}
}
std::string http_post(const std::string& host, std::uint16_t port, const std::string& path, const FormFields& fields,
                      std::chrono::milliseconds timeout) {
  std::string body;
  for (const auto& [key, value] : fields) {
    if (!body.empty()) body.push_back('&');
    body += url_encode(key) + '=' + url_encode(value);
  }
  const std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + host + ':' + std::to_string(port) +
                              "\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                              "Content-Length: " +
                              std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  const auto response = exchange(host, port, request, timeout);
  const int status = response_status(response);
  if (status < 200 || status > 299) throw std::runtime_error("HTTP status " + std::to_string(status) + " from " + path);
  return response_body(response);
}
HttpResponse http_get(const std::string& host, std::uint16_t port, const std::string& path,
                      std::chrono::milliseconds timeout) {
  const std::string request =
      "GET " + path + " HTTP/1.1\r\nHost: " + host + ':' + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
  const auto response = exchange(host, port, request, timeout);
  const int status = response_status(response);
  // The body is only decoded for 2xx; other statuses report the code alone.
  if (status < 200 || status > 299) return HttpResponse{status, {}};
  return HttpResponse{status, response_body(response)};
}
}
