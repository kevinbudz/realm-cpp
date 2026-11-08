#include "StartupHttpTasks.hpp"
#include <thread>
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
  settle_next_ = true;
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

std::optional<HttpTaskService::Completion> HttpTaskService::poll(std::chrono::milliseconds settle) {
  if (settle > std::chrono::milliseconds{0} && settle_next_) {
    const auto deadline = std::chrono::steady_clock::now() + settle;
    while (busy() && request_.valid() && request_.poll() == net::HttpStatus::pending &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  settle_next_ = false;
  return poll();
}

void HttpTaskService::wait() {
  // Dropping the request waits for a native worker, or aborts a browser fetch. The result is unused.
  request_ = net::HttpTask();
}
}
