#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace realm {
struct GuildMember {
  std::string name;
  int rank{};
  int fame{};
};

struct GuildMemberList {
  std::string name;
  int fame{};
  int level{};
  std::vector<GuildMember> members;
};

[[nodiscard]] GuildMemberList parse_guild_members(std::string_view xml);
}
