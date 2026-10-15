// CPU-only tests for realm::audio. Runs in ctest as `audio`; `--bench` prints play() call cost
// instead.
#include "realm/sound/SoundEffectLibrary.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace realm::audio;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

#define CHECK(expr)                                                              \
  do {                                                                           \
    if (!(expr))                                                                 \
      throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expr); \
  } while (0)

namespace {

fs::path work;
int skipped = 0;
std::vector<std::string> notes;

void note(const std::string& line) {
  notes.push_back(line);
  std::cout << "  " << line << '\n';
}

template <class Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 10s) {
  const auto deadline = Clock::now() + timeout;
  while (!predicate()) {
    if (Clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

// Holds the loader thread inside a hook until released, so queue and pending behaviour is
// deterministic.
struct Latch {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered{false};
  bool open{false};

  void enter_and_wait() {
    std::unique_lock lock(mutex);
    entered = true;
    changed.notify_all();
    changed.wait(lock, [this] { return open; });
  }

  bool wait_entered(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, timeout, [this] { return entered; });
  }

  void release() {
    {
      std::lock_guard lock(mutex);
      open = true;
    }
    changed.notify_all();
  }
};

// Declared after the owner it protects, so it releases the loader before the owner joins it.
struct ReleaseOnExit {
  Latch& latch;

  ~ReleaseOnExit() { latch.release(); }
};

void put_u16(std::vector<unsigned char>& out, std::uint16_t value) {
  out.push_back(static_cast<unsigned char>(value & 0xFFU));
  out.push_back(static_cast<unsigned char>(value >> 8U));
}

void put_u32(std::vector<unsigned char>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<unsigned char>(value >> shift));
  }
}

void put_tag(std::vector<unsigned char>& out, const char* tag) {
  for (const char* c = tag; *c; ++c) {
    out.push_back(static_cast<unsigned char>(*c));
  }
}

// 16-bit mono PCM WAV carrying a sine tone. The decoder sniffs the container, so the file keeps the
// .mp3 name the loader expects from the asset server.
void write_wav(const fs::path& path, std::uint32_t rate, std::size_t frames, double hz) {
  std::vector<unsigned char> out;
  const auto data_bytes = static_cast<std::uint32_t>(frames * 2);
  put_tag(out, "RIFF");
  put_u32(out, 36 + data_bytes);
  put_tag(out, "WAVE");
  put_tag(out, "fmt ");
  put_u32(out, 16);
  put_u16(out, 1);
  put_u16(out, 1);
  put_u32(out, rate);
  put_u32(out, rate * 2);
  put_u16(out, 2);
  put_u16(out, 16);
  put_tag(out, "data");
  put_u32(out, data_bytes);
  constexpr double pi = 3.14159265358979323846;
  for (std::size_t i = 0; i < frames; ++i) {
    const double value = 0.5 * std::sin(2.0 * pi * hz * static_cast<double>(i) / rate);
    put_u16(out,
            static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(value * 32767.0))));
  }
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

std::shared_ptr<Clip> make_tone(std::size_t frames, float value) {
  auto clip = std::make_shared<Clip>();
  clip->samples.assign(frames * channels, value);
  return clip;
}

float peak(const std::vector<float>& buffer) {
  float result = 0.f;
  for (const float sample : buffer) {
    result = std::max(result, std::fabs(sample));
  }
  return result;
}

PlayerConfig player_config(const fs::path& root) {
  PlayerConfig config;
  config.source.disk_root = root.string();
  config.output = false;
  config.cache_bytes = std::size_t{8} << 20U;
  return config;
}

void prepare_assets() {
  fs::remove_all(work);
  fs::create_directories(work);
  write_wav(work / "wav" / "tone_a.mp3", 22050, 5512, 440.0);
  write_wav(work / "wav" / "tone_b.mp3", 22050, 5512, 660.0);
  write_wav(work / "long" / "long.mp3", 22050, 31 * 22050, 220.0);
  fs::create_directories(work / "bad");
  std::ofstream(work / "bad" / "garbage.mp3") << std::string(64, 'x') << "this is not audio\n";
}

