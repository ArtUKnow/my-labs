#include "text.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

namespace {
void *checked(void *p) {
  if (!p) {
    fprintf(stderr, "memory allocation failed\n");
    exit(2);
  }
  return p;
}
struct Buffer {
  char *data;
  size_t size, capacity;
  Buffer() : data(NULL), size(0), capacity(0) {}
  ~Buffer() { free(data); }
  void reserve(size_t n) {
    if (n <= capacity)
      return;
    size_t c = capacity ? capacity : 256;
    while (c < n)
      c *= 2;
    data = (char *)checked(realloc(data, c));
    capacity = c;
  }
  void append(const char *p, size_t n) {
    reserve(size + n + 1);
    memcpy(data + size, p, n);
    size += n;
    data[size] = 0;
  }
  void clear() {
    size = 0;
    if (data)
      data[0] = 0;
  }
};
uint32_t decode(const char *s, size_t n, size_t *width) {
  if (!n) {
    *width = 0;
    return 0;
  }
  unsigned char a = (unsigned char)s[0];
  if (a < 128) {
    *width = 1;
    return a;
  }
  unsigned count = a >= 0xf0 && a <= 0xf4   ? 4
                   : a >= 0xe0 && a <= 0xef ? 3
                   : a >= 0xc2 && a <= 0xdf ? 2
                                            : 1;
  if (count == 1 || count > n) {
    *width = 1;
    return 0xfffd;
  }
  uint32_t c = a & ((1u << (7 - count)) - 1);
  for (unsigned i = 1; i < count; i++) {
    unsigned char b = (unsigned char)s[i];
    if ((b & 0xc0) != 0x80) {
      *width = 1;
      return 0xfffd;
    }
    c = (c << 6) | (b & 63);
  }
  if ((count == 2 && c < 128) || (count == 3 && c < 2048) ||
      (count == 4 && c < 65536) || c > 0x10ffff ||
      (c >= 0xd800 && c <= 0xdfff)) {
    *width = 1;
    return 0xfffd;
  }
  *width = count;
  return c;
}
size_t encode(uint32_t c, char *out) {
  if (c < 128) {
    out[0] = (char)c;
    return 1;
  }
  if (c < 2048) {
    out[0] = (char)(0xc0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 63));
    return 2;
  }
  if (c < 65536) {
    out[0] = (char)(0xe0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 63));
    out[2] = (char)(0x80 | (c & 63));
    return 3;
  }
  out[0] = (char)(0xf0 | (c >> 18));
  out[1] = (char)(0x80 | ((c >> 12) & 63));
  out[2] = (char)(0x80 | ((c >> 6) & 63));
  out[3] = (char)(0x80 | (c & 63));
  return 4;
}

