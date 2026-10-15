#pragma once

#include "realm/net/Http.hpp"
#include "StartupTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <string>

namespace realm::startup_detail {
class HttpTaskService {
 public:
  struct Generations {
    std::uint64_t session{};
    std::uint64_t fame{};
    std::uint64_t legends{};
    std::size_t legends_span{};
  };

  struct Completion {
    Task task{Task::none};
    std::string response;
    std::string error;
    Generations generations;
  };

  HttpTaskService() = default;
  HttpTaskService(const HttpTaskService&) = delete;
  HttpTaskService& operator=(const HttpTaskService&) = delete;
  ~HttpTaskService();

  [[nodiscard]] bool busy() const { return task_ != Task::none; }

  bool start(Task task, const std::string& host, std::uint16_t port, const std::string& path,
             net::FormFields fields, Generations generations);
  [[nodiscard]] std::optional<Completion> poll();
  void wait();

 private:
  Task task_{Task::none};
  Generations generations_;
  std::future<std::string> request_;
};
}