// ---- Gate, names and defaults
// ---------------------------------------------------------------------------------------

void test_gate_truth_table() {
  // sfx pew fx -> expected: (playSFX && isFX) || (!isFX && playPewPew)
  const struct Row {
    bool sfx, pew, fx, expected;
  } table[] = {
      {false, false, false, false}, {false, false, true, false}, {false, true, false, true},
      {false, true, true, false},   {true, false, false, false}, {true, false, true, true},
      {true, true, false, true},    {true, true, true, true},
  };

  for (const auto& row : table) {
    CHECK(gate_open(row.sfx, row.pew, row.fx) == row.expected);
  }
}

void test_name_validation() {
  for (const char* name :
       {"error", "monster/default_hit", "player/a-b_c", "spell/x/y", "Weapon2/Dagger"}) {
    CHECK(valid_name(name));
  }
  for (const char* name : {"", "/abs", "trail/", "a//b", "..", "../etc", "a/../b", "a\\b", "a.mp3",
                           "a b", "C:/x", "a:b", "\xC3\xBCmlaut", "a/."}) {
    CHECK(!valid_name(name));
  }
  CHECK(!valid_name(std::string(129, 'a')));
  CHECK(valid_name(std::string(128, 'a')));
}

void test_defaults_and_environment() {
  CHECK(!default_sfx_root().empty());
  const auto config = config_from_environment();
  CHECK(config.cache_bytes >= (std::size_t{1} << 20U));
  CHECK(config.pending_timeout == 250ms);
}

// ---- Mixer
// ----------------------------------------------------------------------------------------------------------

void test_mixer_output_and_stop() {
  Mixer mixer;
  auto tone = make_tone(4800, 0.5f);
  CHECK(mixer.play(*tone, 1.f));
  CHECK(tone->pins.load() == 1);
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  CHECK(peak(buffer) > 0.4f);
  CHECK(mixer.active_voices() == 1);
  mixer.stop_all();
  mixer.mix(buffer.data(), 256);
  CHECK(peak(buffer) == 0.f);
  CHECK(mixer.active_voices() == 0);
  CHECK(tone->pins.load() == 0);
}

void test_mixer_clamp_and_ordering() {
  Mixer mixer;
  auto loud = make_tone(4800, 0.9f);
  auto other = make_tone(4800, 0.9f);
  CHECK(mixer.play(*loud, 1.f));
  CHECK(mixer.play(*other, 1.f));
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  // Two voices sum to 1.8 before the master clamp.
  CHECK(peak(buffer) == 1.f);
  // A stop queued before a play must not silence that play: commands apply in order.
  mixer.stop_all();
  auto later = make_tone(4800, 0.5f);
  CHECK(mixer.play(*later, 1.f));
  mixer.mix(buffer.data(), 256);
  CHECK(mixer.active_voices() == 1);
  CHECK(loud->pins.load() == 0);
  CHECK(later->pins.load() == 1);
  CHECK(peak(buffer) > 0.4f);
}

void test_mixer_voice_cap_and_steal() {
  Mixer mixer;
  constexpr std::size_t burst = 200;
  std::vector<std::shared_ptr<Clip>> clips;
  for (std::size_t i = 0; i < burst; ++i) {
    clips.push_back(make_tone(1000, 0.01f));
  }
  for (auto& clip : clips) {
    CHECK(mixer.play(*clip, 1.f));
  }
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  CHECK(mixer.active_voices() == voice_count);
  CHECK(mixer.steals() == burst - voice_count);
  // Oldest voices are stolen first, so only the newest 32 still hold a pin.
  for (std::size_t i = 0; i < burst; ++i) {
    CHECK(clips[i]->pins.load() == (i >= burst - voice_count ? 1 : 0));
  }
  for (int block = 0; block < 4; ++block) {
    mixer.mix(buffer.data(), 256);
  }
  CHECK(mixer.active_voices() == 0);
  for (const auto& clip : clips) {
    CHECK(clip->pins.load() == 0);
  }
  note("voice cap: burst=" + std::to_string(burst) + " active=" + std::to_string(voice_count) +
       " steals=" + std::to_string(mixer.steals()));
}

