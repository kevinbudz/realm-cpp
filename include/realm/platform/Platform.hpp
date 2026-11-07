#pragma once
#include <filesystem>

namespace realm::platform {
// Current resident memory in MiB; zero on platforms without a provider.
double resident_megabytes();

#if defined(__EMSCRIPTEN__)
// Browser storage. The directory is mounted from IndexedDB and loaded asynchronously: storage_ready()
// turns true once the saved files are in place. persist_storage() writes changes back; it returns at
// once and runs one write at a time, so it is cheap to call often.
void mount_storage(const std::filesystem::path& directory);
[[nodiscard]] bool storage_ready();
void persist_storage();
// Browsers keep audio suspended until the page has a user gesture; true once one has happened.
[[nodiscard]] bool user_gesture_seen();
#endif
}
