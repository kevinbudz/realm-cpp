#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace realm {

// One sound request from game logic (SoundEffectLibrary.play / load). Headless: the audio layer
// decides what to do with it, so game code never includes the audio library.
struct SoundCue {
  std::string name;
  float volume{1.f};
  bool is_fx{true};
  // Load only (SoundEffectLibrary.load, e.g. GameObject.loadSounds): never starts a play.
  bool preload{};
};

// Bounded per-frame queue. Identical cues in one frame (many hits on one monster type, say)
// collapse into one; SoundEffectLibrary's dedupe would play at most one of them anyway, and a flood
// must not push out later, different cues. A distinct cue is dropped when the queue is full and
// counted. The application drains the queue once per rendered frame.
class SoundCues {
 public:
  static constexpr std::size_t capacity = 256;

  // True when the cue is queued or merged into an identical one; false when it was dropped.
  bool push(SoundCue cue) {
    auto key = identity(cue);
    if (keys_.contains(key)) {
      ++coalesced_;
      return true;
    }
    if (cues_.size() >= capacity) {
      ++dropped_;
      return false;
    }
    keys_.insert(std::move(key));
    cues_.push_back(std::move(cue));
    return true;
  }

  void play(std::string name, float volume = 1.f, bool is_fx = true) {
    push({std::move(name), volume, is_fx, false});
  }

  void preload(std::string name) { push({std::move(name), 1.f, true, true}); }

  // Replaces `out` with the pending cues in push order; swapping keeps both buffers' capacity
  // across frames. Returns how many distinct cues were dropped for overflow since the previous
  // drain.
  std::uint64_t drain(std::vector<SoundCue>& out) {
    out.clear();
    out.swap(cues_);
    keys_.clear();
    const auto overflow = dropped_ - dropped_drained_;
    dropped_drained_ = dropped_;
    return overflow;
  }

  [[nodiscard]] std::size_t size() const { return cues_.size(); }

  [[nodiscard]] std::uint64_t dropped() const { return dropped_; }

  [[nodiscard]] std::uint64_t coalesced() const { return coalesced_; }

 private:
  // Everything the audio layer reads from a cue: name, volume bits, gate flag and load flag.
  static std::string identity(const SoundCue& cue) {
    std::string key;
    key.reserve(cue.name.size() + 6);
    key.push_back(cue.preload ? 'p' : 'c');
    key.push_back(cue.is_fx ? 'f' : 'n');
    const auto bits = std::bit_cast<std::uint32_t>(cue.volume);
    for (unsigned shift = 0; shift < 32; shift += 8) {
      key.push_back(static_cast<char>(bits >> shift));
    }
    key += cue.name;
    return key;
  }

  std::vector<SoundCue> cues_;
  std::unordered_set<std::string> keys_;
  std::uint64_t dropped_{}, dropped_drained_{}, coalesced_{};
};

}
