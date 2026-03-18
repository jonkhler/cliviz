#include <gtest/gtest.h>

#include <string>

#include "framebuf.h"

using namespace cliviz;

// ── Creation ──

TEST(Framebuffer, CreateReturnsValidBuffer) {
    auto fb = Framebuffer::create(10, 5);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->width, 10u);
    EXPECT_EQ(fb->height, 5u);
    EXPECT_EQ(fb->cell_count, 50u);
}

TEST(Framebuffer, FrontAndBackStartZeroed) {
    auto fb = Framebuffer::create(4, 2);
    Cell zero{};
    std::memset(&zero, 0, sizeof(Cell));
    for (uint32_t i = 0; i < fb->cell_count; ++i) {
        EXPECT_EQ(fb->front[i], zero);
        EXPECT_EQ(fb->back[i], zero);
    }
}

// ── Set + Dirty ──

TEST(Framebuffer, SetMarkesCellDirty) {
    auto fb = Framebuffer::create(10, 5);
    Cell c{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(2, 3, c);

    // The cell should be in the back buffer
    uint32_t idx = 2 * 10 + 3;
    EXPECT_EQ(fb->back[idx], c);

    // The dirty bit should be set
    EXPECT_NE(fb->dirty_mask[idx >> 6] & (1ULL << (idx & 63)), 0u);
}

TEST(Framebuffer, SetDoesNotTouchFront) {
    auto fb = Framebuffer::create(10, 5);
    Cell c{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c);

    Cell zero{};
    std::memset(&zero, 0, sizeof(Cell));
    EXPECT_EQ(fb->front[0], zero);
}

// ── Flush ──

TEST(Framebuffer, FlushEmitsChangedCells) {
    auto fb = Framebuffer::create(4, 2);
    Cell red{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(0, 0, red);

    OutputBuffer buf;
    uint32_t emitted = fb->flush(buf);

    EXPECT_EQ(emitted, 1u);
    EXPECT_GT(buf.size(), 0u);

    // After flush, front should match back
    EXPECT_EQ(fb->front[0], red);
}

TEST(Framebuffer, FlushSkipsUnchangedCells) {
    auto fb = Framebuffer::create(4, 2);
    // Don't change anything

    OutputBuffer buf;
    uint32_t emitted = fb->flush(buf);

    // Only sync start/end, no cell emissions
    EXPECT_EQ(emitted, 0u);
}

TEST(Framebuffer, FlushSkipsFalseDirty) {
    auto fb = Framebuffer::create(4, 2);
    // Set a cell to the same value it already has (zero)
    Cell zero{};
    std::memset(&zero, 0, sizeof(Cell));
    fb->set(0, 0, zero);

    OutputBuffer buf;
    uint32_t emitted = fb->flush(buf);

    // Dirty bit was set, but back == front, so nothing emitted
    EXPECT_EQ(emitted, 0u);
}

TEST(Framebuffer, FlushMultipleCells) {
    auto fb = Framebuffer::create(10, 5);
    Cell red{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    Cell blue{{0, 0, 255}, {0, 0, 0}, GLYPH_HALF_LOWER};

    fb->set(0, 0, red);
    fb->set(0, 5, blue);
    fb->set(3, 7, red);

    OutputBuffer buf;
    uint32_t emitted = fb->flush(buf);

    EXPECT_EQ(emitted, 3u);
}

TEST(Framebuffer, FlushClearsDirtyMask) {
    auto fb = Framebuffer::create(4, 2);
    Cell c{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c);

    OutputBuffer buf;
    fb->flush(buf);

    // Second flush with no changes: nothing emitted
    buf.clear();
    uint32_t emitted = fb->flush(buf);
    EXPECT_EQ(emitted, 0u);
}

TEST(Framebuffer, FlushEmitsSyncWrap) {
    auto fb = Framebuffer::create(4, 2);
    Cell c{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c);

    OutputBuffer buf;
    fb->flush(buf);

    std::string output(buf.view());
    // Should start with sync start and end with sync end
    EXPECT_NE(output.find("\x1b[?2026h"), std::string::npos);
    EXPECT_NE(output.find("\x1b[?2026l"), std::string::npos);
}

TEST(Framebuffer, FlushEmitsCorrectColorEscapes) {
    auto fb = Framebuffer::create(4, 2);
    Cell c{{128, 64, 32}, {10, 20, 30}, GLYPH_SPACE};
    fb->set(0, 0, c);

    OutputBuffer buf;
    fb->flush(buf);

    std::string output(buf.view());
    EXPECT_NE(output.find("\x1b[38;2;128;64;32m"), std::string::npos);
    EXPECT_NE(output.find("\x1b[48;2;10;20;30m"), std::string::npos);
}

// ── Mark all dirty ──

TEST(Framebuffer, MarkAllDirty) {
    auto fb = Framebuffer::create(10, 5);
    // Set some cells in back to differ from front
    Cell c{{1, 2, 3}, {4, 5, 6}, GLYPH_HALF_UPPER};
    for (uint32_t i = 0; i < fb->cell_count; ++i) {
        fb->back[i] = c;
    }

    fb->mark_all_dirty();

    OutputBuffer buf;
    uint32_t emitted = fb->flush(buf);

    EXPECT_EQ(emitted, 50u); // all cells changed
}

// ── Perceptual delta skip ──

TEST(Framebuffer, PerceptualSkipIgnoresSmallChanges) {
    auto fb = Framebuffer::create(4, 2);
    Cell c1{{100, 100, 100}, {50, 50, 50}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c1);

    OutputBuffer buf;
    fb->flush(buf);

    // Now change by a small delta (below threshold of 4)
    Cell c2{{102, 101, 100}, {51, 50, 49}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c2);

    buf.clear();
    uint32_t emitted = fb->flush(buf, 4);
    EXPECT_EQ(emitted, 0u); // skipped due to perceptual threshold
}

TEST(Framebuffer, PerceptualSkipEmitsLargeChanges) {
    auto fb = Framebuffer::create(4, 2);
    Cell c1{{100, 100, 100}, {50, 50, 50}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c1);

    OutputBuffer buf;
    fb->flush(buf);

    // Change by a larger delta (above threshold)
    Cell c2{{110, 100, 100}, {50, 50, 50}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c2);

    buf.clear();
    uint32_t emitted = fb->flush(buf, 4);
    EXPECT_EQ(emitted, 1u);
}

TEST(Framebuffer, PerceptualSkipAlwaysEmitsGlyphChange) {
    auto fb = Framebuffer::create(4, 2);
    Cell c1{{100, 100, 100}, {50, 50, 50}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c1);

    OutputBuffer buf;
    fb->flush(buf);

    // Same colors but different glyph — must emit
    Cell c2{{100, 100, 100}, {50, 50, 50}, GLYPH_HALF_LOWER};
    fb->set(0, 0, c2);

    buf.clear();
    uint32_t emitted = fb->flush(buf, 4);
    EXPECT_EQ(emitted, 1u);
}

// Cursor position is now always emitted for portability (some terminals
// treat ▀ as width 2), so we verify each cell gets its own cursor move.
TEST(Framebuffer, EachCellGetsExplicitCursorPosition) {
    auto fb = Framebuffer::create(10, 1);
    Cell c{{255, 0, 0}, {0, 0, 0}, GLYPH_HALF_UPPER};
    fb->set(0, 0, c);
    fb->set(0, 1, c);

    OutputBuffer buf;
    fb->flush(buf);

    std::string output(buf.view());
    size_t cursor_moves = 0;
    size_t pos = 0;
    while ((pos = output.find("\x1b[", pos)) != std::string::npos) {
        size_t start = pos + 2;
        bool valid = true;
        bool has_semi = false;
        size_t i = start;
        for (; i < output.size(); ++i) {
            char ch = output[i];
            if (ch == 'H') break;
            if (ch == ';') { has_semi = true; continue; }
            if (ch >= '0' && ch <= '9') continue;
            valid = false;
            break;
        }
        if (valid && has_semi && i < output.size() && output[i] == 'H') {
            cursor_moves++;
        }
        pos += 2;
    }
    EXPECT_EQ(cursor_moves, 2u);
}
