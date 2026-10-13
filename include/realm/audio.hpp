#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Native one-shot SFX that mirrors SoundEffectLibrary. Threads: the main thread owns Player, Library and the
// command side of Mixer; one loader thread reads and decodes; the output device thread calls Mixer::mix. The render
// path never allocates, blocks or frees a clip.
namespace realm::audio {

// Mixer format. Every clip is decoded to this before it is published.
inline constexpr std::uint32_t sample_rate = 48000;
inline constexpr std::uint32_t channels = 2;
inline constexpr std::size_t voice_count = 32;
// Longer assets are cut to this length and counted as truncations.
inline constexpr std::size_t max_clip_seconds = 30;

// SoundEffectLibrary.play gate: (playSFX && isFX) || (!isFX && playPewPew).
[[nodiscard]] constexpr bool gate_open(bool play_sfx, bool play_pew_pew, bool is_fx) noexcept {
  return (play_sfx && is_fx) || (!is_fx && play_pew_pew);
}
// NAME such as "monster/default_hit": [A-Za-z0-9_-] segments separated by single '/', no leading or trailing '/'.
[[nodiscard]] bool valid_name(std::string_view name) noexcept;
// Asset root compiled in from the REALM_SFX_ROOT cache variable (empty when not configured).
[[nodiscard]] std::string default_sfx_root();

// A decoded one-shot sound. Immutable once published, apart from its pin count.
struct Clip {
  std::vector<float> samples;  // interleaved stereo at sample_rate
  bool truncated{false};
  // Voices and queued mixer commands that reference this clip. The main thread increments it before publishing a
  // command; the mixer decrements it when the voice ends. Only the main thread frees a clip, and only at zero.
  std::atomic<int> pins{0};
  [[nodiscard]] std::size_t frames() const noexcept { return samples.size() / channels; }
  [[nodiscard]] std::size_t bytes() const noexcept { return samples.size() * sizeof(float); }
};

// Fixed-voice one-shot mixer. The main thread posts commands through a bounded SPSC ring; the output thread drains
// them and renders. Voices run to the end of their clip, the oldest is stolen when all are busy, and the master is
// clamped to [-1, 1].
class Mixer {
 public:
  static constexpr std::size_t command_capacity = 256;
  // Main thread only. Pins the clip and queues a voice. False when the ring is full, leaving the clip untouched.
  [[nodiscard]] bool play(Clip& clip, float gain) noexcept;
  // Main thread only. Ends every voice before the next rendered block. Never fails; falls back to a flag when full.
  void stop_all() noexcept;
  // Output thread only (or a test). Applies queued commands in order, then writes frames of interleaved stereo.
  void mix(float* out, std::size_t frames) noexcept;
  [[nodiscard]] std::uint32_t active_voices() const noexcept { return active_.load(std::memory_order_relaxed); }
  [[nodiscard]] std::uint64_t steals() const noexcept { return steals_.load(std::memory_order_relaxed); }

 private:
  struct Command {
    Clip* clip;
    float gain;
    bool stop;
  };
  struct Voice {
    Clip* clip{nullptr};
    std::size_t position{0};
    float gain{0.f};
    std::uint64_t order{0};
  };
  void start(Clip& clip, float gain) noexcept;
  void release(Voice& voice) noexcept;
  void end_voices() noexcept;

  std::array<Command, command_capacity> commands_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<bool> stop_requested_{false};
  std::array<Voice, voice_count> voices_{};
  std::uint64_t next_order_{0};
  std::atomic<std::uint32_t> active_{0};
  std::atomic<std::uint64_t> steals_{0};
};

// Where clips come from. Disk is primary; HTTP is the fallback when the file is missing on disk.
struct SourceConfig {
  std::string disk_root;  // holds NAME.mp3 below it; empty disables disk reads
  std::string http_host;  // empty disables the fallback, which requests GET /sfx/NAME.mp3
  std::uint16_t http_port{0};
  std::chrono::milliseconds http_timeout{std::chrono::seconds(5)};
  // Test hook: runs on the loader thread before each load. A blocking hook holds the loader.
  std::function<void()> before_load;
};

// Name to clip cache with one loader thread. A failed load is remembered and never retried. Ready clips are evicted
// least-recently-used once over budget, but never while a voice or queued command pins them.
class Library {
 public:
  enum class State : std::uint8_t { absent, loading, ready, failed };
  struct Stats {
    std::uint64_t loads_ok{0};
    std::uint64_t loads_failed{0};
    std::uint64_t queue_dropped{0};
    std::uint64_t evictions{0};
    std::uint64_t truncations{0};
  };

