#include "realm/audio.hpp"
#include "realm/http.hpp"
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace realm::audio {
namespace {

// Same ceiling as the HTTP response limit; SFX files are a few KB.
constexpr std::size_t max_source_bytes = 8U << 20U;
constexpr std::size_t decode_chunk_frames = 4096;
constexpr std::size_t max_clip_frames = max_clip_seconds * sample_rate;
constexpr std::size_t max_name_length = 128;

// Reads the asset from the disk root. Empty when the root is unset, the file is missing, or it cannot be read.
std::optional<std::string> read_disk(const SourceConfig& source, const std::string& name) {
  if (source.disk_root.empty()) return std::nullopt;
  const auto path = std::filesystem::path(source.disk_root) / (name + ".mp3");
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > static_cast<std::uintmax_t>(max_source_bytes)) return std::nullopt;
  std::string bytes(static_cast<std::size_t>(size), '\0');
  std::ifstream file(path, std::ios::binary);
  if (!file || !file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
  return bytes;
}

// Fallback fetch. Any transport error, timeout or non-200 status counts as a missing asset.
std::optional<std::string> read_http(const SourceConfig& source, const std::string& name) {
  if (source.http_host.empty()) return std::nullopt;
  try {
    auto response = net::http_get(source.http_host, source.http_port, "/sfx/" + name + ".mp3", source.http_timeout);
    if (response.status == 200 && !response.body.empty()) return std::optional<std::string>(std::move(response.body));
  } catch (const std::exception&) {
  }
  return std::nullopt;
}

// Decodes to interleaved stereo float at the mixer rate, cutting at the clip limit. False for malformed or empty
// input, or when stopping is requested mid-decode.
bool decode(std::string_view bytes, const std::atomic<bool>& stopping, Clip& clip) {
  ma_decoder decoder;
  const auto config = ma_decoder_config_init(ma_format_f32, channels, sample_rate);
  if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) return false;
  const struct Release {
    ma_decoder* target;
    ~Release() { ma_decoder_uninit(target); }
  } release{&decoder};

  std::vector<float> samples;
  ma_uint64 length{};
  if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length > 0)
    samples.reserve(static_cast<std::size_t>(std::min<ma_uint64>(length, max_clip_frames + decode_chunk_frames)) *
                    channels);
  std::vector<float> chunk(decode_chunk_frames * channels);
  while (!stopping.load(std::memory_order_relaxed)) {
    ma_uint64 frames{};
    ma_decoder_read_pcm_frames(&decoder, chunk.data(), decode_chunk_frames, &frames);
    if (frames == 0) break;
    samples.insert(samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(frames * channels));
    if (samples.size() > max_clip_frames * channels) break;
  }
  if (stopping.load(std::memory_order_relaxed)) return false;
  if (samples.size() > max_clip_frames * channels) {
    clip.truncated = true;
    samples.resize(max_clip_frames * channels);
  }
  if (samples.empty()) return false;
  clip.samples = std::move(samples);
  return true;
}

// Output device wrapper. Only this translation unit sees miniaudio.
class Device {
 public:
  // Opens and starts playback. Returns null with status set to the reason when no device is available.
  static std::unique_ptr<Device> open(Mixer& mixer, bool null_backend, std::string& status) {
    std::unique_ptr<Device> device(new Device);
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = channels;
    config.sampleRate = sample_rate;
    config.dataCallback = &render;
    config.pUserData = &mixer;
    ma_result result{};
    if (null_backend) {
      const ma_backend backends[] = {ma_backend_null};
      result = ma_device_init_ex(backends, 1, nullptr, &config, &device->device_);
    } else {
      result = ma_device_init(nullptr, &config, &device->device_);
    }
    if (result != MA_SUCCESS) {
      status = "no output device (" + std::string(ma_result_description(result)) + ")";
      return nullptr;
    }
    device->initialized_ = true;
    result = ma_device_start(&device->device_);
    if (result != MA_SUCCESS) {
      status = "output device failed to start (" + std::string(ma_result_description(result)) + ")";
      return nullptr;
    }
    status = std::string("output ") + ma_get_backend_name(device->device_.pContext->backend);
    return device;
  }
  ~Device() {
    if (initialized_) ma_device_uninit(&device_);
  }
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

