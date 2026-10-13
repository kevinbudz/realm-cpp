#include "realm/telemetry.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <new>
#include <stdexcept>
#include <iostream>
int main(int, char** argv) {
  try {
    using namespace realm::telemetry;
    const auto path = std::filesystem::path(argv[1]);
    Recorder recorder(path, {});
    recorder.begin(0, Clock::now());
    void* a = ::operator new(1);
    ::operator delete(a);
    void* b = ::operator new[](17, std::nothrow);
    if (!b) throw std::runtime_error("nothrow new failed");
    ::operator delete[](b, std::nothrow);
    // LLVM/Mesa can request alignment below sizeof(void*); posix_memalign
    // must be given at least its minimum alignment.
    void* c = ::operator new(65, std::align_val_t{4});
    ::operator delete(c, 65, std::align_val_t{4});
    void* d = ::operator new[](101, std::align_val_t{64}, std::nothrow);
    if (!d) throw std::runtime_error("aligned new failed");
    ::operator delete[](d, std::align_val_t{64}, std::nothrow);
    recorder.end({});
    recorder.finish();
    std::ifstream file(path);
    bool found{};
    for (std::string line; std::getline(file, line);) {
      const auto row = nlohmann::json::parse(line);
      if (row["type"] == "frame") {
        found = true;
        if (row["counters"]["allocations"] != 4 || row["counters"]["allocation_bytes"] != 184)
          throw std::runtime_error("Allocation traffic mismatch");
      }
    }
    if (!found) throw std::runtime_error("Missing allocation sample");
    std::cout << "Allocation tracking: scalar/array, aligned, sized-delete and nothrow forms pass\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