struct UnicodeRange {
  uint32_t first, last;
};
const UnicodeRange alpha_ranges[] = {
    {0x41, 0x5a},       {0x61, 0x7a},       {0xaa, 0xaa},
    {0xb5, 0xb5},       {0xba, 0xba},       {0xc0, 0xd6},
    {0xd8, 0xf6},       {0xf8, 0x2c1},      {0x2c6, 0x2d1},
    {0x2e0, 0x2e4},     {0x2ec, 0x2ec},     {0x2ee, 0x2ee},
    {0x370, 0x374},     {0x376, 0x377},     {0x37a, 0x37d},
    {0x37f, 0x37f},     {0x386, 0x386},     {0x388, 0x38a},
    {0x38c, 0x38c},     {0x38e, 0x3a1},     {0x3a3, 0x3f5},
    {0x3f7, 0x481},     {0x48a, 0x52f},     {0x531, 0x556},
    {0x559, 0x559},     {0x560, 0x588},     {0x5d0, 0x5ea},
    {0x5ef, 0x5f2},     {0x620, 0x64a},     {0x66e, 0x66f},
    {0x671, 0x6d3},     {0x6d5, 0x6d5},     {0x6e5, 0x6e6},
    {0x6ee, 0x6ef},     {0x6fa, 0x6fc},     {0x6ff, 0x6ff},
    {0x710, 0x710},     {0x712, 0x72f},     {0x74d, 0x7a5},
    {0x7b1, 0x7b1},     {0x7ca, 0x7ea},     {0x7f4, 0x7f5},
    {0x7fa, 0x7fa},     {0x800, 0x815},     {0x81a, 0x81a},
    {0x824, 0x824},     {0x828, 0x828},     {0x840, 0x858},
    {0x860, 0x86a},     {0x870, 0x887},     {0x889, 0x88e},
    {0x8a0, 0x8c9},     {0x904, 0x939},     {0x93d, 0x93d},
    {0x950, 0x950},     {0x958, 0x961},     {0x971, 0x980},
    {0x985, 0x98c},     {0x98f, 0x990},     {0x993, 0x9a8},
    {0x9aa, 0x9b0},     {0x9b2, 0x9b2},     {0x9b6, 0x9b9},
    {0x9bd, 0x9bd},     {0x9ce, 0x9ce},     {0x9dc, 0x9dd},
    {0x9df, 0x9e1},     {0x9f0, 0x9f1},     {0x9fc, 0x9fc},
    {0xa05, 0xa0a},     {0xa0f, 0xa10},     {0xa13, 0xa28},
    {0xa2a, 0xa30},     {0xa32, 0xa33},     {0xa35, 0xa36},
    {0xa38, 0xa39},     {0xa59, 0xa5c},     {0xa5e, 0xa5e},
    {0xa72, 0xa74},     {0xa85, 0xa8d},     {0xa8f, 0xa91},
    {0xa93, 0xaa8},     {0xaaa, 0xab0},     {0xab2, 0xab3},
    {0xab5, 0xab9},     {0xabd, 0xabd},     {0xad0, 0xad0},
    {0xae0, 0xae1},     {0xaf9, 0xaf9},     {0xb05, 0xb0c},
    {0xb0f, 0xb10},     {0xb13, 0xb28},     {0xb2a, 0xb30},
    {0xb32, 0xb33},     {0xb35, 0xb39},     {0xb3d, 0xb3d},
    {0xb5c, 0xb5d},     {0xb5f, 0xb61},     {0xb71, 0xb71},
    {0xb83, 0xb83},     {0xb85, 0xb8a},     {0xb8e, 0xb90},
    {0xb92, 0xb95},     {0xb99, 0xb9a},     {0xb9c, 0xb9c},
    {0xb9e, 0xb9f},     {0xba3, 0xba4},     {0xba8, 0xbaa},
    {0xbae, 0xbb9},     {0xbd0, 0xbd0},     {0xc05, 0xc0c},
    {0xc0e, 0xc10},     {0xc12, 0xc28},     {0xc2a, 0xc39},
    {0xc3d, 0xc3d},     {0xc58, 0xc5a},     {0xc5d, 0xc5d},
    {0xc60, 0xc61},     {0xc80, 0xc80},     {0xc85, 0xc8c},
    {0xc8e, 0xc90},     {0xc92, 0xca8},     {0xcaa, 0xcb3},
    {0xcb5, 0xcb9},     {0xcbd, 0xcbd},     {0xcdd, 0xcde},
    {0xce0, 0xce1},     {0xcf1, 0xcf2},     {0xd04, 0xd0c},
    {0xd0e, 0xd10},     {0xd12, 0xd3a},     {0xd3d, 0xd3d},
    {0xd4e, 0xd4e},     {0xd54, 0xd56},     {0xd5f, 0xd61},
    {0xd7a, 0xd7f},     {0xd85, 0xd96},     {0xd9a, 0xdb1},
    {0xdb3, 0xdbb},     {0xdbd, 0xdbd},     {0xdc0, 0xdc6},
    {0xe01, 0xe30},     {0xe32, 0xe33},     {0xe40, 0xe46},
    {0xe81, 0xe82},     {0xe84, 0xe84},     {0xe86, 0xe8a},
    {0xe8c, 0xea3},     {0xea5, 0xea5},     {0xea7, 0xeb0},
    {0xeb2, 0xeb3},     {0xebd, 0xebd},     {0xec0, 0xec4},
    {0xec6, 0xec6},     {0xedc, 0xedf},     {0xf00, 0xf00},
    {0xf40, 0xf47},     {0xf49, 0xf6c},     {0xf88, 0xf8c},
    {0x1000, 0x102a},   {0x103f, 0x103f},   {0x1050, 0x1055},
    {0x105a, 0x105d},   {0x1061, 0x1061},   {0x1065, 0x1066},
    {0x106e, 0x1070},   {0x1075, 0x1081},   {0x108e, 0x108e},
    {0x10a0, 0x10c5},   {0x10c7, 0x10c7},   {0x10cd, 0x10cd},
    {0x10d0, 0x10fa},   {0x10fc, 0x1248},   {0x124a, 0x124d},
    {0x1250, 0x1256},   {0x1258, 0x1258},   {0x125a, 0x125d},
    {0x1260, 0x1288},   {0x128a, 0x128d},   {0x1290, 0x12b0},
    {0x12b2, 0x12b5},   {0x12b8, 0x12be},   {0x12c0, 0x12c0},
    {0x12c2, 0x12c5},   {0x12c8, 0x12d6},   {0x12d8, 0x1310},
    {0x1312, 0x1315},   {0x1318, 0x135a},   {0x1380, 0x138f},
    {0x13a0, 0x13f5},   {0x13f8, 0x13fd},   {0x1401, 0x166c},
    {0x166f, 0x167f},   {0x1681, 0x169a},   {0x16a0, 0x16ea},
    {0x16f1, 0x16f8},   {0x1700, 0x1711},   {0x171f, 0x1731},
    {0x1740, 0x1751},   {0x1760, 0x176c},   {0x176e, 0x1770},
    {0x1780, 0x17b3},   {0x17d7, 0x17d7},   {0x17dc, 0x17dc},
    {0x1820, 0x1878},   {0x1880, 0x1884},   {0x1887, 0x18a8},
    {0x18aa, 0x18aa},   {0x18b0, 0x18f5},   {0x1900, 0x191e},
    {0x1950, 0x196d},   {0x1970, 0x1974},   {0x1980, 0x19ab},
    {0x19b0, 0x19c9},   {0x1a00, 0x1a16},   {0x1a20, 0x1a54},
    {0x1aa7, 0x1aa7},   {0x1b05, 0x1b33},   {0x1b45, 0x1b4c},
    {0x1b83, 0x1ba0},   {0x1bae, 0x1baf},   {0x1bba, 0x1be5},
    {0x1c00, 0x1c23},   {0x1c4d, 0x1c4f},   {0x1c5a, 0x1c7d},
    {0x1c80, 0x1c8a},   {0x1c90, 0x1cba},   {0x1cbd, 0x1cbf},
    {0x1ce9, 0x1cec},   {0x1cee, 0x1cf3},   {0x1cf5, 0x1cf6},
    {0x1cfa, 0x1cfa},   {0x1d00, 0x1dbf},   {0x1e00, 0x1f15},
    {0x1f18, 0x1f1d},   {0x1f20, 0x1f45},   {0x1f48, 0x1f4d},
    {0x1f50, 0x1f57},   {0x1f59, 0x1f59},   {0x1f5b, 0x1f5b},
    {0x1f5d, 0x1f5d},   {0x1f5f, 0x1f7d},   {0x1f80, 0x1fb4},
    {0x1fb6, 0x1fbc},   {0x1fbe, 0x1fbe},   {0x1fc2, 0x1fc4},
    {0x1fc6, 0x1fcc},   {0x1fd0, 0x1fd3},   {0x1fd6, 0x1fdb},
    {0x1fe0, 0x1fec},   {0x1ff2, 0x1ff4},   {0x1ff6, 0x1ffc},
    {0x2071, 0x2071},   {0x207f, 0x207f},   {0x2090, 0x209c},
    {0x2102, 0x2102},   {0x2107, 0x2107},   {0x210a, 0x2113},
    {0x2115, 0x2115},   {0x2119, 0x211d},   {0x2124, 0x2124},
    {0x2126, 0x2126},   {0x2128, 0x2128},   {0x212a, 0x212d},
    {0x212f, 0x2139},   {0x213c, 0x213f},   {0x2145, 0x2149},
    {0x214e, 0x214e},   {0x2183, 0x2184},   {0x2c00, 0x2ce4},
    {0x2ceb, 0x2cee},   {0x2cf2, 0x2cf3},   {0x2d00, 0x2d25},
    {0x2d27, 0x2d27},   {0x2d2d, 0x2d2d},   {0x2d30, 0x2d67},
    {0x2d6f, 0x2d6f},   {0x2d80, 0x2d96},   {0x2da0, 0x2da6},
    {0x2da8, 0x2dae},   {0x2db0, 0x2db6},   {0x2db8, 0x2dbe},
    {0x2dc0, 0x2dc6},   {0x2dc8, 0x2dce},   {0x2dd0, 0x2dd6},
    {0x2dd8, 0x2dde},   {0x2e2f, 0x2e2f},   {0x3005, 0x3006},
    {0x3031, 0x3035},   {0x303b, 0x303c},   {0x3041, 0x3096},
    {0x309d, 0x309f},   {0x30a1, 0x30fa},   {0x30fc, 0x30ff},
    {0x3105, 0x312f},   {0x3131, 0x318e},   {0x31a0, 0x31bf},
    {0x31f0, 0x31ff},   {0x3400, 0x4dbf},   {0x4e00, 0xa48c},
    {0xa4d0, 0xa4fd},   {0xa500, 0xa60c},   {0xa610, 0xa61f},
    {0xa62a, 0xa62b},   {0xa640, 0xa66e},   {0xa67f, 0xa69d},
    {0xa6a0, 0xa6e5},   {0xa717, 0xa71f},   {0xa722, 0xa788},
    {0xa78b, 0xa7cd},   {0xa7d0, 0xa7d1},   {0xa7d3, 0xa7d3},
    {0xa7d5, 0xa7dc},   {0xa7f2, 0xa801},   {0xa803, 0xa805},
    {0xa807, 0xa80a},   {0xa80c, 0xa822},   {0xa840, 0xa873},
    {0xa882, 0xa8b3},   {0xa8f2, 0xa8f7},   {0xa8fb, 0xa8fb},
    {0xa8fd, 0xa8fe},   {0xa90a, 0xa925},   {0xa930, 0xa946},
    {0xa960, 0xa97c},   {0xa984, 0xa9b2},   {0xa9cf, 0xa9cf},
    {0xa9e0, 0xa9e4},   {0xa9e6, 0xa9ef},   {0xa9fa, 0xa9fe},
    {0xaa00, 0xaa28},   {0xaa40, 0xaa42},   {0xaa44, 0xaa4b},
    {0xaa60, 0xaa76},   {0xaa7a, 0xaa7a},   {0xaa7e, 0xaaaf},
    {0xaab1, 0xaab1},   {0xaab5, 0xaab6},   {0xaab9, 0xaabd},
    {0xaac0, 0xaac0},   {0xaac2, 0xaac2},   {0xaadb, 0xaadd},
    {0xaae0, 0xaaea},   {0xaaf2, 0xaaf4},   {0xab01, 0xab06},
    {0xab09, 0xab0e},   {0xab11, 0xab16},   {0xab20, 0xab26},
    {0xab28, 0xab2e},   {0xab30, 0xab5a},   {0xab5c, 0xab69},
    {0xab70, 0xabe2},   {0xac00, 0xd7a3},   {0xd7b0, 0xd7c6},
    {0xd7cb, 0xd7fb},   {0xf900, 0xfa6d},   {0xfa70, 0xfad9},
    {0xfb00, 0xfb06},   {0xfb13, 0xfb17},   {0xfb1d, 0xfb1d},
    {0xfb1f, 0xfb28},   {0xfb2a, 0xfb36},   {0xfb38, 0xfb3c},
    {0xfb3e, 0xfb3e},   {0xfb40, 0xfb41},   {0xfb43, 0xfb44},
    {0xfb46, 0xfbb1},   {0xfbd3, 0xfd3d},   {0xfd50, 0xfd8f},
    {0xfd92, 0xfdc7},   {0xfdf0, 0xfdfb},   {0xfe70, 0xfe74},
    {0xfe76, 0xfefc},   {0xff21, 0xff3a},   {0xff41, 0xff5a},
    {0xff66, 0xffbe},   {0xffc2, 0xffc7},   {0xffca, 0xffcf},
    {0xffd2, 0xffd7},   {0xffda, 0xffdc},   {0x10000, 0x1000b},
    {0x1000d, 0x10026}, {0x10028, 0x1003a}, {0x1003c, 0x1003d},
    {0x1003f, 0x1004d}, {0x10050, 0x1005d}, {0x10080, 0x100fa},
    {0x10280, 0x1029c}, {0x102a0, 0x102d0}, {0x10300, 0x1031f},
    {0x1032d, 0x10340}, {0x10342, 0x10349}, {0x10350, 0x10375},
    {0x10380, 0x1039d}, {0x103a0, 0x103c3}, {0x103c8, 0x103cf},
    {0x10400, 0x1049d}, {0x104b0, 0x104d3}, {0x104d8, 0x104fb},
    {0x10500, 0x10527}, {0x10530, 0x10563}, {0x10570, 0x1057a},
    {0x1057c, 0x1058a}, {0x1058c, 0x10592}, {0x10594, 0x10595},
    {0x10597, 0x105a1}, {0x105a3, 0x105b1}, {0x105b3, 0x105b9},
    {0x105bb, 0x105bc}, {0x105c0, 0x105f3}, {0x10600, 0x10736},
    {0x10740, 0x10755}, {0x10760, 0x10767}, {0x10780, 0x10785},
    {0x10787, 0x107b0}, {0x107b2, 0x107ba}, {0x10800, 0x10805},
    {0x10808, 0x10808}, {0x1080a, 0x10835}, {0x10837, 0x10838},
    {0x1083c, 0x1083c}, {0x1083f, 0x10855}, {0x10860, 0x10876},
    {0x10880, 0x1089e}, {0x108e0, 0x108f2}, {0x108f4, 0x108f5},
    {0x10900, 0x10915}, {0x10920, 0x10939}, {0x10980, 0x109b7},
    {0x109be, 0x109bf}, {0x10a00, 0x10a00}, {0x10a10, 0x10a13},
    {0x10a15, 0x10a17}, {0x10a19, 0x10a35}, {0x10a60, 0x10a7c},
    {0x10a80, 0x10a9c}, {0x10ac0, 0x10ac7}, {0x10ac9, 0x10ae4},
    {0x10b00, 0x10b35}, {0x10b40, 0x10b55}, {0x10b60, 0x10b72},
    {0x10b80, 0x10b91}, {0x10c00, 0x10c48}, {0x10c80, 0x10cb2},
    {0x10cc0, 0x10cf2}, {0x10d00, 0x10d23}, {0x10d4a, 0x10d65},
    {0x10d6f, 0x10d85}, {0x10e80, 0x10ea9}, {0x10eb0, 0x10eb1},
    {0x10ec2, 0x10ec4}, {0x10f00, 0x10f1c}, {0x10f27, 0x10f27},
    {0x10f30, 0x10f45}, {0x10f70, 0x10f81}, {0x10fb0, 0x10fc4},
    {0x10fe0, 0x10ff6}, {0x11003, 0x11037}, {0x11071, 0x11072},
    {0x11075, 0x11075}, {0x11083, 0x110af}, {0x110d0, 0x110e8},
    {0x11103, 0x11126}, {0x11144, 0x11144}, {0x11147, 0x11147},
    {0x11150, 0x11172}, {0x11176, 0x11176}, {0x11183, 0x111b2},
    {0x111c1, 0x111c4}, {0x111da, 0x111da}, {0x111dc, 0x111dc},
    {0x11200, 0x11211}, {0x11213, 0x1122b}, {0x1123f, 0x11240},
    {0x11280, 0x11286}, {0x11288, 0x11288}, {0x1128a, 0x1128d},
    {0x1128f, 0x1129d}, {0x1129f, 0x112a8}, {0x112b0, 0x112de},
    {0x11305, 0x1130c}, {0x1130f, 0x11310}, {0x11313, 0x11328},
    {0x1132a, 0x11330}, {0x11332, 0x11333}, {0x11335, 0x11339},
    {0x1133d, 0x1133d}, {0x11350, 0x11350}, {0x1135d, 0x11361},
    {0x11380, 0x11389}, {0x1138b, 0x1138b}, {0x1138e, 0x1138e},
    {0x11390, 0x113b5}, {0x113b7, 0x113b7}, {0x113d1, 0x113d1},
    {0x113d3, 0x113d3}, {0x11400, 0x11434}, {0x11447, 0x1144a},
    {0x1145f, 0x11461}, {0x11480, 0x114af}, {0x114c4, 0x114c5},
    {0x114c7, 0x114c7}, {0x11580, 0x115ae}, {0x115d8, 0x115db},
    {0x11600, 0x1162f}, {0x11644, 0x11644}, {0x11680, 0x116aa},
    {0x116b8, 0x116b8}, {0x11700, 0x1171a}, {0x11740, 0x11746},
    {0x11800, 0x1182b}, {0x118a0, 0x118df}, {0x118ff, 0x11906},
    {0x11909, 0x11909}, {0x1190c, 0x11913}, {0x11915, 0x11916},
    {0x11918, 0x1192f}, {0x1193f, 0x1193f}, {0x11941, 0x11941},
    {0x119a0, 0x119a7}, {0x119aa, 0x119d0}, {0x119e1, 0x119e1},
    {0x119e3, 0x119e3}, {0x11a00, 0x11a00}, {0x11a0b, 0x11a32},
    {0x11a3a, 0x11a3a}, {0x11a50, 0x11a50}, {0x11a5c, 0x11a89},
    {0x11a9d, 0x11a9d}, {0x11ab0, 0x11af8}, {0x11bc0, 0x11be0},
    {0x11c00, 0x11c08}, {0x11c0a, 0x11c2e}, {0x11c40, 0x11c40},
    {0x11c72, 0x11c8f}, {0x11d00, 0x11d06}, {0x11d08, 0x11d09},
    {0x11d0b, 0x11d30}, {0x11d46, 0x11d46}, {0x11d60, 0x11d65},
    {0x11d67, 0x11d68}, {0x11d6a, 0x11d89}, {0x11d98, 0x11d98},
    {0x11ee0, 0x11ef2}, {0x11f02, 0x11f02}, {0x11f04, 0x11f10},
    {0x11f12, 0x11f33}, {0x11fb0, 0x11fb0}, {0x12000, 0x12399},
    {0x12480, 0x12543}, {0x12f90, 0x12ff0}, {0x13000, 0x1342f},
    {0x13441, 0x13446}, {0x13460, 0x143fa}, {0x14400, 0x14646},
    {0x16100, 0x1611d}, {0x16800, 0x16a38}, {0x16a40, 0x16a5e},
    {0x16a70, 0x16abe}, {0x16ad0, 0x16aed}, {0x16b00, 0x16b2f},
    {0x16b40, 0x16b43}, {0x16b63, 0x16b77}, {0x16b7d, 0x16b8f},
    {0x16d40, 0x16d6c}, {0x16e40, 0x16e7f}, {0x16f00, 0x16f4a},
    {0x16f50, 0x16f50}, {0x16f93, 0x16f9f}, {0x16fe0, 0x16fe1},
    {0x16fe3, 0x16fe3}, {0x17000, 0x187f7}, {0x18800, 0x18cd5},
    {0x18cff, 0x18d08}, {0x1aff0, 0x1aff3}, {0x1aff5, 0x1affb},
    {0x1affd, 0x1affe}, {0x1b000, 0x1b122}, {0x1b132, 0x1b132},
    {0x1b150, 0x1b152}, {0x1b155, 0x1b155}, {0x1b164, 0x1b167},
    {0x1b170, 0x1b2fb}, {0x1bc00, 0x1bc6a}, {0x1bc70, 0x1bc7c},
    {0x1bc80, 0x1bc88}, {0x1bc90, 0x1bc99}, {0x1d400, 0x1d454},
    {0x1d456, 0x1d49c}, {0x1d49e, 0x1d49f}, {0x1d4a2, 0x1d4a2},
    {0x1d4a5, 0x1d4a6}, {0x1d4a9, 0x1d4ac}, {0x1d4ae, 0x1d4b9},
    {0x1d4bb, 0x1d4bb}, {0x1d4bd, 0x1d4c3}, {0x1d4c5, 0x1d505},
    {0x1d507, 0x1d50a}, {0x1d50d, 0x1d514}, {0x1d516, 0x1d51c},
    {0x1d51e, 0x1d539}, {0x1d53b, 0x1d53e}, {0x1d540, 0x1d544},
    {0x1d546, 0x1d546}, {0x1d54a, 0x1d550}, {0x1d552, 0x1d6a5},
    {0x1d6a8, 0x1d6c0}, {0x1d6c2, 0x1d6da}, {0x1d6dc, 0x1d6fa},
    {0x1d6fc, 0x1d714}, {0x1d716, 0x1d734}, {0x1d736, 0x1d74e},
    {0x1d750, 0x1d76e}, {0x1d770, 0x1d788}, {0x1d78a, 0x1d7a8},
    {0x1d7aa, 0x1d7c2}, {0x1d7c4, 0x1d7cb}, {0x1df00, 0x1df1e},
    {0x1df25, 0x1df2a}, {0x1e030, 0x1e06d}, {0x1e100, 0x1e12c},
    {0x1e137, 0x1e13d}, {0x1e14e, 0x1e14e}, {0x1e290, 0x1e2ad},
    {0x1e2c0, 0x1e2eb}, {0x1e4d0, 0x1e4eb}, {0x1e5d0, 0x1e5ed},
    {0x1e5f0, 0x1e5f0}, {0x1e7e0, 0x1e7e6}, {0x1e7e8, 0x1e7eb},
    {0x1e7ed, 0x1e7ee}, {0x1e7f0, 0x1e7fe}, {0x1e800, 0x1e8c4},
    {0x1e900, 0x1e943}, {0x1e94b, 0x1e94b}, {0x1ee00, 0x1ee03},
    {0x1ee05, 0x1ee1f}, {0x1ee21, 0x1ee22}, {0x1ee24, 0x1ee24},
    {0x1ee27, 0x1ee27}, {0x1ee29, 0x1ee32}, {0x1ee34, 0x1ee37},
    {0x1ee39, 0x1ee39}, {0x1ee3b, 0x1ee3b}, {0x1ee42, 0x1ee42},
    {0x1ee47, 0x1ee47}, {0x1ee49, 0x1ee49}, {0x1ee4b, 0x1ee4b},
    {0x1ee4d, 0x1ee4f}, {0x1ee51, 0x1ee52}, {0x1ee54, 0x1ee54},
    {0x1ee57, 0x1ee57}, {0x1ee59, 0x1ee59}, {0x1ee5b, 0x1ee5b},
    {0x1ee5d, 0x1ee5d}, {0x1ee5f, 0x1ee5f}, {0x1ee61, 0x1ee62},
    {0x1ee64, 0x1ee64}, {0x1ee67, 0x1ee6a}, {0x1ee6c, 0x1ee72},
    {0x1ee74, 0x1ee77}, {0x1ee79, 0x1ee7c}, {0x1ee7e, 0x1ee7e},
    {0x1ee80, 0x1ee89}, {0x1ee8b, 0x1ee9b}, {0x1eea1, 0x1eea3},
    {0x1eea5, 0x1eea9}, {0x1eeab, 0x1eebb}, {0x20000, 0x2a6df},
    {0x2a700, 0x2b739}, {0x2b740, 0x2b81d}, {0x2b820, 0x2cea1},
    {0x2ceb0, 0x2ebe0}, {0x2ebf0, 0x2ee5d}, {0x2f800, 0x2fa1d},
    {0x30000, 0x3134a}, {0x31350, 0x323af}};
