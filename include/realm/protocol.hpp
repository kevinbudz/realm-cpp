#pragma once
#include "flash_assets.hpp"
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace realm::net {
using Bytes = std::vector<std::uint8_t>;
class ProtocolError : public std::runtime_error { using std::runtime_error::runtime_error; };

// Flash IDataInput/IDataOutput default to big endian. No cipher is applied by
// this fork's SocketServer (unlike some upstream clients).
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::int16_t i16() { return std::bit_cast<std::int16_t>(u16()); }
    std::uint32_t u32();
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
    float f32() { return std::bit_cast<float>(u32()); }
    bool boolean() { return u8() != 0; }
    std::string utf();
    std::span<const std::uint8_t> take(std::size_t count);
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - position_; }
    void finish() const;
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};
class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void utf(std::string_view value);
    void raw(std::span<const std::uint8_t> value);
    [[nodiscard]] const Bytes& bytes() const { return bytes_; }
private:
    Bytes bytes_;
};

inline constexpr std::size_t max_packet_length = 1048576;
struct Packet { std::uint8_t id{}; Bytes body; };
[[nodiscard]] Bytes frame(std::uint8_t id, std::span<const std::uint8_t> body);
inline Bytes frame(flash::PacketId id, std::span<const std::uint8_t> body) {
    return frame(static_cast<std::uint8_t>(id), body);
}
// Consume arbitrary TCP fragments, including multiple packets per fragment.
// Retains a bounded, reusable current-packet buffer, independent of feed size.
class Framer {
public:
    std::vector<Packet> feed(std::span<const std::uint8_t> input);
    void reset();
    [[nodiscard]] std::size_t pending_bytes() const { return buffer_.size(); }
private:
    Bytes buffer_;
    std::size_t expected_{4};
};

struct Position { float x{}, y{}; };
struct Stat { std::uint8_t type{}; std::variant<std::int32_t, std::string> value; };
struct Status { std::int32_t id{}; Position position; std::vector<Stat> stats; };
struct Object { std::uint16_t type{}; Status status; };
struct Tile { std::int16_t x{}, y{}; std::uint16_t type{}; };
struct Drop { std::int32_t id{}; bool explode{}; };
struct Update { std::vector<Tile> tiles; std::vector<Object> objects; std::vector<Drop> drops; };
// This fork has NO tickId/tickTime fields; playerStats is an optional tail.
struct Tick { std::vector<Status> statuses; std::vector<Stat> player_stats; };
struct MapInfo {
    std::int32_t width{}, height{}, background{};
    std::string name, display_name;
    std::uint32_t seed{};
    bool show_displays{}, allow_teleport{};
};
[[nodiscard]] Update parse_update(std::span<const std::uint8_t> body);
[[nodiscard]] Tick parse_tick(std::span<const std::uint8_t> body);
[[nodiscard]] MapInfo parse_map_info(std::span<const std::uint8_t> body);
[[nodiscard]] Bytes move_body(std::int32_t time_ms, Position position);
}
