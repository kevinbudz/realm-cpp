#include "realm/platform/FileDialog.hpp"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <spawn.h>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace realm::platform {
struct FileDialog::Shared {
  std::mutex mutex;
  bool finished{};
  Result result;
  // The picker process while it runs; the destructor signals it so the app does not wait on a dialog.
  pid_t child{-1};
};

namespace {
// Test-only: when set, the next picker returns this path at once (an empty value cancels) and
// nothing is spawned. tests/editor_gpu_tests.py uses it to drive Load and Save without a GUI.
constexpr const char* result_override = "REALM_FILE_DIALOG_RESULT";

std::optional<std::string> override_result() {
  const char* value = std::getenv(result_override);
  return value ? std::optional<std::string>(value) : std::nullopt;
}

bool on_path(const std::string& name) {
  const char* path = std::getenv("PATH");
  if (!path) {
    return false;
  }
  std::string_view rest(path);
  while (true) {
    const auto end = rest.find(':');
    const auto dir = rest.substr(0, end);
    const std::string candidate = (dir.empty() ? std::string(".") : std::string(dir)) + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) {
      return true;
    }
    if (end == std::string_view::npos) {
      return false;
    }
    rest.remove_prefix(end + 1);
  }
}

// KDE desktops get kdialog; elsewhere zenity is preferred, with kdialog as the fallback.
std::string choose_tool() {
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  const bool kde = desktop && std::string_view(desktop).find("KDE") != std::string_view::npos;
  if (kde && on_path("kdialog")) {
    return "kdialog";
  }
  if (on_path("zenity")) {
    return "zenity";
  }
  if (on_path("kdialog")) {
    return "kdialog";
  }
  return {};
}

// Runs the picker with stdout piped back. A non-zero exit status is a cancel, as is empty output.
FileDialog::Result run_picker(const std::vector<std::string>& command, FileDialog::Shared& shared) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    return std::nullopt;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (const auto& arg : command) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  pid_t pid{};
  const int spawned = posix_spawnp(&pid, command.front().c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(fds[1]);
  if (spawned != 0) {
    close(fds[0]);
    return std::nullopt;
  }
  {
    std::lock_guard lock(shared.mutex);
    shared.child = pid;
  }
  std::string output;
  char buffer[512];
  while (true) {
    const auto count = read(fds[0], buffer, sizeof buffer);
    if (count > 0) {
      output.append(buffer, static_cast<std::size_t>(count));
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  close(fds[0]);
  int wait_status{};
  while (waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
  }
  {
    std::lock_guard lock(shared.mutex);
    shared.child = -1;
  }
  if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
    return std::nullopt;
  }
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  if (output.empty()) {
    return std::nullopt;
  }
  return std::filesystem::path(output);
}
}  // namespace

FileDialog::FileDialog() : tool_(choose_tool()) {}

FileDialog::~FileDialog() {
  if (!thread_.joinable()) {
    return;
  }
  {
    std::lock_guard lock(shared_->mutex);
    if (shared_->child > 0) {
      kill(shared_->child, SIGTERM);
    }
  }
  // The thread holds its own reference to the shared state, so it may finish after this object is gone.
  thread_.detach();
}

bool FileDialog::available() const {
  return !tool_.empty() || override_result().has_value();
}

bool FileDialog::start(Mode mode, std::string title, std::filesystem::path suggested,
                       std::string filter_label, std::string pattern) {
  if (running_) {
    return false;
  }
  running_ = true;
  shared_ = std::make_shared<Shared>();
  if (const auto forced = override_result()) {
    if (!forced->empty()) {
      shared_->result = std::filesystem::path(*forced);
    }
    shared_->finished = true;
    return true;
  }
  const auto command = picker_command(tool_, mode, title, suggested, filter_label, pattern);
  if (command.empty()) {
    // The editor checks available() before starting, so a missing tool here is treated as a cancel.
    shared_->finished = true;
    return true;
  }
  thread_ = std::thread([shared = shared_, command] {
    auto result = run_picker(command, *shared);
    std::lock_guard lock(shared->mutex);
    shared->result = std::move(result);
    shared->finished = true;
  });
  return true;
}

std::optional<FileDialog::Result> FileDialog::poll() {
  if (!running_) {
    return std::nullopt;
  }
  Result result;
  {
    std::lock_guard lock(shared_->mutex);
    if (!shared_->finished) {
      return std::nullopt;
    }
    result = std::move(shared_->result);
  }
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  return result;
}

bool FileDialog::running() const {
  return running_;
}

std::vector<std::string> FileDialog::picker_command(std::string_view tool, Mode mode,
                                                    std::string_view title,
                                                    const std::filesystem::path& suggested,
                                                    std::string_view filter_label,
                                                    std::string_view pattern) {
  const bool save = mode == Mode::save;
  const std::string name = suggested.string();
  if (tool == "kdialog") {
    // kdialog takes its filter as "pattern|label".
    return {"kdialog", "--title", std::string(title), save ? "--getsavefilename" : "--getopenfilename",
            name, std::string(pattern) + "|" + std::string(filter_label)};
  }
  if (tool == "zenity") {
    std::vector<std::string> command{"zenity", "--file-selection", "--title=" + std::string(title),
                                     "--file-filter=" + std::string(filter_label) + " | " +
                                         std::string(pattern),
                                     "--filename=" + name};
    if (save) {
      command.insert(command.begin() + 2, {"--save", "--confirm-overwrite"});
    }
    return command;
  }
  return {};
}
}  // namespace realm::platform