const UnicodeRange digit_ranges[] = {
    {0x30, 0x39},       {0xb2, 0xb3},       {0xb9, 0xb9},
    {0x660, 0x669},     {0x6f0, 0x6f9},     {0x7c0, 0x7c9},
    {0x966, 0x96f},     {0x9e6, 0x9ef},     {0xa66, 0xa6f},
    {0xae6, 0xaef},     {0xb66, 0xb6f},     {0xbe6, 0xbef},
    {0xc66, 0xc6f},     {0xce6, 0xcef},     {0xd66, 0xd6f},
    {0xde6, 0xdef},     {0xe50, 0xe59},     {0xed0, 0xed9},
    {0xf20, 0xf29},     {0x1040, 0x1049},   {0x1090, 0x1099},
    {0x1369, 0x1371},   {0x17e0, 0x17e9},   {0x1810, 0x1819},
    {0x1946, 0x194f},   {0x19d0, 0x19da},   {0x1a80, 0x1a89},
    {0x1a90, 0x1a99},   {0x1b50, 0x1b59},   {0x1bb0, 0x1bb9},
    {0x1c40, 0x1c49},   {0x1c50, 0x1c59},   {0x2070, 0x2070},
    {0x2074, 0x2079},   {0x2080, 0x2089},   {0x2460, 0x2468},
    {0x2474, 0x247c},   {0x2488, 0x2490},   {0x24ea, 0x24ea},
    {0x24f5, 0x24fd},   {0x24ff, 0x24ff},   {0x2776, 0x277e},
    {0x2780, 0x2788},   {0x278a, 0x2792},   {0xa620, 0xa629},
    {0xa8d0, 0xa8d9},   {0xa900, 0xa909},   {0xa9d0, 0xa9d9},
    {0xa9f0, 0xa9f9},   {0xaa50, 0xaa59},   {0xabf0, 0xabf9},
    {0xff10, 0xff19},   {0x104a0, 0x104a9}, {0x10a40, 0x10a43},
    {0x10d30, 0x10d39}, {0x10d40, 0x10d49}, {0x10e60, 0x10e68},
    {0x11052, 0x1105a}, {0x11066, 0x1106f}, {0x110f0, 0x110f9},
    {0x11136, 0x1113f}, {0x111d0, 0x111d9}, {0x112f0, 0x112f9},
    {0x11450, 0x11459}, {0x114d0, 0x114d9}, {0x11650, 0x11659},
    {0x116c0, 0x116c9}, {0x116d0, 0x116e3}, {0x11730, 0x11739},
    {0x118e0, 0x118e9}, {0x11950, 0x11959}, {0x11bf0, 0x11bf9},
    {0x11c50, 0x11c59}, {0x11d50, 0x11d59}, {0x11da0, 0x11da9},
    {0x11f50, 0x11f59}, {0x16130, 0x16139}, {0x16a60, 0x16a69},
    {0x16ac0, 0x16ac9}, {0x16b50, 0x16b59}, {0x16d70, 0x16d79},
    {0x1ccf0, 0x1ccf9}, {0x1d7ce, 0x1d7ff}, {0x1e140, 0x1e149},
    {0x1e2f0, 0x1e2f9}, {0x1e4f0, 0x1e4f9}, {0x1e5f1, 0x1e5fa},
    {0x1e950, 0x1e959}, {0x1f100, 0x1f10a}, {0x1fbf0, 0x1fbf9}};