void test_mixer_full_ring() {
  Mixer mixer;
  std::vector<std::shared_ptr<Clip>> clips;
  for (std::size_t i = 0; i < Mixer::command_capacity; ++i) {
    clips.push_back(make_tone(4800, 0.1f));
    CHECK(mixer.play(*clips.back(), 1.f));
  }
  auto extra = make_tone(4800, 0.1f);
  CHECK(!mixer.play(*extra, 1.f));
  CHECK(extra->pins.load() == 0);
  // A full ring cannot take the stop command, so stop_all falls back to a flag honoured by the next
  // block.
  mixer.stop_all();
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  CHECK(mixer.active_voices() == 0);
  for (const auto& clip : clips) {
    CHECK(clip->pins.load() == 0);
  }
}

// ---- Library: decode, failure memory, bounds, eviction, shutdown
// ---------------------------------------------------

void test_library_decodes_wav() {
  SourceConfig source;
  source.disk_root = (work / "wav").string();
  Library library(source, std::size_t{64} << 20U, 256);
  CHECK(library.request("tone_a") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("tone_a") != Library::State::loading;
  }));
  CHECK(library.state("tone_a") == Library::State::ready);
  const auto clip = library.ready("tone_a");
  CHECK(clip != nullptr);
  // 5512 frames at 22.05 kHz become about 12000 frames at 48 kHz.
  CHECK(clip->frames() >= 11990 && clip->frames() <= 12010);
  CHECK(library.stats().loads_ok == 1);
  CHECK(library.bytes() == clip->bytes());
  Mixer mixer;
  CHECK(mixer.play(*clip, 1.f));
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  CHECK(peak(buffer) > 0.01f);
}

void test_library_decodes_mp3() {
  const auto root = fs::path(config_from_environment().source.disk_root);
  if (!fs::is_regular_file(root / "error.mp3")) {
    std::cout << "  skip: real mp3 case, SFX root missing (" << root.string() << ")\n";
    ++skipped;
    return;
  }
  fs::create_directories(work / "mp3");
  fs::copy_file(root / "error.mp3", work / "mp3" / "error.mp3",
                fs::copy_options::overwrite_existing);
  SourceConfig source;
  source.disk_root = (work / "mp3").string();
  Library library(source, std::size_t{64} << 20U, 256);
  const auto started = Clock::now();
  CHECK(library.request("error") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("error") != Library::State::loading;
  }));
  const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count();
  CHECK(library.state("error") == Library::State::ready);
  const auto clip = library.ready("error");
  CHECK(clip->frames() > 0);
  CHECK(peak(clip->samples) > 0.f);
  note("mp3 error.mp3: frames=" + std::to_string(clip->frames()) +
       " decoded_bytes=" + std::to_string(clip->bytes()) + " load_us=" + std::to_string(micros));
}

void test_library_failures_are_memoised() {
  SourceConfig source;
  source.disk_root = (work / "bad").string();
  Library library(source, std::size_t{1} << 20U, 256);
  CHECK(library.request("garbage") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("garbage") != Library::State::loading;
  }));
  CHECK(library.state("garbage") == Library::State::failed);
  CHECK(library.stats().loads_failed == 1);
  CHECK(library.stats().loads_ok == 0);
  // Failed means failed: no second job, no second count.
  CHECK(library.request("garbage") == Library::State::failed);
  CHECK(library.queued() == 0);
  // A missing asset with no fallback fails too, and is never retried.
  CHECK(library.request("nothing/here") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("nothing/here") == Library::State::failed;
  }));
  CHECK(library.request("nothing/here") == Library::State::failed);
  CHECK(library.stats().loads_failed == 2);
  CHECK(library.queued() == 0);
  CHECK(library.request("bad name") == Library::State::absent);
}

