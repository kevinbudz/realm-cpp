#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
namespace realm {
struct TextureSlot { int page{}, x{}, y{}, width{}, height{}; };
// Headless rectangle allocator with LRU eviction for the dynamic atlas pages (no GL: the caller uploads pixels into a slot).
// Each page is cut into horizontal shelves; a shelf holds rectangles of one height class, left to right, with free spans
// on its x axis. Height classes round the requested height up to a multiple of 8 below 64, of 16 below 128, of 32 below 256
// and of 64 from there on, so labels (10-40 px) and appearances (64-1048 px) share few distinct shelf heights. A shelf is
// opened from the page's free row ranges (first fit) and handed back, coalesced, when its last rectangle is released.
// Slots have no gap: the shader clamps every sample to the slot's own texel centers.
// Frame protection: an entry used (found or inserted) within the last `protected_frames` frames is never evicted, because its quad may
// still be queued for this frame's draw; when only such entries are left, insert() fails and the caller falls back. One frame protects
// only the current frame. That is enough for correctness, but with a working set above capacity it turns LRU into cyclic thrash: each
// frame evicts what the next frame needs and re-uploads it. A two-frame window keeps the steady visible set resident and sends the
// excess to the caller's per-image fallback without re-uploads.
class TextureResidency {
public:
    TextureResidency(int pages = 0, int page_size = 2048);
    void begin_frame() { ++frame_; }
    // Frames an entry stays unevictable after its last use (at least 1: the current frame only).
    void set_protected_frames(std::uint64_t frames) { protected_ = frames ? frames : 1; }
    [[nodiscard]] std::uint64_t protected_frames() const { return protected_; }
    // Touches the entry (most recently used, current frame).
    [[nodiscard]] const TextureSlot* find(std::uint64_t key);
    // Replaces an existing entry of that key. On failure evicts stale entries oldest first, retrying after each; nullopt when
    // the rectangle is larger than a page or only protected entries stand in the way. evicted() lists what it removed.
    [[nodiscard]] std::optional<TextureSlot> insert(std::uint64_t key, int width, int height);
    [[nodiscard]] const std::vector<std::uint64_t>& evicted() const { return evicted_; }
    // Growth: a failed allocation does not evict when the cache still holds a recent working set (its least recently used entry was
    // used within `recent_frames`) and fewer than `max_pages` exist; insert() then returns nullopt and growth_requested() names the page
    // count wanted (min(max, 2x), at least one more). The caller adds the pages and retries. 0 pages or frames disables it.
    void set_growth(int max_pages, std::uint64_t recent_frames) { max_pages_ = max_pages; recent_ = recent_frames; }
    [[nodiscard]] int growth_requested() const { return growth_; }
    // New empty pages; existing slots do not move.
    void add_pages(int count);
    void erase(std::uint64_t key);
    void clear();
    // Height a rectangle of this height occupies in its shelf.
    [[nodiscard]] static int height_class(int height);
    [[nodiscard]] int pages() const { return static_cast<int>(pages_.size()); }
    [[nodiscard]] int page_size() const { return size_; }
    [[nodiscard]] std::uint64_t frame() const { return frame_; }
    [[nodiscard]] std::size_t entries() const { return index_.size(); }
    [[nodiscard]] std::uint64_t used_bytes() const { return used_bytes_; }
    [[nodiscard]] std::uint64_t evictions() const { return evictions_; }
    [[nodiscard]] std::uint64_t failures() const { return failures_; }
    // Texels in open shelves (rows claimed by a shelf times the page width) against the texels of all pages.
    [[nodiscard]] std::uint64_t occupied_area() const;
    [[nodiscard]] std::uint64_t page_area() const { return static_cast<std::uint64_t>(pages_.size()) * static_cast<std::uint64_t>(size_) * static_cast<std::uint64_t>(size_); }
    // Every live slot, oldest use first (tests).
    [[nodiscard]] std::vector<std::pair<std::uint64_t, TextureSlot>> slots() const;
    // Key of the least recently used entry, if any (tests).
    [[nodiscard]] std::optional<std::uint64_t> oldest() const { return lru_.empty() ? std::nullopt : std::optional<std::uint64_t>(lru_.back().key); }
private:
    struct Span { int x, width; };
    struct Shelf { int y, height; std::vector<Span> free; };
    struct Page { std::vector<Shelf> shelves; std::vector<Span> rows; };   // shelves by y; rows: free {y, height} ranges by y
    struct Entry { std::uint64_t key; TextureSlot slot; std::uint64_t frame; };
    [[nodiscard]] std::optional<TextureSlot> allocate(int width, int height);
    void release(const TextureSlot& slot);
    int size_{}, max_pages_{}, growth_{};
    std::uint64_t frame_{1}, protected_{1}, recent_{}, used_bytes_{}, evictions_{}, failures_{};
    std::vector<Page> pages_;
    std::list<Entry> lru_;   // front: most recently used
    std::unordered_map<std::uint64_t, std::list<Entry>::iterator> index_;
    std::vector<std::uint64_t> evicted_;
};
}
