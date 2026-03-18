#include <gtest/gtest.h>

#include <string>

#include "framebuf.h"
#include "outbuf.h"
#include "pixbuf.h"

using namespace cliviz;

// Reproduce the plasma artifacts: full-frame encode_all + diff flush
// should emit ALL cells on the first frame (front is zeroed, back has data).

TEST(DiffRegression, FirstFrameEmitsAllCells) {
    auto pb = PixelBuffer::create(20, 10);
    // Fill with a non-trivial color pattern
    for (uint32_t y = 0; y < pb->height; ++y) {
        for (uint32_t x = 0; x < pb->width; ++x) {
            pb->set(x, y,
                    static_cast<uint8_t>((x * 37 + y * 53) % 256),
                    static_cast<uint8_t>((x * 41 + y * 59) % 256),
                    static_cast<uint8_t>((x * 43 + y * 61) % 256));
        }
    }
    pb->encode_all();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);

    // Every cell should be emitted on first frame
    EXPECT_EQ(emitted, 20u * 10u) << "First frame should emit all cells";
}

TEST(DiffRegression, SecondFrameSkipsUnchangedCells) {
    auto pb = PixelBuffer::create(10, 5);
    pb->clear(100, 100, 100);
    pb->encode_all();

    OutputBuffer buf;
    pb->fb->flush(buf);

    // Second frame: same pixels
    pb->clear(100, 100, 100);
    pb->encode_all();

    buf.clear();
    uint32_t emitted = pb->fb->flush(buf);

    // Nothing changed, so nothing emitted (except sync wrapping)
    EXPECT_EQ(emitted, 0u) << "Unchanged cells should be skipped";
}

TEST(DiffRegression, SecondFrameEmitsChangedCellsOnly) {
    auto pb = PixelBuffer::create(10, 5);
    pb->clear(100, 100, 100);
    pb->encode_all();

    OutputBuffer buf;
    pb->fb->flush(buf);

    // Second frame: change one pixel
    pb->clear(100, 100, 100);
    pb->set(5, 5, 200, 0, 0);  // one red pixel
    pb->encode_all();

    buf.clear();
    uint32_t emitted = pb->fb->flush(buf);

    // Only the cell containing the changed pixel should be emitted
    // (pixel row 5 → cell row 2, so cell at (2, 5) changed)
    EXPECT_GE(emitted, 1u);
    EXPECT_LE(emitted, 2u); // might affect 1 or 2 cells depending on pixel pair
}

TEST(DiffRegression, DrawTextAfterEncodeIsPreserved) {
    auto pb = PixelBuffer::create(20, 5);
    pb->clear(50, 50, 50);
    pb->encode_all();
    pb->draw_text(0, 0, "FPS", 255, 255, 255, 0, 0, 0);

    OutputBuffer buf;
    pb->fb->flush(buf);

    std::string output(buf.view());
    // Should contain 'F', 'P', 'S' characters
    EXPECT_NE(output.find('F'), std::string::npos);
    EXPECT_NE(output.find('P'), std::string::npos);
    EXPECT_NE(output.find('S'), std::string::npos);

    // On second frame, same pattern: text should still appear
    pb->clear(50, 50, 50);
    pb->encode_all();
    pb->draw_text(0, 0, "FPS", 255, 255, 255, 0, 0, 0);

    buf.clear();
    uint32_t emitted2 = pb->fb->flush(buf);
    output = std::string(buf.view());

    // Text cells haven't changed between frame 1 and 2 → diff skips them.
    // Pixel cells also unchanged (same clear color). So very few or zero emitted.
    EXPECT_LE(emitted2, 3u); // at most the 3 text cells if text differs
}

TEST(DiffRegression, LargeTerminalFirstFrame) {
    // Simulate the user's actual terminal size
    auto pb = PixelBuffer::create(158, 54);
    pb->clear(15, 15, 25);
    pb->encode_all();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);

    EXPECT_EQ(emitted, 158u * 54u) << "All cells must be emitted on first frame at large size";

    // Verify output starts with sync and is well-formed
    auto v = buf.view();
    EXPECT_EQ(v.substr(0, 8), "\x1b[?2026h");
    EXPECT_EQ(v.substr(v.size() - 8, 8), "\x1b[?2026l");
}