void test_library_truncates_long_clips() {
  SourceConfig source;
  source.disk_root = (work / "long").string();
  Library library(source, std::size_t{64} << 20U, 256);
  CHECK(library.request("long") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("long") != Library::State::loading;
  }));
  const auto clip = library.ready("long");
  CHECK(clip != nullptr);
  CHECK(clip->truncated);
  CHECK(clip->frames() == max_clip_seconds * sample_rate);
  CHECK(library.stats().truncations == 1);
}

void test_library_queue_bounds() {
  Latch latch;
  SourceConfig source;
  source.before_load = [&latch] { latch.enter_and_wait(); };
  Library library(source, std::size_t{1} << 20U, 8);
  ReleaseOnExit release{latch};
  // "held" is taken off the queue by the loader, which then blocks in the hook; q0..q7 fill the
  // queue.
  CHECK(library.request("held") == Library::State::loading);
  CHECK(latch.wait_entered(5s));
  for (int i = 0; i < 8; ++i) {
    CHECK(library.request("q" + std::to_string(i)) == Library::State::loading);
  }
  CHECK(library.queued() == 8);
  CHECK(library.request("q8") == Library::State::absent);
  CHECK(library.request("q9") == Library::State::absent);
  CHECK(library.stats().queue_dropped == 2);
  latch.release();
  CHECK(wait_until([&] {
    library.poll();
    return library.state("q7") != Library::State::loading;
  }));
  // Dropped names were never recorded, so a later request starts a real load.
  CHECK(library.request("q8") == Library::State::loading);
  note("queue bounds: capacity=8 queued=8 dropped=2");
}

void test_pinned_clip_survives_eviction() {
  SourceConfig source;
  source.disk_root = (work / "wav").string();
  // One 0.25 s clip (about 96 kB decoded) fits; two do not.
  Library library(source, 110000, 256);
  CHECK(library.request("tone_a") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("tone_a") == Library::State::ready;
  }));
  auto held = library.ready("tone_a");
  Mixer mixer;
  CHECK(mixer.play(*held, 1.f));
  std::vector<float> buffer(256 * channels);
  mixer.mix(buffer.data(), 256);
  CHECK(held->pins.load() == 1);

  CHECK(library.request("tone_b") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("tone_b") == Library::State::ready;
  }));
  library.trim();
  // tone_b is the only unpinned clip, so it is the victim even though it was loaded last.
  CHECK(library.state("tone_b") == Library::State::absent);
  CHECK(library.state("tone_a") == Library::State::ready);
  CHECK(library.stats().evictions == 1);
  CHECK(library.bytes() == held->bytes());
  // The voice keeps reading the pinned buffer after the eviction pass.
  mixer.mix(buffer.data(), 256);
  CHECK(peak(buffer) > 0.01f);
  for (int block = 0; block < 64 && mixer.active_voices() > 0; ++block) {
    mixer.mix(buffer.data(), 256);
  }
  CHECK(held->pins.load() == 0);
  CHECK(mixer.active_voices() == 0);

  // Unpinned now, so the next pressure round evicts it.
  CHECK(library.request("tone_b") == Library::State::loading);
  CHECK(wait_until([&] {
    library.poll();
    return library.state("tone_b") == Library::State::ready;
  }));
  library.trim();
  CHECK(library.state("tone_a") == Library::State::absent);
  CHECK(library.state("tone_b") == Library::State::ready);
  CHECK(library.stats().evictions == 2);
  held.reset();
  note("eviction: budget=110000 evictions=" + std::to_string(library.stats().evictions) +
       " decoded_bytes=" + std::to_string(library.bytes()));
}

