#pragma once

// Portable double-precision transcendental functions. Use these instead of std:: or :: sin, cos,
// tan, atan, atan2, hypot, exp, log and log2: C libraries round the last bit differently (glibc,
// Emscripten's musl, MSVC), so the same simulation step gives different particle and projectile
// positions per platform. These functions return the same bits wherever IEEE 754 doubles are used
// without contraction. sqrt, fabs, floor, fmod and remainder are exact and stay in std. See
// src/util/PortableMath.cpp.
//
// The float overloads round the double result to float, so they are deterministic too. Pass a float
// or a double (an integer argument is ambiguous; cast it).

namespace realm::math {

struct SinCos {
  double sine;
  double cosine;
};

[[nodiscard]] double sin(double x) noexcept;
[[nodiscard]] double cos(double x) noexcept;
// Equal to {sin(x), cos(x)} bit for bit, with one argument reduction.
[[nodiscard]] SinCos sincos(double x) noexcept;
[[nodiscard]] double tan(double x) noexcept;
[[nodiscard]] double atan(double x) noexcept;
[[nodiscard]] double atan2(double y, double x) noexcept;
[[nodiscard]] double hypot(double x, double y) noexcept;
[[nodiscard]] double exp(double x) noexcept;
[[nodiscard]] double log(double x) noexcept;
[[nodiscard]] double log2(double x) noexcept;

[[nodiscard]] inline float sin(float x) noexcept {
  return static_cast<float>(sin(static_cast<double>(x)));
}

[[nodiscard]] inline float cos(float x) noexcept {
  return static_cast<float>(cos(static_cast<double>(x)));
}

[[nodiscard]] inline float tan(float x) noexcept {
  return static_cast<float>(tan(static_cast<double>(x)));
}

[[nodiscard]] inline float atan(float x) noexcept {
  return static_cast<float>(atan(static_cast<double>(x)));
}

[[nodiscard]] inline float atan2(float y, float x) noexcept {
  return static_cast<float>(atan2(static_cast<double>(y), static_cast<double>(x)));
}

[[nodiscard]] inline float hypot(float x, float y) noexcept {
  return static_cast<float>(hypot(static_cast<double>(x), static_cast<double>(y)));
}

[[nodiscard]] inline float exp(float x) noexcept {
  return static_cast<float>(exp(static_cast<double>(x)));
}

[[nodiscard]] inline float log(float x) noexcept {
  return static_cast<float>(log(static_cast<double>(x)));
}

[[nodiscard]] inline float log2(float x) noexcept {
  return static_cast<float>(log2(static_cast<double>(x)));
}

}  // namespace realm::math
