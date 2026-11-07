#include "realm/platform/FileDialog.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

using realm::platform::FileDialog;
using Command = std::vector<std::string>;

Command open_command(std::string_view tool) {
  return FileDialog::picker_command(tool, FileDialog::Mode::open, "Load map", "map.jm",
                                    "JSON Map (*.jm)", "*.jm");
}

Command save_command(std::string_view tool, const std::filesystem::path& suggested) {
  return FileDialog::picker_command(tool, FileDialog::Mode::save, "Save map", suggested,
                                    "JSON Map (*.jm)", "*.jm");
}
}  // namespace

int main() {
  try {
    check(open_command("zenity") ==
              Command{"zenity", "--file-selection", "--title=Load map",
                      "--file-filter=JSON Map (*.jm) | *.jm", "--filename=map.jm"},
          "zenity open lists the .jm filter and the suggested name");
    check(save_command("zenity", "/maps/my map; rm -rf.jm") ==
              Command{"zenity", "--file-selection", "--save", "--confirm-overwrite", "--title=Save map",
                      "--file-filter=JSON Map (*.jm) | *.jm", "--filename=/maps/my map; rm -rf.jm"},
          "zenity save confirms overwrites and keeps the path as one argument");
    check(open_command("kdialog") ==
              Command{"kdialog", "--title", "Load map", "--getopenfilename", "map.jm",
                      "*.jm|JSON Map (*.jm)"},
          "kdialog open uses pattern|label filters");
    check(save_command("kdialog", "/maps/a.jm") ==
              Command{"kdialog", "--title", "Save map", "--getsavefilename", "/maps/a.jm",
                      "*.jm|JSON Map (*.jm)"},
          "kdialog save names the file to save");
    check(open_command("xdg-open").empty() && open_command("").empty(),
          "unknown picker tools build no command");

    // REALM_FILE_DIALOG_RESULT answers at once without spawning a picker (test only).
    setenv("REALM_FILE_DIALOG_RESULT", "/maps/picked.jm", 1);
    {
      FileDialog dialog;
      check(dialog.available(), "the test override counts as an available picker");
      check(dialog.start(FileDialog::Mode::save, "Save map", "map.jm", "JSON Map (*.jm)", "*.jm"),
            "a started dialog reports true");
      check(dialog.running() && !dialog.start(FileDialog::Mode::open, "Load map", "map.jm", "", ""),
            "a second start is refused while the first is pending");
      const auto answer = dialog.poll();
      check(answer && *answer && **answer == std::filesystem::path("/maps/picked.jm"),
            "the override path is delivered by poll");
      check(!dialog.running() && !dialog.poll(), "poll delivers the result once");
    }
    setenv("REALM_FILE_DIALOG_RESULT", "", 1);
    {
      FileDialog dialog;
      dialog.start(FileDialog::Mode::open, "Load map", "map.jm", "JSON Map (*.jm)", "*.jm");
      const auto answer = dialog.poll();
      check(answer && !*answer, "an empty override is a cancel");
    }
    unsetenv("REALM_FILE_DIALOG_RESULT");
    std::cout << "File dialog picker commands and the test override checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
