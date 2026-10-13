#include "realm/platform.hpp"
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

namespace realm::platform {
// Resident memory for the performance panel (Linux /proc; other platforms report 0).
double resident_megabytes() {
#ifdef __linux__
  std::ifstream statm("/proc/self/statm");
  long long size{}, resident{};
  if (statm >> size >> resident)
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / 1048576.0;
#endif
  return 0;
}
}