void test_library_shutdown_drops_queue() {
  std::atomic<int> hook_calls{0};
  Latch latch;
  {
    // Releases the loader after the destructor has begun, so the queued loads are cancelled, not
    // run. The margin is generous: if the destructor were late, the loader would run queued jobs
    // and the count below would fail.
    std::jthread releaser([&latch] {
      std::this_thread::sleep_for(500ms);
      latch.release();
    });
    SourceConfig source;
    source.disk_root = (work / "wav").string();
    source.before_load = [&hook_calls, &latch] {
      ++hook_calls;
      latch.enter_and_wait();
    };
    Library library(source, std::size_t{1} << 20U, 256);
    CHECK(library.request("tone_a") == Library::State::loading);
    CHECK(latch.wait_entered(5s));
    for (int i = 0; i < 25; ++i) {
      CHECK(library.request("tone_b" + std::to_string(i)) == Library::State::loading);
    }
    CHECK(library.queued() == 25);
  }
  CHECK(hook_calls.load() == 1);
}

// ---- Player: gate, dedupe, pending, cancel, null device
// ------------------------------------------------------------

void test_player_gate_and_dedupe() {
  Player player(player_config(work / "wav"));
  int ticks = 0;
  auto tick = [&] {
    player.tick();
    ++ticks;
  };
  player.preload("tone_a");
  CHECK(wait_until([&] {
    tick();
    return player.counters().loads_ok == 1;
  }));

  player.set_preferences(false, true);
  player.play("tone_a");  // SFX off, FX play: gated
  player.set_preferences(true, false);
  player.play("tone_a", 1.f, false);  // pew-pew off, non-FX play: gated
  CHECK(player.counters().plays_gated == 2);
  CHECK(player.counters().plays_deduped == 0);  // gated plays never mark the name
  CHECK(player.counters().plays_started == 0);

  player.set_preferences(true, true);
  player.play("tone_a");
  CHECK(player.counters().plays_started == 1);
  player.play("tone_a");
  CHECK(player.counters().plays_deduped == 1);
  // Several frames pass without a clear: the name stays deduped.
  while ((ticks + 1) % 5 != 0) {
    tick();
  }
  player.play("tone_a");
  CHECK(player.counters().plays_deduped == 2);
  CHECK(player.counters().plays_started == 1);
  // The 5th tick clears the map (clearTicks % 5 == 0).
  tick();
  player.play("tone_a");
  CHECK(player.counters().plays_started == 2);
  CHECK(player.counters().plays_requested == 6);
  CHECK(player.counters().plays_deduped == 2);
  player.stop_all();
}

void test_player_pending_starts_within_window() {
  Latch latch;
  auto config = player_config(work / "wav");
  config.pending_timeout = 10s;
  config.source.before_load = [&latch] { latch.enter_and_wait(); };
  Player player(config);
  ReleaseOnExit release{latch};
  player.play("tone_b");
  CHECK(player.counters().plays_deferred == 1);
  CHECK(player.counters().pending_now == 1);
  CHECK(player.counters().plays_started == 0);
  latch.release();
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().plays_started == 1;
  }));
  const auto counters = player.counters();
  CHECK(counters.pending_now == 0);
  CHECK(counters.pending_dropped == 0);
  CHECK(counters.loads_ok == 1);
}

void test_player_pending_drops_after_window() {
  Latch latch;
  auto config = player_config(work / "wav");
  config.pending_timeout = 50ms;
  config.source.before_load = [&latch] { latch.enter_and_wait(); };
  Player player(config);
  ReleaseOnExit release{latch};
  player.play("tone_a");
  CHECK(player.counters().pending_now == 1);
  std::this_thread::sleep_for(150ms);
  player.tick();
  CHECK(player.counters().pending_dropped == 1);
  CHECK(player.counters().pending_now == 0);
  latch.release();
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().loads_ok == 1;
  }));
  CHECK(player.counters().plays_started == 0);
  CHECK(player.counters().pending_dropped == 1);
}