 private:
  Device() = default;
  // Audio thread: the only call into the mixer from the device.
  static void render(ma_device* device, void* output, const void*, ma_uint32 frames) {
    static_cast<Mixer*>(device->pUserData)->mix(static_cast<float*>(output), frames);
  }
  ma_device device_{};
  bool initialized_{false};
};

struct NameHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
};

}  // namespace

bool valid_name(std::string_view name) noexcept {
  if (name.empty() || name.size() > max_name_length || name.front() == '/' || name.back() == '/') return false;
  char previous = '\0';
  for (const char c : name) {
    const bool allowed =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
    if (!allowed || (c == '/' && previous == '/')) return false;
    previous = c;
  }
  return true;
}

std::string default_sfx_root() {
#ifdef REALM_SFX_ROOT_DEFAULT
  return REALM_SFX_ROOT_DEFAULT;
#else
  return {};
#endif
}

// ---- Mixer ----------------------------------------------------------------------------------------------------------

bool Mixer::play(Clip& clip, float gain) noexcept {
  const auto tail = tail_.load(std::memory_order_relaxed);
  if (tail - head_.load(std::memory_order_acquire) >= command_capacity) return false;
  // Pin before publishing: the mixer can only release what it has already seen.
  clip.pins.fetch_add(1);
  commands_[tail % command_capacity] = Command{&clip, gain, false};
  tail_.store(tail + 1, std::memory_order_release);
  return true;
}

void Mixer::stop_all() noexcept {
  const auto tail = tail_.load(std::memory_order_relaxed);
  if (tail - head_.load(std::memory_order_acquire) < command_capacity) {
    commands_[tail % command_capacity] = Command{nullptr, 0.f, true};
    tail_.store(tail + 1, std::memory_order_release);
  } else {
    // Ring full: the mixer ends voices at the start of its next block, after the queued plays.
    stop_requested_.store(true, std::memory_order_release);
  }
}

void Mixer::release(Voice& voice) noexcept {
  voice.clip->pins.fetch_sub(1);
  voice.clip = nullptr;
}

void Mixer::start(Clip& clip, float gain) noexcept {
  Voice* target = nullptr;
  for (auto& voice : voices_) {
    if (!voice.clip) {
      target = &voice;
      break;
    }
  }
  if (!target) {
    target = &voices_.front();
    for (auto& voice : voices_) {
      if (voice.order < target->order) target = &voice;
    }
    release(*target);
    steals_.fetch_add(1, std::memory_order_relaxed);
  }
  target->clip = &clip;
  target->position = 0;
  target->gain = gain;
  target->order = ++next_order_;
}

void Mixer::end_voices() noexcept {
  for (auto& voice : voices_) {
    if (voice.clip) release(voice);
  }
}

void Mixer::mix(float* out, std::size_t frames) noexcept {
  auto head = head_.load(std::memory_order_relaxed);
  const auto tail = tail_.load(std::memory_order_acquire);
  for (; head != tail; ++head) {
    const Command command = commands_[head % command_capacity];
    if (command.stop)
      end_voices();
    else
      start(*command.clip, command.gain);
  }
  head_.store(head, std::memory_order_release);
  if (stop_requested_.exchange(false, std::memory_order_acq_rel)) end_voices();

  std::fill_n(out, frames * channels, 0.f);
  std::uint32_t active = 0;
  for (auto& voice : voices_) {
    if (!voice.clip) continue;
    const float* samples = voice.clip->samples.data();
    const std::size_t total = voice.clip->frames();
    const std::size_t count = std::min(frames, total - voice.position);
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t source = (voice.position + i) * channels;
      const std::size_t target = i * channels;
      out[target] += samples[source] * voice.gain;
      out[target + 1] += samples[source + 1] * voice.gain;
    }
    voice.position += count;
    if (voice.position >= total)
      release(voice);
    else
      ++active;
  }
  for (std::size_t i = 0; i < frames * channels; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
  active_.store(active, std::memory_order_relaxed);
}

// ---- Library --------------------------------------------------------------------------------------------------------

Library::Library(SourceConfig source, std::size_t cache_bytes, std::size_t queue_capacity)
    : source_(std::move(source)),
      cache_bytes_(cache_bytes),
      queue_capacity_(std::max<std::size_t>(queue_capacity, 1)),
      worker_([this] { run(); }) {}

Library::~Library() {
  {
    std::lock_guard lock(mutex_);
    stopping_.store(true);
    jobs_.clear();
  }
  wake_.notify_all();
  worker_.join();
}

