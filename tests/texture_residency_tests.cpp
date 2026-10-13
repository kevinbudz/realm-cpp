// Headless checks of the dynamic atlas allocator. A randomized mix of icons, labels and appearances is inserted,
// touched, erased and evicted over many frames against an independent model; after every operation all live slots must
// lie inside their page and be pairwise disjoint, slots must never move, evictions must run oldest-first over stale
// entries only, and a failed insert must mean the rectangle is oversize or only entries of this frame stand in the way.
// Freed space must fully recover.
#include "realm/residency.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>
namespace {
using namespace realm;
std::uint64_t checks{};
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
struct Live {
  TextureSlot slot;
  std::uint64_t frame, use;
};
struct Model {
  std::map<std::uint64_t, Live> live;
  std::uint64_t use{};
};
// A slot's rectangle with its far edges precomputed, as stored for each grid cell it covers.
struct Cover {
  int x0, y0, x1, y1;
};
// One registration of a slot in a grid cell, chained to the next registration in that cell (-1 at the end).
struct CellNode {
  Cover rect;
  std::int64_t next;
  std::size_t cell;
};
// Slots inside their page and pairwise disjoint: a 64-texel grid per page limits the pairs compared to those sharing a
// cell. verify runs after every operation, so the grid, the node storage and their capacity persist between calls. The
// storage is raw (no per-cell containers) because at -O0 every std::vector call is a real call and this runs for every
// operation.
void verify(const TextureResidency& r, const Model& model) {
  static std::vector<std::int64_t> head, tail;  // per cell: first and last registration, -1 when empty
  static std::vector<CellNode> nodes;           // capacity persists; the first `used` entries belong to this call
  static std::vector<std::uint64_t> uses;
  std::size_t used = 0;
  struct Reset {
    std::size_t& used;
    ~Reset() {
      std::int64_t* const heads = head.data();
      const CellNode* const pool = nodes.data();
      for (std::size_t n = 0; n < used; ++n) heads[pool[n].cell] = -1;
      used = 0;
    }
  } reset{used};
  const auto slots = r.slots();
  check(slots.size() == model.live.size() && r.entries() == model.live.size(), "entry count matches the model");
  const int cell = 64, cells = (r.page_size() + cell - 1) / cell;
  const auto cell_count = static_cast<std::size_t>(r.pages() * cells * cells);
  if (head.size() < cell_count) {
    head.resize(cell_count, -1);
    tail.resize(cell_count, -1);
  }
  std::int64_t* const heads = head.data();
  std::int64_t* const tails = tail.data();
  std::size_t capacity = nodes.size();
  CellNode* pool = nodes.data();
  uses.resize(slots.size());
  std::uint64_t bytes = 0;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const auto& s = slots[i].second;
    check(s.page >= 0 && s.page < r.pages() && s.x >= 0 && s.y >= 0 && s.width > 0 && s.height > 0 &&
              s.x + s.width <= r.page_size() && s.y + s.height <= r.page_size(),
          "slot inside its page");
    const auto it = model.live.find(slots[i].first);
    check(it != model.live.end() && it->second.slot.page == s.page && it->second.slot.x == s.x &&
              it->second.slot.y == s.y && it->second.slot.width == s.width && it->second.slot.height == s.height,
          "slot never moves");
    uses[i] = it->second.use;
    bytes += static_cast<std::uint64_t>(s.width) * static_cast<std::uint64_t>(s.height) * 4;
    const Cover mine{s.x, s.y, s.x + s.width, s.y + s.height};
    for (int cy = s.y / cell; cy <= (s.y + s.height - 1) / cell; ++cy)
      for (int cx = s.x / cell; cx <= (s.x + s.width - 1) / cell; ++cx) {
        const auto index = static_cast<std::size_t>((s.page * cells + cy) * cells + cx);
        // Walked in insertion order, so the pairs compared and their order are those of a per-cell list.
        for (auto j = heads[index]; j >= 0; j = pool[j].next) {
          const Cover& o = pool[j].rect;
          check(mine.x0 >= o.x1 || o.x0 >= mine.x1 || mine.y0 >= o.y1 || o.y0 >= mine.y1, "slots are disjoint");
        }
        if (used == capacity) {
          capacity = capacity ? 2 * capacity : 1024;
          nodes.resize(capacity);
          pool = nodes.data();
        }
        const auto node = static_cast<std::int64_t>(used++);
        pool[node] = CellNode{mine, -1, index};
        if (heads[index] < 0)
          heads[index] = node;
        else
          pool[tails[index]].next = node;
        tails[index] = node;
      }
  }
  check(bytes == r.used_bytes(), "used bytes are the live slot area");
  check(r.occupied_area() <= r.page_area(), "shelves fit the pages");
  // Least recently used first, as the model orders them.
  for (std::size_t i = 1; i < slots.size(); ++i) check(uses[i - 1] < uses[i], "list is ordered by last use");
}
struct Size {
  int w, h;
};
Size random_size(std::mt19937& rng) {
  const auto pick = [&](int low, int high) {
    return low + static_cast<int>(rng() % static_cast<unsigned>(high - low + 1));
  };
  switch (rng() % 8) {
    case 0:
    case 1:
    case 2:
      return {16, 16};  // condition icons
    case 3:
    case 4:
    case 5:
      return {pick(30, 300), pick(10, 40)};  // name labels
    case 6:
      return {pick(64, 400), pick(64, 400)};  // ordinary appearances
    default:
      return {pick(64, 1048), pick(64, 1048)};  // large appearances
  }
}
// Every operation goes through here so the model sees exactly what the allocator reports.
struct Driver {
  TextureResidency r;
  Model m;
  std::uint64_t next{1}, failures{}, evictions{}, oversize{};
  std::uint64_t window;
  int max_pages{};
  std::uint64_t grown{};
  std::vector<std::pair<std::uint64_t, std::uint64_t>> by_use;  // reused eviction-order buffer, see insert()
  void grow(int maximum, std::uint64_t recent) {
    max_pages = maximum;
    r.set_growth(maximum, recent);
  }
  Driver(int pages, int size, std::uint64_t protect = 1) : r(pages, size), window(protect) {
    r.set_protected_frames(protect);
  }
  [[nodiscard]] bool protected_now(const Live& live) const { return r.frame() - live.frame < window; }
  void frame() { r.begin_frame(); }
  bool touch(std::uint64_t key) {
    const auto* slot = r.find(key);
    const auto it = m.live.find(key);
    check((slot != nullptr) == (it != m.live.end()), "find agrees with the model");
    if (!slot) return false;
    check(slot->page == it->second.slot.page && slot->x == it->second.slot.x && slot->y == it->second.slot.y,
          "find returns the stored slot");
    it->second.frame = r.frame();
    it->second.use = ++m.use;
    return true;
  }
  std::optional<TextureSlot> insert(std::uint64_t key, Size size) {
    const bool replaced = m.live.erase(key) != 0;
    auto result = r.insert(key, size.w, size.h);
    if (!result && r.growth_requested()) {
      // As Scene does: the request replaces eviction, and nothing was evicted or moved by the refused attempt.
      check(r.evicted().empty() && r.growth_requested() > r.pages() && r.growth_requested() <= max_pages,
            "a growth request evicts nothing and stays within the cap");
      check(r.pages() < max_pages, "growth only below the cap");
      const int wanted = r.growth_requested();
      ++grown;
      r.add_pages(wanted - r.pages());
      result = r.insert(key, size.w, size.h);
    }
    const auto& gone = r.evicted();
    evictions += gone.size();
    // Expected eviction order: stale entries, least recently used first. Only the first gone.size() entries are
    // compared, and the model is unchanged since the erase above, so the prefix is taken once something was evicted.
    if (!gone.empty()) {
      by_use.clear();
      for (const auto& [k, live] : m.live) by_use.emplace_back(live.use, k);
      const auto oldest = std::min(gone.size(), by_use.size());
      std::partial_sort(by_use.begin(), by_use.begin() + static_cast<std::ptrdiff_t>(oldest), by_use.end());
    }
    for (std::size_t i = 0; i < gone.size(); ++i) {
      check(i < by_use.size() && gone[i] == by_use[i].second, "evictions run oldest first");
      check(!protected_now(m.live.at(gone[i])), "an entry inside the protection window is never evicted");
      m.live.erase(gone[i]);
    }
    if (result) {
      check(result->width == size.w && result->height == size.h, "slot has the requested size");
      m.live[key] = {*result, r.frame(), ++m.use};
    } else {
      ++failures;
      if (size.w > r.page_size() || size.h > r.page_size() || size.w < 1 || size.h < 1)
        ++oversize;
      else
        for (const auto& [k, live] : m.live)
          check(protected_now(live), "a failed insert leaves only protected entries in the way");
    }
    (void)replaced;
    verify(r, m);
    return result;
  }
  void erase(std::uint64_t key) {
    r.erase(key);
    m.live.erase(key);
    verify(r, m);
  }
  void clear() {
    r.clear();
    m.live.clear();
    verify(r, m);
  }
  [[nodiscard]] std::vector<std::uint64_t> keys() const {
    std::vector<std::uint64_t> k;
    for (const auto& [key, live] : m.live) {
      (void)live;
      k.push_back(key);
    }
    return k;
  }
  void keys_into(std::vector<std::uint64_t>& out) const {
    out.clear();
    for (const auto& [key, live] : m.live) {
      (void)live;
      out.push_back(key);
    }
  }
};
void height_classes() {
  const std::pair<int, int> expected[]{{1, 8},     {8, 8},     {9, 16},      {16, 16},     {40, 40},     {63, 64},
                                       {64, 64},   {65, 80},   {127, 128},   {128, 128},   {129, 160},   {255, 256},
                                       {256, 256}, {257, 320}, {1048, 1088}, {1984, 1984}, {1985, 2048}, {2048, 2048}};
  for (const auto& [height, rows] : expected) check(TextureResidency::height_class(height) == rows, "height class");
}
void randomized(int pages, unsigned seed, int frames, int operations, std::uint64_t window, int max_pages = 0) {
  std::mt19937 rng(seed);
  Driver d(pages, 2048, window);
  if (max_pages) d.grow(max_pages, 6);
  std::uint64_t inserted = 0;
  std::vector<std::uint64_t> keys;  // snapshot of the live keys before each operation, reusing its capacity
  for (int f = 0; f < frames; ++f) {
    d.frame();
    for (int op = 0; op < operations; ++op) {
      d.keys_into(keys);
      switch (rng() % 10) {
        case 0:
        case 1:
        case 2:
        case 3: {
          const auto size = random_size(rng);
          (void)d.insert(d.next++, size);
          ++inserted;
          break;
        }
        case 4:
        case 5:
        case 6:
          if (!keys.empty()) (void)d.touch(keys[rng() % keys.size()]);
          break;
        case 7:
          if (!keys.empty()) d.erase(keys[rng() % keys.size()]);
          break;
        case 8:
          if (!keys.empty()) {
            const auto key = keys[rng() % keys.size()];
            (void)d.insert(key, random_size(rng));
          }
          break;  // replace in place
        default:
          (void)d.touch(d.next + 5);
          break;  // a key never inserted
      }
    }
  }
  check(inserted > 0 && d.evictions > 0, "the churn evicts");
  if (max_pages) {
    check(d.grown > 0 && d.r.pages() > pages && d.r.pages() <= max_pages, "the churn grew the page set within its cap");
    std::cout << "  grew " << pages << " -> " << d.r.pages() << " pages in " << d.grown << " steps\n";
    d.grow(0, 0);
    pages = d.r.pages();
  }
  (void)window;
  std::cout << "random pages=" << pages << " window=" << window << " seed=" << seed << ": inserted " << inserted
            << ", evictions " << d.evictions << ", failures " << d.failures << ", final " << d.r.entries()
            << " entries, " << d.r.used_bytes() / 1024 << " KiB used of " << d.r.occupied_area() * 4 / 1024
            << " KiB in shelves\n";
  // Erase everything: all space returns, so whole-page allocations succeed once per page and then fail.
  for (const auto key : d.keys()) d.erase(key);
  check(d.r.entries() == 0 && d.r.used_bytes() == 0 && d.r.occupied_area() == 0,
        "erasing everything frees every shelf");
  d.frame();
  for (int p = 0; p < pages; ++p) {
    const auto slot = d.insert(d.next++, {2048, 2048});
    check(slot && slot->page >= 0 && slot->x == 0 && slot->y == 0, "a page-sized slot fits again");
  }
  check(!d.insert(d.next++, {2048, 2048}), "one page-sized slot per page and no more");
}
void frame_protection() {
  Driver d(1, 2048);
  d.frame();
  // Four 1024x1024 slots fill the page; everything is current, so a fifth must fail without evicting any of them.
  for (std::uint64_t k = 1; k <= 4; ++k) check(d.insert(k, {1024, 1024}).has_value(), "four 1024 squares fit one page");
  check(!d.insert(5, {1024, 1024}), "no room and nothing stale: fallback");
  check(d.r.evicted().empty() && d.r.entries() == 4 && d.r.failures() >= 1, "a failed insert evicted nothing");
  // Next frame: touch 2 then 1, so the use order is 3, 4, 2, 1.
  d.frame();
  check(d.touch(2) && d.touch(1), "touch");
  d.frame();
  check(d.insert(5, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{3},
        "evicts the least recently used first");
  check(d.insert(6, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{4}, "then the next oldest");
  check(d.insert(7, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{2}, "then the next");
  check(d.insert(8, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{1}, "then the last stale one");
  check(!d.insert(9, {1024, 1024}) && d.r.evicted().empty(),
        "entries of this frame stand in the way: fallback, nothing evicted");
  check(d.r.entries() == 4 && d.r.evictions() == 4, "eviction totals");
  // A found entry counts as used in the current frame.
  d.frame();
  check(d.touch(5), "touch 5");
  check(d.insert(9, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{6},
        "a touched entry outlives older ones");
  // A mix of sizes: one in-frame overflow of small entries falls back instead of evicting.
  Driver s(1, 256);
  s.frame();
  int fitted = 0;
  for (std::uint64_t k = 1; k <= 400; ++k)
    if (s.insert(k, {16, 16})) ++fitted;
  check(fitted == 256, "16x16 icons: 16 per row, 16 rows");
  check(s.r.evictions() == 0 && s.failures == 400 - 256, "overflow in one frame never evicts");
}
void protection_window() {
  // Window 2: an entry used last frame survives, so a full page of last-frame entries makes the next insert fall back.
  Driver d(1, 2048, 2);
  d.frame();
  for (std::uint64_t k = 1; k <= 4; ++k) check(d.insert(k, {1024, 1024}).has_value(), "four 1024 squares fit one page");
  d.frame();
  check(!d.insert(5, {1024, 1024}) && d.r.evicted().empty() && d.r.entries() == 4,
        "entries used in the previous frame are protected");
  d.frame();
  check(d.touch(2), "touch 2");
  check(d.insert(5, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{1},
        "two frames old: evictable, oldest first");
  check(d.insert(6, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{3}, "a touched entry is skipped");
  check(d.insert(7, {1024, 1024}) && d.r.evicted() == std::vector<std::uint64_t>{4}, "the last unprotected one");
  check(!d.insert(8, {1024, 1024}) && d.r.evicted().empty() && d.r.entries() == 4,
        "everything left is within the window");
  d.frame();
  check(!d.insert(8, {1024, 1024}), "entries from the previous frame are still protected");
  d.frame();
  check(d.insert(8, {1024, 1024}) && d.r.evicted().size() == 1, "and evictable one frame later");
  // A steady over-capacity set visited in a rotating order keeps what fits resident under window 2.
  Driver w(1, 256, 2);
  std::uint64_t uploads{};
  for (int f = 0; f < 20; ++f) {
    w.frame();
    for (std::uint64_t i = 0; i < 300; ++i) {
      const auto k = 1 + (i + static_cast<std::uint64_t>(f) * 37) % 300;
      if (!w.touch(k) && w.insert(k, {16, 16})) ++uploads;
    }
  }
  check(uploads == 256 && w.r.evictions() == 0,
        "a cyclic over-capacity set uploads each resident entry once and never thrashes");
  Driver one(1, 256, 1);
  std::uint64_t one_uploads{};
  for (int f = 0; f < 20; ++f) {
    one.frame();
    for (std::uint64_t i = 0; i < 300; ++i) {
      const auto k = 1 + (i + static_cast<std::uint64_t>(f) * 37) % 300;
      if (!one.touch(k) && one.insert(k, {16, 16})) ++one_uploads;
    }
  }
  check(one_uploads > 256, "window 1 thrashes the same cycle");
}
void growth() {
  const auto slots_of = [](const TextureResidency& r) {
    std::map<std::uint64_t, TextureSlot> all;
    for (const auto& [k, s] : r.slots()) all[k] = s;
    return all;
  };
  const auto same = [](const TextureSlot& a, const TextureSlot& b) {
    return a.page == b.page && a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  // A recent working set is never evicted while pages may be added: the insert asks for more room instead.
  TextureResidency r(1, 2048);
  r.set_growth(3, 10);
  r.begin_frame();
  for (std::uint64_t k = 1; k <= 4; ++k) check(r.insert(k, 1024, 1024).has_value(), "four squares fit one page");
  r.begin_frame();
  const auto before = slots_of(r);
  check(!r.insert(5, 1024, 1024) && r.growth_requested() == 2 && r.evicted().empty() && r.entries() == 4 &&
            r.evictions() == 0 && r.failures() == 0,
        "growth is requested instead of evicting recent entries");
  r.add_pages(1);
  check(r.pages() == 2, "a page was added");
  const auto fifth = r.insert(5, 1024, 1024);
  check(fifth && fifth->page == 1 && r.growth_requested() == 0 && r.evicted().empty(), "the added page makes room");
  for (const auto& [k, s] : before) check(same(slots_of(r).at(k), s), "slots never move when pages are added");
  for (std::uint64_t k = 6; k <= 8; ++k) check(r.insert(k, 1024, 1024).has_value(), "the second page fills");
  check(!r.insert(9, 1024, 1024) && r.growth_requested() == 3, "the request is capped at the maximum");
  r.add_pages(1);
  check(r.insert(9, 1024, 1024).has_value() && r.pages() == 3, "third page");
  for (std::uint64_t k = 10; k <= 12; ++k) check(r.insert(k, 1024, 1024).has_value(), "the third page fills");
  // At the cap the usual rule applies: evict stale entries, oldest first, never protected ones.
  r.begin_frame();
  check(r.insert(13, 1024, 1024) && r.growth_requested() == 0 && r.evicted() == std::vector<std::uint64_t>{1},
        "at the cap the least recently used entry is evicted");
  check(r.pages() == 3, "no growth beyond the cap");
  // Entries older than `recent_frames` are evicted, not a reason to grow.
  TextureResidency old(1, 2048);
  old.set_growth(4, 5);
  old.begin_frame();
  for (std::uint64_t k = 1; k <= 4; ++k) check(old.insert(k, 1024, 1024).has_value(), "fill");
  for (int f = 0; f < 5; ++f) old.begin_frame();
  check(old.insert(5, 1024, 1024) && old.growth_requested() == 0 && old.evicted() == std::vector<std::uint64_t>{1} &&
            old.pages() == 1,
        "a stale cache evicts instead of growing");
  // A touch makes the oldest entry recent again.
  old.begin_frame();
  for (std::uint64_t k = 2; k <= 5; ++k) (void)old.find(k);
  old.begin_frame();
  check(!old.insert(6, 1024, 1024) && old.growth_requested() == 2, "a recently touched cache asks to grow");
  TextureResidency off(1, 2048);
  off.begin_frame();
  for (std::uint64_t k = 1; k <= 4; ++k) check(off.insert(k, 1024, 1024).has_value(), "fill");
  check(!off.insert(5, 1024, 1024) && off.growth_requested() == 0, "growth is off by default");
}
void limits() {
  Driver d(2, 2048);
  d.frame();
  check(!d.insert(1, {2049, 16}) && !d.insert(2, {16, 2049}) && !d.insert(3, {0, 16}) && !d.insert(4, {16, 0}),
        "oversize and empty rectangles are refused");
  check(d.oversize == 4 && d.r.entries() == 0 && d.r.failures() == 4, "refusals are counted as failures");
  check(d.insert(5, {2048, 2048}) && d.insert(6, {2048, 2048}) && !d.insert(7, {1, 1}), "whole pages");
  d.clear();
  check(d.r.entries() == 0 && d.r.used_bytes() == 0 && d.r.occupied_area() == 0 && d.r.find(5) == nullptr,
        "clear empties it");
  check(d.insert(8, {2048, 2048}) && d.insert(9, {2048, 2048}), "clear frees both pages");
  TextureResidency none(0, 2048);
  check(!none.insert(1, 16, 16) && none.entries() == 0, "no pages: always a fallback");
}
void height_class_reuse() {
  // Shelves of one height class hold only that class; freeing a shelf's last slot returns its rows to other classes.
  Driver d(1, 2048);
  d.frame();
  for (std::uint64_t k = 1; k <= 128; ++k) check(d.insert(k, {16, 16}).has_value(), "first icon shelf");
  const auto first = d.m.live.at(1).slot, last = d.m.live.at(128).slot;
  check(first.y == 0 && last.y == 0 && last.x == 127 * 16, "icons fill one shelf left to right");
  check(d.insert(200, {16, 16}) && d.m.live.at(200).slot.y == 16, "the next icon opens the next shelf");
  check(d.insert(300, {100, 100}) && d.m.live.at(300).slot.y == 32, "a taller class opens its own shelf below");
  for (std::uint64_t k = 1; k <= 128; ++k) d.erase(k);
  check(d.insert(301, {2048, 16}) && d.m.live.at(301).slot.y == 0, "the emptied shelf's rows are reused");
  d.erase(200);
  d.erase(300);
  d.erase(301);
  check(d.r.occupied_area() == 0, "all shelves released");
}
}
int main() {
  try {
    height_classes();
    frame_protection();
    limits();
    protection_window();
    growth();
    height_class_reuse();
    for (const std::uint64_t window : {1U, 2U, 3U}) {
      randomized(1, 11, 60, 250, window);
      randomized(2, 5051, 120, 250, window);
      randomized(4, 90210, 80, 400, window);
      randomized(1, 777, 120, 300, window, 5);
      randomized(2, 4242, 100, 300, window, 8);
    }
    std::cout << checks << " texture residency checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "texture_residency: " << error.what() << '\n';
    return 1;
  }
}
