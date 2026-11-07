#include "realm/platform/Platform.hpp"
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <string>
#endif

namespace realm::platform {
// Resident memory for the performance panel (Linux /proc; other platforms report 0).
double resident_megabytes() {
#ifdef __linux__
  std::ifstream statm("/proc/self/statm");
  long long pages{}, resident{};
  if (statm >> pages >> resident) {
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / 1048576.0;
  }
#endif
  return 0;
}

#if defined(__EMSCRIPTEN__)
// The page-side state is globalThis.realmStorage: ready once the load finishes, busy while a write
// runs, gesture once the page has seen a click, tap or key press. The scripts are plain strings, so
// no macro parsing applies to them.
void mount_storage(const std::filesystem::path& directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const std::string script =
      "globalThis.realmStorage = {ready: false, busy: false, gesture: false};"
      "['pointerdown', 'keydown', 'touchend'].forEach(function(type) {"
      "  document.addEventListener(type, function() { globalThis.realmStorage.gesture = true; },"
      "                            {capture: true, passive: true});"
      "});"
      "FS.mount(IDBFS, {}, '" +
      directory.string() +
      "');"
      "FS.syncfs(true, function(err) {"
      "  if (err) { console.error('Saved settings could not be loaded', err); }"
      "  globalThis.realmStorage.ready = true;"
      "});";
  emscripten_run_script(script.c_str());
}

bool storage_ready() {
  return emscripten_run_script_int(
             "globalThis.realmStorage && globalThis.realmStorage.ready ? 1 : 0") != 0;
}

void persist_storage() {
  emscripten_run_script(
      "(function() {"
      "  const state = globalThis.realmStorage;"
      "  if (state && state.ready && !state.busy) {"
      "    state.busy = true;"
      "    FS.syncfs(false, function(err) {"
      "      state.busy = false;"
      "      if (err) { console.error('Settings could not be saved', err); }"
      "    });"
      "  }"
      "})();");
}

bool user_gesture_seen() {
  return emscripten_run_script_int(
             "globalThis.realmStorage && globalThis.realmStorage.gesture ? 1 : 0") != 0;
}
#endif
}