struct LowerRange {
  uint32_t first, last;
  int32_t delta;
};
const LowerRange lower_ranges[] = {
    {0x41, 0x5a, 32},         {0xc0, 0xd6, 32},
    {0xd8, 0xde, 32},         {0x100, 0x100, 1},
    {0x102, 0x102, 1},        {0x104, 0x104, 1},
    {0x106, 0x106, 1},        {0x108, 0x108, 1},
    {0x10a, 0x10a, 1},        {0x10c, 0x10c, 1},
    {0x10e, 0x10e, 1},        {0x110, 0x110, 1},
    {0x112, 0x112, 1},        {0x114, 0x114, 1},
    {0x116, 0x116, 1},        {0x118, 0x118, 1},
    {0x11a, 0x11a, 1},        {0x11c, 0x11c, 1},
    {0x11e, 0x11e, 1},        {0x120, 0x120, 1},
    {0x122, 0x122, 1},        {0x124, 0x124, 1},
    {0x126, 0x126, 1},        {0x128, 0x128, 1},
    {0x12a, 0x12a, 1},        {0x12c, 0x12c, 1},
    {0x12e, 0x12e, 1},        {0x132, 0x132, 1},
    {0x134, 0x134, 1},        {0x136, 0x136, 1},
    {0x139, 0x139, 1},        {0x13b, 0x13b, 1},
    {0x13d, 0x13d, 1},        {0x13f, 0x13f, 1},
    {0x141, 0x141, 1},        {0x143, 0x143, 1},
    {0x145, 0x145, 1},        {0x147, 0x147, 1},
    {0x14a, 0x14a, 1},        {0x14c, 0x14c, 1},
    {0x14e, 0x14e, 1},        {0x150, 0x150, 1},
    {0x152, 0x152, 1},        {0x154, 0x154, 1},
    {0x156, 0x156, 1},        {0x158, 0x158, 1},
    {0x15a, 0x15a, 1},        {0x15c, 0x15c, 1},
    {0x15e, 0x15e, 1},        {0x160, 0x160, 1},
    {0x162, 0x162, 1},        {0x164, 0x164, 1},
    {0x166, 0x166, 1},        {0x168, 0x168, 1},
    {0x16a, 0x16a, 1},        {0x16c, 0x16c, 1},
    {0x16e, 0x16e, 1},        {0x170, 0x170, 1},
    {0x172, 0x172, 1},        {0x174, 0x174, 1},
    {0x176, 0x176, 1},        {0x178, 0x178, -121},
    {0x179, 0x179, 1},        {0x17b, 0x17b, 1},
    {0x17d, 0x17d, 1},        {0x181, 0x181, 210},
    {0x182, 0x182, 1},        {0x184, 0x184, 1},
    {0x186, 0x186, 206},      {0x187, 0x187, 1},
    {0x189, 0x18a, 205},      {0x18b, 0x18b, 1},
    {0x18e, 0x18e, 79},       {0x18f, 0x18f, 202},
    {0x190, 0x190, 203},      {0x191, 0x191, 1},
    {0x193, 0x193, 205},      {0x194, 0x194, 207},
    {0x196, 0x196, 211},      {0x197, 0x197, 209},
    {0x198, 0x198, 1},        {0x19c, 0x19c, 211},
    {0x19d, 0x19d, 213},      {0x19f, 0x19f, 214},
    {0x1a0, 0x1a0, 1},        {0x1a2, 0x1a2, 1},
    {0x1a4, 0x1a4, 1},        {0x1a6, 0x1a6, 218},
    {0x1a7, 0x1a7, 1},        {0x1a9, 0x1a9, 218},
    {0x1ac, 0x1ac, 1},        {0x1ae, 0x1ae, 218},
    {0x1af, 0x1af, 1},        {0x1b1, 0x1b2, 217},
    {0x1b3, 0x1b3, 1},        {0x1b5, 0x1b5, 1},
    {0x1b7, 0x1b7, 219},      {0x1b8, 0x1b8, 1},
    {0x1bc, 0x1bc, 1},        {0x1c4, 0x1c4, 2},
    {0x1c5, 0x1c5, 1},        {0x1c7, 0x1c7, 2},
    {0x1c8, 0x1c8, 1},        {0x1ca, 0x1ca, 2},
    {0x1cb, 0x1cb, 1},        {0x1cd, 0x1cd, 1},
    {0x1cf, 0x1cf, 1},        {0x1d1, 0x1d1, 1},
    {0x1d3, 0x1d3, 1},        {0x1d5, 0x1d5, 1},
    {0x1d7, 0x1d7, 1},        {0x1d9, 0x1d9, 1},
    {0x1db, 0x1db, 1},        {0x1de, 0x1de, 1},
    {0x1e0, 0x1e0, 1},        {0x1e2, 0x1e2, 1},
    {0x1e4, 0x1e4, 1},        {0x1e6, 0x1e6, 1},
    {0x1e8, 0x1e8, 1},        {0x1ea, 0x1ea, 1},
    {0x1ec, 0x1ec, 1},        {0x1ee, 0x1ee, 1},
    {0x1f1, 0x1f1, 2},        {0x1f2, 0x1f2, 1},
    {0x1f4, 0x1f4, 1},        {0x1f6, 0x1f6, -97},
    {0x1f7, 0x1f7, -56},      {0x1f8, 0x1f8, 1},
    {0x1fa, 0x1fa, 1},        {0x1fc, 0x1fc, 1},
    {0x1fe, 0x1fe, 1},        {0x200, 0x200, 1},
    {0x202, 0x202, 1},        {0x204, 0x204, 1},
    {0x206, 0x206, 1},        {0x208, 0x208, 1},
    {0x20a, 0x20a, 1},        {0x20c, 0x20c, 1},
    {0x20e, 0x20e, 1},        {0x210, 0x210, 1},
    {0x212, 0x212, 1},        {0x214, 0x214, 1},
    {0x216, 0x216, 1},        {0x218, 0x218, 1},
    {0x21a, 0x21a, 1},        {0x21c, 0x21c, 1},
    {0x21e, 0x21e, 1},        {0x220, 0x220, -130},
    {0x222, 0x222, 1},        {0x224, 0x224, 1},
    {0x226, 0x226, 1},        {0x228, 0x228, 1},
    {0x22a, 0x22a, 1},        {0x22c, 0x22c, 1},
    {0x22e, 0x22e, 1},        {0x230, 0x230, 1},
    {0x232, 0x232, 1},        {0x23a, 0x23a, 10795},
    {0x23b, 0x23b, 1},        {0x23d, 0x23d, -163},
    {0x23e, 0x23e, 10792},    {0x241, 0x241, 1},
    {0x243, 0x243, -195},     {0x244, 0x244, 69},
    {0x245, 0x245, 71},       {0x246, 0x246, 1},
    {0x248, 0x248, 1},        {0x24a, 0x24a, 1},
    {0x24c, 0x24c, 1},        {0x24e, 0x24e, 1},
    {0x370, 0x370, 1},        {0x372, 0x372, 1},
    {0x376, 0x376, 1},        {0x37f, 0x37f, 116},
    {0x386, 0x386, 38},       {0x388, 0x38a, 37},
    {0x38c, 0x38c, 64},       {0x38e, 0x38f, 63},
    {0x391, 0x3a1, 32},       {0x3a3, 0x3ab, 32},
    {0x3cf, 0x3cf, 8},        {0x3d8, 0x3d8, 1},
    {0x3da, 0x3da, 1},        {0x3dc, 0x3dc, 1},
    {0x3de, 0x3de, 1},        {0x3e0, 0x3e0, 1},
    {0x3e2, 0x3e2, 1},        {0x3e4, 0x3e4, 1},
    {0x3e6, 0x3e6, 1},        {0x3e8, 0x3e8, 1},
    {0x3ea, 0x3ea, 1},        {0x3ec, 0x3ec, 1},
    {0x3ee, 0x3ee, 1},        {0x3f4, 0x3f4, -60},
    {0x3f7, 0x3f7, 1},        {0x3f9, 0x3f9, -7},
    {0x3fa, 0x3fa, 1},        {0x3fd, 0x3ff, -130},
    {0x400, 0x40f, 80},       {0x410, 0x42f, 32},
    {0x460, 0x460, 1},        {0x462, 0x462, 1},
    {0x464, 0x464, 1},        {0x466, 0x466, 1},
    {0x468, 0x468, 1},        {0x46a, 0x46a, 1},
    {0x46c, 0x46c, 1},        {0x46e, 0x46e, 1},
    {0x470, 0x470, 1},        {0x472, 0x472, 1},
    {0x474, 0x474, 1},        {0x476, 0x476, 1},
    {0x478, 0x478, 1},        {0x47a, 0x47a, 1},
    {0x47c, 0x47c, 1},        {0x47e, 0x47e, 1},
    {0x480, 0x480, 1},        {0x48a, 0x48a, 1},
    {0x48c, 0x48c, 1},        {0x48e, 0x48e, 1},
    {0x490, 0x490, 1},        {0x492, 0x492, 1},
    {0x494, 0x494, 1},        {0x496, 0x496, 1},
    {0x498, 0x498, 1},        {0x49a, 0x49a, 1},
    {0x49c, 0x49c, 1},        {0x49e, 0x49e, 1},
    {0x4a0, 0x4a0, 1},        {0x4a2, 0x4a2, 1},
    {0x4a4, 0x4a4, 1},        {0x4a6, 0x4a6, 1},
    {0x4a8, 0x4a8, 1},        {0x4aa, 0x4aa, 1},
    {0x4ac, 0x4ac, 1},        {0x4ae, 0x4ae, 1},
    {0x4b0, 0x4b0, 1},        {0x4b2, 0x4b2, 1},
    {0x4b4, 0x4b4, 1},        {0x4b6, 0x4b6, 1},
    {0x4b8, 0x4b8, 1},        {0x4ba, 0x4ba, 1},
    {0x4bc, 0x4bc, 1},        {0x4be, 0x4be, 1},
    {0x4c0, 0x4c0, 15},       {0x4c1, 0x4c1, 1},
    {0x4c3, 0x4c3, 1},        {0x4c5, 0x4c5, 1},
    {0x4c7, 0x4c7, 1},        {0x4c9, 0x4c9, 1},
    {0x4cb, 0x4cb, 1},        {0x4cd, 0x4cd, 1},
    {0x4d0, 0x4d0, 1},        {0x4d2, 0x4d2, 1},
    {0x4d4, 0x4d4, 1},        {0x4d6, 0x4d6, 1},
    {0x4d8, 0x4d8, 1},        {0x4da, 0x4da, 1},
    {0x4dc, 0x4dc, 1},        {0x4de, 0x4de, 1},
    {0x4e0, 0x4e0, 1},        {0x4e2, 0x4e2, 1},
    {0x4e4, 0x4e4, 1},        {0x4e6, 0x4e6, 1},
    {0x4e8, 0x4e8, 1},        {0x4ea, 0x4ea, 1},
    {0x4ec, 0x4ec, 1},        {0x4ee, 0x4ee, 1},
    {0x4f0, 0x4f0, 1},        {0x4f2, 0x4f2, 1},
    {0x4f4, 0x4f4, 1},        {0x4f6, 0x4f6, 1},
    {0x4f8, 0x4f8, 1},        {0x4fa, 0x4fa, 1},
    {0x4fc, 0x4fc, 1},        {0x4fe, 0x4fe, 1},
    {0x500, 0x500, 1},        {0x502, 0x502, 1},
    {0x504, 0x504, 1},        {0x506, 0x506, 1},
    {0x508, 0x508, 1},        {0x50a, 0x50a, 1},
    {0x50c, 0x50c, 1},        {0x50e, 0x50e, 1},
    {0x510, 0x510, 1},        {0x512, 0x512, 1},
    {0x514, 0x514, 1},        {0x516, 0x516, 1},
    {0x518, 0x518, 1},        {0x51a, 0x51a, 1},
    {0x51c, 0x51c, 1},        {0x51e, 0x51e, 1},
    {0x520, 0x520, 1},        {0x522, 0x522, 1},
    {0x524, 0x524, 1},        {0x526, 0x526, 1},
    {0x528, 0x528, 1},        {0x52a, 0x52a, 1},
    {0x52c, 0x52c, 1},        {0x52e, 0x52e, 1},
    {0x531, 0x556, 48},       {0x10a0, 0x10c5, 7264},
    {0x10c7, 0x10c7, 7264},   {0x10cd, 0x10cd, 7264},
    {0x13a0, 0x13ef, 38864},  {0x13f0, 0x13f5, 8},
    {0x1c89, 0x1c89, 1},      {0x1c90, 0x1cba, -3008},
    {0x1cbd, 0x1cbf, -3008},  {0x1e00, 0x1e00, 1},
    {0x1e02, 0x1e02, 1},      {0x1e04, 0x1e04, 1},
    {0x1e06, 0x1e06, 1},      {0x1e08, 0x1e08, 1},
    {0x1e0a, 0x1e0a, 1},      {0x1e0c, 0x1e0c, 1},
    {0x1e0e, 0x1e0e, 1},      {0x1e10, 0x1e10, 1},
    {0x1e12, 0x1e12, 1},      {0x1e14, 0x1e14, 1},
    {0x1e16, 0x1e16, 1},      {0x1e18, 0x1e18, 1},
    {0x1e1a, 0x1e1a, 1},      {0x1e1c, 0x1e1c, 1},
    {0x1e1e, 0x1e1e, 1},      {0x1e20, 0x1e20, 1},
    {0x1e22, 0x1e22, 1},      {0x1e24, 0x1e24, 1},
    {0x1e26, 0x1e26, 1},      {0x1e28, 0x1e28, 1},
    {0x1e2a, 0x1e2a, 1},      {0x1e2c, 0x1e2c, 1},
    {0x1e2e, 0x1e2e, 1},      {0x1e30, 0x1e30, 1},
    {0x1e32, 0x1e32, 1},      {0x1e34, 0x1e34, 1},
    {0x1e36, 0x1e36, 1},      {0x1e38, 0x1e38, 1},
    {0x1e3a, 0x1e3a, 1},      {0x1e3c, 0x1e3c, 1},
    {0x1e3e, 0x1e3e, 1},      {0x1e40, 0x1e40, 1},
    {0x1e42, 0x1e42, 1},      {0x1e44, 0x1e44, 1},
    {0x1e46, 0x1e46, 1},      {0x1e48, 0x1e48, 1},
    {0x1e4a, 0x1e4a, 1},      {0x1e4c, 0x1e4c, 1},
    {0x1e4e, 0x1e4e, 1},      {0x1e50, 0x1e50, 1},
    {0x1e52, 0x1e52, 1},      {0x1e54, 0x1e54, 1},
    {0x1e56, 0x1e56, 1},      {0x1e58, 0x1e58, 1},
    {0x1e5a, 0x1e5a, 1},      {0x1e5c, 0x1e5c, 1},
    {0x1e5e, 0x1e5e, 1},      {0x1e60, 0x1e60, 1},
    {0x1e62, 0x1e62, 1},      {0x1e64, 0x1e64, 1},
    {0x1e66, 0x1e66, 1},      {0x1e68, 0x1e68, 1},
    {0x1e6a, 0x1e6a, 1},      {0x1e6c, 0x1e6c, 1},
    {0x1e6e, 0x1e6e, 1},      {0x1e70, 0x1e70, 1},
    {0x1e72, 0x1e72, 1},      {0x1e74, 0x1e74, 1},
    {0x1e76, 0x1e76, 1},      {0x1e78, 0x1e78, 1},
    {0x1e7a, 0x1e7a, 1},      {0x1e7c, 0x1e7c, 1},
    {0x1e7e, 0x1e7e, 1},      {0x1e80, 0x1e80, 1},
    {0x1e82, 0x1e82, 1},      {0x1e84, 0x1e84, 1},
    {0x1e86, 0x1e86, 1},      {0x1e88, 0x1e88, 1},
    {0x1e8a, 0x1e8a, 1},      {0x1e8c, 0x1e8c, 1},
    {0x1e8e, 0x1e8e, 1},      {0x1e90, 0x1e90, 1},
    {0x1e92, 0x1e92, 1},      {0x1e94, 0x1e94, 1},
    {0x1e9e, 0x1e9e, -7615},  {0x1ea0, 0x1ea0, 1},
    {0x1ea2, 0x1ea2, 1},      {0x1ea4, 0x1ea4, 1},
    {0x1ea6, 0x1ea6, 1},      {0x1ea8, 0x1ea8, 1},
    {0x1eaa, 0x1eaa, 1},      {0x1eac, 0x1eac, 1},
    {0x1eae, 0x1eae, 1},      {0x1eb0, 0x1eb0, 1},
    {0x1eb2, 0x1eb2, 1},      {0x1eb4, 0x1eb4, 1},
    {0x1eb6, 0x1eb6, 1},      {0x1eb8, 0x1eb8, 1},
    {0x1eba, 0x1eba, 1},      {0x1ebc, 0x1ebc, 1},
    {0x1ebe, 0x1ebe, 1},      {0x1ec0, 0x1ec0, 1},
    {0x1ec2, 0x1ec2, 1},      {0x1ec4, 0x1ec4, 1},
    {0x1ec6, 0x1ec6, 1},      {0x1ec8, 0x1ec8, 1},
    {0x1eca, 0x1eca, 1},      {0x1ecc, 0x1ecc, 1},
    {0x1ece, 0x1ece, 1},      {0x1ed0, 0x1ed0, 1},
    {0x1ed2, 0x1ed2, 1},      {0x1ed4, 0x1ed4, 1},
    {0x1ed6, 0x1ed6, 1},      {0x1ed8, 0x1ed8, 1},
    {0x1eda, 0x1eda, 1},      {0x1edc, 0x1edc, 1},
    {0x1ede, 0x1ede, 1},      {0x1ee0, 0x1ee0, 1},
    {0x1ee2, 0x1ee2, 1},      {0x1ee4, 0x1ee4, 1},
    {0x1ee6, 0x1ee6, 1},      {0x1ee8, 0x1ee8, 1},
    {0x1eea, 0x1eea, 1},      {0x1eec, 0x1eec, 1},
    {0x1eee, 0x1eee, 1},      {0x1ef0, 0x1ef0, 1},
    {0x1ef2, 0x1ef2, 1},      {0x1ef4, 0x1ef4, 1},
    {0x1ef6, 0x1ef6, 1},      {0x1ef8, 0x1ef8, 1},
    {0x1efa, 0x1efa, 1},      {0x1efc, 0x1efc, 1},
    {0x1efe, 0x1efe, 1},      {0x1f08, 0x1f0f, -8},
    {0x1f18, 0x1f1d, -8},     {0x1f28, 0x1f2f, -8},
    {0x1f38, 0x1f3f, -8},     {0x1f48, 0x1f4d, -8},
    {0x1f59, 0x1f59, -8},     {0x1f5b, 0x1f5b, -8},
    {0x1f5d, 0x1f5d, -8},     {0x1f5f, 0x1f5f, -8},
    {0x1f68, 0x1f6f, -8},     {0x1f88, 0x1f8f, -8},
    {0x1f98, 0x1f9f, -8},     {0x1fa8, 0x1faf, -8},
    {0x1fb8, 0x1fb9, -8},     {0x1fba, 0x1fbb, -74},
    {0x1fbc, 0x1fbc, -9},     {0x1fc8, 0x1fcb, -86},
    {0x1fcc, 0x1fcc, -9},     {0x1fd8, 0x1fd9, -8},
    {0x1fda, 0x1fdb, -100},   {0x1fe8, 0x1fe9, -8},
    {0x1fea, 0x1feb, -112},   {0x1fec, 0x1fec, -7},
    {0x1ff8, 0x1ff9, -128},   {0x1ffa, 0x1ffb, -126},
    {0x1ffc, 0x1ffc, -9},     {0x2126, 0x2126, -7517},
    {0x212a, 0x212a, -8383},  {0x212b, 0x212b, -8262},
    {0x2132, 0x2132, 28},     {0x2160, 0x216f, 16},
    {0x2183, 0x2183, 1},      {0x24b6, 0x24cf, 26},
    {0x2c00, 0x2c2f, 48},     {0x2c60, 0x2c60, 1},
    {0x2c62, 0x2c62, -10743}, {0x2c63, 0x2c63, -3814},
    {0x2c64, 0x2c64, -10727}, {0x2c67, 0x2c67, 1},
    {0x2c69, 0x2c69, 1},      {0x2c6b, 0x2c6b, 1},
    {0x2c6d, 0x2c6d, -10780}, {0x2c6e, 0x2c6e, -10749},
    {0x2c6f, 0x2c6f, -10783}, {0x2c70, 0x2c70, -10782},
    {0x2c72, 0x2c72, 1},      {0x2c75, 0x2c75, 1},
    {0x2c7e, 0x2c7f, -10815}, {0x2c80, 0x2c80, 1},
    {0x2c82, 0x2c82, 1},      {0x2c84, 0x2c84, 1},
    {0x2c86, 0x2c86, 1},      {0x2c88, 0x2c88, 1},
    {0x2c8a, 0x2c8a, 1},      {0x2c8c, 0x2c8c, 1},
    {0x2c8e, 0x2c8e, 1},      {0x2c90, 0x2c90, 1},
    {0x2c92, 0x2c92, 1},      {0x2c94, 0x2c94, 1},
    {0x2c96, 0x2c96, 1},      {0x2c98, 0x2c98, 1},
    {0x2c9a, 0x2c9a, 1},      {0x2c9c, 0x2c9c, 1},
    {0x2c9e, 0x2c9e, 1},      {0x2ca0, 0x2ca0, 1},
    {0x2ca2, 0x2ca2, 1},      {0x2ca4, 0x2ca4, 1},
    {0x2ca6, 0x2ca6, 1},      {0x2ca8, 0x2ca8, 1},
    {0x2caa, 0x2caa, 1},      {0x2cac, 0x2cac, 1},
    {0x2cae, 0x2cae, 1},      {0x2cb0, 0x2cb0, 1},
    {0x2cb2, 0x2cb2, 1},      {0x2cb4, 0x2cb4, 1},
    {0x2cb6, 0x2cb6, 1},      {0x2cb8, 0x2cb8, 1},
    {0x2cba, 0x2cba, 1},      {0x2cbc, 0x2cbc, 1},
    {0x2cbe, 0x2cbe, 1},      {0x2cc0, 0x2cc0, 1},
    {0x2cc2, 0x2cc2, 1},      {0x2cc4, 0x2cc4, 1},
    {0x2cc6, 0x2cc6, 1},      {0x2cc8, 0x2cc8, 1},
    {0x2cca, 0x2cca, 1},      {0x2ccc, 0x2ccc, 1},
    {0x2cce, 0x2cce, 1},      {0x2cd0, 0x2cd0, 1},
    {0x2cd2, 0x2cd2, 1},      {0x2cd4, 0x2cd4, 1},
    {0x2cd6, 0x2cd6, 1},      {0x2cd8, 0x2cd8, 1},
    {0x2cda, 0x2cda, 1},      {0x2cdc, 0x2cdc, 1},
    {0x2cde, 0x2cde, 1},      {0x2ce0, 0x2ce0, 1},
    {0x2ce2, 0x2ce2, 1},      {0x2ceb, 0x2ceb, 1},
    {0x2ced, 0x2ced, 1},      {0x2cf2, 0x2cf2, 1},
    {0xa640, 0xa640, 1},      {0xa642, 0xa642, 1},
    {0xa644, 0xa644, 1},      {0xa646, 0xa646, 1},
    {0xa648, 0xa648, 1},      {0xa64a, 0xa64a, 1},
    {0xa64c, 0xa64c, 1},      {0xa64e, 0xa64e, 1},
    {0xa650, 0xa650, 1},      {0xa652, 0xa652, 1},
    {0xa654, 0xa654, 1},      {0xa656, 0xa656, 1},
    {0xa658, 0xa658, 1},      {0xa65a, 0xa65a, 1},
    {0xa65c, 0xa65c, 1},      {0xa65e, 0xa65e, 1},
    {0xa660, 0xa660, 1},      {0xa662, 0xa662, 1},
    {0xa664, 0xa664, 1},      {0xa666, 0xa666, 1},
    {0xa668, 0xa668, 1},      {0xa66a, 0xa66a, 1},
    {0xa66c, 0xa66c, 1},      {0xa680, 0xa680, 1},
    {0xa682, 0xa682, 1},      {0xa684, 0xa684, 1},
    {0xa686, 0xa686, 1},      {0xa688, 0xa688, 1},
    {0xa68a, 0xa68a, 1},      {0xa68c, 0xa68c, 1},
    {0xa68e, 0xa68e, 1},      {0xa690, 0xa690, 1},
    {0xa692, 0xa692, 1},      {0xa694, 0xa694, 1},
    {0xa696, 0xa696, 1},      {0xa698, 0xa698, 1},
    {0xa69a, 0xa69a, 1},      {0xa722, 0xa722, 1},
    {0xa724, 0xa724, 1},      {0xa726, 0xa726, 1},
    {0xa728, 0xa728, 1},      {0xa72a, 0xa72a, 1},
    {0xa72c, 0xa72c, 1},      {0xa72e, 0xa72e, 1},
    {0xa732, 0xa732, 1},      {0xa734, 0xa734, 1},
    {0xa736, 0xa736, 1},      {0xa738, 0xa738, 1},
    {0xa73a, 0xa73a, 1},      {0xa73c, 0xa73c, 1},
    {0xa73e, 0xa73e, 1},      {0xa740, 0xa740, 1},
    {0xa742, 0xa742, 1},      {0xa744, 0xa744, 1},
    {0xa746, 0xa746, 1},      {0xa748, 0xa748, 1},
    {0xa74a, 0xa74a, 1},      {0xa74c, 0xa74c, 1},
    {0xa74e, 0xa74e, 1},      {0xa750, 0xa750, 1},
    {0xa752, 0xa752, 1},      {0xa754, 0xa754, 1},
    {0xa756, 0xa756, 1},      {0xa758, 0xa758, 1},
    {0xa75a, 0xa75a, 1},      {0xa75c, 0xa75c, 1},
    {0xa75e, 0xa75e, 1},      {0xa760, 0xa760, 1},
    {0xa762, 0xa762, 1},      {0xa764, 0xa764, 1},
    {0xa766, 0xa766, 1},      {0xa768, 0xa768, 1},
    {0xa76a, 0xa76a, 1},      {0xa76c, 0xa76c, 1},
    {0xa76e, 0xa76e, 1},      {0xa779, 0xa779, 1},
    {0xa77b, 0xa77b, 1},      {0xa77d, 0xa77d, -35332},
    {0xa77e, 0xa77e, 1},      {0xa780, 0xa780, 1},
    {0xa782, 0xa782, 1},      {0xa784, 0xa784, 1},
    {0xa786, 0xa786, 1},      {0xa78b, 0xa78b, 1},
    {0xa78d, 0xa78d, -42280}, {0xa790, 0xa790, 1},
    {0xa792, 0xa792, 1},      {0xa796, 0xa796, 1},
    {0xa798, 0xa798, 1},      {0xa79a, 0xa79a, 1},
    {0xa79c, 0xa79c, 1},      {0xa79e, 0xa79e, 1},
    {0xa7a0, 0xa7a0, 1},      {0xa7a2, 0xa7a2, 1},
    {0xa7a4, 0xa7a4, 1},      {0xa7a6, 0xa7a6, 1},
    {0xa7a8, 0xa7a8, 1},      {0xa7aa, 0xa7aa, -42308},
    {0xa7ab, 0xa7ab, -42319}, {0xa7ac, 0xa7ac, -42315},
    {0xa7ad, 0xa7ad, -42305}, {0xa7ae, 0xa7ae, -42308},
    {0xa7b0, 0xa7b0, -42258}, {0xa7b1, 0xa7b1, -42282},
    {0xa7b2, 0xa7b2, -42261}, {0xa7b3, 0xa7b3, 928},
    {0xa7b4, 0xa7b4, 1},      {0xa7b6, 0xa7b6, 1},
    {0xa7b8, 0xa7b8, 1},      {0xa7ba, 0xa7ba, 1},
    {0xa7bc, 0xa7bc, 1},      {0xa7be, 0xa7be, 1},
    {0xa7c0, 0xa7c0, 1},      {0xa7c2, 0xa7c2, 1},
    {0xa7c4, 0xa7c4, -48},    {0xa7c5, 0xa7c5, -42307},
    {0xa7c6, 0xa7c6, -35384}, {0xa7c7, 0xa7c7, 1},
    {0xa7c9, 0xa7c9, 1},      {0xa7cb, 0xa7cb, -42343},
    {0xa7cc, 0xa7cc, 1},      {0xa7d0, 0xa7d0, 1},
    {0xa7d6, 0xa7d6, 1},      {0xa7d8, 0xa7d8, 1},
    {0xa7da, 0xa7da, 1},      {0xa7dc, 0xa7dc, -42561},
    {0xa7f5, 0xa7f5, 1},      {0xff21, 0xff3a, 32},
    {0x10400, 0x10427, 40},   {0x104b0, 0x104d3, 40},
    {0x10570, 0x1057a, 39},   {0x1057c, 0x1058a, 39},
    {0x1058c, 0x10592, 39},   {0x10594, 0x10595, 39},
    {0x10c80, 0x10cb2, 64},   {0x10d50, 0x10d65, 32},
    {0x118a0, 0x118bf, 32},   {0x16e40, 0x16e5f, 32},
    {0x1e900, 0x1e921, 34}};
