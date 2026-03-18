#include <gtest/gtest.h>

#include <cstring>

#include "pixbuf.h"

using namespace cliviz;

// ── Creation ──

TEST(PixelBuffer, CreateReturnsValidBuffer) {
    auto pb = PixelBuffer::create(10, 5);
    ASSERT_NE(pb, nullptr);
    EXPECT_EQ(pb->width, 10u);
    EXPECT_EQ(pb->height, 10u); // 5 terminal rows × 2
    EXPECT_NE(pb->fb, nullptr);
    EXPECT_EQ(pb->fb->width, 10u);
    EXPECT_EQ(pb->fb->height, 5u);
}

TEST(PixelBuffer, PixelsStartZeroed) {
    auto pb = PixelBuffer::create(4, 2);
    for (uint32_t y = 0; y < pb->height; ++y) {
        for (uint32_t x = 0; x < pb->width; ++x) {
            uint32_t idx = (y * pb->width + x) * 3;
            EXPECT_EQ(pb->pixels[idx + 0], 0u);
            EXPECT_EQ(pb->pixels[idx + 1], 0u);
            EXPECT_EQ(pb->pixels[idx + 2], 0u);
        }
    }
}

// ── px_set ──

TEST(PixelBuffer, SetPixelStoresRGB) {
    auto pb = PixelBuffer::create(10, 5);
    pb->set(3, 4, 128, 64, 32);

    uint32_t idx = (4 * pb->width + 3) * 3;
    EXPECT_EQ(pb->pixels[idx + 0], 128u);
    EXPECT_EQ(pb->pixels[idx + 1], 64u);
    EXPECT_EQ(pb->pixels[idx + 2], 32u);
}

TEST(PixelBuffer, SetPixelMarksDirty) {
    auto pb = PixelBuffer::create(10, 5);
    pb->set(3, 4, 128, 64, 32);

    // Pixel row 4 → cell row 2, cell col 3
    uint32_t cell_idx = 2 * 10 + 3;
    EXPECT_NE(pb->fb->dirty_mask[cell_idx >> 6] & (1ULL << (cell_idx & 63)), 0u);
}

// ── px_row ──

TEST(PixelBuffer, RowReturnsPointerToRowData) {
    auto pb = PixelBuffer::create(10, 5);
    pb->set(0, 2, 99, 88, 77);

    uint8_t* row = pb->row(2);
    EXPECT_EQ(row[0], 99u);
    EXPECT_EQ(row[1], 88u);
    EXPECT_EQ(row[2], 77u);
}

// ── encode ──

TEST(PixelBuffer, EncodeMapsPairsToCells) {
    auto pb = PixelBuffer::create(2, 1); // 2 cols, 1 terminal row → 2 pixel rows

    // Set top pixel (row 0) to red
    pb->set(0, 0, 255, 0, 0);
    // Set bottom pixel (row 1) to blue
    pb->set(0, 1, 0, 0, 255);

    pb->fb->mark_all_dirty();
    pb->encode();

    // Cell at (0, 0): fg = top pixel (red), bg = bottom pixel (blue), ch = ▀
    const Cell& c = pb->fb->back[0];
    EXPECT_EQ(c.fg[0], 255u);
    EXPECT_EQ(c.fg[1], 0u);
    EXPECT_EQ(c.fg[2], 0u);
    EXPECT_EQ(c.bg[0], 0u);
    EXPECT_EQ(c.bg[1], 0u);
    EXPECT_EQ(c.bg[2], 255u);
    EXPECT_EQ(c.ch, GLYPH_HALF_UPPER);
}

