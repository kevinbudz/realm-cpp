#pragma once
// Flash: FileReference.browse/save as used by com/company/assembleegameclient/mapeditor/EditingScreen.as.
// SDL has no file dialog API, so the desktop's own picker (kdialog or zenity) runs as a child process.
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace realm::platform {
class FileDialog {
 public:
  enum class Mode { open, save };
  // The chosen path, or nullopt when the picker was cancelled or failed.
  using Result = std::optional<std::filesystem::path>;
  struct Shared;

  FileDialog();
  virtual ~FileDialog();
  FileDialog(const FileDialog&) = delete;
  FileDialog& operator=(const FileDialog&) = delete;

  // False when no picker tool is installed. REALM_FILE_DIALOG_RESULT counts as available.
  [[nodiscard]] virtual bool available() const;

  // Starts the picker on its own thread; false when one is already running.
  virtual bool start(Mode mode, std::string title, std::filesystem::path suggested,
                     std::string filter_label, std::string pattern);

  // Outer empty while the picker runs. Once it closes, the inner result is delivered once and the
  // dialog is idle again; a dialog that is not running returns empty.
  [[nodiscard]] virtual std::optional<Result> poll();

  [[nodiscard]] virtual bool running() const;

  // The picker's argv for the given tool ("kdialog" or "zenity"); empty for any other tool. The
  // argv is spawned directly with no shell, so the names cannot inject commands.
  [[nodiscard]] static std::vector<std::string> picker_command(std::string_view tool, Mode mode,
                                                               std::string_view title,
                                                               const std::filesystem::path& suggested,
                                                               std::string_view filter_label,
                                                               std::string_view pattern);

 private:
  std::string tool_;
  std::shared_ptr<Shared> shared_;
  std::thread thread_;
  bool running_{};
};
}  // namespace realm::platform