bool property(uint32_t c, const UnicodeRange *ranges, size_t n) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (c < ranges[m].first)
      hi = m;
    else if (c > ranges[m].last)
      lo = m + 1;
    else
      return true;
  }
  return false;
}
uint32_t lower_codepoint(uint32_t c) {
  if (c < 128)
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
  if (c == 0x130)
    return 'i';
  size_t lo = 0, hi = sizeof(lower_ranges) / sizeof(*lower_ranges);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (c < lower_ranges[m].first)
      hi = m;
    else if (c > lower_ranges[m].last)
      lo = m + 1;
    else
      return (uint32_t)((int64_t)c + lower_ranges[m].delta);
  }
  return c;
}
void unicode_locale() {}
bool digit(uint32_t c) {
  if (c < 128)
    return c >= '0' && c <= '9';
  return property(c, digit_ranges,
                  sizeof(digit_ranges) / sizeof(*digit_ranges));
}
bool alpha(uint32_t c) {
  if (c < 128)
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  return property(c, alpha_ranges,
                  sizeof(alpha_ranges) / sizeof(*alpha_ranges));
}
bool word(uint32_t c) { return alpha(c) || digit(c); }
size_t characters(const char *s) {
  size_t count = 0, n = strlen(s), w;
  for (size_t i = 0; i < n; i += w) {
    decode(s + i, n - i, &w);
    count++;
  }
  return count;
}
void lower(char *s) {
  unicode_locale();
  size_t n = strlen(s), out = 0, w;
  for (size_t i = 0; i < n; i += w) {
    uint32_t c = decode(s + i, n - i, &w);
    c = lower_codepoint(c);
    if (c == 0x451)
      c = 0x435;
    char b[4];
    size_t k = encode(c, b);
    if (k > w) {
      memcpy(s + out, s + i, w);
      out += w;
    } else {
      memcpy(s + out, b, k);
      out += k;
    }
  }
  s[out] = 0;
}
bool suffix(const char *s, const char *end) {
  size_t a = strlen(s), b = strlen(end);
  return a >= b && memcmp(s + a - b, end, b) == 0;
}
bool vowel(uint32_t c) {
  return c == 0x430 || c == 0x435 || c == 0x438 || c == 0x43e || c == 0x443 ||
         c == 0x44b || c == 0x44d || c == 0x44e || c == 0x44f;
}
size_t region(const char *s, size_t start) {
  size_t n = strlen(s), w;
  bool previous = false;
  for (size_t i = start; i < n; i += w) {
    uint32_t c = decode(s + i, n - i, &w);
    if (previous && !vowel(c))
      return i + w;
    previous = vowel(c);
  }
  return n;
}
bool cut(char *s, const char *const *endings, size_t boundary,
         bool previous = false) {
  size_t n = strlen(s);
  for (size_t i = 0; endings[i]; i++) {
    size_t k = strlen(endings[i]);
    if (n < k || n - k < boundary || memcmp(s + n - k, endings[i], k))
      continue;
    if (previous && (n - k < 2 || (memcmp(s + n - k - 2, "а", 2) &&
                                   memcmp(s + n - k - 2, "я", 2))))
      continue;
    s[n - k] = 0;
    return true;
  }
  return false;
}
const char *const PG1[] = {"вшись", "вши", "в", NULL};
const char *const PG2[] = {"ившись", "ывшись", "ивши", "ывши",
                           "ив",     "ыв",     NULL};
