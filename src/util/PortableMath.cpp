/*
 * PortableMath.cpp: double-precision sin, cos, sincos, tan, atan, atan2, hypot, exp, log and log2
 * that give the same bits on every platform with IEEE 754 doubles. See PortableMath.hpp.
 *
 * The algorithms are musl libc 1.2.6 (src/math): FreeBSD msun (fdlibm) code for the trigonometric
 * and atan functions, musl's own hypot, and the Arm optimized-routines exp, log and log2 with their
 * tables. The constants and tables below are copied from those sources (generated, not retyped),
 * and every expression keeps musl's operation order. The changes are: no errno, no FORCE_EVAL side
 * effects, the non-FMA branches of log and log2 (never fused multiply-add), sincos uses sin's and
 * cos's own thresholds (so its results equal separate sin and cos calls bit for bit), and the float
 * overloads in the header round a double result.
 *
 * This file must compile with -ffp-contract=off (cmake/ClientTargets.cmake) and without -ffast-math,
 * -march or -mfma. The only library calls are std::sqrt, std::floor, std::fabs and std::isnan, which
 * are exact. tests/PortableMathTests.cpp checks the results against the C library within 1 ulp and
 * against golden hashes, which the native build and the Emscripten build (run under node) must both
 * reproduce. When the port was written, every function was also compared bit for bit with Emscripten's
 * musl over 2 million random inputs and special values, with no mismatch.
 */

#include "realm/util/PortableMath.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace realm::math {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr std::uint64_t bits_of(double x) noexcept {
  return std::bit_cast<std::uint64_t>(x);
}

constexpr double double_of(std::uint64_t bits) noexcept {
  return std::bit_cast<double>(bits);
}

constexpr std::uint32_t high_word(double x) noexcept {
  return static_cast<std::uint32_t>(bits_of(x) >> 32U);
}

constexpr std::uint32_t low_word(double x) noexcept {
  return static_cast<std::uint32_t>(bits_of(x));
}

// musl's SET_LOW_WORD(x, 0): the low 32 bits of the mantissa are cleared.
constexpr double clear_low_word(double x) noexcept {
  return double_of(bits_of(x) & (~std::uint64_t{0} << 32U));
}

// musl scalbn: x * 2^n, with the same two-step scaling so subnormal results round once.
double scale_by_pow2(double x, int n) noexcept {
  double y = x;
  if (n > 1023) {
    y *= 0x1p1023;
    n -= 1023;
    if (n > 1023) {
      y *= 0x1p1023;
      n -= 1023;
      if (n > 1023) {
        n = 1023;
      }
    }
  } else if (n < -1022) {
    // Make sure the final n is below -53 to avoid double rounding in the subnormal range.
    y *= 0x1p-1022 * 0x1p53;
    n += 1022 - 53;
    if (n < -1022) {
      y *= 0x1p-1022 * 0x1p53;
      n += 1022 - 53;
      if (n < -1022) {
        n = -1022;
      }
    }
  }
  return y * double_of(static_cast<std::uint64_t>(0x3ff + n) << 52U);
}

// ---------------------------------------------------------------------------------------------
// sin, cos, tan: argument reduction and kernels (musl __rem_pio2.c, __rem_pio2_large.c, __sin.c,
// __cos.c, __tan.c).
// ---------------------------------------------------------------------------------------------

constexpr double kS1 = -1.66666666666666324348e-01;
constexpr double kS2 = 8.33333333332248946124e-03;
constexpr double kS3 = -1.98412698298579493134e-04;
constexpr double kS4 = 2.75573137070700676789e-06;
constexpr double kS5 = -2.50507602534068634195e-08;
constexpr double kS6 = 1.58969099521155010221e-10;

constexpr double kC1 = 4.16666666666666019037e-02;
constexpr double kC2 = -1.38888888888741095749e-03;
constexpr double kC3 = 2.48015872894767294178e-05;
constexpr double kC4 = -2.75573143513906633035e-07;
constexpr double kC5 = 2.08757232129817482790e-09;
constexpr double kC6 = -1.13596475577881948265e-11;

// 1.5 / DBL_EPSILON: adding and subtracting it rounds to the nearest integer.
constexpr double kToint = 6755399441055744.0;
constexpr double kPio4 = 0x1.921fb54442d18p-1;
constexpr double kInvpio2 = 6.36619772367581382433e-01;
constexpr double kPio2_1 = 1.57079632673412561417e+00;
constexpr double kPio2_1t = 6.07710050650619224932e-11;
constexpr double kPio2_2 = 6.07710050630396597660e-11;
constexpr double kPio2_2t = 2.02226624879595063154e-21;
constexpr double kPio2_3 = 2.02226624871116645580e-21;
constexpr double kPio2_3t = 8.47842766036889956997e-32;

// 24-bit pieces of 2/pi.
constexpr std::int32_t kIpio2[690] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041, 0xFE5163,
    0xABDEBB, 0xC561B7, 0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
    0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C,
    0x845F8B, 0xBDF928, 0x3B1FF8, 0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
    0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5, 0xF17B3D, 0x0739F7, 0x8A5292,
    0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
    0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880, 0x4D7327, 0x310606, 0x1556CA,
    0x73A8C9, 0x60E27B, 0xC08C6B, 0x47C419, 0xC367CD, 0xDCE809, 0x2A8359, 0xC4768B, 0x961CA6,
    0xDDAF44, 0xD15719, 0x053EA5, 0xFF0705, 0x3F7E33, 0xE832C2, 0xDE4F98, 0x327DBB, 0xC33D26,
    0xEF6B1E, 0x5EF89F, 0x3A1F35, 0xCAF27F, 0x1D87F1, 0x21907C, 0x7C246A, 0xFA6ED5, 0x772D30,
    0x433B15, 0xC614B5, 0x9D19C3, 0xC2C4AD, 0x414D2C, 0x5D000C, 0x467D86, 0x2D71E3, 0x9AC69B,
    0x006233, 0x7CD2B4, 0x97A7B4, 0xD55537, 0xF63ED7, 0x1810A3, 0xFC764D, 0x2A9D64, 0xABD770,
    0xF87C63, 0x57B07A, 0xE71517, 0x5649C0, 0xD9D63B, 0x3884A7, 0xCB2324, 0x778AD6, 0x23545A,
    0xB91F00, 0x1B0AF1, 0xDFCE19, 0xFF319F, 0x6A1E66, 0x615799, 0x47FBAC, 0xD87F7E, 0xB76522,
    0x89E832, 0x60BFE6, 0xCDC4EF, 0x09366C, 0xD43F5D, 0xD7DE16, 0xDE3B58, 0x929BDE, 0x2822D2,
    0xE88628, 0x4D58E2, 0x32CAC6, 0x16E308, 0xCB7DE0, 0x50C017, 0xA71DF3, 0x5BE018, 0x34132E,
    0x621283, 0x014883, 0x5B8EF5, 0x7FB0AD, 0xF2E91E, 0x434A48, 0xD36710, 0xD8DDAA, 0x425FAE,
    0xCE616A, 0xA4280A, 0xB499D3, 0xF2A606, 0x7F775C, 0x83C2A3, 0x883C61, 0x78738A, 0x5A8CAF,
    0xBDD76F, 0x63A62D, 0xCBBFF4, 0xEF818D, 0x67C126, 0x45CA55, 0x36D9CA, 0xD2A828, 0x8D61C2,
    0x77C912, 0x142604, 0x9B4612, 0xC459C4, 0x44C5C8, 0x91B24D, 0xF31700, 0xAD43D4, 0xE54929,
    0x10D5FD, 0xFCBE00, 0xCC941E, 0xEECE70, 0xF53E13, 0x80F1EC, 0xC3E7B3, 0x28F8C7, 0x940593,
    0x3E71C1, 0xB3092E, 0xF3450B, 0x9C1288, 0x7B20AB, 0x9FB52E, 0xC29247, 0x2F327B, 0x6D550C,
    0x90A772, 0x1FE76B, 0x96CB31, 0x4A1679, 0xE27941, 0x89DFF4, 0x9794E8, 0x84E6E2, 0x973199,
    0x6BED88, 0x365F5F, 0x0EFDBB, 0xB49A48, 0x6CA467, 0x427271, 0x325D8D, 0xB8159F, 0x09E5BC,
    0x25318D, 0x3974F7, 0x1C0530, 0x010C0D, 0x68084B, 0x58EE2C, 0x90AA47, 0x02E774, 0x24D6BD,
    0xA67DF7, 0x72486E, 0xEF169F, 0xA6948E, 0xF691B4, 0x5153D1, 0xF20ACF, 0x339820, 0x7E4BF5,
    0x6863B2, 0x5F3EDD, 0x035D40, 0x7F8985, 0x295255, 0xC06437, 0x10D86D, 0x324832, 0x754C5B,
    0xD4714E, 0x6E5445, 0xC1090B, 0x69F52A, 0xD56614, 0x9D0727, 0x50045D, 0xDB3BB4, 0xC576EA,
    0x17F987, 0x7D6B49, 0xBA271D, 0x296996, 0xACCCC6, 0x5414AD, 0x6AE290, 0x89D988, 0x50722C,
    0xBEA404, 0x940777, 0x7030F3, 0x27FC00, 0xA871EA, 0x49C266, 0x3DE064, 0x83DD97, 0x973FA3,
    0xFD9443, 0x8C860D, 0xDE4131, 0x9D3992, 0x8C70DD, 0xE7B717, 0x3BDF08, 0x2B3715, 0xA0805C,
    0x93805A, 0x921110, 0xD8E80F, 0xAF806C, 0x4BFFDB, 0x0F9038, 0x761859, 0x15A562, 0xBBCB61,
    0xB989C7, 0xBD4010, 0x04F2D2, 0x277549, 0xF6B6EB, 0xBB22DB, 0xAA140A, 0x2F2689, 0x768364,
    0x333B09, 0x1A940E, 0xAA3A51, 0xC2A31D, 0xAEEDAF, 0x12265C, 0x4DC26D, 0x9C7A2D, 0x9756C0,
    0x833F03, 0xF6F009, 0x8C402B, 0x99316D, 0x07B439, 0x15200C, 0x5BC3D8, 0xC492F5, 0x4BADC6,
    0xA5CA4E, 0xCD37A7, 0x36A9E6, 0x9492AB, 0x6842DD, 0xDE6319, 0xEF8C76, 0x528B68, 0x37DBFC,
    0xABA1AE, 0x3115DF, 0xA1AE00, 0xDAFB0C, 0x664D64, 0xB705ED, 0x306529, 0xBF5657, 0x3AFF47,
    0xB9F96A, 0xF3BE75, 0xDF9328, 0x3080AB, 0xF68C66, 0x15CB04, 0x0622FA, 0x1DE4D9, 0xA4B33D,
    0x8F1B57, 0x09CD36, 0xE9424E, 0xA4BE13, 0xB52333, 0x1AAAF0, 0xA8654F, 0xA5C1D2, 0x0F3F0B,
    0xCD785B, 0x76F923, 0x048B7B, 0x721789, 0x53A6C6, 0xE26E6F, 0x00EBEF, 0x584A9B, 0xB7DAC4,
    0xBA66AA, 0xCFCF76, 0x1D02D1, 0x2DF1B1, 0xC1998C, 0x77ADC3, 0xDA4886, 0xA05DF7, 0xF480C6,
    0x2FF0AC, 0x9AECDD, 0xBC5C3F, 0x6DDED0, 0x1FC790, 0xB6DB2A, 0x3A25A3, 0x9AAF00, 0x9353AD,
    0x0457B6, 0xB42D29, 0x7E804B, 0xA707DA, 0x0EAA76, 0xA1597B, 0x2A1216, 0x2DB7DC, 0xFDE5FA,
    0xFEDB89, 0xFDBE89, 0x6C76E4, 0xFCA906, 0x70803E, 0x156E85, 0xFF87FD, 0x073E28, 0x336761,
    0x86182A, 0xEABD4D, 0xAFE7B3, 0x6E6D8F, 0x396795, 0x5BBF31, 0x48D784, 0x16DF30, 0x432DC7,
    0x356125, 0xCE70C9, 0xB8CB30, 0xFD6CBF, 0xA200A4, 0xE46C05, 0xA0DD5A, 0x476F21, 0xD21262,
    0x845CB9, 0x496170, 0xE0566B, 0x015299, 0x375550, 0xB7D51E, 0xC4F133, 0x5F6E13, 0xE4305D,
    0xA92E85, 0xC3B21D, 0x3632A1, 0xA4B708, 0xD4B1EA, 0x21F716, 0xE4698F, 0x77FF27, 0x80030C,
    0x2D408D, 0xA0CD4F, 0x99A520, 0xD3A2B3, 0x0A5D2F, 0x42F9B4, 0xCBDA11, 0xD0BE7D, 0xC1DB9B,
    0xBD17AB, 0x81A2CA, 0x5C6A08, 0x17552E, 0x550027, 0xF0147F, 0x8607E1, 0x640B14, 0x8D4196,
    0xDEBE87, 0x2AFDDA, 0xB6256B, 0x34897B, 0xFEF305, 0x9EBFB9, 0x4F6A68, 0xA82A4A, 0x5AC44F,
    0xBCF82D, 0x985AD7, 0x95C7F4, 0x8D4D0D, 0xA63A20, 0x5F57A4, 0xB13F14, 0x953880, 0x0120CC,
    0x86DD71, 0xB6DEC9, 0xF560BF, 0x11654D, 0x6B0701, 0xACB08C, 0xD0C0B2, 0x485551, 0x0EFB1E,
    0xC37295, 0x3B06A3, 0x3540C0, 0x7BDC06, 0xCC45E0, 0xFA294E, 0xC8CAD6, 0x41F3E8, 0xDE647C,
    0xD8649B, 0x31BED9, 0xC397A4, 0xD45877, 0xC5E369, 0x13DAF0, 0x3C3ABA, 0x461846, 0x5F7555,
    0xF5BDD2, 0xC6926E, 0x5D2EAC, 0xED440E, 0x423E1C, 0x87C461, 0xE9FD29, 0xF3D6E7, 0xCA7C22,
    0x35916F, 0xC5E008, 0x8DD7FF, 0xE26A6E, 0xC6FDB0, 0xC10893, 0x745D7C, 0xB2AD6B, 0x9D6ECD,
    0x7B723E, 0x6A11C6, 0xA9CFF7, 0xDF7329, 0xBAC9B5, 0x5100B7, 0x0DB2E2, 0x24BA74, 0x607DE5,
    0x8AD874, 0x2C150D, 0x0C1881, 0x94667E, 0x162901, 0x767A9F, 0xBEFDFD, 0xEF4556, 0x367ED9,
    0x13D9EC, 0xB9BA8B, 0xFC97C4, 0x27A831, 0xC36EF1, 0x36C594, 0x56A8D8, 0xB5A8B4, 0x0ECCCF,
    0x2D8912, 0x34576F, 0x89562C, 0xE3CE99, 0xB920D6, 0xAA5E6B, 0x9C2A3E, 0xCC5F11, 0x4A0BFD,
    0xFBF4E1, 0x6D3B8E, 0x2C86E2, 0x84D4E9, 0xA9B4FC, 0xD1EEEF, 0xC9352E, 0x61392F, 0x442138,
    0xC8D91B, 0x0AFC81, 0x6A4AFB, 0xD81C2F, 0x84B453, 0x8C994E, 0xCC2254, 0xDC552A, 0xD6C6C0,
    0x96190B, 0xB8701A, 0x649569, 0x605A26, 0xEE523F, 0x0F117F, 0x11B5F4, 0xF5CBFC, 0x2DBC34,
    0xEEBC34, 0xCC5DE8, 0x605EDD, 0x9B8E67, 0xEF3392, 0xB817C9, 0x9B5861, 0xBC57E1, 0xC68351,
    0x103ED8, 0x4871DD, 0xDD1C2D, 0xA118AF, 0x462C21, 0xD7F359, 0x987AD9, 0xC0549E, 0xFA864F,
    0xFC0656, 0xAE79E5, 0x362289, 0x22AD38, 0xDC9367, 0xAAE855, 0x382682, 0x9BE7CA, 0xA40D51,
    0xB13399, 0x0ED7A9, 0x480569, 0xF0B265, 0xA7887F, 0x974C88, 0x36D1F9, 0xB39221, 0x4A827B,
    0x21CF98, 0xDC9F40, 0x5547DC, 0x3A74E1, 0x42EB67, 0xDF9DFE, 0x5FD45E, 0xA4677B, 0x7AACBA,
    0xA2F655, 0x23882B, 0x55BA41, 0x086E59, 0x862A21, 0x834739, 0xE6E389, 0xD49EE5, 0x40FB49,
    0xE956FF, 0xCA0F1C, 0x8A59C5, 0x2BFA94, 0xC5C1D3, 0xCFC50F, 0xAE5ADB, 0x86C547, 0x624385,
    0x3B8621, 0x94792C, 0x876110, 0x7B4C2A, 0x1A2C80, 0x12BF43, 0x902688, 0x893C78, 0xE4C4A8,
    0x7BDBE5, 0xC23AC4, 0xEAF426, 0x8A67F7, 0xBF920D, 0x2BA365, 0xB1933D, 0x0B7CBD, 0xDC51A4,
    0x63DD27, 0xDDE169, 0x19949A, 0x9529A8, 0x28CE68, 0xB4ED09, 0x209F44, 0xCA984E, 0x638270,
    0x237C7E, 0x32B90F, 0x8EF5A7, 0xE75614, 0x08F121, 0x2A9DB5, 0x4D7E6F, 0x5119A5, 0xABF9B5,
    0xD6DF82, 0x61DD96, 0x023616, 0x9F3AC4, 0xA1A283, 0x6DED72, 0x7A8D39, 0xA9B882, 0x5C326B,
    0x5B2746, 0xED3400, 0x7700D2, 0x55F4FC, 0x4D5901, 0x8071E0};

