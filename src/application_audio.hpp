#pragma once
#include "application_options.hpp"
#include "realm/settings.hpp"
#include "realm/sound_cues.hpp"
#include <memory>
#include <string>

namespace realm::application {

// The process's one sound output. Game code queues SoundCues; this bridge plays them once per rendered frame and
// ticks the player. Without REALM_AUDIO it is inert but has the same interface, so the frame loop never branches.
class AudioBridge {
 public:
  // Options.audio: auto (the interactive client has a device; fixtures and tools do not), on, off, or null (miniaudio's
  // null backend). REALM_AUDIO, REALM_AUDIO_DEVICE, REALM_AUDIO_CACHE_MB and REALM_SFX_ROOT still apply.
  explicit AudioBridge(const Options& options);
  ~AudioBridge();
  AudioBridge(const AudioBridge&) = delete;
  AudioBridge& operator=(const AudioBridge&) = delete;

  // Parameters.data_ playSFX and playPewPew: at settings load and whenever options change.
  void set_preferences(const Preferences& preferences);
  // Plays (or preloads) the cues one source queued, in order. Call for each queue, then tick() once per frame.
  void play(SoundCues& cues);
  void tick();
  // MAPINFO: plays still waiting for a load belong to the old map.
  void map_changed();
  // The session ended (death, disconnect, leaving): voices end and waiting plays are dropped.
  void stop();
  [[nodiscard]] bool output_active() const;
  [[nodiscard]] const std::string& status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