  Library(SourceConfig source, std::size_t cache_bytes, std::size_t queue_capacity);
  // Drops queued loads, lets an in-flight load stop at its next check, then joins the loader.
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  // Main thread only from here on.
  [[nodiscard]] State state(std::string_view name) const;
  // Starts a load for an absent name. Returns loading, or absent when the queue is full or the name is invalid.
  State request(std::string_view name);
  // Ready clip for name (refreshes its recency), or null.
  [[nodiscard]] std::shared_ptr<Clip> ready(std::string_view name);
  // Publishes finished loads.
  void poll();
  // Evicts unpinned clips, least recently used first, until the decoded bytes fit the budget.
  void trim();
  [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
  [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t queued() const;

 private:
  struct Entry {
    State state{State::loading};
    std::shared_ptr<Clip> clip;
    std::uint64_t last_used{0};
  };
  struct Result {
    std::string name;
    std::shared_ptr<Clip> clip;  // null on failure
    bool truncated{false};
  };
  void run();

  const SourceConfig source_;
  const std::size_t cache_bytes_;
  const std::size_t queue_capacity_;
  std::map<std::string, Entry, std::less<>> entries_;
  std::size_t bytes_{0};
  std::uint64_t clock_{0};
  Stats stats_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::string> jobs_;  // guarded by mutex_
  std::vector<Result> results_;   // guarded by mutex_
  std::atomic<bool> stopping_{false};
  std::thread worker_;  // declared last: starts after every other member exists
};

// Test and tuning knobs. The defaults are the SoundEffectLibrary behavior.
struct PlayerConfig {
  SourceConfig source{};
  std::size_t cache_bytes{std::size_t{48} << 20U};
  std::size_t queue_capacity{256};
  // false: no device. Plays are still gated, deduplicated and loaded, and count as started, but nothing renders.
  bool output{true};
  // true: miniaudio's null backend instead of the system device (silent, real-time).
  bool null_output{false};
  // A play waiting for its load starts only if the load completes within this window.
  std::chrono::milliseconds pending_timeout{250};
};

// Defaults with the environment applied: REALM_AUDIO=0 disables output, REALM_AUDIO_DEVICE=null selects the null
// backend, REALM_AUDIO_CACHE_MB sets the decoded budget (1-4096 MiB), REALM_SFX_ROOT overrides the disk root.
[[nodiscard]] PlayerConfig config_from_environment();

// Monotonic counts since construction, except the gauges pending_now, voices_active and decoded_bytes.
// plays_started counts plays accepted by the mixer, or accepted with no device.
struct Counters {
  std::uint64_t plays_requested{0};
  std::uint64_t plays_gated{0};
  std::uint64_t plays_rejected{0};
  std::uint64_t plays_deduped{0};
  std::uint64_t plays_started{0};
  std::uint64_t plays_deferred{0};
  std::uint64_t pending_now{0};
  std::uint64_t pending_dropped{0};
  std::uint64_t command_dropped{0};
  std::uint64_t voices_active{0};
  std::uint64_t steals{0};
  std::uint64_t loads_ok{0};
  std::uint64_t loads_failed{0};
  std::uint64_t queue_dropped{0};
  std::uint64_t decoded_bytes{0};
  std::uint64_t evictions{0};
  std::uint64_t truncations{0};
};

// Main-thread facade. play, preload, tick, cancel_pending and stop_all never block on disk, HTTP, decode or the device.
class Player {
 public:
  explicit Player(const PlayerConfig& config);
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  void set_preferences(bool play_sfx, bool play_pew_pew);
  // SoundEffectLibrary.play: gate, then dedupe, then start, defer, or drop when the sound failed to load.
  void play(std::string_view name, float volume = 1.f, bool is_fx = true);
  void preload(std::string_view name);
  // Once per rendered frame. Clears the dedupe set on every 5th call and services loads and deferred plays.
  void tick();
  // Drops plays waiting for a load (map change).
  void cancel_pending();
  // Ends every voice and cancels pending plays (leaving the game).
  void stop_all();
  [[nodiscard]] bool output_active() const noexcept;
  // "output <backend>" when a device runs, otherwise the reason it does not.
  [[nodiscard]] const std::string& output_status() const noexcept;
  [[nodiscard]] Counters counters() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace realm::audio
