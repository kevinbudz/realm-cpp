#include "realm/telemetry.hpp"
#include "realm/protocol.hpp"
#include <algorithm>
#include <limits>

namespace realm::net {
std::span<const std::uint8_t> Reader::take(std::size_t count) {
    if (count > remaining()) throw ProtocolError("Truncated packet");
    const auto result = bytes_.subspan(position_, count);
    position_ += count;
    return result;
}
std::uint8_t Reader::u8() { return take(1)[0]; }
std::uint16_t Reader::u16() {
    const auto b = take(2);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[0]) << 8U) | b[1]);
}
std::uint32_t Reader::u32() {
    const auto b = take(4);
    return (static_cast<std::uint32_t>(b[0]) << 24U) | (static_cast<std::uint32_t>(b[1]) << 16U) |
           (static_cast<std::uint32_t>(b[2]) << 8U) | b[3];
}
std::string Reader::utf() {
    const auto b = take(u16());
    return std::string(b.begin(), b.end());
}
void Reader::finish() const { if (remaining()) throw ProtocolError("Unexpected packet trailing bytes"); }
void Writer::u16(std::uint16_t value) { u8(static_cast<std::uint8_t>(value >> 8U)); u8(static_cast<std::uint8_t>(value)); }
void Writer::u32(std::uint32_t value) {
    u8(static_cast<std::uint8_t>(value >> 24U)); u8(static_cast<std::uint8_t>(value >> 16U));
    u8(static_cast<std::uint8_t>(value >> 8U)); u8(static_cast<std::uint8_t>(value));
}
void Writer::utf(std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint16_t>::max()) throw ProtocolError("String exceeds writeUTF byte limit");
    u16(static_cast<std::uint16_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}
void Writer::raw(std::span<const std::uint8_t> value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }
Bytes frame(std::uint8_t id, std::span<const std::uint8_t> body) {
    if (body.size() > max_packet_length - 5) throw ProtocolError("Packet exceeds length limit");
    Writer w; w.u32(static_cast<std::uint32_t>(body.size() + 5)); w.u8(id); w.raw(body);
    return w.bytes();
}
std::vector<Packet> Framer::feed(std::span<const std::uint8_t> input) {
    REALM_ZONE("network.framing");
    std::vector<Packet> packets;
    try {
        while (!input.empty()) {
            const auto count = std::min(input.size(), expected_ - buffer_.size());
            buffer_.insert(buffer_.end(), input.begin(), input.begin() + static_cast<std::ptrdiff_t>(count));
            input = input.subspan(count);
            if (buffer_.size() < expected_) break;
            if (expected_ == 4) {
                Reader r(buffer_); expected_ = r.u32();
                if (expected_ < 5 || expected_ > max_packet_length) throw ProtocolError("Invalid frame length");
            } else {
                packets.push_back({buffer_[4], Bytes(buffer_.begin() + 5, buffer_.end())});
                reset();
            }
        }
    } catch (...) { reset(); throw; }
    telemetry::set(telemetry::Counter::framing_queue_bytes,buffer_.size());
    return packets;
}
void Framer::reset() { buffer_.clear(); expected_ = 4; }

namespace {
Position position(Reader& r) { Position p; p.x = r.f32(); p.y = r.f32(); return p; }
Stat stat(Reader& r) {
    Stat s; s.type = r.u8();
    if (std::find(flash::string_stats.begin(), flash::string_stats.end(), s.type) != flash::string_stats.end())
        s.value = r.utf();
    else s.value = r.i32();
    return s;
}
Status status(Reader& r) {
    Status s; s.id = r.i32(); s.position = position(r);
    const auto count = r.u8();
    if (count > r.remaining() / 3) throw ProtocolError("Invalid stat count");
    s.stats.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) s.stats.push_back(stat(r));
    return s;
}
std::size_t count16(Reader& r, std::size_t minimum_size) {
    const auto count = r.i16();
    if (count < 0 || static_cast<std::size_t>(count) > r.remaining() / minimum_size)
        throw ProtocolError("Invalid packet collection length");
    return static_cast<std::size_t>(count);
}
}
Update parse_update(std::span<const std::uint8_t> body) {
    Reader r(body); Update u;
    auto count = count16(r, 6); u.tiles.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Tile t; t.x = r.i16(); t.y = r.i16(); t.type = r.u16(); u.tiles.push_back(t);
    }
    count = count16(r, 15); u.objects.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Object o; o.type = r.u16(); o.status = status(r); u.objects.push_back(std::move(o));
    }
    count = count16(r, 5); u.drops.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Drop d; d.id = r.i32(); d.explode = r.boolean(); u.drops.push_back(d);
    }
    r.finish(); return u;
}
Tick parse_tick(std::span<const std::uint8_t> body) {
    Reader r(body); Tick t;
    const auto count = count16(r, 13); t.statuses.reserve(count);
    for (std::size_t i = 0; i < count; ++i) t.statuses.push_back(status(r));
    if (r.remaining()) {
        const auto stats = r.u8();
        if (stats > r.remaining() / 3) throw ProtocolError("Invalid player stat count");
        t.player_stats.reserve(stats);
        for (std::uint16_t i = 0; i < stats; ++i) t.player_stats.push_back(stat(r));
    }
    r.finish(); return t;
}
MapInfo parse_map_info(std::span<const std::uint8_t> body) {
    Reader r(body); MapInfo m;
    m.width = r.i32(); m.height = r.i32(); m.name = r.utf(); m.display_name = r.utf();
    m.seed = r.u32(); m.background = r.i32(); m.show_displays = r.boolean(); m.allow_teleport = r.boolean();
    r.finish(); return m;
}
Bytes move_body(std::int32_t time_ms, Position p) {
    Writer w; w.i32(time_ms); w.f32(p.x); w.f32(p.y); return w.bytes();
}
}
