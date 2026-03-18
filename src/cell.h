#pragma once

#include <cstdint>

namespace cliviz {

// 8 bytes packed — fits in a single uint64_t compare.
// SIMD can compare 4 cells at once with 256-bit NEON/AVX2 registers.
struct __attribute__((packed)) Cell {
    uint8_t fg[3]; // foreground RGB
    uint8_t bg[3]; // background RGB
    uint16_t ch;   // character index: 0=space, 1=▀, 2=▄

    bool operator==(const Cell& o) const {
        uint64_t a, b;
        __builtin_memcpy(&a, this, 8);
        __builtin_memcpy(&b, &o, 8);
        return a == b;
    }

    bool operator!=(const Cell& o) const { return !(*this == o); }
};

static_assert(sizeof(Cell) == 8, "Cell must be exactly 8 bytes");

// Well-known glyph indices
inline constexpr uint16_t GLYPH_SPACE = 0;
inline constexpr uint16_t GLYPH_HALF_UPPER = 1; // ▀
inline constexpr uint16_t GLYPH_HALF_LOWER = 2; // ▄

// Map glyph index → UTF-8 bytes + length
struct GlyphEntry {
    char utf8[4];
    uint8_t len;
};

inline constexpr GlyphEntry glyph_table[] = {
    {{' ', '\0', '\0', '\0'}, 1},                          // 0: space
    {{'\xe2', '\x96', '\x80', '\0'}, 3},                   // 1: ▀
    {{'\xe2', '\x96', '\x84', '\0'}, 3},                   // 2: ▄
};

inline constexpr uint32_t GLYPH_COUNT = sizeof(glyph_table) / sizeof(glyph_table[0]);

} // namespace cliviz
