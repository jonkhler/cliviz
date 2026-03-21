#include <gtest/gtest.h>

#include <string>

#include "framebuf.h"
#include "outbuf.h"
#include "pixbuf.h"

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
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
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

TEST(Integration, LargeTerminalFullFrame) {
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

    pb->clear(255, 0, 0);
    pb->encode();
    OutputBuffer buf;
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");

    pb->set(5, 5, 0, 255, 0);
    pb->encode();
    buf.clear();
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");

    buf.clear();
    pb->fb->flush(buf);
    EXPECT_EQ(buf.view().substr(0, 8), "\x1b[?2026h");
}

TEST(Integration, AllCursorPositionsAreOneBased) {
    auto pb = PixelBuffer::create(10, 5);
    pb->clear(100, 100, 100);
    pb->encode();

    OutputBuffer buf;
    pb->fb->flush(buf);

    std::string output(buf.view());
    size_t pos = 0;
    while ((pos = output.find("\x1b[", pos)) != std::string::npos) {
        size_t start = pos + 2;
        size_t semi = output.find(';', start);
        size_t h = output.find('H', start);
        if (semi != std::string::npos && h != std::string::npos &&
            semi < h && h - start < 12) {
            bool valid = true;
            for (size_t i = start; i < h; ++i) {
                char c = output[i];
                if (c != ';' && (c < '0' || c > '9')) { valid = false; break; }
            }
            if (valid) {
                int row = std::stoi(output.substr(start, semi - start));
                int col = std::stoi(output.substr(semi + 1, h - semi - 1));
                EXPECT_GE(row, 1);
                EXPECT_GE(col, 1);
                EXPECT_LE(row, 5);
                EXPECT_LE(col, 10);
            }
        }
        pos += 2;
    }
}

TEST(Integration, AppendNearCapacityDoesNotOverflow) {
    OutputBuffer buf;
    const uint32_t FILL = buf.capacity - 5;
    for (uint32_t i = 0; i < FILL; ++i) {
        buf.append_byte('A');
    }
    EXPECT_EQ(buf.size(), FILL);

    buf.append("HELLO", 5);
    EXPECT_EQ(buf.size(), buf.capacity);

    buf.append("X", 1);
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.view(), "X");
}
