#include "StartupHttpTasks.hpp"
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

  request_ = net::HttpTask::post(host, port, path, std::move(fields));
  return true;
}

std::optional<HttpTaskService::Completion> HttpTaskService::poll() {
  if (!busy() || !request_.valid() || request_.poll() == net::HttpStatus::pending) {
    return std::nullopt;
  }

  Completion result;
  result.task = std::exchange(task_, Task::none);
  result.generations = generations_;

  if (request_.poll() == net::HttpStatus::done) {
    result.response = request_.body();
  } else {
    result.error = request_.error();
  }

  request_ = net::HttpTask();
  generations_ = {};
  return result;
}

void HttpTaskService::wait() {
  // Dropping the request waits for a native worker, or aborts a browser fetch. The result is unused.
  request_ = net::HttpTask();
}
}
