#include "realm/util/Telemetry.hpp"
#include "realm/messaging/Protocol.hpp"
#include <algorithm>
#include <limits>

namespace realm::net {
std::span<const std::uint8_t> Reader::take(std::size_t count) {
  if (count > remaining()) {
    throw ProtocolError("Truncated packet");
  }
  const auto result = bytes_.subspan(position_, count);
  position_ += count;
  return result;
}

std::uint8_t Reader::u8() {
  return take(1)[0];
}

std::uint16_t Reader::u16() {
  const auto bytes = take(2);
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

std::uint32_t Reader::u32() {
  const auto bytes = take(4);
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
}

std::string Reader::utf() {
  const auto bytes = take(u16());
  return std::string(bytes.begin(), bytes.end());
}

void Reader::finish() const {
  if (remaining()) {
    throw ProtocolError("Unexpected packet trailing bytes");
  }
}

void Writer::u16(std::uint16_t value) {
  u8(static_cast<std::uint8_t>(value >> 8U));
  u8(static_cast<std::uint8_t>(value));
}

void Writer::u32(std::uint32_t value) {
  u8(static_cast<std::uint8_t>(value >> 24U));
  u8(static_cast<std::uint8_t>(value >> 16U));
  u8(static_cast<std::uint8_t>(value >> 8U));
  u8(static_cast<std::uint8_t>(value));
}

void Writer::utf(std::string_view value) {
  if (value.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw ProtocolError("String exceeds writeUTF byte limit");
  }

  u16(static_cast<std::uint16_t>(value.size()));
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void Writer::raw(std::span<const std::uint8_t> value) {
  bytes_.insert(bytes_.end(), value.begin(), value.end());
}

Bytes frame(std::uint8_t id, std::span<const std::uint8_t> body) {
  if (body.size() > max_packet_length - 5) {
    throw ProtocolError("Packet exceeds length limit");
  }

  Writer writer;
  writer.u32(static_cast<std::uint32_t>(body.size() + 5));
  writer.u8(id);
  writer.raw(body);
  return writer.bytes();
}

std::vector<Packet> Framer::feed(std::span<const std::uint8_t> input) {
  REALM_ZONE("network.framing");
  std::vector<Packet> packets;
  try {
    while (!input.empty()) {
      const auto count = std::min(input.size(), expected_ - buffer_.size());
      buffer_.insert(buffer_.end(), input.begin(),
                     input.begin() + static_cast<std::ptrdiff_t>(count));
      input = input.subspan(count);
      if (buffer_.size() < expected_) {
        break;
      }
      if (expected_ == 4) {
        Reader reader(buffer_);
        expected_ = reader.u32();
        if (expected_ < 5 || expected_ > max_packet_length) {
          throw ProtocolError("Invalid frame length");
        }
      } else {
        packets.push_back({buffer_[4], Bytes(buffer_.begin() + 5, buffer_.end())});
        reset();
      }
    }
  } catch (...) {
    reset();
    throw;
  }
  telemetry::set(telemetry::Counter::framing_queue_bytes, buffer_.size());
  return packets;
}

void Framer::reset() {
  buffer_.clear();
  expected_ = 4;
}

namespace {
Position position(Reader& reader) {
  Position point;
  point.x = reader.f32();
  point.y = reader.f32();
  return point;
}

Stat stat(Reader& reader) {
  Stat parsed;
  parsed.type = reader.u8();
  if (std::find(flash::string_stats.begin(), flash::string_stats.end(), parsed.type) !=
      flash::string_stats.end()) {
    parsed.value = reader.utf();
  } else {
    parsed.value = reader.i32();
  }
  return parsed;
}

Status status(Reader& reader) {
  Status parsed;
  parsed.id = reader.i32();
  parsed.position = position(reader);
  const auto count = reader.u8();
  if (count > reader.remaining() / 3) {
    throw ProtocolError("Invalid stat count");
  }
  parsed.stats.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    parsed.stats.push_back(stat(reader));
  }
  return parsed;
}

std::size_t count16(Reader& reader, std::size_t minimum_size) {
  const auto count = reader.i16();
  if (count < 0 || static_cast<std::size_t>(count) > reader.remaining() / minimum_size) {
    throw ProtocolError("Invalid packet collection length");
  }
  return static_cast<std::size_t>(count);
}
}

Update parse_update(std::span<const std::uint8_t> body) {
  Reader reader(body);
  Update update;
  auto count = count16(reader, 6);
  update.tiles.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    Tile tile;
    tile.x = reader.i16();
    tile.y = reader.i16();
    tile.type = reader.u16();
    update.tiles.push_back(tile);
  }

  count = count16(reader, 15);
  update.objects.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    Object object;
    object.type = reader.u16();
    object.status = status(reader);
    update.objects.push_back(std::move(object));
  }

  count = count16(reader, 5);
  update.drops.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    Drop drop;
    drop.id = reader.i32();
    drop.explode = reader.boolean();
    update.drops.push_back(drop);
  }

  reader.finish();
  return update;
}

Tick parse_tick(std::span<const std::uint8_t> body) {
  Reader reader(body);
  Tick tick;
  const auto count = count16(reader, 13);
  tick.statuses.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    tick.statuses.push_back(status(reader));
  }

  if (reader.remaining()) {
    const auto stats = reader.u8();
    if (stats > reader.remaining() / 3) {
      throw ProtocolError("Invalid player stat count");
    }
    tick.player_stats.reserve(stats);
    for (std::uint16_t i = 0; i < stats; ++i) {
      tick.player_stats.push_back(stat(reader));
    }
  }

  reader.finish();
  return tick;
}

MapInfo parse_map_info(std::span<const std::uint8_t> body) {
  Reader reader(body);
  MapInfo map;
  map.width = reader.i32();
  map.height = reader.i32();
  map.name = reader.utf();
  map.display_name = reader.utf();
  map.seed = reader.u32();
  map.background = reader.i32();
  map.show_displays = reader.boolean();
  map.allow_teleport = reader.boolean();
  reader.finish();
  return map;
}

Bytes move_body(std::int32_t time_ms, Position position) {
  Writer writer;
  writer.i32(time_ms);
  writer.f32(position.x);
  writer.f32(position.y);
  return writer.bytes();
}
}
