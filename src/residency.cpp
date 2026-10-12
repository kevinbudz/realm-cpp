#include "realm/residency.hpp"
#include <algorithm>
#include <iterator>
#include <utility>
namespace realm {
namespace {
// Inserts {at, width} into a list sorted by position, merging with touching neighbours (the shelf's free x spans and the page's free rows).
template <class Span> void give_back(std::vector<Span>& spans, int at, int width) {
    auto next = std::lower_bound(spans.begin(), spans.end(), at, [](const Span& s, int value) { return s.x < value; });
    if (next != spans.begin() && std::prev(next)->x + std::prev(next)->width == at) { --next; next->width += width; }
    else next = spans.insert(next, Span{at, width});
    const auto after = std::next(next);
    if (after != spans.end() && next->x + next->width == after->x) { next->width += after->width; spans.erase(after); }
}
// Takes `width` from the start of spans[i].
template <class Span> void take(std::vector<Span>& spans, std::size_t i, int width) {
    spans[i].x += width; spans[i].width -= width;
    if (!spans[i].width) spans.erase(spans.begin() + static_cast<std::ptrdiff_t>(i));
}
}
TextureResidency::TextureResidency(int pages, int page_size) : size_(page_size), pages_(static_cast<std::size_t>(std::max(0, pages))) {
    for (auto& page : pages_) page.rows.push_back({0, size_});
}
int TextureResidency::height_class(int height) {
    const int step = height < 64 ? 8 : height < 128 ? 16 : height < 256 ? 32 : 64;
    return (height + step - 1) / step * step;
}
std::uint64_t TextureResidency::occupied_area() const {
    std::uint64_t rows = 0;
    for (const auto& page : pages_) for (const auto& shelf : page.shelves) rows += static_cast<std::uint64_t>(shelf.height);
    return rows * static_cast<std::uint64_t>(size_);
}
std::optional<TextureSlot> TextureResidency::allocate(int width, int height) {
    const int rows = height_class(height);
    for (std::size_t p = 0; p < pages_.size(); ++p) for (auto& shelf : pages_[p].shelves) {
        if (shelf.height != rows) continue;
        for (std::size_t i = 0; i < shelf.free.size(); ++i) if (shelf.free[i].width >= width) {
            const TextureSlot slot{static_cast<int>(p), shelf.free[i].x, shelf.y, width, height};
            take(shelf.free, i, width); return slot;
        }
    }
    // No shelf of this class has room: open one from the first free row range that is tall enough.
    for (std::size_t p = 0; p < pages_.size(); ++p) {
        auto& page = pages_[p];
        for (std::size_t i = 0; i < page.rows.size(); ++i) if (page.rows[i].width >= rows) {
            Shelf shelf{page.rows[i].x, rows, {}};
            if (width < size_) shelf.free.push_back({width, size_ - width});
            take(page.rows, i, rows);
            const auto at = std::lower_bound(page.shelves.begin(), page.shelves.end(), shelf.y, [](const Shelf& s, int y) { return s.y < y; });
            const TextureSlot slot{static_cast<int>(p), 0, shelf.y, width, height};
            page.shelves.insert(at, std::move(shelf)); return slot;
        }
    }
    return std::nullopt;
}
void TextureResidency::release(const TextureSlot& slot) {
    auto& page = pages_[static_cast<std::size_t>(slot.page)];
    const auto shelf = std::lower_bound(page.shelves.begin(), page.shelves.end(), slot.y, [](const Shelf& s, int y) { return s.y < y; });
    give_back(shelf->free, slot.x, slot.width);
    // A completely free shelf returns its rows, so a later request of another height class can use them.
    if (shelf->free.size() == 1 && shelf->free[0].width == size_) {
        give_back(page.rows, shelf->y, shelf->height);
        page.shelves.erase(shelf);
    }
}
const TextureSlot* TextureResidency::find(std::uint64_t key) {
    const auto found = index_.find(key);
    if (found == index_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, found->second);
    found->second->frame = frame_;
    return &found->second->slot;
}
std::optional<TextureSlot> TextureResidency::insert(std::uint64_t key, int width, int height) {
    evicted_.clear(); growth_ = 0;
    erase(key);
    if (width < 1 || height < 1 || width > size_ || height > size_) { ++failures_; return std::nullopt; }
    for (;;) {
        if (const auto slot = allocate(width, height)) {
            lru_.push_front({key, *slot, frame_}); index_[key] = lru_.begin();
            used_bytes_ += static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4;
            return slot;
        }
        if (pages() < max_pages_ && (lru_.empty() || frame_ - lru_.back().frame < recent_)) { growth_ = std::min(max_pages_, std::max(pages() + 1, 2 * pages())); return std::nullopt; }
        // Entries are ordered by last use, so a protected entry at the back means nothing evictable is left.
        if (lru_.empty() || frame_ - lru_.back().frame < protected_) { ++failures_; return std::nullopt; }
        const auto victim = lru_.back();
        release(victim.slot);
        used_bytes_ -= static_cast<std::uint64_t>(victim.slot.width) * static_cast<std::uint64_t>(victim.slot.height) * 4;
        index_.erase(victim.key); lru_.pop_back(); evicted_.push_back(victim.key); ++evictions_;
    }
}
void TextureResidency::add_pages(int count) {
    for (int i = 0; i < count; ++i) { pages_.emplace_back(); pages_.back().rows.push_back({0, size_}); }
}
void TextureResidency::erase(std::uint64_t key) {
    const auto found = index_.find(key);
    if (found == index_.end()) return;
    const auto slot = found->second->slot;
    release(slot);
    used_bytes_ -= static_cast<std::uint64_t>(slot.width) * static_cast<std::uint64_t>(slot.height) * 4;
    lru_.erase(found->second); index_.erase(found);
}
void TextureResidency::clear() {
    lru_.clear(); index_.clear(); evicted_.clear(); used_bytes_ = 0;
    for (auto& page : pages_) { page.shelves.clear(); page.rows.assign(1, {0, size_}); }
}
std::vector<std::pair<std::uint64_t, TextureSlot>> TextureResidency::slots() const {
    std::vector<std::pair<std::uint64_t, TextureSlot>> result; result.reserve(lru_.size());
    for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) result.emplace_back(it->key, it->slot);
    return result;
}
}