constexpr double kPIo2[8] = {1.57079625129699707031e+00, 7.54978941586159635335e-08,
                             5.39030252995776476554e-15, 3.28200341580791294123e-22,
                             1.27065575308067607349e-29, 1.22933308981111328932e-36,
                             2.73370053816464559624e-44, 2.16741683877804819444e-51};

// Reduces x (which must have a non-zero 53-bit exponent above the small range) to y[0] + y[1] in
// (-pi/4, pi/4]; the return value is the quadrant n, reduced modulo 8 on the large path.
// musl __rem_pio2_large with prec = 1 (the only precision the client's callers use).
int rem_pio2_large(const double* x, double* y, int e0, int nx) noexcept {
  constexpr int jk = 4;  // init_jk[1]
  std::int32_t jz, jx, jv, jp, carry, n, iq[20], i, j, k, m, q0, ih;
  double z, fw, f[20] = {}, fq[20] = {}, q[20] = {};

  jp = jk;
  jx = nx - 1;
  jv = (e0 - 3) / 24;
  if (jv < 0) {
    jv = 0;
  }
  q0 = e0 - 24 * (jv + 1);
  j = jv - jx;
  m = jx + jk;
  for (i = 0; i <= m; i++, j++) {
    f[i] = j < 0 ? 0.0 : static_cast<double>(kIpio2[j]);
  }
  for (i = 0; i <= jk; i++) {
    for (j = 0, fw = 0.0; j <= jx; j++) {
      fw += x[j] * f[jx + i - j];
    }
    q[i] = fw;
  }
  jz = jk;
  for (;;) {  // recompute
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
      fw = static_cast<double>(static_cast<std::int32_t>(0x1p-24 * z));
      iq[i] = static_cast<std::int32_t>(z - 0x1p24 * fw);
      z = q[j - 1] + fw;
    }
    z = scale_by_pow2(z, q0);
    z -= 8.0 * std::floor(z * 0.125);
    n = static_cast<std::int32_t>(z);
    z -= static_cast<double>(n);
    ih = 0;
    if (q0 > 0) {
      i = iq[jz - 1] >> (24 - q0);
      n += i;
      iq[jz - 1] -= i << (24 - q0);
      ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 == 0) {
      ih = iq[jz - 1] >> 23;
    } else if (z >= 0.5) {
      ih = 2;
    }
    if (ih > 0) {
      n += 1;
      carry = 0;
      for (i = 0; i < jz; i++) {
        j = iq[i];
        if (carry == 0) {
          if (j != 0) {
            carry = 1;
            iq[i] = 0x1000000 - j;
          }
        } else {
          iq[i] = 0xffffff - j;
        }
      }
      if (q0 > 0) {
        switch (q0) {
          case 1:
            iq[jz - 1] &= 0x7fffff;
            break;
          case 2:
            iq[jz - 1] &= 0x3fffff;
            break;
          default:
            break;
        }
      }
      if (ih == 2) {
        z = 1.0 - z;
        if (carry != 0) {
          z -= scale_by_pow2(1.0, q0);
        }
      }
    }
    if (z == 0.0) {
      j = 0;
      for (i = jz - 1; i >= jk; i--) {
        j |= iq[i];
      }
      if (j == 0) {
        for (k = 1; iq[jk - k] == 0; k++) {
        }
        for (i = jz + 1; i <= jz + k; i++) {
          f[jx + i] = static_cast<double>(kIpio2[jv + i]);
          for (j = 0, fw = 0.0; j <= jx; j++) {
            fw += x[j] * f[jx + i - j];
          }
          q[i] = fw;
        }
        jz += k;
        continue;
      }
    }
    break;
  }
  if (z == 0.0) {
    jz -= 1;
    q0 -= 24;
    while (iq[jz] == 0) {
      jz--;
      q0 -= 24;
    }
  } else {
    z = scale_by_pow2(z, -q0);
    if (z >= 0x1p24) {
      fw = static_cast<double>(static_cast<std::int32_t>(0x1p-24 * z));
      iq[jz] = static_cast<std::int32_t>(z - 0x1p24 * fw);
      jz += 1;
      q0 += 24;
      iq[jz] = static_cast<std::int32_t>(fw);
    } else {
      iq[jz] = static_cast<std::int32_t>(z);
    }
  }
  fw = scale_by_pow2(1.0, q0);
  for (i = jz; i >= 0; i--) {
    q[i] = fw * static_cast<double>(iq[i]);
    fw *= 0x1p-24;
  }
  for (i = jz; i >= 0; i--) {
    for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) {
      fw += kPIo2[k] * q[i + k];
    }
    fq[jz - i] = fw;
  }
  // prec == 1 (musl's case 1 and 2)
  fw = 0.0;
  for (i = jz; i >= 0; i--) {
    fw += fq[i];
  }
  y[0] = ih == 0 ? fw : -fw;
  fw = fq[0] - fw;
  for (i = 1; i <= jz; i++) {
    fw += fq[i];
  }
  y[1] = ih == 0 ? fw : -fw;
  return n & 7;
}

