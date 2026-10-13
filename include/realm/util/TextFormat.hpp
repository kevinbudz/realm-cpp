#pragma once
#include <iomanip>
#include <sstream>
#include <string>

namespace realm {
// Four decimals with trailing zeros and a bare decimal point removed ("1.5", "2", "-0"). Unlike a fixed snprintf
// buffer, the stream grows with the value, so large magnitudes are never truncated.
[[nodiscard]] inline std::string trimmed_number(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(4) << value;
  auto result = out.str();
  while (result.size() > 1 && result.back() == '0') result.pop_back();
  if (result.back() == '.') result.pop_back();
  return result;
}
}