const char *const ADJ[] = {"ими", "ыми", "его", "ого", "ему", "ому", "ее",
                           "ие",  "ые",  "ое",  "ей",  "ий",  "ый",  "ой",
                           "ем",  "им",  "ым",  "ом",  "их",  "ых",  "ую",
                           "юю",  "ая",  "яя",  "ою",  "ею",  NULL};
const char *const P1[] = {"ем", "нн", "вш", "ющ", "щ", NULL};
const char *const P2[] = {"ивш", "ывш", "ующ", NULL};
const char *const REFL[] = {"ся", "сь", NULL};
const char *const V1[] = {"ешь", "нно", "ете", "йте", "ла", "на",
                          "ли",  "ем",  "ло",  "но",  "ет", "ют",
                          "ны",  "ть",  "й",   "л",   "н",  NULL};
const char *const V2[] = {
    "ейте", "уйте", "ила", "ыла", "ена", "ите", "или", "ыли", "ило", "ыло",
    "ено",  "ует",  "уют", "ены", "ить", "ыть", "ишь", "ей",  "уй",  "ил",
    "ыл",   "им",   "ым",  "ен",  "ят",  "ит",  "ыт",  "ую",  "ю",   NULL};
const char *const NOUN[] = {
    "иями", "ями", "ами", "ией", "иям", "ием", "иях", "ев", "ов", "ие",
    "ье",   "еи",  "ии",  "ей",  "ой",  "ий",  "ям",  "ем", "ам", "ом",
    "ах",   "ях",  "ию",  "ью",  "ия",  "ья",  "а",   "е",  "и",  "й",
    "о",    "у",   "ы",   "ь",   "ю",   "я",   NULL};
