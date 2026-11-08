// realm::math (src/util/PortableMath.cpp) checks:
//   1. every function is within 1 ulp of the C library over 1e6 deterministic inputs per function,
//   across
//      small, medium, multiples of pi/2, large and huge arguments, and special values;
//   2. sincos(x) equals {sin(x), cos(x)} bit for bit;
//   3. a 64-bit FNV-1a hash of the output bit patterns matches a golden value. The golden values
//   are
//      platform independent: the same file built with Emscripten and run under node must print the
//      same hashes (the web build is the second platform this check protects).
// The test is compiled with -ffp-contract=off (CMakeLists.txt) so the inputs themselves are
// reproducible.
#include "realm/util/PortableMath.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
int checks{};

void check(bool ok, const std::string& why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}

constexpr std::size_t kSamples = 1000000;

// splitmix64: a fixed sequence that uses no library code.
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
  }

  // Uniform in [0, 1).
  double unit() { return static_cast<double>(next() >> 11U) * 0x1p-53; }

  // Uniform in [lo, hi).
  double range(double lo, double hi) { return lo + (hi - lo) * unit(); }

  // Random sign, magnitude 2^e with e uniform in [lo, hi] and a random mantissa.
  double magnitude(int lo, int hi) {
    const int e = lo + static_cast<int>(next() % static_cast<std::uint64_t>(hi - lo + 1));
    const double v = std::ldexp(1.0 + unit(), e);
    return (next() & 1U) != 0 ? -v : v;
  }

 private:
  std::uint64_t state_;
};

std::uint64_t bits_of(double x) {
  return std::bit_cast<std::uint64_t>(x);
}

// Ordered integer for the distance between two doubles in ulps.
std::int64_t ordered(double x) {
  const auto i = std::bit_cast<std::int64_t>(x);
  return i < 0 ? std::numeric_limits<std::int64_t>::min() - i : i;
}

std::int64_t ulp_distance(double a, double b) {
  const std::int64_t d = ordered(a) - ordered(b);
  return d < 0 ? -d : d;
}

// Special values every function sees.
const std::vector<double>& specials() {
  static const std::vector<double> values = {
      0.0,
      -0.0,
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::denorm_min(),
      -std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::min(),
      std::numeric_limits<double>::max(),
      -std::numeric_limits<double>::max(),
      1.0,
      -1.0,
      0x1p-54,
      0x1p-27,
      3.14159265358979323846,
      1.57079632679489661923,
      0.78539816339744830962,
      709.782712893384,
      -745.1332191019411,
      -708.39,
  };
  return values;
}

// Inputs for one-argument functions: a fixed mix of ranges, then the specials. Every generator draw
// has its own statement, because C++ leaves the order of operands and arguments unspecified (GCC
// and Clang differ).
std::vector<double> unary_inputs(const char* which) {
  Rng rng(0x5eed0000ULL + std::string_view(which).size() * 977U + which[0]);
  std::vector<double> out;
  out.reserve(kSamples + 64);
  const std::string_view name(which);
  const bool trig = name == "sin" || name == "cos" || name == "tan";
  while (out.size() < kSamples) {
    if (trig) {
      switch (out.size() % 6) {
        case 0:
          out.push_back(rng.range(-8.0, 8.0));
          break;
        case 1:
          out.push_back(rng.magnitude(-30, 0));
          break;
        case 2: {  // near multiples of pi/2 (the first reduction branches)
          // Each draw has its own statement: the order of operands is unspecified in C++.
          const double k = static_cast<double>(rng.next() % (1U << 20U));
          const double delta = rng.range(-1, 1) * 0x1p-40;
          out.push_back(k * 1.57079632679489655800 + delta);
          break;
        }
        case 3:  // medium and large: the table-free reduction and the first large-argument branch
          out.push_back(rng.magnitude(3, 40));
          break;
        case 4:  // huge: __rem_pio2_large
          out.push_back(rng.magnitude(60, 1000));
          break;
        default:
          out.push_back(rng.magnitude(-1074, 1023));
          break;
      }
    } else if (name == "atan") {
      out.push_back(rng.magnitude(-60, 60));
    } else if (name == "exp") {
      out.push_back(rng.range(-745.0, 710.0));
    } else {  // log, log2: positive, subnormal to huge, and values near 1
      const double v = std::fabs(rng.magnitude(-1074, 1023));
      out.push_back(out.size() % 4 == 0 ? rng.range(0.5, 2.0) : v);
    }
  }
  for (const double v : specials()) {
    out.push_back(v);
  }
  return out;
}

