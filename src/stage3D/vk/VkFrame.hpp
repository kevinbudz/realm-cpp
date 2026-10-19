#pragma once
// Frames and the swapchain. A frame is recorded into one slot's command buffer, submitted, and
// presented; up to frames_in_flight frames are in flight. The swapchain image is acquired lazily, at
// the first render pass that writes it, so a frame that only uploads needs no image.
#include "VkCommon.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace realm::gfx::vk {
// A constant: two frames in flight; phase S7 measures 2 against 3 on Nexus and S3.
inline constexpr std::uint32_t frames_in_flight = 2;

// Host-visible memory the CPU bump-allocates from while a frame is recorded. The chunks are
// persistently mapped and reused by the slot's later frames.
class Ring {
 public:
  struct Span {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    std::byte* data = nullptr;
  };

  Span allocate(VkDeviceSize bytes, VkDeviceSize alignment);
  void reset() noexcept;
  void destroy() noexcept;

 private:
  std::vector<Buffer> chunks_;
  std::size_t current_ = 0;
  VkDeviceSize used_ = 0;
};

// The swapchain image a render pass writes.
struct Target {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkExtent2D extent{};
  std::uint32_t index = 0;
};

class Frames {
 public:
  // Creates the frame slots. The swapchain comes with the first target().
  void create();
  // Waits for submitted frames and releases everything, including the swapchain. Idempotent.
  void destroy() noexcept;

  // The command buffer of the frame being recorded; opens the frame when none is open.
  [[nodiscard]] VkCommandBuffer record();
  // The ring of the frame being recorded; opens the frame when none is open.
  [[nodiscard]] Ring& ring();

  // The swapchain target for a width x height drawable: acquires an image for the frame on first use
  // and rebuilds the swapchain when the size or the present mode changed.
  [[nodiscard]] const Target& target(std::uint32_t width, std::uint32_t height);
  [[nodiscard]] bool has_target() const { return acquired_; }
  // The frame's render passes write the target: finish() then leaves it in PRESENT_SRC from COLOR.
  void mark_written();
  [[nodiscard]] VkFormat format() const { return format_; }
  // Bumped on every swapchain rebuild; caches of swapchain views and framebuffers key on it.
  [[nodiscard]] std::uint64_t generation() const { return generation_; }

  // Queues an object for destruction once the frame that may still use it has finished.
  void retire(std::function<void()> destroy);

  // Presentation: the vsync interval of the session. Applies at the next rebuild.
  void set_swap_interval(int interval);

  // Submits the frame being recorded without presenting it, and waits until the GPU is done with it.
  // Capture uses this to read the image back; finish() presents afterwards.
  void submit_and_wait();
  // Records a copy of the acquired image into `destination` (tightly packed rows, top row first) and
  // waits for it: the frame then stays in the submitted state, presented by finish().
  void read_target(VkBuffer destination, std::uint32_t width, std::uint32_t height);
  // Runs with the frame's command buffer before it is submitted (the transfer batch closes here).
  void set_before_submit(std::function<void(VkCommandBuffer)> before) { before_submit_ = std::move(before); }
  // Counts finished frames; caches of per-frame results key on it.
  [[nodiscard]] std::uint64_t serial() const { return serial_; }
  // Ends the frame: submits it when still recording, then presents the image when one was acquired.
  // A frame with no GPU work records nothing and presents nothing.
  void finish();

 private:
  enum class State { idle, recording, submitted };

  struct Slot {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;  // signalled by the swapchain acquire of this frame
    Ring ring;
    bool in_flight = false;  // submitted and not yet waited for
    std::vector<std::function<void()>> retired;
  };

  std::vector<Slot> slots_;
  std::uint32_t current_ = 0;  // the slot of the frame being recorded, or the next one
  std::uint32_t last_ = 0;     // the slot of the last submitted frame
  bool has_last_ = false;
  State state_ = State::idle;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  VkPresentModeKHR mode_ = VK_PRESENT_MODE_FIFO_KHR;
  std::vector<VkPresentModeKHR> modes_;  // the surface's present modes
  int swap_interval_ = 1;
  bool dirty_ = true;
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  std::vector<VkSemaphore> render_done_;  // one per swapchain image
  std::uint64_t generation_ = 0;

  Target target_;
  bool acquired_ = false;
  bool presentable_ = false;  // the frame's last command left the image in PRESENT_SRC
  bool written_ = false;      // a render pass wrote the acquired image
  std::uint64_t serial_ = 0;
  std::function<void(VkCommandBuffer)> before_submit_;

  void open();
  void wait_in_flight();
  void rebuild(std::uint32_t width, std::uint32_t height);
  void destroy_swapchain(VkSwapchainKHR old) noexcept;
  void submit(bool wait);
  void present();
  void retire_into_slot(std::function<void()> destroy);
};

// The process's frames (the Vulkan backend creates them with its device).
[[nodiscard]] Frames& frames();
}  // namespace realm::gfx::vk