// musl __rem_pio2: x = n * pi/2 + y[0] + y[1]; returns n.
int rem_pio2(double x, double* y) noexcept {
  const std::uint64_t u = bits_of(x);
  const bool sign = (u >> 63U) != 0;
  const std::uint32_t ix = static_cast<std::uint32_t>(u >> 32U) & 0x7fffffffU;
  double z, w, t, r, fn;
  int n;
  bool medium = false;

  if (ix <= 0x400f6a7aU) {
    if ((ix & 0xfffffU) == 0x921fbU) {
      medium = true;
    } else if (ix <= 0x4002d97cU) {
      if (!sign) {
        z = x - kPio2_1;
        y[0] = z - kPio2_1t;
        y[1] = (z - y[0]) - kPio2_1t;
        return 1;
      }
      z = x + kPio2_1;
      y[0] = z + kPio2_1t;
      y[1] = (z - y[0]) + kPio2_1t;
      return -1;
    } else {
      if (!sign) {
        z = x - 2 * kPio2_1;
        y[0] = z - 2 * kPio2_1t;
        y[1] = (z - y[0]) - 2 * kPio2_1t;
        return 2;
      }
      z = x + 2 * kPio2_1;
      y[0] = z + 2 * kPio2_1t;
      y[1] = (z - y[0]) + 2 * kPio2_1t;
      return -2;
    }
  } else if (ix <= 0x401c463bU) {
    if (ix <= 0x4015fdbcU) {
      if (ix == 0x4012d97cU) {
        medium = true;
      } else if (!sign) {
        z = x - 3 * kPio2_1;
        y[0] = z - 3 * kPio2_1t;
        y[1] = (z - y[0]) - 3 * kPio2_1t;
        return 3;
      } else {
        z = x + 3 * kPio2_1;
        y[0] = z + 3 * kPio2_1t;
        y[1] = (z - y[0]) + 3 * kPio2_1t;
        return -3;
      }
    } else {
      if (ix == 0x401921fbU) {
        medium = true;
      } else if (!sign) {
        z = x - 4 * kPio2_1;
        y[0] = z - 4 * kPio2_1t;
        y[1] = (z - y[0]) - 4 * kPio2_1t;
        return 4;
      } else {
        z = x + 4 * kPio2_1;
        y[0] = z + 4 * kPio2_1t;
        y[1] = (z - y[0]) + 4 * kPio2_1t;
        return -4;
      }
    }
  }

  if (medium || ix < 0x413921fbU) {
    fn = x * kInvpio2 + kToint - kToint;
    n = static_cast<int>(fn);
    r = x - fn * kPio2_1;
    w = fn * kPio2_1t;
    if (r - w < -kPio4) {
      n--;
      fn--;
      r = x - fn * kPio2_1;
      w = fn * kPio2_1t;
    } else if (r - w > kPio4) {
      n++;
      fn++;
      r = x - fn * kPio2_1;
      w = fn * kPio2_1t;
    }
    y[0] = r - w;
    std::uint32_t ey = static_cast<std::uint32_t>(bits_of(y[0]) >> 52U) & 0x7ffU;
    const std::uint32_t ex = ix >> 20U;
    if (static_cast<std::int32_t>(ex) - static_cast<std::int32_t>(ey) > 16) {
      t = r;
      w = fn * kPio2_2;
      r = t - w;
      w = fn * kPio2_2t - ((t - r) - w);
      y[0] = r - w;
      ey = static_cast<std::uint32_t>(bits_of(y[0]) >> 52U) & 0x7ffU;
      if (static_cast<std::int32_t>(ex) - static_cast<std::int32_t>(ey) > 49) {
        t = r;
        w = fn * kPio2_3;
        r = t - w;
        w = fn * kPio2_3t - ((t - r) - w);
        y[0] = r - w;
      }
    }
    y[1] = (r - y[0]) - w;
    return n;
  }
  if (ix >= 0x7ff00000U) {
    y[0] = x - x;
    y[1] = x - x;
    return 0;
  }

  // Huge arguments: split x into three 24-bit pieces and reduce with the table.
  std::uint64_t ub = bits_of(x);
  ub &= ~std::uint64_t{0} >> 12U;
  ub |= static_cast<std::uint64_t>(0x3ff + 23) << 52U;
  z = double_of(ub);
  double tx[3], ty[2];
  int i;
  for (i = 0; i < 2; i++) {
    tx[i] = static_cast<double>(static_cast<std::int32_t>(z));
    z = (z - tx[i]) * 0x1p24;
  }
  tx[i] = z;
  while (tx[i] == 0.0) {
    i--;
  }
  n = rem_pio2_large(tx, ty, static_cast<int>(ix >> 20U) - (0x3ff + 23), i + 1);
  if (sign) {
    y[0] = -ty[0];
    y[1] = -ty[1];
    return -n;
  }
  y[0] = ty[0];
  y[1] = ty[1];
  return n;
}

// musl __sin: sin on [-pi/4, pi/4]; tail is y, present when has_tail is true.
double kernel_sin(double x, double y, bool has_tail) noexcept {
  const double z = x * x;
  const double w = z * z;
  const double r = kS2 + z * (kS3 + z * kS4) + z * w * (kS5 + z * kS6);
  const double v = z * x;
  if (!has_tail) {
    return x + v * (kS1 + z * r);
  }
  return x - ((z * (0.5 * y - v * r) - y) - v * kS1);
}

// musl __cos: cos on [-pi/4, pi/4] with tail y.
double kernel_cos(double x, double y) noexcept {
  const double z = x * x;
  double w = z * z;
  const double r = z * (kC1 + z * (kC2 + z * kC3)) + w * w * (kC4 + z * (kC5 + z * kC6));
  const double hz = 0.5 * z;
  w = 1.0 - hz;
  return w + (((1.0 - w) - hz) + (z * r - x * y));
}

constexpr double kT[13] = {
    3.33333333333334091986e-01, 1.33333333333201242699e-01, 5.39682539762260521377e-02,
    2.18694882948595424599e-02, 8.86323982359930005737e-03, 3.59207910759131235356e-03,
    1.45620945432529025516e-03, 5.88041240820264096874e-04, 2.46463134818469906812e-04,
    7.81794442939557092300e-05, 7.14072491382608190305e-05, -1.85586374855275456654e-05,
    2.59073051863633712884e-05};
constexpr double kTanPio4 = 7.85398163397448278999e-01;
constexpr double kTanPio4lo = 3.06161699786838301793e-17;

// musl __tan: tan (odd false) or -1/tan (odd true) on [-pi/4, pi/4] with tail y.
double kernel_tan(double x, double y, bool odd) noexcept {
  const std::uint32_t hx = high_word(x);
  const bool big = (hx & 0x7fffffffU) >= 0x3FE59428U;
  bool sign = false;
  if (big) {
    sign = (hx >> 31U) != 0;
    if (sign) {
      x = -x;
      y = -y;
    }
    x = (kTanPio4 - x) + (kTanPio4lo - y);
    y = 0.0;
  }
  const double z = x * x;
  double w = z * z;
  double r = kT[1] + w * (kT[3] + w * (kT[5] + w * (kT[7] + w * (kT[9] + w * kT[11]))));
  double v = z * (kT[2] + w * (kT[4] + w * (kT[6] + w * (kT[8] + w * (kT[10] + w * kT[12])))));
  double s = z * x;
  r = y + z * (s * (r + v) + y) + s * kT[0];
  w = x + r;
  if (big) {
    s = odd ? -1.0 : 1.0;
    v = s - 2.0 * (x + (r - w * w / (w + s)));
    return sign ? -v : v;
  }
  if (!odd) {
    return w;
  }
  // -1/(x+r) with extra precision (musl __tan).
  double w0 = clear_low_word(w);
  v = r - (w0 - x);
  const double a = -1.0 / w;
  const double a0 = clear_low_word(a);
  return a0 + a * (1.0 + a0 * w0 + a0 * v);
}

}  // namespace

double sin(double x) noexcept {
  double y[2];
  const std::uint32_t ix = high_word(x) & 0x7fffffffU;
  if (ix <= 0x3fe921fbU) {
    if (ix < 0x3e500000U) {
      return x;
    }
    return kernel_sin(x, 0.0, false);
  }
  if (ix >= 0x7ff00000U) {
    return x - x;
  }
  const int n = rem_pio2(x, y);
  switch (n & 3) {
    case 0:
      return kernel_sin(y[0], y[1], true);
    case 1:
      return kernel_cos(y[0], y[1]);
    case 2:
      return -kernel_sin(y[0], y[1], true);
    default:
      return -kernel_cos(y[0], y[1]);
  }
}

double cos(double x) noexcept {
  double y[2];
  const std::uint32_t ix = high_word(x) & 0x7fffffffU;
  if (ix <= 0x3fe921fbU) {
    if (ix < 0x3e46a09eU) {
      return 1.0;
    }
    return kernel_cos(x, 0.0);
  }
  if (ix >= 0x7ff00000U) {
    return x - x;
  }
  const int n = rem_pio2(x, y);
  switch (n & 3) {
    case 0:
      return kernel_cos(y[0], y[1]);
    case 1:
      return -kernel_sin(y[0], y[1], true);
    case 2:
      return -kernel_cos(y[0], y[1]);
    default:
      return kernel_sin(y[0], y[1], true);
  }
}

// Each result equals sin(x) or cos(x) bit for bit: the branches below are the ones sin and cos
// take.
SinCos sincos(double x) noexcept {
  double y[2];
  const std::uint32_t ix = high_word(x) & 0x7fffffffU;
  if (ix <= 0x3fe921fbU) {
    const double s = ix < 0x3e500000U ? x : kernel_sin(x, 0.0, false);
    const double c = ix < 0x3e46a09eU ? 1.0 : kernel_cos(x, 0.0);
    return {s, c};
  }
  if (ix >= 0x7ff00000U) {
    return {x - x, x - x};
  }
  const int n = rem_pio2(x, y);
  const double s = kernel_sin(y[0], y[1], true);
  const double c = kernel_cos(y[0], y[1]);
  switch (n & 3) {
    case 0:
      return {s, c};
    case 1:
      return {c, -s};
    case 2:
      return {-s, -c};
    default:
      return {-c, s};
  }
}

double tan(double x) noexcept {
  double y[2];
  const std::uint32_t ix = high_word(x) & 0x7fffffffU;
  if (ix <= 0x3fe921fbU) {
    if (ix < 0x3e400000U) {
      return x;
    }
    return kernel_tan(x, 0.0, false);
  }
  if (ix >= 0x7ff00000U) {
    return x - x;
  }
  const int n = rem_pio2(x, y);
  return kernel_tan(y[0], y[1], (n & 1) != 0);
}

namespace {

// ---------------------------------------------------------------------------------------------
// atan and atan2 (musl atan.c, atan2.c).
// ---------------------------------------------------------------------------------------------

constexpr double kAtanhi[4] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                               9.82793723247329054082e-01, 1.57079632679489655800e+00};
constexpr double kAtanlo[4] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                               1.39033110312309984516e-17, 6.12323399573676603587e-17};
constexpr double kAT[11] = {
    3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
    -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
    6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
    -3.65315727442169155270e-02, 1.62858201153657823623e-02};

constexpr double kPi = 3.1415926535897931160E+00;
constexpr double kPiLo = 1.2246467991473531772E-16;

}  // namespace