void test_player_never_blocks_on_loads() {
  // The loader stays held for a second. Every call must return inside that window.
  Latch latch;
  auto config = player_config(work / "wav");
  config.source.before_load = [&latch] { latch.enter_and_wait(); };
  Player player(config);
  std::jthread timed_release([&latch] {
    std::this_thread::sleep_for(1000ms);
    latch.release();
  });
  ReleaseOnExit release{latch};
  const auto started = Clock::now();
  for (int i = 0; i < 50; ++i) {
    player.play("missing/" + std::to_string(i));
  }
  player.preload("tone_b");
  player.tick();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
  CHECK(elapsed < 800);
  note("never-blocks: 50 plays + preload + tick in " + std::to_string(elapsed) +
       " ms while the loader was held");
  CHECK(player.counters().plays_deferred == 50);
}

void test_player_cancel_and_failed_loads() {
  Latch latch;
  auto config = player_config(work / "wav");
  config.pending_timeout = 10s;
  config.source.before_load = [&latch] { latch.enter_and_wait(); };
  Player player(config);
  ReleaseOnExit release{latch};
  player.play("tone_a");
  player.play("tone_b");
  CHECK(player.counters().pending_now == 2);
  player.cancel_pending();
  CHECK(player.counters().pending_dropped == 2);
  CHECK(player.counters().pending_now == 0);
  latch.release();
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().loads_ok == 2;
  }));
  CHECK(player.counters().plays_started == 0);

  // A missing asset fails once: its play is dropped, and later plays after the dedupe clear never
  // retry it.
  player.play("nothing/here");
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().loads_failed == 1;
  }));
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().pending_dropped == 3;
  }));
  const auto deferred = player.counters().plays_deferred;
  for (int i = 0; i < 5; ++i) {
    player.tick();
  }
  player.play("nothing/here");
  CHECK(player.counters().loads_failed == 1);
  CHECK(player.counters().plays_deferred == deferred);
  CHECK(player.counters().plays_started == 0);
}

void test_player_null_device() {
  auto config = player_config(work / "wav");
  config.output = true;
  config.null_output = true;
  Player player(config);
  if (!player.output_active()) {
    std::cout << "  skip: null backend unavailable (" << player.output_status() << ")\n";
    ++skipped;
    return;
  }
  CHECK(player.output_status().rfind("output ", 0) == 0);
  player.preload("tone_a");
  CHECK(wait_until([&] {
    player.tick();
    return player.counters().loads_ok == 1;
  }));
  player.play("tone_a");
  CHECK(player.counters().plays_started == 1);
  CHECK(player.counters().decoded_bytes > 0);
  // The device thread drains the command ring and runs the voice; the 0.25 s clip outlives the poll
  // interval.
  CHECK(wait_until([&] { return player.counters().voices_active == 1; }, 2s));
  player.stop_all();
  CHECK(wait_until([&] { return player.counters().voices_active == 0; }, 2s));
  note("null device: " + player.output_status() +
       ", decoded_bytes=" + std::to_string(player.counters().decoded_bytes));
}

// ---- Bench
// ----------------------------------------------------------------------------------------------------------

double percentile(const std::vector<double>& sorted, double fraction) {
  return sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1))];
}

