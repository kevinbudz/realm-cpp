#pragma once
#include <chrono>
#include <cstdint>
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
}