const char *const SUPER[] = {"ейше", "ейш", NULL};
const char *const DERIV[] = {"ость", "ост", NULL};
void russian(char *s) {
  size_t n = strlen(s), w, rv = n;
  for (size_t i = 0; i < n; i += w) {
    uint32_t c = decode(s + i, n - i, &w);
    if (vowel(c)) {
      rv = i + w;
      break;
    }
  }
  size_t r1 = region(s, 0), r2 = region(s, r1);
  if (rv >= n)
    return;
  bool done = cut(s, PG2, rv);
  if (!done)
    done = cut(s, PG1, rv, true);
  if (!done) {
    cut(s, REFL, rv);
    done = cut(s, ADJ, rv);
    if (done && !cut(s, P2, rv))
      cut(s, P1, rv, true);
    if (!done) {
      done = cut(s, V2, rv);
      if (!done)
        done = cut(s, V1, rv, true);
    }
    if (!done)
      cut(s, NOUN, rv);
  }
  const char *const I[] = {"и", NULL};
  cut(s, I, rv);
  cut(s, DERIV, r2);
  const char *const NN[] = {"н", NULL};
  if (suffix(s, "нн"))
    cut(s, NN, rv);
  else {
    done = cut(s, SUPER, rv);
    if (done && suffix(s, "нн"))
      cut(s, NN, rv);
    else {
      const char *const SOFT[] = {"ь", NULL};
      cut(s, SOFT, rv);
    }
  }
}
bool has_vowel(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (strchr("aeiouy", s[i]))
      return true;
  return false;
}
void english(char *s) {
  size_t n = strlen(s);
  if (characters(s) <= 3)
    return;
  if (suffix(s, "sses"))
    s[n - 2] = 0;
  else if (suffix(s, "ies"))
    s[n - 2] = 0;
  else if (!suffix(s, "ss") && suffix(s, "s"))
    s[n - 1] = 0;
  n = strlen(s);
  size_t count = characters(s);
  if (count > 5 && suffix(s, "ing") && has_vowel(s, n - 3))
    s[n - 3] = 0;
  else if (count > 4 && suffix(s, "ed") && has_vowel(s, n - 2))
    s[n - 2] = 0;
  n = strlen(s);
  if (characters(s) > 4 && suffix(s, "ly"))
    s[n - 2] = 0;
  n = strlen(s);
  if (characters(s) > 6 && suffix(s, "ness"))
    s[n - 4] = 0;
  n = strlen(s);
  if (characters(s) > 7 && suffix(s, "ment"))
    s[n - 4] = 0;
  n = strlen(s);
  if (characters(s) > 6 && suffix(s, "tion"))
    s[n - 3] = 0;
}

double now() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
char *clone(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char *)checked(malloc(n));
  memcpy(p, s, n);
  return p;
}
void json_string(FILE *f, const char *s) {
  fputc('"', f);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') {
      fputc('\\', f);
      fputc(*p, f);
    } else if (*p == '\n')
      fputs("\\n", f);
    else if (*p == '\r')
      fputs("\\r", f);
    else if (*p == '\t')
      fputs("\\t", f);
    else if (*p < 32)
      fprintf(f, "\\u%04x", *p);
    else
      fputc(*p, f);
  }
  fputc('"', f);
}
uint64_t hash(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) {
    h ^= (unsigned char)*s;
    h *= 1099511628211ULL;
  }
  return h;
}
struct Entry {
  char *key;
  uint64_t count;
};
struct Frequencies {
  Entry *slots;
  size_t capacity, used;
  Frequencies() : slots(NULL), capacity(0), used(0) { grow(2048); }
  ~Frequencies() {
    for (size_t i = 0; i < capacity; i++)
      free(slots[i].key);
    free(slots);
  }
  void grow(size_t n) {
    Entry *old = slots;
    size_t oldn = capacity;
    slots = (Entry *)checked(calloc(n, sizeof(Entry)));
    capacity = n;
    for (size_t i = 0; i < oldn; i++)
      if (old[i].key) {
        size_t at = hash(old[i].key) & (capacity - 1);
        while (slots[at].key)
          at = (at + 1) & (capacity - 1);
        slots[at] = old[i];
      }
    free(old);
  }
  void add(const char *key) {
    if ((used + 1) * 10 >= capacity * 7)
      grow(capacity * 2);
    size_t at = hash(key) & (capacity - 1);
    while (slots[at].key && strcmp(slots[at].key, key))
      at = (at + 1) & (capacity - 1);
    if (!slots[at].key) {
      slots[at].key = clone(key);
      used++;
    }
    slots[at].count++;
  }
};
struct Point {
  uint64_t bytes, tokens;
  double seconds;
};
struct Stats {
  uint64_t tokens, length, bytes, documents, words, numbers, mixed;
  Point *points;
  size_t cap;
  char *examples[15];
  size_t example_count;
  Frequencies *frequencies;
  Stats()
      : tokens(0), length(0), bytes(0), documents(0), words(0), numbers(0),
        mixed(0), points(NULL), cap(0), example_count(0), frequencies(NULL) {
    memset(examples, 0, sizeof(examples));
  }
  ~Stats() {
    free(points);
    for (size_t i = 0; i < example_count; i++)
      free(examples[i]);
  }
};
void stats_token(const char *t, size_t, void *user) {
  Stats *s = (Stats *)user;
  s->tokens++;
  size_t n = strlen(t), w, count = 0;
  bool letter = false, number = false;
  for (size_t i = 0; i < n; i += w) {
    uint32_t c = decode(t + i, n - i, &w);
    count++;
    letter |= alpha(c);
    number |= digit(c);
  }
  s->length += count;
  if (letter && number)
    s->mixed++;
  else if (number)
    s->numbers++;
  else
    s->words++;
  if (s->frequencies)
    s->frequencies->add(t);
  if (count >= 25 && s->example_count < 15) {
    bool exists = false;
    for (size_t i = 0; i < s->example_count; i++)
      if (!strcmp(s->examples[i], t))
        exists = true;
    if (!exists)
      s->examples[s->example_count++] = clone(t);
  }
}
bool read_document(const char *path, Buffer &text) {
  text.clear();
  gzFile f = gzopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot open document %s\n", path);
    return false;
  }
  char buffer[65536];
  int n;
  while ((n = gzread(f, buffer, sizeof(buffer))) > 0)
    text.append(buffer, (size_t)n);
  if (n < 0) {
    int error;
    fprintf(stderr, "cannot read document %s: %s\n", path, gzerror(f, &error));
    gzclose(f);
    return false;
  }
  if (gzclose(f) != Z_OK) {
    fprintf(stderr, "cannot close document %s\n", path);
    return false;
  }
  return true;
}
bool corpus(const char *manifest, Stats &stats) {
  FILE *f = fopen(manifest, "r");
  if (!f) {
    perror(manifest);
    return false;
  }
  char *line = NULL;
  size_t length = 0;
  ssize_t n;
  if ((n = getline(&line, &length, f)) < 0 || strncmp(line, "id\tpath\t", 8)) {
    fprintf(stderr,
            "documents TSV must have id,path,url,title,journal header\n");
    free(line);
    fclose(f);
    return false;
  }
  Buffer text;
  while ((n = getline(&line, &length, f)) >= 0) {
    char *first = strchr(line, '\t');
    char *second = first ? strchr(first + 1, '\t') : NULL;
    if (!first || !second) {
      fprintf(stderr, "invalid documents TSV row\n");
      free(line);
      fclose(f);
      return false;
    }
    *second = 0;
    if (!read_document(first + 1, text)) {
      free(line);
      fclose(f);
      return false;
    }
    uint64_t before = stats.tokens;
    double begin = now();
    ir_tokenize(text.data ? text.data : "", text.size, stats_token, &stats);
    double seconds = now() - begin;
    if (stats.documents == stats.cap) {
      stats.cap = stats.cap ? stats.cap * 2 : 128;
      stats.points =
          (Point *)checked(realloc(stats.points, stats.cap * sizeof(Point)));
    }
    stats.points[stats.documents++] =
        Point{(uint64_t)text.size, stats.tokens - before, seconds};
    stats.bytes += text.size;
    if (stats.documents % 5000 == 0)
      fprintf(stderr, "text documents: %llu\n",
              (unsigned long long)stats.documents);
  }
  bool ok = !ferror(f);
  if (!ok)
    perror(manifest);
  free(line);
  fclose(f);
  return ok;
}
void print_token(const char *t, size_t n, void *) {
  fwrite(t, 1, n, stdout);
  fputc('\n', stdout);
}
void merge_entries(Entry **a, Entry **temp, size_t start, size_t end) {
  if (end - start < 2)
    return;
  size_t middle = start + (end - start) / 2;
  merge_entries(a, temp, start, middle);
  merge_entries(a, temp, middle, end);
  size_t i = start, j = middle, k = start;
  while (i < middle && j < end) {
    bool left = a[i]->count > a[j]->count || (a[i]->count == a[j]->count &&
                                              strcmp(a[i]->key, a[j]->key) < 0);
    temp[k++] = left ? a[i++] : a[j++];
  }
  while (i < middle)
    temp[k++] = a[i++];
  while (j < end)
    temp[k++] = a[j++];
  for (i = start; i < end; i++)
    a[i] = temp[i];
}
int write_stats(const char *output, const Stats &s, double elapsed) {
  FILE *f = fopen(output, "w");
  if (!f) {
    perror(output);
    return 1;
  }
  double token_time = 0;
  for (size_t i = 0; i < s.documents; i++)
    token_time += s.points[i].seconds;
  fprintf(
      f,
      "{\n  \"implementation\": \"C++ without STL\",\n  \"documents\": %llu,\n "
      " \"tokens\": %llu,\n  \"avg_token_length\": %.6f,\n  \"input_bytes\": "
      "%llu,\n  \"time_seconds\": %.6f,\n  \"wall_time_seconds\": %.6f,\n  "
      "\"kb_per_second\": %.6f,\n  \"kinds\": {\"word\": %llu, \"number\": "
      "%llu, \"mixed\": %llu},\n  \"timing_points\": [",
      (unsigned long long)s.documents, (unsigned long long)s.tokens,
      s.tokens ? (double)s.length / s.tokens : 0, (unsigned long long)s.bytes,
      token_time, elapsed, token_time ? s.bytes / 1024.0 / token_time : 0,
      (unsigned long long)s.words, (unsigned long long)s.numbers,
      (unsigned long long)s.mixed);
  for (size_t i = 0; i < s.documents; i++)
    fprintf(f, "%s{\"bytes\":%llu,\"tokens\":%llu,\"ms\":%.6f}", i ? "," : "",
            (unsigned long long)s.points[i].bytes,
            (unsigned long long)s.points[i].tokens, s.points[i].seconds * 1000);
  fputs("],\n  \"long_tokens\": [", f);
  for (size_t i = 0; i < s.example_count; i++) {
    if (i)
      fputc(',', f);
    json_string(f, s.examples[i]);
  }
  fputs("],\n  \"timing_scope\": \"time_seconds measures tokenization and "
        "callback statistics only; wall_time_seconds additionally includes "
        "manifest and gzip input\",\n  \"unicode\": \"UTF-8 decoding; Unicode "
        "16.0.0 alphabetic/digit tables and lowercase mappings; no locale "
        "dependency\"\n}\n",
        f);
  return fclose(f) ? 1 : 0;
}
int zipf(const char *manifest, const char *output, const char *svg) {
  Stats stats;
  Frequencies freq;
  stats.frequencies = &freq;
  double begin = now();
  if (!corpus(manifest, stats))
    return 1;
  Entry **ranked = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
  Entry **temp = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
  size_t n = 0;
  uint64_t hapax = 0;
  for (size_t i = 0; i < freq.capacity; i++)
    if (freq.slots[i].key) {
      ranked[n++] = &freq.slots[i];
      if (freq.slots[i].count == 1)
        hapax++;
    }
  merge_entries(ranked, temp, 0, n);
  free(temp);
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (size_t i = 0; i < n; i++) {
    double x = log((double)i + 1), y = log((double)ranked[i]->count);
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
  }
  double denominator = n * sxx - sx * sx;
  double exponent = denominator ? -(n * sxy - sx * sy) / denominator : 1;
  double c = n ? exp((sy + exponent * sx) / n) : 0;
  double classical = n ? (double)ranked[0]->count : 0;
  double error = 0;
  for (size_t i = 0; i < n; i++) {
    double d =
        log((double)ranked[i]->count) - log(c) + exponent * log((double)i + 1);
    error += d * d;
  }
  if (n)
    error /= n;
  FILE *f = fopen(output, "w");
  if (!f) {
    perror(output);
    free(ranked);
    return 1;
  }
  fprintf(
      f,
      "{\n\"implementation\":\"C++ custom hash table and merge "
      "sort\",\n\"documents\":%llu,\n\"total_tokens\":%llu,\n\"unique_terms\":%"
      "zu,\n\"hapax_legomena\":%llu,\n\"hapax_share\":%.8f,\n\"time_seconds\":%"
      ".6f,\n\"zipf_classical\":{\"c\":%.8f,\"s\":1},\n\"zipf_fit\":{\"c\":%."
      "8f,\"s\":%.8f,\"log_mse\":%.8f},\n\"top_terms\":[",
      (unsigned long long)stats.documents, (unsigned long long)stats.tokens, n,
      (unsigned long long)hapax, n ? (double)hapax / n : 0, now() - begin,
      classical, c, exponent, error);
  for (size_t i = 0; i < n && i < 30; i++) {
    if (i)
      fputc(',', f);
    fputs("{\"term\":", f);
    json_string(f, ranked[i]->key);
    fprintf(f, ",\"freq\":%llu}", (unsigned long long)ranked[i]->count);
  }
  fputs("],\n\"points\":[", f);
  size_t last = (size_t)-1;
  for (size_t point = 0; point < 400 && n; point++) {
    size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
    if (at >= n)
      at = n - 1;
    if (at == last)
      continue;
    if (last != (size_t)-1)
      fputc(',', f);
    fprintf(f, "{\"rank\":%zu,\"freq\":%llu,\"zipf\":%.8f,\"fit\":%.8f}",
            at + 1, (unsigned long long)ranked[at]->count, classical / (at + 1),
            c / pow(at + 1, exponent));
    last = at;
  }
  fputs("]\n}\n", f);
  if (fclose(f)) {
    free(ranked);
    return 1;
  }
  f = fopen(svg, "w");
  if (!f) {
    perror(svg);
    free(ranked);
    return 1;
  }
  fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" "
        "height=\"560\" viewBox=\"0 0 900 560\"><rect width=\"900\" "
        "height=\"560\" fill=\"white\"/><g font-family=\"sans-serif\" "
        "font-size=\"14\"><text x=\"65\" y=\"25\">Term frequencies: log rank / "
        "log frequency</text><text x=\"65\" y=\"545\">Blue: corpus; red: "
        "classical c/r; green: fitted c/r^s</text>",
        f);
  double xmax = log10((double)(n > 1 ? n : 2)),
         ymax = log10(classical > 1 ? classical : 2);
  for (int e = 0; e <= (int)ceil(xmax); e++) {
    double x = 65 + 760 * e / xmax;
    fprintf(f,
            "<path d=\"M %.2f 45 V 495\" stroke=\"#ddd\"/><text x=\"%.2f\" "
            "y=\"520\">10^%d</text>",
            x, x, e);
  }
  for (int e = 0; e <= (int)ceil(ymax); e++) {
    double y = 495 - 450 * e / ymax;
    fprintf(f,
            "<path d=\"M 65 %.2f H 825\" stroke=\"#ddd\"/><text x=\"10\" "
            "y=\"%.2f\">10^%d</text>",
            y, y, e);
  }
  const char *colors[] = {"#2563eb", "#dc2626", "#16a34a"};
  for (int curve = 0; curve < 3; curve++) {
    fprintf(
        f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\" points=\"",
        colors[curve]);
    last = (size_t)-1;
    for (size_t point = 0; point < 400 && n; point++) {
      size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
      if (at >= n)
        at = n - 1;
      if (at == last)
        continue;
      double rank = at + 1, y = curve == 0   ? (double)ranked[at]->count
                                : curve == 1 ? classical / rank
                                             : c / pow(rank, exponent);
      fprintf(f, "%.2f,%.2f ", 65 + 760 * log10(rank) / xmax,
              495 - 450 * log10(y) / ymax);
      last = at;
    }
    fputs("\"/>", f);
  }
  fputs("</g></svg>\n", f);
  int result = fclose(f) ? 1 : 0;
  free(ranked);
  return result;
}
} // namespace

