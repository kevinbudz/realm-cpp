#include "realm/stress.hpp"
#include "realm/session.hpp"
#include "realm/model.hpp"
#include "realm/hud_art.hpp"
#include "realm/telemetry.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc, char** argv) {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    if (argc == 2 && std::string(argv[1]) == "--guards") {
      World world(catalog);
      Session live(world);
      bool live_rejected = false, offline_rejected = false;
      try {
        live.replay({1, {}}, 0);
      } catch (const std::logic_error&) {
        live_rejected = true;
      }
      Session offline(world, [](const net::Packet&) {}, SessionOptions{});
      try {
        offline.connect(SessionOptions{});
      } catch (const std::logic_error&) {
        offline_rejected = true;
      }
      if (!live_rejected || !offline_rejected) throw std::runtime_error("Replay/socket ownership guard failed");
      std::cout << "Offline replay/socket ownership guards passed\n";
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--describe") {
      quality::StressTimeline::describe(catalog, argv[2]);
      return 0;
    }
    if (argc == 6 && std::string(argv[1]) == "--icons") {
      const int working = std::stoi(argv[2]), ticks = std::stoi(argv[3]);
      if (working < 1 || working > 512 || ticks < 2 || ticks > 1000) throw std::invalid_argument("Icon fixture bounds");
      AnimationLibrary animations(flash::source_root);
      Appearances appearances(flash::source_root, animations);
      HudArt art;
      art.appearances(&appearances, &catalog);
      std::vector<std::string> keys;
      for (const auto& d : catalog.objects())
        if (d.xml.has("Item")) {
          auto key = "item" + std::to_string(d.type);
          if (art.contains(key)) keys.push_back(key);
        }
      if (static_cast<int>(keys.size()) < working) throw std::runtime_error("Insufficient legal icon definitions");
      keys.resize(static_cast<std::size_t>(working));
      const auto checksum = [](const Image& image) {
        std::uint64_t h = 1469598103934665603ULL;
        for (auto b : image.pixels) {
          h ^= b;
          h *= 1099511628211ULL;
        }
        return h;
      };
      const auto first = checksum(art.image(keys.front()));
      telemetry::Recorder recorder(argv[5], {});
      recorder.aggregate_repeated(true);
      std::size_t peak{}, bytes{};
      std::uint64_t hash{};
      for (int tick = 0; tick < ticks; ++tick) {
        recorder.begin(static_cast<std::uint64_t>(tick), std::chrono::steady_clock::now());
        {
          REALM_ZONE("fixture.icons");
          for (const auto& key : keys) {
            hash = hash * 1099511628211ULL + checksum(art.image(key));
            peak = std::max(peak, art.cache_entries());
            bytes = std::max(bytes, art.cache_bytes());
          }
        }
        recorder.end({static_cast<std::uint64_t>(tick), tick == 0, false, false, 0, 0, 0, 0, -1, 0, 0, 0, 0});
      }
      if (first != checksum(art.image(keys.front())) || peak > 256 || bytes > 8 * 1024 * 1024 ||
          (working > 256 && art.evictions() == 0))
        throw std::runtime_error("Icon eviction/reentry invariant failed");
      std::ofstream(argv[4]) << nlohmann::json(
                                    {{"version", 1},
                                     {"evidence", "CPU original HUD icon cache component, no GPU residency claim"},
                                     {"keys", keys},
                                     {"working_set", working},
                                     {"ticks", ticks},
                                     {"cache_peak_entries", peak},
                                     {"cache_peak_bytes", bytes},
                                     {"evictions", art.evictions()},
                                     {"checksum", std::to_string(hash)},
                                     {"reentry_checksum", std::to_string(first)}})
                                    .dump(2)
                             << '\n';
      return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--model-library") {
      const int ticks = std::stoi(argv[2]);
      if (ticks < 1 || ticks > 10000) throw std::invalid_argument("Library ticks");
      telemetry::Recorder recorder(argv[4], {});
      recorder.aggregate_repeated(true);
      Camera camera;
      camera.configure({64, 64}, 0, 1920, 1080, 1, true);
      std::uint64_t vertices{}, hash{};
      std::vector<std::string> families;
      for (std::size_t i = 0; i < model_count(); ++i) families.emplace_back(model_name(i));
      for (int tick = 0; tick < ticks; ++tick) {
        recorder.begin(static_cast<std::uint64_t>(tick), std::chrono::steady_clock::now());
        {
          REALM_ZONE("fixture.model_library");
          for (const auto& name : families)
            if (auto mesh = project_model(name, camera, {64, 64}, tick * 3.6, 0xffffff, {})) {
              vertices += mesh->vertices.size();
              hash = hash * 1099511628211ULL + mesh->vertices.size();
            }
        }
        recorder.end({static_cast<std::uint64_t>(tick), false, false, false, 0, 0, 0, 0, -1, 1920, 1080, 0, 0});
      }
      std::ofstream(argv[3])
          << nlohmann::json(
                 {{"version", 1},
                  {"evidence", "CPU geometry component; registered models lacking legal XML objects still exercised"},
                  {"families", families},
                  {"ticks", ticks},
                  {"vertices", vertices},
                  {"checksum", std::to_string(hash)}})
                 .dump(2)
          << '\n';
      return 0;
    }
    if (argc < 4)
      throw std::runtime_error(
          "realm_stress manifest ticks summary [delay-ms] [telemetry] OR --packets port summary telemetry");
    if (std::string(argv[1]) == "--packets") {
      if (argc != 6) throw std::invalid_argument("Packet arguments");
      World world(catalog);
      Session session(world);
      SessionOptions o;
      o.port = static_cast<std::uint16_t>(std::stoi(argv[2]));
      o.username = "Q02Fixture";
      o.password = "disposable-mock";
      o.character_id = 7;
      telemetry::Recorder recorder(argv[4], {});
      session.connect(o);
      int barriers{}, reconnects{}, event_count{}, pending_ticks{};
      std::uint64_t hash = 1469598103934665603ULL;
      std::size_t queue_peak{}, framing_peak{};
      const auto epoch = std::chrono::steady_clock::now();
      int tick{};
      while (barriers < 4) {
        if (std::chrono::steady_clock::now() - epoch > std::chrono::seconds(30))
          throw std::runtime_error("Packet peer timeout");
        recorder.begin(static_cast<std::uint64_t>(tick), std::chrono::steady_clock::now());
        {
          REALM_ZONE("fixture.session");
          session.poll(barriers * 100);
          if (session.state() == SessionState::failed || session.state() == SessionState::disconnected)
            throw std::runtime_error("Fixture connection ended: " + session.error());
          queue_peak = std::max(queue_peak, session.outgoing_queue_bytes());
          framing_peak = std::max(framing_peak, session.framing_queue_bytes());
          bool barrier_end{};
          for (const auto& e : session.take_events()) {
            if (e.id == flash::PacketId::NEWTICK) ++pending_ticks;
            ++event_count;
            if (e.id == flash::PacketId::RECONNECT) ++reconnects;
            if (e.id == flash::PacketId::TEXT && std::get<net::incoming::Text>(e.data).text == "BARRIER") {
              for (const auto& v : world.entities()) {
                hash ^= static_cast<std::uint32_t>(v.id);
                hash *= 1099511628211ULL;
                hash ^= std::bit_cast<std::uint32_t>(static_cast<float>(v.motion.target.x));
                hash *= 1099511628211ULL;
              }
              barrier_end = true;
              ++barriers;
              session.player_text("BARRIER_ACK_" + std::to_string(barriers));
            }
          }
          if (barrier_end) {
            for (int i = 0; i < pending_ticks; ++i) session.finish_frame((barriers - 1) * 100);
            pending_ticks = 0;
          }
          queue_peak = std::max(queue_peak, session.outgoing_queue_bytes());
          framing_peak = std::max(framing_peak, session.framing_queue_bytes());
        }
        recorder.end({static_cast<std::uint64_t>(tick), false, false, false, 0, 0, 0, 0, -1, 0, 0, 0, 0});
        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, std::stoi(argv[5]))));
      }
      // Flush the final application ACK before closing the mock connection.
      for (int i = 0; i < 10; ++i) {
        session.poll(400);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      recorder.finish();
      session.disconnect();
      std::ofstream(argv[3]) << nlohmann::json({{"version", 1},
                                                {"barriers", barriers},
                                                {"reconnects", reconnects},
                                                {"events", event_count},
                                                {"state_checksum", std::to_string(hash)},
                                                {"resident", world.entities().size()},
                                                {"queue_peak", queue_peak},
                                                {"framing_peak", framing_peak}})
                                    .dump(2)
                             << '\n';
      return 0;
    }
    const int ticks = std::stoi(argv[2]);
    if (ticks < 1 || ticks > 100000) throw std::invalid_argument("Stress ticks outside bounds");
    World world(catalog);
    quality::StressTimeline fixture(world, quality::StressConfig::load(argv[1]));
    std::unique_ptr<telemetry::Recorder> recorder;
    if (argc > 5) recorder = std::make_unique<telemetry::Recorder>(argv[5], std::filesystem::path{});
    if (recorder) recorder->aggregate_repeated(true);
    for (int i = 0; i < ticks; ++i) {
      if (recorder) recorder->begin(static_cast<std::uint64_t>(i), std::chrono::steady_clock::now());
      fixture.step();
      (void)world.take_status_events();
      if (recorder) recorder->end({static_cast<std::uint64_t>(i), false, false, false, 0, 0, 0, 0, -1, 0, 0, 0, 0});
      if (argc > 4) std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[4])));
    }
    fixture.write_summary(argv[3]);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