void report_bench(const char* label, Player& player, int frames) {
  std::vector<double> micros;
  const std::vector<std::string> names = [] {
    std::vector<std::string> result;
    for (int i = 0; i < 16; ++i) {
      result.push_back("bench/b" + std::to_string(i));
    }
    return result;
  }();
  for (int frame = 0; frame < frames; ++frame) {
    player.tick();
    for (const auto& name : names) {
      const auto t0 = Clock::now();
      player.play(name);
      const auto t1 = Clock::now();
      micros.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
  }
  std::sort(micros.begin(), micros.end());
  const auto counters = player.counters();
  std::cout << "bench " << label << ": calls=" << micros.size()
            << " p50=" << percentile(micros, 0.5) << "us p99=" << percentile(micros, 0.99)
            << "us max=" << micros.back() << "us started=" << counters.plays_started
            << " deduped=" << counters.plays_deduped
            << " command_dropped=" << counters.command_dropped << " steals=" << counters.steals
            << '\n';
}

int run_bench() {
  const fs::path bench = work / "bench";
  for (int i = 0; i < 16; ++i) {
    write_wav(bench / "bench" / ("b" + std::to_string(i) + ".mp3"), 22050, 5512, 300.0 + i * 20);
  }
  {
    auto config = player_config(bench);
    Player player(config);
    for (int i = 0; i < 16; ++i) {
      player.preload("bench/b" + std::to_string(i));
    }
    for (int i = 0; i < 2000 && player.counters().loads_ok < 16; ++i) {
      player.tick();
      std::this_thread::sleep_for(1ms);
    }
    report_bench("no-device", player, 2000);
  }
  {
    auto config = player_config(bench);
    config.output = true;
    config.null_output = true;
    Player player(config);
    for (int i = 0; i < 16; ++i) {
      player.preload("bench/b" + std::to_string(i));
    }
    for (int i = 0; i < 2000 && player.counters().loads_ok < 16; ++i) {
      player.tick();
      std::this_thread::sleep_for(1ms);
    }
    if (player.output_active()) {
      report_bench("null-device", player, 2000);
    } else {
      std::cout << "bench null-device: unavailable (" << player.output_status() << ")\n";
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    bool bench_mode = false;
    std::string directory = "audio-test";
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--bench") {
        bench_mode = true;
      } else {
        directory = argument;
      }
    }
    work = directory;
    prepare_assets();
    if (bench_mode) {
      return run_bench();
    }

    const struct Test {
      const char* name;
      void (*run)();
    } tests[] = {
        {"gate_truth_table", test_gate_truth_table},
        {"name_validation", test_name_validation},
        {"defaults_and_environment", test_defaults_and_environment},
        {"mixer_output_and_stop", test_mixer_output_and_stop},
        {"mixer_clamp_and_ordering", test_mixer_clamp_and_ordering},
        {"mixer_voice_cap_and_steal", test_mixer_voice_cap_and_steal},
        {"mixer_full_ring", test_mixer_full_ring},
        {"library_decodes_wav", test_library_decodes_wav},
        {"library_decodes_mp3", test_library_decodes_mp3},
        {"library_failures_are_memoised", test_library_failures_are_memoised},
        {"library_truncates_long_clips", test_library_truncates_long_clips},
        {"library_queue_bounds", test_library_queue_bounds},
        {"pinned_clip_survives_eviction", test_pinned_clip_survives_eviction},
        {"library_shutdown_drops_queue", test_library_shutdown_drops_queue},
        {"player_gate_and_dedupe", test_player_gate_and_dedupe},
        {"player_pending_starts_within_window", test_player_pending_starts_within_window},
        {"player_pending_drops_after_window", test_player_pending_drops_after_window},
        {"player_never_blocks_on_loads", test_player_never_blocks_on_loads},
        {"player_cancel_and_failed_loads", test_player_cancel_and_failed_loads},
        {"player_null_device", test_player_null_device},
    };

    int failures = 0;
    int passed = 0;
    for (const auto& test : tests) {
      try {
        test.run();
        ++passed;
      } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
      }
    }
    std::cout << "Audio: " << passed << " passed, " << failures << " failed, " << skipped
              << " skipped of " << std::size(tests)
              << " (gate, names, mixer voices/steals/clamp/pins, WAV and mp3 decode, failure "
              << "memory, queue bounds, pinned LRU eviction, shutdown, Player "
                 "gate/dedupe/pending/null device)\n";
    return failures == 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
