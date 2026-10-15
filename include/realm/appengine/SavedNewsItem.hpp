#pragma once
#include <cstdint>
#include <string>

namespace realm {
struct NewsItem {
  std::string icon;
  std::string title;
  std::string tagline;
  std::string link;
  std::int64_t date{};
};
}
