#include "StartupHttpTasks.hpp"
#include <chrono>
#include <exception>
#include <utility>

namespace realm::startup_detail {
HttpTaskService::~HttpTaskService() {
  wait();
}

bool HttpTaskService::start(Task task, const std::string& host, std::uint16_t port,
                            const std::string& path, net::FormFields fields,
                            Generations generations) {
  if (busy()) {
    return false;
  }

  task_ = task;
  generations_ = generations;

  request_ = std::async(std::launch::async, [host, port, path, fields = std::move(fields)] {
    return net::http_post(host, port, path, fields);
  });
  return true;
}

std::optional<HttpTaskService::Completion> HttpTaskService::poll() {
  if (!busy() || !request_.valid() ||
      request_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return std::nullopt;
  }

  Completion result;
  result.task = std::exchange(task_, Task::none);
  result.generations = generations_;

  try {
    result.response = request_.get();
  } catch (const std::exception& error) {
    result.error = error.what();
  }

  generations_ = {};
  return result;
}

void HttpTaskService::wait() {
  if (request_.valid()) {
    request_.wait();
  }
}
}
