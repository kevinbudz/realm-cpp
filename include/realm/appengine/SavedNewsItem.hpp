#pragma once
#include <cstdint>
#include <string>
namespace realm {
struct NewsItem {
  std::string icon, title, tagline, link;
  std::int64_t date{};
};
}