void ir_tokenize(const char *text, size_t length, IrTokenCallback callback,
                 void *context) {
  unicode_locale();
  Buffer token;
  size_t width;
  uint32_t previous = 0;
  for (size_t i = 0; i < length; i += width) {
    uint32_t c = decode(text + i, length - i, &width);
    bool keep = word(c);
    if (!keep &&
        (c == '-' || c == '\'' || c == 0x2019 || c == '.' || c == ',') &&
        token.size && i + width < length) {
      size_t next_width;
      uint32_t next = decode(text + i + width, length - i - width, &next_width);
      keep = word(next) &&
             ((c != '.' && c != ',') || (digit(previous) && digit(next)));
    }
    if (keep) {
      uint32_t lc = lower_codepoint(c);
      char bytes[4];
      size_t size = encode(lc, bytes);
      token.append(bytes, size);
      if (c == 0x130) {
        const char dot[] = {char(0xcc), char(0x87)};
        token.append(dot, 2);
      }
      previous = c;
    } else if (token.size) {
      callback(token.data, token.size, context);
      token.clear();
      previous = 0;
    }
  }
  if (token.size)
    callback(token.data, token.size, context);
}
void ir_stem(char *token) {
  lower(token);
  bool ru = false;
  size_t n = strlen(token), w;
  for (size_t i = 0; i < n; i += w) {
    uint32_t c = decode(token + i, n - i, &w);
    if (c >= 0x430 && c <= 0x44f) {
      ru = true;
      break;
    }
  }
  if (ru)
    russian(token);
  else
    english(token);
}
void ir_lemma(char *token) {
  lower(token);
  static const char *exceptions[][2] = {
      {"was", "be"},        {"were", "be"},       {"is", "be"},
      {"are", "be"},        {"been", "be"},       {"has", "have"},
      {"had", "have"},      {"went", "go"},       {"gone", "go"},
      {"men", "man"},       {"people", "person"}, {"children", "child"},
      {"feet", "foot"},     {"mice", "mouse"},    {"люди", "человек"},
      {"людей", "человек"}, {"дети", "ребенок"},  {"детей", "ребенок"},
      {"лет", "год"},       {NULL, NULL}};
  for (size_t i = 0; exceptions[i][0]; i++)
    if (!strcmp(token, exceptions[i][0])) {
      if (strlen(exceptions[i][1]) <= strlen(token))
        strcpy(token, exceptions[i][1]);
      return;
    }
  size_t n = strlen(token);
  if (n > 4 && suffix(token, "ies")) {
    token[n - 3] = 'y';
    token[n - 2] = 0;
    return;
  }
  if (n > 5 && suffix(token, "ing")) {
    token[n - 3] = 0;
    n -= 3;
    if (n > 2 && token[n - 1] == token[n - 2] &&
        strchr("bdgmnprt", token[n - 1]))
      token[--n] = 0;
    return;
  }
  if (n > 4 && suffix(token, "ed")) {
    token[n - 2] = 0;
    n -= 2;
    if (n > 2 && token[n - 1] == token[n - 2] &&
        strchr("bdgmnprt", token[n - 1]))
      token[--n] = 0;
    return;
  }
  if (n > 3 && suffix(token, "s") && !suffix(token, "ss") &&
      !suffix(token, "us") && !suffix(token, "is") && !suffix(token, "ics")) {
    if (suffix(token, "ches") || suffix(token, "shes") || suffix(token, "xes"))
      token[n - 2] = 0;
    else
      token[n - 1] = 0;
    return;
  }
  bool ru = false;
  for (size_t i = 0; i < n; i++)
    if ((unsigned char)token[i] >= 0xd0) {
      ru = true;
      break;
    }
  if (ru)
    ir_stem(token);
}
bool ir_lemma_copy(const char *input, char *output, size_t capacity) {
  Buffer normalized;
  normalized.append(input, strlen(input));
  lower(normalized.data);
  static const char *expanding[][2] = {
      {"has", "have"},      {"had", "have"},     {"люди", "человек"},
      {"людей", "человек"}, {"дети", "ребенок"}, {"детей", "ребенок"},
      {"лет", "год"},       {NULL, NULL}};
  const char *result = NULL;
  for (size_t i = 0; expanding[i][0]; i++)
    if (!strcmp(normalized.data, expanding[i][0])) {
      result = expanding[i][1];
      break;
    }
  if (!result) {
    ir_lemma(normalized.data);
    result = normalized.data;
  }
  size_t n = strlen(result);
  if (n >= capacity)
    return false;
  memcpy(output, result, n + 1);
  return true;
}
int ir_text_main(int argc, char **argv) {
  if (argc < 1)
    return 2;
  if (!strcmp(argv[0], "tokenize")) {
    Buffer input;
    char bytes[65536];
    size_t n;
    while ((n = fread(bytes, 1, sizeof(bytes), stdin)))
      input.append(bytes, n);
    if (ferror(stdin)) {
      perror("stdin");
      return 1;
    }
    ir_tokenize(input.data ? input.data : "", input.size, print_token, NULL);
    return ferror(stdout) ? 1 : 0;
  }
  if (!strcmp(argv[0], "stem") || !strcmp(argv[0], "lemma")) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, stdin)) >= 0) {
      while (n && strchr("\r\n \t", line[n - 1]))
        line[--n] = 0;
      char *start = line;
      while (*start == ' ' || *start == '\t')
        start++;
      if (!*start)
        continue;
      if (!strcmp(argv[0], "stem")) {
        ir_stem(start);
        puts(start);
      } else {
        Buffer result;
        result.reserve(strlen(start) + 64);
        if (!ir_lemma_copy(start, result.data, result.capacity)) {
          fprintf(stderr, "lemma output buffer too small\n");
          free(line);
          return 1;
        }
        puts(result.data);
      }
    }
    free(line);
    return ferror(stdin) || ferror(stdout) ? 1 : 0;
  }
  if (!strcmp(argv[0], "text-stats") && argc == 3) {
    Stats stats;
    double start = now();
    if (!corpus(argv[1], stats))
      return 1;
    return write_stats(argv[2], stats, now() - start);
  }
  if (!strcmp(argv[0], "zipf") && argc == 4)
    return zipf(argv[1], argv[2], argv[3]);
  fprintf(stderr, "usage: tokenize | stem | lemma | text-stats DOCUMENTS_TSV "
                  "OUTPUT_JSON | zipf DOCUMENTS_TSV OUTPUT_JSON OUTPUT_SVG\n");
  return 2;
}
