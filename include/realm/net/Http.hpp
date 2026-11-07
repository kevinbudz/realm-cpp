#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace realm::net {
using FormFields = std::vector<std::pair<std::string, std::string>>;
[[nodiscard]] std::string url_encode(std::string_view value);
// Blocking form POST, as the AppEngineClient URLLoader sends. Returns the
// response body; throws on connection, timeout or non-2xx status. Run it on
// a worker thread from interactive code.
[[nodiscard]] std::string http_post(const std::string& host, std::uint16_t port,
                                    const std::string& path, const FormFields& fields,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(10));

struct HttpResponse {
  int status{0};
  // Entity body for 2xx responses only; empty otherwise.
  std::string body;
};

// Blocking GET for static assets such as /sfx/NAME.mp3. Any status is returned rather than thrown;
// throws on connection, timeout or a malformed response. The caller owns the timeout and must not
// run it on a UI thread.
[[nodiscard]] HttpResponse http_get(const std::string& host, std::uint16_t port,
                                    const std::string& path,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(5));

enum class HttpStatus { pending, done, failed };

// Non-blocking request for interactive code. Natively the blocking call runs on a worker thread;
// on web it is an emscripten_fetch. poll() never blocks. Destroying a pending task is safe: the
// native worker is joined as std::future would, the web fetch is aborted.
class HttpTask {
 public:
  HttpTask();
  HttpTask(HttpTask&&) noexcept;
  HttpTask& operator=(HttpTask&&) noexcept;
  HttpTask(const HttpTask&) = delete;
  HttpTask& operator=(const HttpTask&) = delete;
  ~HttpTask();

  // Same contract as http_post: done only for 2xx; any other status fails with the http_post text.
  [[nodiscard]] static HttpTask post(const std::string& host, std::uint16_t port,
                                     const std::string& path, FormFields fields,
                                     std::chrono::milliseconds timeout = std::chrono::seconds(10));
  // Same contract as http_get: done for any status; failed only on connection, timeout or framing.
  [[nodiscard]] static HttpTask get(const std::string& host, std::uint16_t port,
                                    const std::string& path,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(5));

  // False for a default-constructed task. Assigning HttpTask() releases a finished request.
  [[nodiscard]] bool valid() const { return state_ != nullptr; }
  [[nodiscard]] HttpStatus poll();
  // Valid after poll() returns done.
  [[nodiscard]] const HttpResponse& response() const;
  [[nodiscard]] const std::string& body() const;
  // Valid after poll() returns failed.
  [[nodiscard]] const std::string& error() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}