double atan(double x) noexcept {
  const std::uint32_t full = high_word(x);
  const std::uint32_t sign = full >> 31U;
  std::uint32_t ix = full & 0x7fffffffU;
  int id;
  if (ix >= 0x44100000U) {
    if (std::isnan(x)) {
      return x;
    }
    const double z = kAtanhi[3] + 0x1p-120;
    return sign != 0 ? -z : z;
  }
  if (ix < 0x3fdc0000U) {
    if (ix < 0x3e400000U) {
      return x;
    }
    id = -1;
  } else {
    x = std::fabs(x);
    if (ix < 0x3ff30000U) {
      if (ix < 0x3fe60000U) {
        id = 0;
        x = (2.0 * x - 1.0) / (2.0 + x);
      } else {
        id = 1;
        x = (x - 1.0) / (x + 1.0);
      }
    } else {
      if (ix < 0x40038000U) {
        id = 2;
        x = (x - 1.5) / (1.0 + 1.5 * x);
      } else {
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  const double z = x * x;
  const double w = z * z;
  const double s1 =
      z * (kAT[0] + w * (kAT[2] + w * (kAT[4] + w * (kAT[6] + w * (kAT[8] + w * kAT[10])))));
  const double s2 = w * (kAT[1] + w * (kAT[3] + w * (kAT[5] + w * (kAT[7] + w * kAT[9]))));
  if (id < 0) {
    return x - x * (s1 + s2);
  }
  const double zr = kAtanhi[id] - (x * (s1 + s2) - kAtanlo[id] - x);
  return sign != 0 ? -zr : zr;
}

double atan2(double y, double x) noexcept {
  if (std::isnan(x) || std::isnan(y)) {
    return x + y;
  }
  std::uint32_t ix = high_word(x);
  const std::uint32_t lx = low_word(x);
  std::uint32_t iy = high_word(y);
  const std::uint32_t ly = low_word(y);

  if (((ix - 0x3ff00000U) | lx) == 0) {  // x is 1.0
    return atan(y);
  }
  // m: bit 0 = sign of y, bit 1 = sign of x.
  const std::uint32_t m = ((iy >> 31U) & 1U) | ((ix >> 30U) & 2U);
  ix = ix & 0x7fffffffU;
  iy = iy & 0x7fffffffU;

  if ((iy | ly) == 0) {  // y is +-0
    if (m == 0 || m == 1) {
      return y;
    }
    return m == 2 ? kPi : -kPi;
  }
  if ((ix | lx) == 0) {  // x is +-0
    return (m & 1U) != 0 ? -kPi / 2 : kPi / 2;
  }
  if (ix == 0x7ff00000U) {
    if (iy == 0x7ff00000U) {  // both infinite
      switch (m) {
        case 0:
          return kPi / 4;
        case 1:
          return -kPi / 4;
        case 2:
          return 3 * kPi / 4;
        default:
          return -3 * kPi / 4;
      }
    }
    switch (m) {
      case 0:
        return 0.0;
      case 1:
        return -0.0;
      case 2:
        return kPi;
      default:
        return -kPi;
    }
  }
  if (ix + (64U << 20U) < iy || iy == 0x7ff00000U) {  // |y/x| > 2^26
    return (m & 1U) != 0 ? -kPi / 2 : kPi / 2;
  }
  double z;
  if ((m & 2U) != 0 && iy + (64U << 20U) < ix) {  // |y/x| < 2^-26 with x < 0
    z = 0;
  } else {
    z = atan(std::fabs(y / x));
  }
  switch (m) {
    case 0:
      return z;
    case 1:
      return -z;
    case 2:
      return kPi - (z - kPiLo);
    default:
      return (z - kPiLo) - kPi;
  }
}

// ---------------------------------------------------------------------------------------------
// hypot (musl hypot.c: exact squares by Dekker's split, then one square root).
// ---------------------------------------------------------------------------------------------

namespace {

constexpr double kSplit = 0x1p27 + 1;

void square_split(double* hi, double* lo, double x) noexcept {
  const double xc = x * kSplit;
  const double xh = x - xc + xc;
  const double xl = x - xh;
  *hi = x * x;
  *lo = xh * xh - *hi + 2 * xh * xl + xl * xl;
}

// ---------------------------------------------------------------------------------------------
// exp, log, log2 (Arm optimized-routines, as in musl exp.c, log.c, log2.c). Table layout: exp tab
// holds pairs (tail, scale - 1), log tables hold {1/c, log(c)} and {chi, clo}.
// ---------------------------------------------------------------------------------------------

constexpr std::uint32_t kExpTableBits = 7;
constexpr std::uint32_t kExpN = 1U << kExpTableBits;
constexpr double kExpInvLn2N = 0x1.71547652b82fep0 * 128;
constexpr double kExpShift = 0x1.8p52;
constexpr double kExpNegLn2hiN = -0x1.62e42fefa0000p-8;
constexpr double kExpNegLn2loN = -0x1.cf79abc9e3b3ap-47;
constexpr double kExpC[4] = {0x1.ffffffffffdbdp-2, 0x1.555555555543cp-3, 0x1.55555cf172b91p-5,
                             0x1.1111167a4d017p-7};
constexpr std::uint64_t kExpTab[2 * kExpN] = {0x0,
                                              0x3ff0000000000000,
                                              0x3c9b3b4f1a88bf6e,
                                              0x3feff63da9fb3335,
                                              0xbc7160139cd8dc5d,
                                              0x3fefec9a3e778061,
                                              0xbc905e7a108766d1,
                                              0x3fefe315e86e7f85,
                                              0x3c8cd2523567f613,
                                              0x3fefd9b0d3158574,
                                              0xbc8bce8023f98efa,
                                              0x3fefd06b29ddf6de,
                                              0x3c60f74e61e6c861,
                                              0x3fefc74518759bc8,
                                              0x3c90a3e45b33d399,
                                              0x3fefbe3ecac6f383,
                                              0x3c979aa65d837b6d,
                                              0x3fefb5586cf9890f,
                                              0x3c8eb51a92fdeffc,
                                              0x3fefac922b7247f7,
                                              0x3c3ebe3d702f9cd1,
                                              0x3fefa3ec32d3d1a2,
                                              0xbc6a033489906e0b,
                                              0x3fef9b66affed31b,
                                              0xbc9556522a2fbd0e,
                                              0x3fef9301d0125b51,
                                              0xbc5080ef8c4eea55,
                                              0x3fef8abdc06c31cc,
                                              0xbc91c923b9d5f416,
                                              0x3fef829aaea92de0,
                                              0x3c80d3e3e95c55af,
                                              0x3fef7a98c8a58e51,
                                              0xbc801b15eaa59348,
                                              0x3fef72b83c7d517b,
                                              0xbc8f1ff055de323d,
                                              0x3fef6af9388c8dea,
                                              0x3c8b898c3f1353bf,
                                              0x3fef635beb6fcb75,
                                              0xbc96d99c7611eb26,
                                              0x3fef5be084045cd4,
                                              0x3c9aecf73e3a2f60,
                                              0x3fef54873168b9aa,
                                              0xbc8fe782cb86389d,
                                              0x3fef4d5022fcd91d,
                                              0x3c8a6f4144a6c38d,
                                              0x3fef463b88628cd6,
                                              0x3c807a05b0e4047d,
                                              0x3fef3f49917ddc96,
                                              0x3c968efde3a8a894,
                                              0x3fef387a6e756238,
                                              0x3c875e18f274487d,
                                              0x3fef31ce4fb2a63f,
                                              0x3c80472b981fe7f2,
                                              0x3fef2b4565e27cdd,
                                              0xbc96b87b3f71085e,
                                              0x3fef24dfe1f56381,
                                              0x3c82f7e16d09ab31,
                                              0x3fef1e9df51fdee1,
                                              0xbc3d219b1a6fbffa,
                                              0x3fef187fd0dad990,
                                              0x3c8b3782720c0ab4,
                                              0x3fef1285a6e4030b,
                                              0x3c6e149289cecb8f,
                                              0x3fef0cafa93e2f56,
                                              0x3c834d754db0abb6,
                                              0x3fef06fe0a31b715,
                                              0x3c864201e2ac744c,
                                              0x3fef0170fc4cd831,
                                              0x3c8fdd395dd3f84a,
                                              0x3feefc08b26416ff,
                                              0xbc86a3803b8e5b04,
                                              0x3feef6c55f929ff1,
                                              0xbc924aedcc4b5068,
                                              0x3feef1a7373aa9cb,
                                              0xbc9907f81b512d8e,
                                              0x3feeecae6d05d866,
                                              0xbc71d1e83e9436d2,
                                              0x3feee7db34e59ff7,
                                              0xbc991919b3ce1b15,
                                              0x3feee32dc313a8e5,
                                              0x3c859f48a72a4c6d,
                                              0x3feedea64c123422,
                                              0xbc9312607a28698a,
                                              0x3feeda4504ac801c,
                                              0xbc58a78f4817895b,
                                              0x3feed60a21f72e2a,
                                              0xbc7c2c9b67499a1b,
                                              0x3feed1f5d950a897,
                                              0x3c4363ed60c2ac11,
                                              0x3feece086061892d,
                                              0x3c9666093b0664ef,
                                              0x3feeca41ed1d0057,
                                              0x3c6ecce1daa10379,
                                              0x3feec6a2b5c13cd0,
                                              0x3c93ff8e3f0f1230,
                                              0x3feec32af0d7d3de,
                                              0x3c7690cebb7aafb0,
                                              0x3feebfdad5362a27,
                                              0x3c931dbdeb54e077,
                                              0x3feebcb299fddd0d,
                                              0xbc8f94340071a38e,
                                              0x3feeb9b2769d2ca7,
                                              0xbc87deccdc93a349,
                                              0x3feeb6daa2cf6642,
                                              0xbc78dec6bd0f385f,
                                              0x3feeb42b569d4f82,
                                              0xbc861246ec7b5cf6,
                                              0x3feeb1a4ca5d920f,
                                              0x3c93350518fdd78e,
                                              0x3feeaf4736b527da,
                                              0x3c7b98b72f8a9b05,
                                              0x3feead12d497c7fd,
                                              0x3c9063e1e21c5409,
                                              0x3feeab07dd485429,
                                              0x3c34c7855019c6ea,
                                              0x3feea9268a5946b7,
                                              0x3c9432e62b64c035,
                                              0x3feea76f15ad2148,
                                              0xbc8ce44a6199769f,
                                              0x3feea5e1b976dc09,
                                              0xbc8c33c53bef4da8,
                                              0x3feea47eb03a5585,
                                              0xbc845378892be9ae,
                                              0x3feea34634ccc320,
                                              0xbc93cedd78565858,
                                              0x3feea23882552225,
                                              0x3c5710aa807e1964,
                                              0x3feea155d44ca973,
                                              0xbc93b3efbf5e2228,
                                              0x3feea09e667f3bcd,
                                              0xbc6a12ad8734b982,
                                              0x3feea012750bdabf,
                                              0xbc6367efb86da9ee,
                                              0x3fee9fb23c651a2f,
                                              0xbc80dc3d54e08851,
                                              0x3fee9f7df9519484,
                                              0xbc781f647e5a3ecf,
                                              0x3fee9f75e8ec5f74,
                                              0xbc86ee4ac08b7db0,
                                              0x3fee9f9a48a58174,
                                              0xbc8619321e55e68a,
                                              0x3fee9feb564267c9,
                                              0x3c909ccb5e09d4d3,
                                              0x3feea0694fde5d3f,
                                              0xbc7b32dcb94da51d,
                                              0x3feea11473eb0187,
                                              0x3c94ecfd5467c06b,
                                              0x3feea1ed0130c132,
                                              0x3c65ebe1abd66c55,
                                              0x3feea2f336cf4e62,
                                              0xbc88a1c52fb3cf42,
                                              0x3feea427543e1a12,
                                              0xbc9369b6f13b3734,
                                              0x3feea589994cce13,
                                              0xbc805e843a19ff1e,
                                              0x3feea71a4623c7ad,
                                              0xbc94d450d872576e,
                                              0x3feea8d99b4492ed,
                                              0x3c90ad675b0e8a00,
                                              0x3feeaac7d98a6699,
                                              0x3c8db72fc1f0eab4,
                                              0x3feeace5422aa0db,
                                              0xbc65b6609cc5e7ff,
                                              0x3feeaf3216b5448c,
                                              0x3c7bf68359f35f44,
                                              0x3feeb1ae99157736,
                                              0xbc93091fa71e3d83,
                                              0x3feeb45b0b91ffc6,
                                              0xbc5da9b88b6c1e29,
                                              0x3feeb737b0cdc5e5,
                                              0xbc6c23f97c90b959,
                                              0x3feeba44cbc8520f,
                                              0xbc92434322f4f9aa,
                                              0x3feebd829fde4e50,
                                              0xbc85ca6cd7668e4b,
                                              0x3feec0f170ca07ba,
                                              0x3c71affc2b91ce27,
                                              0x3feec49182a3f090,
                                              0x3c6dd235e10a73bb,
                                              0x3feec86319e32323,
                                              0xbc87c50422622263,
                                              0x3feecc667b5de565,
                                              0x3c8b1c86e3e231d5,
                                              0x3feed09bec4a2d33,
                                              0xbc91bbd1d3bcbb15,
                                              0x3feed503b23e255d,
                                              0x3c90cc319cee31d2,
                                              0x3feed99e1330b358,
                                              0x3c8469846e735ab3,
                                              0x3feede6b5579fdbf,
                                              0xbc82dfcd978e9db4,
                                              0x3feee36bbfd3f37a,
                                              0x3c8c1a7792cb3387,
                                              0x3feee89f995ad3ad,
                                              0xbc907b8f4ad1d9fa,
                                              0x3feeee07298db666,
                                              0xbc55c3d956dcaeba,
                                              0x3feef3a2b84f15fb,
                                              0xbc90a40e3da6f640,
                                              0x3feef9728de5593a,
                                              0xbc68d6f438ad9334,
                                              0x3feeff76f2fb5e47,
                                              0xbc91eee26b588a35,
                                              0x3fef05b030a1064a,
                                              0x3c74ffd70a5fddcd,
                                              0x3fef0c1e904bc1d2,
                                              0xbc91bdfbfa9298ac,
                                              0x3fef12c25bd71e09,
                                              0x3c736eae30af0cb3,
                                              0x3fef199bdd85529c,
                                              0x3c8ee3325c9ffd94,
                                              0x3fef20ab5fffd07a,
                                              0x3c84e08fd10959ac,
                                              0x3fef27f12e57d14b,
                                              0x3c63cdaf384e1a67,
                                              0x3fef2f6d9406e7b5,
                                              0x3c676b2c6c921968,
                                              0x3fef3720dcef9069,
                                              0xbc808a1883ccb5d2,
                                              0x3fef3f0b555dc3fa,
                                              0xbc8fad5d3ffffa6f,
                                              0x3fef472d4a07897c,
                                              0xbc900dae3875a949,
                                              0x3fef4f87080d89f2,
                                              0x3c74a385a63d07a7,
                                              0x3fef5818dcfba487,
                                              0xbc82919e2040220f,
                                              0x3fef60e316c98398,
                                              0x3c8e5a50d5c192ac,
                                              0x3fef69e603db3285,
                                              0x3c843a59ac016b4b,
                                              0x3fef7321f301b460,
                                              0xbc82d52107b43e1f,
                                              0x3fef7c97337b9b5f,
                                              0xbc892ab93b470dc9,
                                              0x3fef864614f5a129,
                                              0x3c74b604603a88d3,
                                              0x3fef902ee78b3ff6,
                                              0x3c83c5ec519d7271,
                                              0x3fef9a51fbc74c83,
                                              0xbc8ff7128fd391f0,
                                              0x3fefa4afa2a490da,
                                              0xbc8dae98e223747d,
                                              0x3fefaf482d8e67f1,
                                              0x3c8ec3bc41aa2008,
                                              0x3fefba1bee615a27,
                                              0x3c842b94c3a9eb32,
                                              0x3fefc52b376bba97,
                                              0x3c8a64a931d185ee,
                                              0x3fefd0765b6e4540,
                                              0xbc8e37bae43be3ed,
                                              0x3fefdbfdad9cbe14,
                                              0x3c77893b4d91cd9d,
                                              0x3fefe7c1819e90d8,
                                              0x3c5305c14160cc89,
                                              0x3feff3c22b8f71f1};

constexpr double kLogLn2hi = 0x1.62e42fefa3800p-1;
constexpr double kLogLn2lo = 0x1.ef35793c76730p-45;
constexpr double kLogPoly1[11] = {-0x1p-1,
                                  0x1.5555555555577p-2,
                                  -0x1.ffffffffffdcbp-3,
                                  0x1.999999995dd0cp-3,
                                  -0x1.55555556745a7p-3,
                                  0x1.24924a344de3p-3,
                                  -0x1.fffffa4423d65p-4,
                                  0x1.c7184282ad6cap-4,
                                  -0x1.999eb43b068ffp-4,
                                  0x1.78182f7afd085p-4,
                                  -0x1.5521375d145cdp-4};
constexpr double kLogPoly[5] = {-0x1.0000000000001p-1, 0x1.555555551305bp-2, -0x1.fffffffeb459p-3,
                                0x1.999b324f10111p-3, -0x1.55575e506c89fp-3};
constexpr double kLogTab[128][2] = {
    {0x1.734f0c3e0de9fp+0, -0x1.7cc7f79e69000p-2}, {0x1.713786a2ce91fp+0, -0x1.76feec20d0000p-2},
    {0x1.6f26008fab5a0p+0, -0x1.713e31351e000p-2}, {0x1.6d1a61f138c7dp+0, -0x1.6b85b38287800p-2},
    {0x1.6b1490bc5b4d1p+0, -0x1.65d5590807800p-2}, {0x1.69147332f0cbap+0, -0x1.602d076180000p-2},
    {0x1.6719f18224223p+0, -0x1.5a8ca86909000p-2}, {0x1.6524f99a51ed9p+0, -0x1.54f4356035000p-2},
    {0x1.63356aa8f24c4p+0, -0x1.4f637c36b4000p-2}, {0x1.614b36b9ddc14p+0, -0x1.49da7fda85000p-2},
    {0x1.5f66452c65c4cp+0, -0x1.445923989a800p-2}, {0x1.5d867b5912c4fp+0, -0x1.3edf439b0b800p-2},
    {0x1.5babccb5b90dep+0, -0x1.396ce448f7000p-2}, {0x1.59d61f2d91a78p+0, -0x1.3401e17bda000p-2},
    {0x1.5805612465687p+0, -0x1.2e9e2ef468000p-2}, {0x1.56397cee76bd3p+0, -0x1.2941b3830e000p-2},
    {0x1.54725e2a77f93p+0, -0x1.23ec58cda8800p-2}, {0x1.52aff42064583p+0, -0x1.1e9e129279000p-2},
    {0x1.50f22dbb2bddfp+0, -0x1.1956d2b48f800p-2}, {0x1.4f38f4734ded7p+0, -0x1.141679ab9f800p-2},
    {0x1.4d843cfde2840p+0, -0x1.0edd094ef9800p-2}, {0x1.4bd3ec078a3c8p+0, -0x1.09aa518db1000p-2},
    {0x1.4a27fc3e0258ap+0, -0x1.047e65263b800p-2}, {0x1.4880524d48434p+0, -0x1.feb224586f000p-3},
    {0x1.46dce1b192d0bp+0, -0x1.f474a7517b000p-3}, {0x1.453d9d3391854p+0, -0x1.ea4443d103000p-3},
    {0x1.43a2744b4845ap+0, -0x1.e020d44e9b000p-3}, {0x1.420b54115f8fbp+0, -0x1.d60a22977f000p-3},
    {0x1.40782da3ef4b1p+0, -0x1.cc00104959000p-3}, {0x1.3ee8f5d57fe8fp+0, -0x1.c202956891000p-3},
    {0x1.3d5d9a00b4ce9p+0, -0x1.b81178d811000p-3}, {0x1.3bd60c010c12bp+0, -0x1.ae2c9ccd3d000p-3},
    {0x1.3a5242b75dab8p+0, -0x1.a45402e129000p-3}, {0x1.38d22cd9fd002p+0, -0x1.9a877681df000p-3},
    {0x1.3755bc5847a1cp+0, -0x1.90c6d69483000p-3}, {0x1.35dce49ad36e2p+0, -0x1.87120a645c000p-3},
    {0x1.34679984dd440p+0, -0x1.7d68fb4143000p-3}, {0x1.32f5cceffcb24p+0, -0x1.73cb83c627000p-3},
    {0x1.3187775a10d49p+0, -0x1.6a39a9b376000p-3}, {0x1.301c8373e3990p+0, -0x1.60b3154b7a000p-3},
    {0x1.2eb4ebb95f841p+0, -0x1.5737d76243000p-3}, {0x1.2d50a0219a9d1p+0, -0x1.4dc7b8fc23000p-3},
    {0x1.2bef9a8b7fd2ap+0, -0x1.4462c51d20000p-3}, {0x1.2a91c7a0c1babp+0, -0x1.3b08abc830000p-3},
    {0x1.293726014b530p+0, -0x1.31b996b490000p-3}, {0x1.27dfa5757a1f5p+0, -0x1.2875490a44000p-3},
    {0x1.268b39b1d3bbfp+0, -0x1.1f3b9f879a000p-3}, {0x1.2539d838ff5bdp+0, -0x1.160c8252ca000p-3},
    {0x1.23eb7aac9083bp+0, -0x1.0ce7f57f72000p-3}, {0x1.22a012ba940b6p+0, -0x1.03cdc49fea000p-3},
    {0x1.2157996cc4132p+0, -0x1.f57bdbc4b8000p-4}, {0x1.201201dd2fc9bp+0, -0x1.e370896404000p-4},
    {0x1.1ecf4494d480bp+0, -0x1.d17983ef94000p-4}, {0x1.1d8f5528f6569p+0, -0x1.bf9674ed8a000p-4},
    {0x1.1c52311577e7cp+0, -0x1.adc79202f6000p-4}, {0x1.1b17c74cb26e9p+0, -0x1.9c0c3e7288000p-4},
    {0x1.19e010c2c1ab6p+0, -0x1.8a646b372c000p-4}, {0x1.18ab07bb670bdp+0, -0x1.78d01b3ac0000p-4},
    {0x1.1778a25efbcb6p+0, -0x1.674f145380000p-4}, {0x1.1648d354c31dap+0, -0x1.55e0e6d878000p-4},
    {0x1.151b990275fddp+0, -0x1.4485cdea1e000p-4}, {0x1.13f0ea432d24cp+0, -0x1.333d94d6aa000p-4},
    {0x1.12c8b7210f9dap+0, -0x1.22079f8c56000p-4}, {0x1.11a3028ecb531p+0, -0x1.10e4698622000p-4},
    {0x1.107fbda8434afp+0, -0x1.ffa6c6ad20000p-5}, {0x1.0f5ee0f4e6bb3p+0, -0x1.dda8d4a774000p-5},
    {0x1.0e4065d2a9fcep+0, -0x1.bbcece4850000p-5}, {0x1.0d244632ca521p+0, -0x1.9a1894012c000p-5},
    {0x1.0c0a77ce2981ap+0, -0x1.788583302c000p-5}, {0x1.0af2f83c636d1p+0, -0x1.5715e67d68000p-5},
    {0x1.09ddb98a01339p+0, -0x1.35c8a49658000p-5}, {0x1.08cabaf52e7dfp+0, -0x1.149e364154000p-5},
    {0x1.07b9f2f4e28fbp+0, -0x1.e72c082eb8000p-6}, {0x1.06ab58c358f19p+0, -0x1.a55f152528000p-6},
    {0x1.059eea5ecf92cp+0, -0x1.63d62cf818000p-6}, {0x1.04949cdd12c90p+0, -0x1.228fb8caa0000p-6},
    {0x1.038c6c6f0ada9p+0, -0x1.c317b20f90000p-7}, {0x1.02865137932a9p+0, -0x1.419355daa0000p-7},
    {0x1.0182427ea7348p+0, -0x1.81203c2ec0000p-8}, {0x1.008040614b195p+0, -0x1.0040979240000p-9},
    {0x1.fe01ff726fa1ap-1, 0x1.feff384900000p-9},  {0x1.fa11cc261ea74p-1, 0x1.7dc41353d0000p-7},
    {0x1.f6310b081992ep-1, 0x1.3cea3c4c28000p-6},  {0x1.f25f63ceeadcdp-1, 0x1.b9fc114890000p-6},
    {0x1.ee9c8039113e7p-1, 0x1.1b0d8ce110000p-5},  {0x1.eae8078cbb1abp-1, 0x1.58a5bd001c000p-5},
    {0x1.e741aa29d0c9bp-1, 0x1.95c8340d88000p-5},  {0x1.e3a91830a99b5p-1, 0x1.d276aef578000p-5},
    {0x1.e01e009609a56p-1, 0x1.07598e598c000p-4},  {0x1.dca01e577bb98p-1, 0x1.253f5e30d2000p-4},
    {0x1.d92f20b7c9103p-1, 0x1.42edd8b380000p-4},  {0x1.d5cac66fb5ccep-1, 0x1.606598757c000p-4},
    {0x1.d272caa5ede9dp-1, 0x1.7da76356a0000p-4},  {0x1.cf26e3e6b2ccdp-1, 0x1.9ab434e1c6000p-4},
    {0x1.cbe6da2a77902p-1, 0x1.b78c7bb0d6000p-4},  {0x1.c8b266d37086dp-1, 0x1.d431332e72000p-4},
    {0x1.c5894bd5d5804p-1, 0x1.f0a3171de6000p-4},  {0x1.c26b533bb9f8cp-1, 0x1.067152b914000p-3},
    {0x1.bf583eeece73fp-1, 0x1.147858292b000p-3},  {0x1.bc4fd75db96c1p-1, 0x1.2266ecdca3000p-3},
    {0x1.b951e0c864a28p-1, 0x1.303d7a6c55000p-3},  {0x1.b65e2c5ef3e2cp-1, 0x1.3dfc33c331000p-3},
    {0x1.b374867c9888bp-1, 0x1.4ba366b7a8000p-3},  {0x1.b094b211d304ap-1, 0x1.5933928d1f000p-3},
    {0x1.adbe885f2ef7ep-1, 0x1.66acd2418f000p-3},  {0x1.aaf1d31603da2p-1, 0x1.740f8ec669000p-3},
    {0x1.a82e63fd358a7p-1, 0x1.815c0f51af000p-3},  {0x1.a5740ef09738bp-1, 0x1.8e92954f68000p-3},
    {0x1.a2c2a90ab4b27p-1, 0x1.9bb3602f84000p-3},  {0x1.a01a01393f2d1p-1, 0x1.a8bed1c2c0000p-3},
    {0x1.9d79f24db3c1bp-1, 0x1.b5b515c01d000p-3},  {0x1.9ae2505c7b190p-1, 0x1.c2967ccbcc000p-3},
    {0x1.9852ef297ce2fp-1, 0x1.cf635d5486000p-3},  {0x1.95cbaeea44b75p-1, 0x1.dc1bd3446c000p-3},
    {0x1.934c69de74838p-1, 0x1.e8c01b8cfe000p-3},  {0x1.90d4f2f6752e6p-1, 0x1.f5509c0179000p-3},
    {0x1.8e6528effd79dp-1, 0x1.00e6c121fb800p-2},  {0x1.8bfce9fcc007cp-1, 0x1.071b80e93d000p-2},
    {0x1.899c0dabec30ep-1, 0x1.0d46b9e867000p-2},  {0x1.87427aa2317fbp-1, 0x1.13687334bd000p-2},
    {0x1.84f00acb39a08p-1, 0x1.1980d67234800p-2},  {0x1.82a49e8653e55p-1, 0x1.1f8ffe0cc8000p-2},
    {0x1.8060195f40260p-1, 0x1.2595fd7636800p-2},  {0x1.7e22563e0a329p-1, 0x1.2b9300914a800p-2},
    {0x1.7beb377dcb5adp-1, 0x1.3187210436000p-2},  {0x1.79baa679725c2p-1, 0x1.377266dec1800p-2},
    {0x1.77907f2170657p-1, 0x1.3d54ffbaf3000p-2},  {0x1.756cadbd6130cp-1, 0x1.432eee32fe000p-2}};
constexpr double kLogTab2[128][2] = {
    {0x1.61000014fb66bp-1, 0x1.e026c91425b3cp-56},  {0x1.63000034db495p-1, 0x1.dbfea48005d41p-55},
    {0x1.650000d94d478p-1, 0x1.e7fa786d6a5b7p-55},  {0x1.67000074e6fadp-1, 0x1.1fcea6b54254cp-57},
    {0x1.68ffffedf0faep-1, -0x1.c7e274c590efdp-56}, {0x1.6b0000763c5bcp-1, -0x1.ac16848dcda01p-55},
    {0x1.6d0001e5cc1f6p-1, 0x1.33f1c9d499311p-55},  {0x1.6efffeb05f63ep-1, -0x1.e80041ae22d53p-56},
    {0x1.710000e86978p-1, 0x1.bff6671097952p-56},   {0x1.72ffffc67e912p-1, 0x1.c00e226bd8724p-55},
    {0x1.74fffdf81116ap-1, -0x1.e02916ef101d2p-57}, {0x1.770000f679c9p-1, -0x1.7fc71cd549c74p-57},
    {0x1.78ffffa7ec835p-1, 0x1.1bec19ef50483p-55},  {0x1.7affffe20c2e6p-1, -0x1.07e1729cc6465p-56},
    {0x1.7cfffed3fc9p-1, -0x1.08072087b8b1cp-55},   {0x1.7efffe9261a76p-1, 0x1.dc0286d9df9aep-55},
    {0x1.81000049ca3e8p-1, 0x1.97fd251e54c33p-55},  {0x1.8300017932c8fp-1, -0x1.afee9b630f381p-55},
    {0x1.850000633739cp-1, 0x1.9bfbf6b6535bcp-55},  {0x1.87000204289c6p-1, -0x1.bbf65f3117b75p-55},
    {0x1.88fffebf57904p-1, -0x1.9006ea23dcb57p-55}, {0x1.8b00022bc04dfp-1, -0x1.d00df38e04b0ap-56},
    {0x1.8cfffe50c1b8ap-1, -0x1.8007146ff9f05p-55}, {0x1.8effffc918e43p-1, 0x1.3817bd07a7038p-55},
    {0x1.910001efa5fc7p-1, 0x1.93e9176dfb403p-55},  {0x1.9300013467bb9p-1, 0x1.f804e4b980276p-56},
    {0x1.94fffe6ee076fp-1, -0x1.f7ef0d9ff622ep-55}, {0x1.96fffde3c12d1p-1, -0x1.082aa962638bap-56},
    {0x1.98ffff4458a0dp-1, -0x1.7801b9164a8efp-55}, {0x1.9afffdd982e3ep-1, -0x1.740e08a5a9337p-55},
    {0x1.9cfffed49fb66p-1, 0x1.fce08c19bep-60},     {0x1.9f00020f19c51p-1, -0x1.a3faa27885b0ap-55},
    {0x1.a10001145b006p-1, 0x1.4ff489958da56p-56},  {0x1.a300007bbf6fap-1, 0x1.cbeab8a2b6d18p-55},
    {0x1.a500010971d79p-1, 0x1.8fecadd78793p-55},   {0x1.a70001df52e48p-1, -0x1.f41763dd8abdbp-55},
    {0x1.a90001c593352p-1, -0x1.ebf0284c27612p-55}, {0x1.ab0002a4f3e4bp-1, -0x1.9fd043cff3f5fp-57},
    {0x1.acfffd7ae1ed1p-1, -0x1.23ee7129070b4p-55}, {0x1.aefffee510478p-1, 0x1.a063ee00edea3p-57},
    {0x1.b0fffdb650d5bp-1, 0x1.a06c8381f0ab9p-58},  {0x1.b2ffffeaaca57p-1, -0x1.9011e74233c1dp-56},
    {0x1.b4fffd995badcp-1, -0x1.9ff1068862a9fp-56}, {0x1.b7000249e659cp-1, 0x1.aff45d0864f3ep-55},
    {0x1.b8ffff987164p-1, 0x1.cfe7796c2c3f9p-56},   {0x1.bafffd204cb4fp-1, -0x1.3ff27eef22bc4p-57},
    {0x1.bcfffd2415c45p-1, -0x1.cffb7ee3bea21p-57}, {0x1.beffff86309dfp-1, -0x1.14103972e0b5cp-55},
    {0x1.c0fffe1b57653p-1, 0x1.bc16494b76a19p-55},  {0x1.c2ffff1fa57e3p-1, -0x1.4feef8d30c6edp-57},
    {0x1.c4fffdcbfe424p-1, -0x1.43f68bcec4775p-55}, {0x1.c6fffed54b9f7p-1, 0x1.47ea3f053e0ecp-55},
    {0x1.c8fffeb998fd5p-1, 0x1.383068df992f1p-56},  {0x1.cb0002125219ap-1, -0x1.8fd8e64180e04p-57},
    {0x1.ccfffdd94469cp-1, 0x1.e7ebe1cc7ea72p-55},  {0x1.cefffeafdc476p-1, 0x1.ebe39ad9f88fep-55},
    {0x1.d1000169af82bp-1, 0x1.57d91a8b95a71p-56},  {0x1.d30000d0ff71dp-1, 0x1.9c1906970c7dap-55},
    {0x1.d4fffea790fc4p-1, -0x1.80e37c558fe0cp-58}, {0x1.d70002edc87e5p-1, -0x1.f80d64dc10f44p-56},
    {0x1.d900021dc82aap-1, -0x1.47c8f94fd5c5cp-56}, {0x1.dafffd86b0283p-1, 0x1.c7f1dc521617ep-55},
    {0x1.dd000296c4739p-1, 0x1.8019eb2ffb153p-55},  {0x1.defffe54490f5p-1, 0x1.e00d2c652cc89p-57},
    {0x1.e0fffcdabf694p-1, -0x1.f8340202d69d2p-56}, {0x1.e2fffdb52c8ddp-1, 0x1.b00c1ca1b0864p-56},
    {0x1.e4ffff24216efp-1, 0x1.2ffa8b094ab51p-56},  {0x1.e6fffe88a5e11p-1, -0x1.7f673b1efbe59p-58},
    {0x1.e9000119eff0dp-1, -0x1.4808d5e0bc801p-55}, {0x1.eafffdfa51744p-1, 0x1.80006d54320b5p-56},
    {0x1.ed0001a127fa1p-1, -0x1.002f860565c92p-58}, {0x1.ef00007babcc4p-1, -0x1.540445d35e611p-55},
    {0x1.f0ffff57a8d02p-1, -0x1.ffb3139ef9105p-59}, {0x1.f30001ee58ac7p-1, 0x1.a81acf2731155p-55},
    {0x1.f4ffff5823494p-1, 0x1.a3f41d4d7c743p-55},  {0x1.f6ffffca94c6bp-1, -0x1.202f41c987875p-57},
    {0x1.f8fffe1f9c441p-1, 0x1.77dd1f477e74bp-56},  {0x1.fafffd2e0e37ep-1, -0x1.f01199a7ca331p-57},
    {0x1.fd0001c77e49ep-1, 0x1.181ee4bceacb1p-56},  {0x1.feffff7e0c331p-1, -0x1.e05370170875ap-57},
    {0x1.00ffff465606ep+0, -0x1.a7ead491c0adap-55}, {0x1.02ffff3867a58p+0, -0x1.77f69c3fcb2ep-54},
    {0x1.04ffffdfc0d17p+0, 0x1.7bffe34cb945bp-54},  {0x1.0700003cd4d82p+0, 0x1.20083c0e456cbp-55},
    {0x1.08ffff9f2cbe8p+0, -0x1.dffdfbe37751ap-57}, {0x1.0b000010cda65p+0, -0x1.13f7faee626ebp-54},
    {0x1.0d00001a4d338p+0, 0x1.07dfa79489ff7p-55},  {0x1.0effffadafdfdp+0, -0x1.7040570d66bcp-56},
    {0x1.110000bbafd96p+0, 0x1.e80d4846d0b62p-55},  {0x1.12ffffae5f45dp+0, 0x1.dbffa64fd36efp-54},
    {0x1.150000dd59ad9p+0, 0x1.a0077701250aep-54},  {0x1.170000f21559ap+0, 0x1.dfdf9e2e3deeep-55},
    {0x1.18ffffc275426p+0, 0x1.10030dc3b7273p-54},  {0x1.1b000123d3c59p+0, 0x1.97f7980030188p-54},
    {0x1.1cffff8299eb7p+0, -0x1.5f932ab9f8c67p-57}, {0x1.1effff48ad4p+0, 0x1.37fbf9da75bebp-54},
    {0x1.210000c8b86a4p+0, 0x1.f806b91fd5b22p-54},  {0x1.2300003854303p+0, 0x1.3ffc2eb9fbf33p-54},
    {0x1.24fffffbcf684p+0, 0x1.601e77e2e2e72p-56},  {0x1.26ffff52921d9p+0, 0x1.ffcbb767f0c61p-56},
    {0x1.2900014933a3cp+0, -0x1.202ca3c02412bp-56}, {0x1.2b00014556313p+0, -0x1.2808233f21f02p-54},
    {0x1.2cfffebfe523bp+0, -0x1.8ff7e384fdcf2p-55}, {0x1.2f0000bb8ad96p+0, -0x1.5ff51503041c5p-55},
    {0x1.30ffffb7ae2afp+0, -0x1.10071885e289dp-55}, {0x1.32ffffeac5f7fp+0, -0x1.1ff5d3fb7b715p-54},
    {0x1.350000ca66756p+0, 0x1.57f82228b82bdp-54},  {0x1.3700011fbf721p+0, 0x1.000bac40dd5ccp-55},
    {0x1.38ffff9592fb9p+0, -0x1.43f9d2db2a751p-54}, {0x1.3b00004ddd242p+0, 0x1.57f6b707638e1p-55},
    {0x1.3cffff5b2c957p+0, 0x1.a023a10bf1231p-56},  {0x1.3efffeab0b418p+0, 0x1.87f6d66b152bp-54},
    {0x1.410001532aff4p+0, 0x1.7f8375f198524p-57},  {0x1.4300017478b29p+0, 0x1.301e672dc5143p-55},
    {0x1.44fffe795b463p+0, 0x1.9ff69b8b2895ap-55},  {0x1.46fffe80475ep+0, -0x1.5c0b19bc2f254p-54},
    {0x1.48fffef6fc1e7p+0, 0x1.b4009f23a2a72p-54},  {0x1.4afffe5bea704p+0, -0x1.4ffb7bf0d7d45p-54},
    {0x1.4d000171027dep+0, -0x1.9c06471dc6a3dp-54}, {0x1.4f0000ff03ee2p+0, 0x1.77f890b85531cp-54},
    {0x1.5100012dc4bd1p+0, 0x1.004657166a436p-57},  {0x1.530001605277ap+0, -0x1.6bfcece233209p-54},
    {0x1.54fffecdb704cp+0, -0x1.902720505a1d7p-55}, {0x1.56fffef5f54a9p+0, 0x1.bbfe60ec96412p-54},
    {0x1.5900017e61012p+0, 0x1.87ec581afef9p-55},   {0x1.5b00003c93e92p+0, -0x1.f41080abf0ccp-54},
    {0x1.5d0001d4919bcp+0, -0x1.8812afb254729p-54}, {0x1.5efffe7b87a89p+0, -0x1.47eb780ed6904p-54}};

constexpr double kLog2InvLn2hi = 0x1.7154765200000p+0;
constexpr double kLog2InvLn2lo = 0x1.705fc2eefa200p-33;
constexpr double kLog2Poly1[10] = {
    -0x1.71547652b82fep-1, 0x1.ec709dc3a03f7p-2, -0x1.71547652b7c3fp-2, 0x1.2776c50f05be4p-2,
    -0x1.ec709dd768fe5p-3, 0x1.a61761ec4e736p-3, -0x1.7153fbc64a79bp-3, 0x1.484d154f01b4ap-3,
    -0x1.289e4a72c383cp-3, 0x1.0b32f285aee66p-3};
constexpr double kLog2Poly[6] = {-0x1.71547652b8339p-1, 0x1.ec709dc3a04bep-2,
                                 -0x1.7154764702ffbp-2, 0x1.2776c50034c48p-2,
                                 -0x1.ec7b328ea92bcp-3, 0x1.a6225e117f92ep-3};
constexpr double kLog2Tab[64][2] = {
    {0x1.724286bb1acf8p+0, -0x1.1095feecdb000p-1}, {0x1.6e1f766d2cca1p+0, -0x1.08494bd76d000p-1},
    {0x1.6a13d0e30d48ap+0, -0x1.00143aee8f800p-1}, {0x1.661ec32d06c85p+0, -0x1.efec5360b4000p-2},
    {0x1.623fa951198f8p+0, -0x1.dfdd91ab7e000p-2}, {0x1.5e75ba4cf026cp+0, -0x1.cffae0cc79000p-2},
    {0x1.5ac055a214fb8p+0, -0x1.c043811fda000p-2}, {0x1.571ed0f166e1ep+0, -0x1.b0b67323ae000p-2},
    {0x1.53909590bf835p+0, -0x1.a152f5a2db000p-2}, {0x1.5014fed61adddp+0, -0x1.9217f5af86000p-2},
    {0x1.4cab88e487bd0p+0, -0x1.8304db0719000p-2}, {0x1.49539b4334feep+0, -0x1.74189f9a9e000p-2},
    {0x1.460cbdfafd569p+0, -0x1.6552bb5199000p-2}, {0x1.42d664ee4b953p+0, -0x1.56b23a29b1000p-2},
    {0x1.3fb01111dd8a6p+0, -0x1.483650f5fa000p-2}, {0x1.3c995b70c5836p+0, -0x1.39de937f6a000p-2},
    {0x1.3991c4ab6fd4ap+0, -0x1.2baa1538d6000p-2}, {0x1.3698e0ce099b5p+0, -0x1.1d98340ca4000p-2},
    {0x1.33ae48213e7b2p+0, -0x1.0fa853a40e000p-2}, {0x1.30d191985bdb1p+0, -0x1.01d9c32e73000p-2},
    {0x1.2e025cab271d7p+0, -0x1.e857da2fa6000p-3}, {0x1.2b404cf13cd82p+0, -0x1.cd3c8633d8000p-3},
    {0x1.288b02c7ccb50p+0, -0x1.b26034c14a000p-3}, {0x1.25e2263944de5p+0, -0x1.97c1c2f4fe000p-3},
    {0x1.234563d8615b1p+0, -0x1.7d6023f800000p-3}, {0x1.20b46e33eaf38p+0, -0x1.633a71a05e000p-3},
    {0x1.1e2eefdcda3ddp+0, -0x1.494f5e9570000p-3}, {0x1.1bb4a580b3930p+0, -0x1.2f9e424e0a000p-3},
    {0x1.19453847f2200p+0, -0x1.162595afdc000p-3}, {0x1.16e06c0d5d73cp+0, -0x1.f9c9a75bd8000p-4},
    {0x1.1485f47b7e4c2p+0, -0x1.c7b575bf9c000p-4}, {0x1.12358ad0085d1p+0, -0x1.960c60ff48000p-4},
    {0x1.0fef00f532227p+0, -0x1.64ce247b60000p-4}, {0x1.0db2077d03a8fp+0, -0x1.33f78b2014000p-4},
    {0x1.0b7e6d65980d9p+0, -0x1.0387d1a42c000p-4}, {0x1.0953efe7b408dp+0, -0x1.a6f9208b50000p-5},
    {0x1.07325cac53b83p+0, -0x1.47a954f770000p-5}, {0x1.05197e40d1b5cp+0, -0x1.d23a8c50c0000p-6},
    {0x1.03091c1208ea2p+0, -0x1.16a2629780000p-6}, {0x1.0101025b37e21p+0, -0x1.720f8d8e80000p-8},
    {0x1.fc07ef9caa76bp-1, 0x1.6fe53b1500000p-7},  {0x1.f4465d3f6f184p-1, 0x1.11ccce10f8000p-5},
    {0x1.ecc079f84107fp-1, 0x1.c4dfc8c8b8000p-5},  {0x1.e573a99975ae8p-1, 0x1.3aa321e574000p-4},
    {0x1.de5d6f0bd3de6p-1, 0x1.918a0d08b8000p-4},  {0x1.d77b681ff38b3p-1, 0x1.e72e9da044000p-4},
    {0x1.d0cb5724de943p-1, 0x1.1dcd2507f6000p-3},  {0x1.ca4b2dc0e7563p-1, 0x1.476ab03dea000p-3},
    {0x1.c3f8ee8d6cb51p-1, 0x1.7074377e22000p-3},  {0x1.bdd2b4f020c4cp-1, 0x1.98ede8ba94000p-3},
    {0x1.b7d6c006015cap-1, 0x1.c0db86ad2e000p-3},  {0x1.b20366e2e338fp-1, 0x1.e840aafcee000p-3},
    {0x1.ac57026295039p-1, 0x1.0790ab4678000p-2},  {0x1.a6d01bc2731ddp-1, 0x1.1ac056801c000p-2},
    {0x1.a16d3bc3ff18bp-1, 0x1.2db11d4fee000p-2},  {0x1.9c2d14967feadp-1, 0x1.406464ec58000p-2},
    {0x1.970e4f47c9902p-1, 0x1.52dbe093af000p-2},  {0x1.920fb3982bcf2p-1, 0x1.651902050d000p-2},
    {0x1.8d30187f759f1p-1, 0x1.771d2cdeaf000p-2},  {0x1.886e5ebb9f66dp-1, 0x1.88e9c857d9000p-2},
    {0x1.83c97b658b994p-1, 0x1.9a80155e16000p-2},  {0x1.7f405ffc61022p-1, 0x1.abe186ed3d000p-2},
    {0x1.7ad22181415cap-1, 0x1.bd0f2aea0e000p-2},  {0x1.767dcf99eff8cp-1, 0x1.ce0a43dbf4000p-2}};
constexpr double kLog2Tab2[64][2] = {
    {0x1.6200012b90a8ep-1, 0x1.904ab0644b605p-55},  {0x1.66000045734a6p-1, 0x1.1ff9bea62f7a9p-57},
    {0x1.69fffc325f2c5p-1, 0x1.27ecfcb3c90bap-55},  {0x1.6e00038b95a04p-1, 0x1.8ff8856739326p-55},
    {0x1.71fffe09994e3p-1, 0x1.afd40275f82b1p-55},  {0x1.7600015590e1p-1, -0x1.2fd75b4238341p-56},
    {0x1.7a00012655bd5p-1, 0x1.808e67c242b76p-56},  {0x1.7e0003259e9a6p-1, -0x1.208e426f622b7p-57},
    {0x1.81fffedb4b2d2p-1, -0x1.402461ea5c92fp-55}, {0x1.860002dfafcc3p-1, 0x1.df7f4a2f29a1fp-57},
    {0x1.89ffff78c6b5p-1, -0x1.e0453094995fdp-55},  {0x1.8e00039671566p-1, -0x1.a04f3bec77b45p-55},
    {0x1.91fffe2bf1745p-1, -0x1.7fa34400e203cp-56}, {0x1.95fffcc5c9fd1p-1, -0x1.6ff8005a0695dp-56},
    {0x1.9a0003bba4767p-1, 0x1.0f8c4c4ec7e03p-56},  {0x1.9dfffe7b92da5p-1, 0x1.e7fd9478c4602p-55},
    {0x1.a1fffd72efdafp-1, -0x1.a0c554dcdae7ep-57}, {0x1.a5fffde04ff95p-1, 0x1.67da98ce9b26bp-55},
    {0x1.a9fffca5e8d2bp-1, -0x1.284c9b54c13dep-55}, {0x1.adfffddad03eap-1, 0x1.812c8ea602e3cp-58},
    {0x1.b1ffff10d3d4dp-1, -0x1.efaddad27789cp-55}, {0x1.b5fffce21165ap-1, 0x1.3cb1719c61237p-58},
    {0x1.b9fffd950e674p-1, 0x1.3f7d94194cep-56},    {0x1.be000139ca8afp-1, 0x1.50ac4215d9bcp-56},
    {0x1.c20005b46df99p-1, 0x1.beea653e9c1c9p-57},  {0x1.c600040b9f7aep-1, -0x1.c079f274a70d6p-56},
    {0x1.ca0006255fd8ap-1, -0x1.a0b4076e84c1fp-56}, {0x1.cdfffd94c095dp-1, 0x1.8f933f99ab5d7p-55},
    {0x1.d1ffff975d6cfp-1, -0x1.82c08665fe1bep-58}, {0x1.d5fffa2561c93p-1, -0x1.b04289bd295f3p-56},
    {0x1.d9fff9d228b0cp-1, 0x1.70251340fa236p-55},  {0x1.de00065bc7e16p-1, -0x1.5011e16a4d80cp-56},
    {0x1.e200002f64791p-1, 0x1.9802f09ef62ep-55},   {0x1.e600057d7a6d8p-1, -0x1.e0b75580cf7fap-56},
    {0x1.ea00027edc00cp-1, -0x1.c848309459811p-55}, {0x1.ee0006cf5cb7cp-1, -0x1.f8027951576f4p-55},
    {0x1.f2000782b7dccp-1, -0x1.f81d97274538fp-55}, {0x1.f6000260c450ap-1, -0x1.071002727ffdcp-59},
    {0x1.f9fffe88cd533p-1, -0x1.81bdce1fda8bp-58},  {0x1.fdfffd50f8689p-1, 0x1.7f91acb918e6ep-55},
    {0x1.0200004292367p+0, 0x1.b7ff365324681p-54},  {0x1.05fffe3e3d668p+0, 0x1.6fa08ddae957bp-55},
    {0x1.0a0000a85a757p+0, -0x1.7e2de80d3fb91p-58}, {0x1.0e0001a5f3fccp+0, -0x1.1823305c5f014p-54},
    {0x1.11ffff8afbaf5p+0, -0x1.bfabb6680bac2p-55}, {0x1.15fffe54d91adp+0, -0x1.d7f121737e7efp-54},
    {0x1.1a00011ac36e1p+0, 0x1.c000a0516f5ffp-54},  {0x1.1e00019c84248p+0, -0x1.082fbe4da5dap-54},
    {0x1.220000ffe5e6ep+0, -0x1.8fdd04c9cfb43p-55}, {0x1.26000269fd891p+0, 0x1.cfe2a7994d182p-55},
    {0x1.2a00029a6e6dap+0, -0x1.00273715e8bc5p-56}, {0x1.2dfffe0293e39p+0, 0x1.b7c39dab2a6f9p-54},
    {0x1.31ffff7dcf082p+0, 0x1.df1336edc5254p-56},  {0x1.35ffff05a8b6p+0, -0x1.e03564ccd31ebp-54},
    {0x1.3a0002e0eaeccp+0, 0x1.5f0e74bd3a477p-56},  {0x1.3e000043bb236p+0, 0x1.c7dcb149d8833p-54},
    {0x1.4200002d187ffp+0, 0x1.e08afcf2d3d28p-56},  {0x1.460000d387cb1p+0, 0x1.20837856599a6p-55},
    {0x1.4a00004569f89p+0, -0x1.9fa5c904fbcd2p-55}, {0x1.4e000043543f3p+0, -0x1.81125ed175329p-56},
    {0x1.51fffcc027f0fp+0, 0x1.883d8847754dcp-54},  {0x1.55ffffd87b36fp+0, -0x1.709e731d02807p-55},
    {0x1.59ffff21df7bap+0, 0x1.7f79f68727b02p-55},  {0x1.5dfffebfc3481p+0, -0x1.180902e30e93ep-54}};

constexpr std::uint64_t kLogOff = 0x3fe6000000000000ULL;

constexpr std::uint32_t top12(double x) noexcept {
  return static_cast<std::uint32_t>(bits_of(x) >> 52U);
}

constexpr std::uint32_t top16(double x) noexcept {
  return static_cast<std::uint32_t>(bits_of(x) >> 48U);
}

// musl exp specialcase: results near overflow and underflow, scaled in two steps.
double exp_specialcase(double tmp, std::uint64_t sbits, std::uint64_t ki) noexcept {
  double scale;
  double y;
  if ((ki & 0x80000000ULL) == 0) {
    sbits -= 1009ULL << 52;
    scale = double_of(sbits);
    y = 0x1p1009 * (scale + scale * tmp);
    return y;
  }
  sbits += 1022ULL << 52;
  scale = double_of(sbits);
  y = scale + scale * tmp;
  if (y < 1.0) {
    double hi;
    double lo;
    lo = scale - y + scale * tmp;
    hi = 1.0 + y;
    lo = 1.0 - hi + y + lo;
    y = (hi + lo) - 1.0;
    if (y == 0.0) {
      y = 0.0;  // round to +0 (musl WANT_ROUNDING)
    }
  }
  return 0x1p-1022 * y;
}

}  // namespace

double hypot(double x, double y) noexcept {
  constexpr std::uint64_t kAbsMask = ~std::uint64_t{0} >> 1U;
  std::uint64_t ux = bits_of(x) & kAbsMask;
  std::uint64_t uy = bits_of(y) & kAbsMask;
  if (ux < uy) {
    const std::uint64_t t = ux;
    ux = uy;
    uy = t;
  }
  const int ex = static_cast<int>(ux >> 52U);
  const int ey = static_cast<int>(uy >> 52U);
  x = double_of(ux);
  y = double_of(uy);
  if (ey == 0x7ff) {
    return y;
  }
  if (ex == 0x7ff || uy == 0) {
    return x;
  }
  if (ex - ey > 64) {
    return x + y;
  }
  double z = 1;
  if (ex > 0x3ff + 510) {
    z = 0x1p700;
    x *= 0x1p-700;
    y *= 0x1p-700;
  } else if (ey < 0x3ff - 450) {
    z = 0x1p-700;
    x *= 0x1p700;
    y *= 0x1p700;
  }
  double hx;
  double lx;
  double hy;
  double ly;
  square_split(&hx, &lx, x);
  square_split(&hy, &ly, y);
  return z * std::sqrt(ly + lx + hy + hx);
}

double exp(double x) noexcept {
  std::uint32_t abstop = top12(x) & 0x7ffU;
  if (abstop - top12(0x1p-54) >= top12(512.0) - top12(0x1p-54)) {
    if (abstop - top12(0x1p-54) >= 0x80000000U) {
      return 1.0 + x;  // tiny: 1 + x rounds to 1 or x
    }
    if (abstop >= top12(1024.0)) {
      if (bits_of(x) == bits_of(-kInf)) {
        return 0.0;
      }
      if (abstop >= top12(kInf)) {
        return 1.0 + x;
      }
      // musl __math_uflow(0) and __math_oflow(0): 0x1p-767 squared and 0x1p769 squared.
      return (bits_of(x) >> 63U) != 0 ? 0.0 : kInf;
    }
    abstop = 0;  // large but finite: take specialcase on the scaled path
  }
  const double z = kExpInvLn2N * x;
  double kd = z + kExpShift;
  const std::uint64_t ki = bits_of(kd);
  kd -= kExpShift;
  const double r = x + kd * kExpNegLn2hiN + kd * kExpNegLn2loN;
  const std::uint64_t idx = 2 * (ki % kExpN);
  const std::uint64_t top = ki << (52 - kExpTableBits);
  const double tail = double_of(kExpTab[idx]);
  const std::uint64_t sbits = kExpTab[idx + 1] + top;
  const double r2 = r * r;
  const double tmp =
      tail + r + r2 * (kExpC[0] + r * kExpC[1]) + r2 * r2 * (kExpC[2] + r * kExpC[3]);
  if (abstop == 0) {
    return exp_specialcase(tmp, sbits, ki);
  }
  const double scale = double_of(sbits);
  return scale + scale * tmp;
}

double log(double x) noexcept {
  constexpr std::uint64_t kLo = bits_of(1.0 - 0x1p-4);
  constexpr std::uint64_t kHi = bits_of(1.0 + 0x1.09p-4);
  constexpr int kTableBits = 7;
  constexpr std::uint64_t kN = 1U << kTableBits;
  const double* const b = kLogPoly1;
  const double* const a = kLogPoly;
  std::uint64_t ix = bits_of(x);
  const std::uint32_t top = top16(x);

  if (ix - kLo < kHi - kLo) {
    // Near 1: log(1 + r) polynomial, no table.
    if (ix == bits_of(1.0)) {
      return 0;
    }
    const double r = x - 1.0;
    const double r2 = r * r;
    const double r3 = r * r2;
    double y =
        r3 * (b[1] + r * b[2] + r2 * b[3] +
              r3 * (b[4] + r * b[5] + r2 * b[6] + r3 * (b[7] + r * b[8] + r2 * b[9] + r3 * b[10])));
    double w = r * 0x1p27;
    const double rhi = r + w - w;
    const double rlo = r - rhi;
    w = rhi * rhi * b[0];
    const double hi = r + w;
    double lo = r - hi + w;
    lo += b[0] * rlo * (rhi + r);
    y += lo;
    y += hi;
    return y;
  }
  if (top - 0x0010U >= 0x7ff0U - 0x0010U) {
    if (ix * 2 == 0) {
      return -kInf;  // musl __math_divzero(1)
    }
    if (ix == bits_of(kInf)) {
      return x;
    }
    if ((top & 0x8000U) != 0 || (top & 0x7ff0U) == 0x7ff0U) {
      return (x - x) / (x - x);  // musl __math_invalid
    }
    // Subnormal: scale into the normal range.
    ix = bits_of(x * 0x1p52);
    ix -= 52ULL << 52;
  }
  const std::uint64_t tmp = ix - kLogOff;
  const auto i = static_cast<std::size_t>((tmp >> (52 - kTableBits)) % kN);
  const auto k = static_cast<std::int64_t>(tmp) >> 52;
  const std::uint64_t iz = ix - (tmp & (0xfffULL << 52));
  const double invc = kLogTab[i][0];
  const double logc = kLogTab[i][1];
  const double z = double_of(iz);
  // The non-FMA reduction (musl, __FP_FAST_FMA undefined): never fused.
  const double r = (z - kLogTab2[i][0] - kLogTab2[i][1]) * invc;
  const double kd = static_cast<double>(k);
  const double w = kd * kLogLn2hi + logc;
  const double hi = w + r;
  const double lo = w - hi + r + kd * kLogLn2lo;
  const double r2 = r * r;
  return lo + r2 * a[0] + r * r2 * (a[1] + r * a[2] + r2 * (a[3] + r * a[4])) + hi;
}

double log2(double x) noexcept {
  constexpr std::uint64_t kLo = bits_of(1.0 - 0x1.5b51p-5);
  constexpr std::uint64_t kHi = bits_of(1.0 + 0x1.6ab2p-5);
  constexpr int kTableBits = 6;
  constexpr std::uint64_t kN = 1U << kTableBits;
  const double* const b = kLog2Poly1;
  const double* const a = kLog2Poly;
  std::uint64_t ix = bits_of(x);
  const std::uint32_t top = top16(x);

  if (ix - kLo < kHi - kLo) {
    // Near 1: polynomial in r with a split of r so hi and lo are exact.
    if (ix == bits_of(1.0)) {
      return 0;
    }
    const double r = x - 1.0;
    const double rhi = clear_low_word(r);
    const double rlo = r - rhi;
    double hi = rhi * kLog2InvLn2hi;
    double lo = rlo * kLog2InvLn2hi + r * kLog2InvLn2lo;
    const double r2 = r * r;
    const double r4 = r2 * r2;
    const double p = r2 * (b[0] + r * b[1]);
    double y = hi + p;
    lo += hi - y + p;
    lo += r4 * (b[2] + r * b[3] + r2 * (b[4] + r * b[5]) +
                r4 * (b[6] + r * b[7] + r2 * (b[8] + r * b[9])));
    y += lo;
    return y;
  }
  if (top - 0x0010U >= 0x7ff0U - 0x0010U) {
    if (ix * 2 == 0) {
      return -kInf;  // musl __math_divzero(1)
    }
    if (ix == bits_of(kInf)) {
      return x;
    }
    if ((top & 0x8000U) != 0 || (top & 0x7ff0U) == 0x7ff0U) {
      return (x - x) / (x - x);  // musl __math_invalid
    }
    ix = bits_of(x * 0x1p52);
    ix -= 52ULL << 52;
  }
  const std::uint64_t tmp = ix - kLogOff;
  const auto i = static_cast<std::size_t>((tmp >> (52 - kTableBits)) % kN);
  const auto k = static_cast<std::int64_t>(tmp) >> 52;
  const std::uint64_t iz = ix - (tmp & (0xfffULL << 52));
  const double invc = kLog2Tab[i][0];
  const double logc = kLog2Tab[i][1];
  const double z = double_of(iz);
  const double kd = static_cast<double>(k);
  // The non-FMA reduction (musl, __FP_FAST_FMA undefined): never fused.
  const double r = (z - kLog2Tab2[i][0] - kLog2Tab2[i][1]) * invc;
  const double rhi = clear_low_word(r);
  const double rlo = r - rhi;
  const double t1 = rhi * kLog2InvLn2hi;
  const double t2 = rlo * kLog2InvLn2hi + r * kLog2InvLn2lo;
  const double t3 = kd + logc;
  const double hi = t3 + t1;
  const double lo = t3 - hi + t1 + t2;
  const double r2 = r * r;
  const double r4 = r2 * r2;
  const double p = a[0] + r * a[1] + r2 * (a[2] + r * a[3]) + r4 * (a[4] + r * a[5]);
  return lo + r2 * p + hi;
}

}  // namespace realm::math