TEST(PixelBuffer, EncodeOnlyProcessesDirtyCells) {
    auto pb = PixelBuffer::create(4, 2);

    // Set one pixel, which marks one cell dirty
    pb->set(1, 0, 200, 100, 50);
    // Set the corresponding bottom pixel too
    pb->set(1, 1, 100, 50, 25);

    pb->encode();

    // The dirty cell (0, 1) should be encoded
    const Cell& c = pb->fb->back[1]; // cell at col 1, row 0
    EXPECT_EQ(c.fg[0], 200u); // top pixel
    EXPECT_EQ(c.bg[0], 100u); // bottom pixel

    // Other cells should remain zeroed
    const Cell& other = pb->fb->back[2];
    Cell zero{};
    std::memset(&zero, 0, sizeof(Cell));
    EXPECT_EQ(other, zero);
}

// ── Bulk operations ──

TEST(PixelBuffer, ClearFillsAllPixels) {
    auto pb = PixelBuffer::create(4, 2);
    pb->clear(128, 64, 32);

    for (uint32_t y = 0; y < pb->height; ++y) {
        for (uint32_t x = 0; x < pb->width; ++x) {
            uint32_t idx = (y * pb->width + x) * 3;
            EXPECT_EQ(pb->pixels[idx + 0], 128u);
            EXPECT_EQ(pb->pixels[idx + 1], 64u);
            EXPECT_EQ(pb->pixels[idx + 2], 32u);
        }
    }
}

TEST(PixelBuffer, ClearMarksAllDirty) {
    auto pb = PixelBuffer::create(4, 2);
    pb->clear(0, 0, 0);

    // All cells should be dirty
    for (uint32_t i = 0; i < pb->fb->mask_words; ++i) {
        // At least some bits set (all for non-partial words)
        if (i < pb->fb->mask_words - 1) {
            EXPECT_EQ(pb->fb->dirty_mask[i], ~0ULL);
        }
    }
}

TEST(PixelBuffer, FillRectSetsPixelsInRegion) {
    auto pb = PixelBuffer::create(10, 5);
    pb->fill_rect(2, 2, 5, 6, 255, 128, 0);

    // Inside the rect
    uint32_t idx_in = (3 * pb->width + 3) * 3;
    EXPECT_EQ(pb->pixels[idx_in + 0], 255u);
    EXPECT_EQ(pb->pixels[idx_in + 1], 128u);
    EXPECT_EQ(pb->pixels[idx_in + 2], 0u);

    // Outside the rect
    uint32_t idx_out = (0 * pb->width + 0) * 3;
    EXPECT_EQ(pb->pixels[idx_out + 0], 0u);
}

// ── encode_all (fast path) ──

TEST(PixelBuffer, EncodeAllMatchesEncode) {
    auto pb1 = PixelBuffer::create(10, 5);
    auto pb2 = PixelBuffer::create(10, 5);

    // Set identical pixels in both
    for (uint32_t y = 0; y < pb1->height; ++y) {
        for (uint32_t x = 0; x < pb1->width; ++x) {
            auto r = static_cast<uint8_t>((x * 37 + y * 53) % 256);
            auto g = static_cast<uint8_t>((x * 41 + y * 59) % 256);
            auto b = static_cast<uint8_t>((x * 43 + y * 61) % 256);
            pb1->set(x, y, r, g, b);
            pb2->set(x, y, r, g, b);
        }
    }

    pb1->fb->mark_all_dirty();
    pb1->encode();

    pb2->encode_all();

    // Both should produce identical back buffers
    for (uint32_t i = 0; i < pb1->fb->cell_count; ++i) {
        EXPECT_EQ(pb1->fb->back[i], pb2->fb->back[i])
            << "Cell " << i << " differs between encode and encode_all";
    }
}

// ── Full pipeline: set → encode → flush ──

TEST(PixelBuffer, FullPipeline) {
    auto pb = PixelBuffer::create(2, 1);

    pb->set(0, 0, 255, 0, 0);   // top-left top pixel
    pb->set(0, 1, 0, 255, 0);   // top-left bottom pixel
    pb->set(1, 0, 0, 0, 255);   // top-right top pixel
    pb->set(1, 1, 255, 255, 0); // top-right bottom pixel

    pb->encode();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);

    EXPECT_EQ(emitted, 2u);
    EXPECT_GT(buf.size(), 0u);
}