Library::State Library::state(std::string_view name) const {
  const auto found = entries_.find(name);
  return found == entries_.end() ? State::absent : found->second.state;
}

Library::State Library::request(std::string_view name) {
  if (const auto found = entries_.find(name); found != entries_.end()) return found->second.state;
  if (!valid_name(name)) return State::absent;
  {
    std::lock_guard lock(mutex_);
    if (jobs_.size() >= queue_capacity_) {
      ++stats_.queue_dropped;
      return State::absent;
    }
    jobs_.emplace_back(name);
  }
  wake_.notify_one();
  entries_.emplace(std::string(name), Entry{});
  return State::loading;
}

std::shared_ptr<Clip> Library::ready(std::string_view name) {
  const auto found = entries_.find(name);
  if (found == entries_.end() || found->second.state != State::ready) return nullptr;
  found->second.last_used = ++clock_;
  return found->second.clip;
}

void Library::poll() {
  std::vector<Result> done;
  {
    std::lock_guard lock(mutex_);
    done.swap(results_);
  }
  for (auto& result : done) {
    const auto found = entries_.find(result.name);
    if (found == entries_.end() || found->second.state != State::loading) continue;
    Entry& entry = found->second;
    if (result.clip) {
      entry.state = State::ready;
      entry.clip = std::move(result.clip);
      entry.last_used = ++clock_;
      bytes_ += entry.clip->bytes();
      ++stats_.loads_ok;
      if (result.truncated) ++stats_.truncations;
    } else {
      entry.state = State::failed;
      ++stats_.loads_failed;
    }
  }
}

void Library::trim() {
  while (bytes_ > cache_bytes_) {
    auto victim = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      const Entry& entry = it->second;
      if (entry.state != State::ready || entry.clip->pins.load() != 0) continue;
      if (victim == entries_.end() || entry.last_used < victim->second.last_used) victim = it;
    }
    if (victim == entries_.end()) return;
    bytes_ -= victim->second.clip->bytes();
    entries_.erase(victim);
    ++stats_.evictions;
  }
}

std::size_t Library::queued() const {
  std::lock_guard lock(mutex_);
  return jobs_.size();
}

void Library::run() {
  for (;;) {
    std::string name;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_.load() || !jobs_.empty(); });
      if (stopping_.load()) return;
      name = std::move(jobs_.front());
      jobs_.pop_front();
    }
    if (source_.before_load) source_.before_load();
    Result result{name, nullptr, false};
    if (!stopping_.load()) {
      auto clip = std::make_shared<Clip>();
      // Disk is primary: HTTP is only asked when the file is not on disk.
      const auto disk = read_disk(source_, name);
      const auto fetched = disk ? disk : read_http(source_, name);
      if (fetched && decode(*fetched, stopping_, *clip)) {
        result.truncated = clip->truncated;
        result.clip = std::move(clip);
      }
    }
    std::lock_guard lock(mutex_);
    if (stopping_.load()) return;
    results_.push_back(std::move(result));
  }
}

// ---- Player ---------------------------------------------------------------------------------------------------------

PlayerConfig config_from_environment() {
  PlayerConfig config;
  config.source.disk_root = default_sfx_root();
  if (const char* root = std::getenv("REALM_SFX_ROOT"); root && *root) config.source.disk_root = root;
  if (const char* mode = std::getenv("REALM_AUDIO"); mode && std::string_view(mode) == "0") config.output = false;
  if (const char* device = std::getenv("REALM_AUDIO_DEVICE"); device && std::string_view(device) == "null")
    config.null_output = true;
  if (const char* megabytes = std::getenv("REALM_AUDIO_CACHE_MB"); megabytes && *megabytes) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(megabytes, &end, 10);
    if (end != megabytes && *end == '\0' && value >= 1 && value <= 4096)
      config.cache_bytes = static_cast<std::size_t>(value) << 20U;
  }
  return config;
}

struct Player::Impl {
  using Clock = std::chrono::steady_clock;
  struct Pending {
    std::string name;
    float gain{1.f};
    Clock::time_point deadline{};
  };

  explicit Impl(const PlayerConfig& config)
      : library(config.source, config.cache_bytes, config.queue_capacity), pending_timeout(config.pending_timeout) {
    if (!config.output) {
      status = "output disabled";
      return;
    }
    device = Device::open(mixer, config.null_output, status);
    if (!device) std::fprintf(stderr, "realm audio: %s; sounds disabled\n", status.c_str());
  }

