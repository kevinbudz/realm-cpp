#include "realm/telemetry.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace realm::telemetry;
using nlohmann::json;
void check(bool value) {
  if (!value) throw std::runtime_error("Telemetry assertion failed");
}
int main(int argc, char** argv) {
  try {
    const auto dir = std::filesystem::path(argc > 1 ? argv[1] : "telemetry-test");
    std::filesystem::create_directories(dir);
    Recorder recorder(dir / "samples.jsonl", dir / "trace.json");
    recorder.metadata("escape", "quote\" slash\\ newline\n tab\t");
    recorder.begin(7, Clock::now());
    {
      Zone outer("outer");
      add(Counter::allocations, 2);
      add(Counter::allocation_bytes, 33);
      {
        Zone inner("inner");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        add(Counter::allocations, 3);
        add(Counter::allocation_bytes, 66);
      }
    }
    set(Counter::visible_entities, 17);
    recorder.end({7, false, false, true, 8, 2, 3, 1, -1, 1920, 1080, 0, 0});
    recorder.gpu({7, true, 1, .1, .5, .2, .2, .3, 5, 5});
    recorder.begin(8, Clock::now());
    for (std::size_t i = 0; i < Recorder::maximum_zones + 17; ++i) {
      Zone zone("overflow");
    }
    recorder.end({8, true, true, false, 0, 0, 0, 0, -1, 800, 600, 0, 0});
    recorder.aggregate_repeated(true);
    recorder.begin(9, Clock::now());
    for (std::size_t i = 0; i < Recorder::maximum_zones + 17; ++i) {
      Zone zone("aggregated");
      add(Counter::allocations);
    }
    recorder.end({9, false, false, false, 0, 0, 0, 0, -1, 800, 600, 0, 0});
    recorder.finish();
    check(active == nullptr);
    add(Counter::allocations, 999);  // Disabled hooks cannot contaminate a completed frame.
    std::ifstream file(dir / "samples.jsonl");
    std::size_t frames{}, zones{}, gpu{};
    for (std::string line; std::getline(file, line);) {
      const auto row = json::parse(line);
      if (row["type"] == "frame") {
        ++frames;
        if (row["frame_id"] == 7) {
          check(row["cpu_ms"] == 2);
          check(row["present_ms"] == 3);
          check(row["input_to_submit_ms"].is_null());
          check(row["counters"]["allocations"] == 5);
          check(row["counters"]["allocation_bytes"] == 99);
          check(row["counters"]["visible_entities"] == 17);
        } else if (row["frame_id"] == 9) {
          check(row["dropped_zones"] == 0);
          check(row["counters"]["allocations"] == Recorder::maximum_zones + 17);
        } else {
          check(row["capture"] == 1);
          check(row["dropped_zones"] == 17);
          check(row["counters"]["allocations"] == 0);
        }
      } else if (row["type"] == "zone") {
        ++zones;
        if (row["name"] == "aggregated") {
          check(row["calls"] == Recorder::maximum_zones + 17);
          check(row["aggregate"] == 1);
          check(row["allocations"] == Recorder::maximum_zones + 17);
        }
        if (row["name"] == "inner") {
          check(row["duration_us"].get<double>() >= 900);
          check(row["allocations"] == 3);
          check(row["allocation_bytes"] == 66);
        }
        if (row["name"] == "outer") {
          check(row["allocations"] == 5);
          check(row["allocation_bytes"] == 99);
        }
      } else if (row["type"] == "gpu") {
        ++gpu;
        check(row["frame_id"] == 7);
        check(row["total_ms"] == 1);
      }
    }
    check(frames == 3 && zones == Recorder::maximum_zones + 3 && gpu == 1);
    std::ifstream trace(dir / "trace.json");
    json parsed;
    trace >> parsed;
    check(parsed["traceEvents"].size() == zones);
    std::cout
        << "Telemetry: units, attribution, nested allocations, bounded overflow, escaping and trace export pass\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
