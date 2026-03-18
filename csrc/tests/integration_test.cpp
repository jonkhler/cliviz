#include <gtest/gtest.h>

#include <string>

#include "framebuf.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "raster.h"
#include "sdf.h"

using namespace cliviz;

// ── Output buffer integrity ──

TEST(Integration, SyncWrapPresent) {
    auto pb = PixelBuffer::create(80, 24);
    pb->clear(15, 15, 25);
    pb->encode();

    OutputBuffer buf;
    pb->fb->flush(buf);

    auto v = buf.view();
    EXPECT_GE(v.size(), 16u);
    // Must start with sync start
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
    // Must end with sync end
    EXPECT_EQ(v.substr(v.size() - 8, 8), "\x1b[?2026l");
}

TEST(Integration, NoStrayNewlines) {
    auto pb = PixelBuffer::create(80, 24);
    pb->clear(100, 50, 25);
    pb->encode();

    OutputBuffer buf;
    pb->fb->flush(buf);

    auto v = buf.view();
    EXPECT_EQ(v.find('\n'), std::string_view::npos);
    EXPECT_EQ(v.find('\r'), std::string_view::npos);
}

// ── Large terminal regression (the 128-byte status bar bug) ──

TEST(Integration, LargeTerminalFullFrame) {
    // Simulate a wide terminal — this is what caught the buffer overread
    auto pb = PixelBuffer::create(200, 50);
    pb->clear(15, 15, 25);
    pb->encode();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);

    EXPECT_EQ(emitted, 200u * 50u);
    auto v = buf.view();
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
    EXPECT_EQ(v.substr(v.size() - 8, 8), "\x1b[?2026l");
}

TEST(Integration, EveryFlushStartsWithSync) {
    auto pb = PixelBuffer::create(40, 10);

    // Frame 1: full clear
    pb->clear(255, 0, 0);
    pb->encode();
    OutputBuffer buf;
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");

    // Frame 2: partial change
    pb->set(5, 5, 0, 255, 0);
    pb->encode();
    buf.clear();
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");

    // Frame 3: no change
    buf.clear();
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");
}

// ── Cursor positions are valid ANSI ──

TEST(Integration, AllCursorPositionsAreOneBased) {
    auto pb = PixelBuffer::create(10, 5);
    pb->clear(100, 100, 100);
    pb->encode();

    OutputBuffer buf;
    pb->fb->flush(buf);

    // Parse all cursor position sequences and verify row,col >= 1
    std::string output(buf.view());
    size_t pos = 0;
    while ((pos = output.find("\x1b[", pos)) != std::string::npos) {
        size_t start = pos + 2;
        size_t semi = output.find(';', start);
        size_t h = output.find('H', start);
        if (semi != std::string::npos && h != std::string::npos &&
            semi < h && h - start < 12) {
            // Check all chars between start and h are digits or semicolon
            bool valid = true;
            for (size_t i = start; i < h; ++i) {
                char c = output[i];
                if (c != ';' && (c < '0' || c > '9')) { valid = false; break; }
            }
            if (valid) {
                int row = std::stoi(output.substr(start, semi - start));
                int col = std::stoi(output.substr(semi + 1, h - semi - 1));
                EXPECT_GE(row, 1) << "Row must be >= 1";
                EXPECT_GE(col, 1) << "Col must be >= 1";
                EXPECT_LE(row, 5) << "Row must be <= height";
                EXPECT_LE(col, 10) << "Col must be <= width";
            }
        }
        pos += 2;
    }
}

// ── Full render pipeline at large size ──

TEST(Integration, RasterPipelineLargeTerminal) {
    auto pb = PixelBuffer::create(160, 50);
    ZBuffer zb(pb->width, pb->height);
    Mesh cube = make_cube();

    mat4 view = mat4::look_at({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
    float aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);

    pb->clear(0, 0, 0);
    zb.clear();
    rasterize(cube, proj * view, *pb, zb);
    pb->encode();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);
    EXPECT_GT(emitted, 0u);

    auto v = buf.view();
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
    EXPECT_EQ(v.substr(v.size() - 8, 8), "\x1b[?2026l");
}

TEST(Integration, SdfPipelineLargeTerminal) {
    auto pb = PixelBuffer::create(160, 50);
    sdf_render(*pb, sdf_scene_default, 0.0f,
               {0, 2, 5}, {0, 0, 0}, {0, 1, 0});
    pb->encode_all();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);
    EXPECT_EQ(emitted, 160u * 50u);

    auto v = buf.view();
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
}

// ── OutputBuffer auto-flush doesn't break sync ──

TEST(Integration, AppendNearCapacityDoesNotOverflow) {
    OutputBuffer buf;
    // Fill to exactly capacity - 5
    constexpr uint32_t FILL = OutputBuffer::CAPACITY - 5;
    for (uint32_t i = 0; i < FILL; ++i) {
        buf.append_byte('A');
    }
    EXPECT_EQ(buf.size(), FILL);

    // This fits exactly
    buf.append("HELLO", 5);
    EXPECT_EQ(buf.size(), OutputBuffer::CAPACITY);

    // Next append triggers auto-flush (write to stdout), then appends
    buf.append("X", 1);
    // After flush: len was reset to 0, then "X" appended
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.view(), "X");
}