  // Accepted play: with a device it joins the mixer; without one it is only counted.
  void start(Clip& clip, float gain) {
    if (!device) {
      ++counts.plays_started;
      return;
    }
    if (mixer.play(clip, gain))
      ++counts.plays_started;
    else
      ++counts.command_dropped;
  }

  void defer(std::string_view name, float gain, Clock::time_point now) {
    ++counts.plays_deferred;
    pending.push_back(Pending{std::string(name), gain, now + pending_timeout});
  }

  // Integrates finished loads, starts or drops deferred plays, then evicts over budget.
  void service(Clock::time_point now) {
    library.poll();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < pending.size(); ++i) {
      Pending& play = pending[i];
      const auto state = library.state(play.name);
      if (state == Library::State::ready && now <= play.deadline) {
        if (const auto clip = library.ready(play.name)) {
          start(*clip, play.gain);
          continue;
        }
      }
      if (state == Library::State::loading && now <= play.deadline) {
        if (kept != i) pending[kept] = std::move(play);
        ++kept;
        continue;
      }
      ++counts.pending_dropped;
    }
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(kept), pending.end());
    library.trim();
  }

  Library library;
  Mixer mixer;
  // Declared after the mixer it renders from, so it is destroyed first.
  std::unique_ptr<Device> device;
  std::string status;
  std::chrono::milliseconds pending_timeout;
  bool play_sfx{true};
  bool play_pew_pew{true};
  std::uint64_t ticks{0};
  std::unordered_set<std::string, NameHash, std::equal_to<>> deduped;
  std::vector<Pending> pending;
  Counters counts;
};

Player::Player(const PlayerConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Player::~Player() = default;

void Player::set_preferences(bool play_sfx, bool play_pew_pew) {
  impl_->play_sfx = play_sfx;
  impl_->play_pew_pew = play_pew_pew;
}

void Player::play(std::string_view name, float volume, bool is_fx) {
  auto& i = *impl_;
  ++i.counts.plays_requested;
  if (!gate_open(i.play_sfx, i.play_pew_pew, is_fx)) {
    ++i.counts.plays_gated;
    return;
  }
  if (!valid_name(name)) {
    ++i.counts.plays_rejected;
    return;
  }
  if (i.deduped.contains(name)) {
    ++i.counts.plays_deduped;
    return;
  }
  // The name is marked before loading, so a failed sound also stays deduped until the next clear.
  i.deduped.emplace(name);
  const auto now = Impl::Clock::now();
  i.service(now);
  const float gain = std::isfinite(volume) && volume > 0.f ? volume : 0.f;
  switch (i.library.state(name)) {
    case Library::State::ready:
      if (const auto clip = i.library.ready(name)) i.start(*clip, gain);
      break;
    case Library::State::loading:
      i.defer(name, gain, now);
      break;
    case Library::State::absent:
      if (i.library.request(name) == Library::State::loading) i.defer(name, gain, now);
      break;
    case Library::State::failed:
      // A failed sound is never retried, so the play is silently lost.
      break;
  }
}

void Player::preload(std::string_view name) {
  if (valid_name(name)) impl_->library.request(name);
}

void Player::tick() {
  auto& i = *impl_;
  // SoundEffectLibrary.clear: the dedupe map resets on every 5th tick.
  if (++i.ticks % 5 == 0) i.deduped.clear();
  i.service(Impl::Clock::now());
}

void Player::cancel_pending() {
  auto& i = *impl_;
  i.counts.pending_dropped += i.pending.size();
  i.pending.clear();
}

void Player::stop_all() {
  auto& i = *impl_;
  if (i.device) i.mixer.stop_all();
  cancel_pending();
}

bool Player::output_active() const noexcept { return impl_->device != nullptr; }

const std::string& Player::output_status() const noexcept { return impl_->status; }

Counters Player::counters() const {
  const auto& i = *impl_;
  Counters result = i.counts;
  result.pending_now = i.pending.size();
  result.voices_active = i.mixer.active_voices();
  result.steals = i.mixer.steals();
  const auto& stats = i.library.stats();
  result.loads_ok = stats.loads_ok;
  result.loads_failed = stats.loads_failed;
  result.queue_dropped = stats.queue_dropped;
  result.evictions = stats.evictions;
  result.truncations = stats.truncations;
  result.decoded_bytes = i.library.bytes();
  return result;
}

}  // namespace realm::audio
