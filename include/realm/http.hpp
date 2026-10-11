#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace realm::net {
using FormFields = std::vector<std::pair<std::string,std::string>>;
[[nodiscard]] std::string url_encode(std::string_view value);
// Blocking form POST, as the AppEngineClient URLLoader sends. Returns the
// response body; throws on connection, timeout or non-2xx status. Run it on
// a worker thread from interactive code.
[[nodiscard]] std::string http_post(const std::string& host,std::uint16_t port,const std::string& path,const FormFields& fields,
                                    std::chrono::milliseconds timeout=std::chrono::seconds(10));
}