// Input pairs for atan2 and hypot.
std::vector<std::pair<double, double>> binary_inputs(const char* which) {
  Rng rng(0xb1a2c300ULL + which[0] * 31U + which[1]);
  std::vector<std::pair<double, double>> out;
  out.reserve(kSamples + specials().size() * specials().size());
  while (out.size() < kSamples) {
    switch (out.size() % 3) {
      case 0: {
        const double a = rng.range(-10.0, 10.0);
        const double b = rng.range(-10.0, 10.0);
        out.emplace_back(a, b);
        break;
      }
      case 1: {  // wide exponent spread: the ratio decides the atan2 branch
        const double a = rng.magnitude(-60, 60);
        const double b = rng.magnitude(-60, 60);
        out.emplace_back(a, b);
        break;
      }
      default: {  // hypot's scaling paths: exponents far apart and near the overflow and underflow
                  // limits
        const double a = rng.magnitude(-1000, 1000);
        const double b = rng.magnitude(-1000, 1000);
        out.emplace_back(a, b);
        break;
      }
    }
  }
  for (const double a : specials()) {
    for (const double b : specials()) {
      out.emplace_back(a, b);
    }
  }
  return out;
}

// FNV-1a over the bit patterns of the outputs; every NaN hashes as the canonical quiet NaN, because
// NaN signs differ between hardware (x86 default NaN is negative) and wasm arithmetic.
class Fnv {
 public:
  void add(double x) {
    const std::uint64_t b =
        std::isnan(x) ? bits_of(std::numeric_limits<double>::quiet_NaN()) : bits_of(x);
    for (int i = 0; i < 8; ++i) {
      hash_ ^= (b >> (8 * i)) & 0xffU;
      hash_ *= 0x100000001b3ULL;
    }
  }

  [[nodiscard]] std::uint64_t value() const { return hash_; }

 private:
  std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

struct Report {
  std::string name;
  std::uint64_t golden;
  std::uint64_t hash;
  std::int64_t max_ulp;
};

// Golden values: the same hashes come out of the native build (GCC 16, -O3) and the Emscripten
// build run under node (clang, -O2, -fwasm-exceptions). A change means the port's results changed;
// explain it in the commit.
constexpr std::uint64_t kGoldenSin = 0x272379db7e3aedecULL;
constexpr std::uint64_t kGoldenCos = 0xd07577969ac3df41ULL;
constexpr std::uint64_t kGoldenSincos = 0x6ae42af65ca1ee2fULL;
constexpr std::uint64_t kGoldenTan = 0xfdf88ad7ef57d848ULL;
constexpr std::uint64_t kGoldenAtan = 0xf22ca916a8d8c44bULL;
constexpr std::uint64_t kGoldenAtan2 = 0x4d0c92bb20ece90bULL;
constexpr std::uint64_t kGoldenHypot = 0x34b3d28ad7482d56ULL;
constexpr std::uint64_t kGoldenExp = 0xc1e7544909b6050eULL;
constexpr std::uint64_t kGoldenLog = 0xea95692efcfd17a1ULL;
constexpr std::uint64_t kGoldenLog2 = 0x3d701075e693d56eULL;

void report(const Report& r) {
  std::printf("%-8s max_ulp=%lld hash=0x%016llx%s\n", r.name.c_str(),
              static_cast<long long>(r.max_ulp), static_cast<unsigned long long>(r.hash),
              r.golden == r.hash ? "" : "  (golden differs)");
}

// Compares one unary function with the C library and hashes its outputs.
Report unary(const char* name, double (*ours)(double), double (*theirs)(double),
             std::uint64_t golden) {
  const std::vector<double> inputs = unary_inputs(name);
  Fnv fnv;
  std::int64_t max_ulp = 0;
  for (const double x : inputs) {
    const double got = ours(x);
    const double want = theirs(x);
    fnv.add(got);
    if (std::isnan(got) || std::isnan(want)) {
      check(std::isnan(got) && std::isnan(want), std::string(name) + ": NaN mismatch");
      continue;
    }
    const std::int64_t d = ulp_distance(got, want);
    max_ulp = d > max_ulp ? d : max_ulp;
    check(d <= 1, std::string(name) + " is more than 1 ulp from the C library");
  }
  check(max_ulp <= 1, std::string(name) + ": ulp bound");
  return {name, golden, fnv.value(), max_ulp};
}

Report binary(const char* name, double (*ours)(double, double), double (*theirs)(double, double),
              std::uint64_t golden) {
  const auto inputs = binary_inputs(name);
  Fnv fnv;
  std::int64_t max_ulp = 0;
  for (const auto& [a, b] : inputs) {
    const double got = ours(a, b);
    const double want = theirs(a, b);
    fnv.add(got);
    if (std::isnan(got) || std::isnan(want)) {
      check(std::isnan(got) && std::isnan(want), std::string(name) + ": NaN mismatch");
      continue;
    }
    const std::int64_t d = ulp_distance(got, want);
    max_ulp = d > max_ulp ? d : max_ulp;
    check(d <= 1, std::string(name) + " is more than 1 ulp from the C library");
  }
  return {name, golden, fnv.value(), max_ulp};
}

double sin_ref(double x) {
  return std::sin(x);
}

double cos_ref(double x) {
  return std::cos(x);
}

double tan_ref(double x) {
  return std::tan(x);
}

double atan_ref(double x) {
  return std::atan(x);
}

double exp_ref(double x) {
  return std::exp(x);
}

double log_ref(double x) {
  return std::log(x);
}

double log2_ref(double x) {
  return std::log2(x);
}

double atan2_ref(double y, double x) {
  return std::atan2(y, x);
}

double hypot_ref(double x, double y) {
  return std::hypot(x, y);
}

double sin_ours(double x) {
  return realm::math::sin(x);
}

double cos_ours(double x) {
  return realm::math::cos(x);
}

double tan_ours(double x) {
  return realm::math::tan(x);
}

double atan_ours(double x) {
  return realm::math::atan(x);
}

double exp_ours(double x) {
  return realm::math::exp(x);
}

double log_ours(double x) {
  return realm::math::log(x);
}

double log2_ours(double x) {
  return realm::math::log2(x);
}

double atan2_ours(double y, double x) {
  return realm::math::atan2(y, x);
}

double hypot_ours(double x, double y) {
  return realm::math::hypot(x, y);
}

// Float overloads: the double result rounded to float, within 1 float ulp of the C library's float
// functions.
void float_overloads() {
  Rng rng(0xf10a7000ULL);
  std::int64_t max_ulp = 0;
  for (std::size_t i = 0; i < 100000; ++i) {
    const float x = static_cast<float>(rng.magnitude(-20, 20));
    const float p = std::fabs(static_cast<float>(rng.magnitude(-100, 100)));
    const float e = static_cast<float>(rng.range(-80.0, 80.0));
    const float pairs[3][2] = {{realm::math::exp(e), std::exp(e)},
                               {realm::math::log2(p), std::log2(p)},
                               {realm::math::sin(x), std::sin(x)}};
    for (const auto& pr : pairs) {
      if (std::isnan(pr[0]) || std::isnan(pr[1])) {
        check(std::isnan(pr[0]) && std::isnan(pr[1]), "float overload NaN mismatch");
        continue;
      }
      std::int32_t a = std::bit_cast<std::int32_t>(pr[0]);
      std::int32_t b = std::bit_cast<std::int32_t>(pr[1]);
      a = a < 0 ? std::numeric_limits<std::int32_t>::min() - a : a;
      b = b < 0 ? std::numeric_limits<std::int32_t>::min() - b : b;
      const std::int64_t d = std::abs(static_cast<std::int64_t>(a) - b);
      max_ulp = d > max_ulp ? d : max_ulp;
      check(d <= 1, "float overload is more than 1 float ulp from the C library");
    }
  }
  std::printf("float    max_ulp=%lld (exp, log2, sin over 100000 inputs each)\n",
              static_cast<long long>(max_ulp));
}

void sincos_identity() {
  std::size_t count = 0;
  for (const double x : unary_inputs("sin")) {
    const realm::math::SinCos sc = realm::math::sincos(x);
    check(bits_of(sc.sine) == bits_of(realm::math::sin(x)), "sincos sine differs from sin");
    check(bits_of(sc.cosine) == bits_of(realm::math::cos(x)), "sincos cosine differs from cos");
    ++count;
  }
  std::printf("sincos   %zu inputs equal {sin, cos} bit for bit\n", count);
}

}  // namespace

