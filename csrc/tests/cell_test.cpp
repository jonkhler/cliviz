#include <gtest/gtest.h>

#include "cell.h"

using namespace cliviz;

TEST(Cell, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(Cell), 8u);
}

TEST(Cell, EqualCellsCompareEqual) {
    Cell a{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    Cell b{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    EXPECT_EQ(a, b);
}

TEST(Cell, DifferentFgCompareUnequal) {
    Cell a{{255, 0, 0}, {0, 0, 0}, GLYPH_SPACE};
    Cell b{{254, 0, 0}, {0, 0, 0}, GLYPH_SPACE};
    EXPECT_NE(a, b);
}

TEST(Cell, DifferentBgCompareUnequal) {
    Cell a{{0, 0, 0}, {0, 0, 255}, GLYPH_SPACE};
    Cell b{{0, 0, 0}, {0, 0, 254}, GLYPH_SPACE};
    EXPECT_NE(a, b);
}

TEST(Cell, DifferentGlyphCompareUnequal) {
    Cell a{{0, 0, 0}, {0, 0, 0}, GLYPH_SPACE};
    Cell b{{0, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    EXPECT_NE(a, b);
}

TEST(Cell, GlyphTableHasCorrectEntries) {
    EXPECT_GE(GLYPH_COUNT, 3u);
    EXPECT_EQ(glyph_table[GLYPH_SPACE].utf8[0], ' ');
    EXPECT_EQ(glyph_table[GLYPH_SPACE].len, 1u);
    EXPECT_EQ(glyph_table[GLYPH_HALF_UPPER].len, 3u);
    EXPECT_EQ(glyph_table[GLYPH_HALF_LOWER].len, 3u);
}
