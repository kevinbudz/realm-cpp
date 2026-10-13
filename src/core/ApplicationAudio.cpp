#include "ApplicationAudio.hpp"
#include "realm/util/Telemetry.hpp"
#ifdef REALM_AUDIO
#include "realm/sound/SoundEffectLibrary.hpp"
#include <array>
#endif
#include <vector>

namespace realm::application {

#ifdef REALM_AUDIO
namespace {
// AssetLoader.addSoundEffects: the names loaded before any object needs them.
constexpr std::array startup_sounds{"button_click", "death_screen", "enter_realm", "error",   "inventory_move_item",
                                    "level_up",     "loot_appears", "no_mana",     "use_key", "use_potion"};
}

struct AudioBridge::Impl {
  explicit Impl(const audio::PlayerConfig& config) : player(config), enabled(config.output) {}
  audio::Player player;
  // Off (fixtures, tools, --audio off): cues are discarded and nothing loads, so frame timings are unaffected.
  bool enabled;
  std::vector<SoundCue> scratch;
  audio::Counters previous{};
  std::uint64_t overflow{};
};

AudioBridge::AudioBridge(const Options& options) {
  auto config = audio::config_from_environment();
  // Loads fall back to the application server's /sfx/NAME.mp3 when the file is not on disk.
  config.source.http_host = options.session.host;
  config.source.http_port = options.app_port;
  if (options.audio == "off")
    config.output = false;
  else if (options.audio == "on")
    config.output = true;
  else if (options.audio == "null") {
    config.output = true;
    config.null_output = true;
  } else
    config.output = config.output && options.client;
  impl_ = std::make_unique<Impl>(config);
  if (impl_->enabled)
    for (const char* name : startup_sounds) impl_->player.preload(name);
}

AudioBridge::~AudioBridge() = default;

void AudioBridge::set_preferences(const Preferences& preferences) {
  if (impl_->enabled) impl_->player.set_preferences(preferences.play_sfx, preferences.play_pew_pew);
}

void AudioBridge::play(SoundCues& cues) {
  REALM_ZONE("audio.play");
  impl_->overflow += cues.drain(impl_->scratch);
  if (!impl_->enabled) {
    impl_->scratch.clear();
    return;
  }
  for (const auto& cue : impl_->scratch) {
    if (cue.preload)
      impl_->player.preload(cue.name);
    else
      impl_->player.play(cue.name, cue.volume, cue.is_fx);
  }
}

void AudioBridge::tick() {
  REALM_ZONE("audio.tick");
  if (!impl_->enabled) return;
  impl_->player.tick();
  const auto now = impl_->player.counters();
  const auto& before = impl_->previous;
  using C = telemetry::Counter;
  // Event counts are per-frame deltas (the recorder resets its counters each frame); gauges are values.
  telemetry::add(C::audio_plays_requested, now.plays_requested - before.plays_requested);
  telemetry::add(C::audio_plays_gated, now.plays_gated - before.plays_gated);
  telemetry::add(C::audio_plays_deduped, now.plays_deduped - before.plays_deduped);
  telemetry::add(C::audio_plays_started, now.plays_started - before.plays_started);
  telemetry::add(C::audio_plays_deferred, now.plays_deferred - before.plays_deferred);
  telemetry::add(C::audio_pending_dropped, now.pending_dropped - before.pending_dropped);
  telemetry::add(C::audio_command_dropped, now.command_dropped - before.command_dropped);
  telemetry::add(C::audio_steals, now.steals - before.steals);
  telemetry::add(C::audio_loads_ok, now.loads_ok - before.loads_ok);
  telemetry::add(C::audio_loads_failed, now.loads_failed - before.loads_failed);
  telemetry::add(C::audio_queue_dropped, now.queue_dropped - before.queue_dropped);
  telemetry::add(C::audio_evictions, now.evictions - before.evictions);
  telemetry::add(C::audio_cue_overflow, impl_->overflow);
  telemetry::set(C::audio_voices_active, now.voices_active);
  telemetry::set(C::audio_decoded_bytes, now.decoded_bytes);
  impl_->previous = now;
  impl_->overflow = 0;
}

void AudioBridge::map_changed() {
  if (impl_->enabled) impl_->player.cancel_pending();
}

void AudioBridge::stop() {
  if (impl_->enabled) impl_->player.stop_all();
}

bool AudioBridge::output_active() const { return impl_->enabled && impl_->player.output_active(); }

const std::string& AudioBridge::status() const { return impl_->player.output_status(); }
#else
// Built without REALM_AUDIO: cues are consumed and discarded, so the game side behaves identically.
struct AudioBridge::Impl {};
AudioBridge::AudioBridge(const Options&) {}
AudioBridge::~AudioBridge() = default;
void AudioBridge::set_preferences(const Preferences&) {}
void AudioBridge::play(SoundCues& cues) {
  std::vector<SoundCue> discarded;
  cues.drain(discarded);
}
void AudioBridge::tick() {}
void AudioBridge::map_changed() {}
void AudioBridge::stop() {}
bool AudioBridge::output_active() const { return false; }
const std::string& AudioBridge::status() const {
  static const std::string disabled{"audio disabled (built without REALM_AUDIO)"};
  return disabled;
}
#endif

}