int main() {
  try {
    // Each report runs the 1 ulp checks first; golden comparisons come after all reports are
    // printed.
    const std::vector<Report> reports = {
        unary("sin", sin_ours, sin_ref, kGoldenSin),
        unary("cos", cos_ours, cos_ref, kGoldenCos),
        unary("tan", tan_ours, tan_ref, kGoldenTan),
        unary("atan", atan_ours, atan_ref, kGoldenAtan),
        binary("atan2", atan2_ours, atan2_ref, kGoldenAtan2),
        binary("hypot", hypot_ours, hypot_ref, kGoldenHypot),
        unary("exp", exp_ours, exp_ref, kGoldenExp),
        unary("log", log_ours, log_ref, kGoldenLog),
        unary("log2", log2_ours, log2_ref, kGoldenLog2),
    };
    for (const Report& r : reports) {
      report(r);
    }
    Fnv pairs;
    for (const double x : unary_inputs("sin")) {
      const realm::math::SinCos sc = realm::math::sincos(x);
      pairs.add(sc.sine);
      pairs.add(sc.cosine);
    }
    std::printf("%-8s hash=0x%016llx%s\n", "sincos", static_cast<unsigned long long>(pairs.value()),
                pairs.value() == kGoldenSincos ? "" : "  (golden differs)");
    for (const Report& r : reports) {
      check(r.golden != 0, r.name + ": golden not set");
      check(r.hash == r.golden, r.name + ": output hash differs from the golden value");
    }
    check(kGoldenSincos != 0 && pairs.value() == kGoldenSincos,
          "sincos: pair hash differs from the golden value");
    sincos_identity();
    float_overloads();
    std::printf("portable_math: %d checks passed\n", checks);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "portable_math: %s\n", error.what());
    return 1;
  }
}
