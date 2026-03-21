#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

#include "outbuf.h"

using namespace cliviz;

// ── Construction ──

TEST(OutputBuffer, StartsEmpty) {
    OutputBuffer buf;
    EXPECT_EQ(buf.size(), 0u);
}

// ── Append bytes ──

TEST(OutputBuffer, AppendSingleByte) {
    OutputBuffer buf;
    buf.append_byte('A');
    EXPECT_EQ(buf.size(), 1u);
    EXPECT_EQ(buf.view(), "A");
}

TEST(OutputBuffer, AppendMultipleBytes) {
    OutputBuffer buf;
    const char* text = "hello";
    buf.append(text, 5);
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(buf.view(), "hello");
}

TEST(OutputBuffer, AppendAccumulates) {
    OutputBuffer buf;
    buf.append("ab", 2);
    buf.append("cd", 2);
    buf.append_byte('e');
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(buf.view(), "abcde");
}

TEST(OutputBuffer, ClearResetsSize) {
    OutputBuffer buf;
    buf.append("data", 4);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
}

// ── uint8 to ASCII ──

TEST(OutputBuffer, Uint8ToStr_Zero) {
    OutputBuffer buf;
    buf.append_uint8(0);
    EXPECT_EQ(buf.view(), "0");
}

TEST(OutputBuffer, Uint8ToStr_SingleDigit) {
    OutputBuffer buf;
    buf.append_uint8(7);
    EXPECT_EQ(buf.view(), "7");
}

TEST(OutputBuffer, Uint8ToStr_TwoDigits) {
    OutputBuffer buf;
    buf.append_uint8(42);
    EXPECT_EQ(buf.view(), "42");
}

TEST(OutputBuffer, Uint8ToStr_ThreeDigits) {
    OutputBuffer buf;
    buf.append_uint8(255);
    EXPECT_EQ(buf.view(), "255");
}

TEST(OutputBuffer, Uint8ToStr_AllValues) {
    // Verify every value 0-255 produces the correct decimal string
    for (uint32_t i = 0; i <= 255; ++i) {
        OutputBuffer buf;
        buf.append_uint8(static_cast<uint8_t>(i));
        EXPECT_EQ(buf.view(), std::to_string(i)) << "Failed for value " << i;
    }
}

// ── ANSI escape sequences ──

TEST(OutputBuffer, EmitCursorTo) {
    OutputBuffer buf;
    buf.emit_cursor_to(1, 1);
    EXPECT_EQ(buf.view(), "\x1b[1;1H");
}

TEST(OutputBuffer, EmitCursorTo_LargeCoords) {
    OutputBuffer buf;
    buf.emit_cursor_to(100, 200);
    EXPECT_EQ(buf.view(), "\x1b[100;200H");
}

TEST(OutputBuffer, EmitCursorTo_Beyond255) {
    OutputBuffer buf;
    buf.emit_cursor_to(1, 300);
    EXPECT_EQ(buf.view(), "\x1b[1;300H");
}

TEST(OutputBuffer, EmitFg) {
    OutputBuffer buf;
    buf.emit_fg(255, 128, 0);
    EXPECT_EQ(buf.view(), "\x1b[38;2;255;128;0m");
}

TEST(OutputBuffer, EmitBg) {
    OutputBuffer buf;
    buf.emit_bg(0, 64, 128);
    EXPECT_EQ(buf.view(), "\x1b[48;2;0;64;128m");
}

TEST(OutputBuffer, EmitSyncStart) {
    OutputBuffer buf;
    buf.emit_sync_start();
    EXPECT_EQ(buf.view(), "\x1b[?2026h");
}

TEST(OutputBuffer, EmitSyncEnd) {
    OutputBuffer buf;
    buf.emit_sync_end();
    EXPECT_EQ(buf.view(), "\x1b[?2026l");
}

TEST(OutputBuffer, EmitChar_Ascii) {
    OutputBuffer buf;
    buf.emit_char("X", 1);
    EXPECT_EQ(buf.view(), "X");
}

TEST(OutputBuffer, EmitChar_Utf8HalfBlock) {
    OutputBuffer buf;
    // ▀ is U+2580, UTF-8: E2 96 80
    buf.emit_char("\xe2\x96\x80", 3);
    EXPECT_EQ(buf.view(), "▀");
}

// ── Composed sequence correctness ──

TEST(OutputBuffer, FullCellEmission) {
    // Simulate emitting one colored half-block cell at position (5, 10)
    OutputBuffer buf;
    buf.emit_cursor_to(5, 10);
    buf.emit_fg(255, 0, 0);
    buf.emit_bg(0, 0, 255);
    buf.emit_char("\xe2\x96\x80", 3);

    std::string expected =
        "\x1b[5;10H"
        "\x1b[38;2;255;0;0m"
        "\x1b[48;2;0;0;255m"
        "\xe2\x96\x80";
    EXPECT_EQ(buf.view(), expected);
}

// ── Dynamic capacity ──

TEST(OutputBuffer, DynamicCapacity) {
    OutputBuffer buf(512);
    EXPECT_EQ(buf.capacity, 512u);
    EXPECT_EQ(buf.size(), 0u);
}

TEST(OutputBuffer, CapacityForCells) {
    uint32_t cap = OutputBuffer::capacity_for_cells(100);
    // 100 cells × 53 bytes + 16 sync bytes
    EXPECT_EQ(cap, 100u * 53u + 16u);
}

TEST(OutputBuffer, CapacityForCellsLargeTerminal) {
    // 518 cols × 160 rows = 82,880 cells — fits without overflow
    uint32_t cells = 518u * 160u;
    uint32_t cap = OutputBuffer::capacity_for_cells(cells);
    EXPECT_EQ(cap, cells * 53u + 16u);
    EXPECT_GT(cap, 4u * 1024u * 1024u); // > 4MB
}

TEST(OutputBuffer, AutoFlushAtDynamicCapacity) {
    // Small buffer — auto-flush triggers at the right boundary
    OutputBuffer buf(64);
    for (uint32_t i = 0; i < 64; ++i) {
        buf.append_byte('A');
    }
    EXPECT_EQ(buf.size(), 64u);
    // One more byte triggers auto-flush (write to stdout) + appends
    buf.append_byte('X');
    EXPECT_EQ(buf.size(), 1u);
}

// ── Capacity ──

TEST(OutputBuffer, LargeAppendFitsInBuffer) {
    OutputBuffer buf;
    // Fill with a known pattern up to a reasonable size
    constexpr uint32_t fill_size = 100'000;
    for (uint32_t i = 0; i < fill_size; ++i) {
        buf.append_byte(static_cast<char>('A' + (i % 26)));
    }
    EXPECT_EQ(buf.size(), fill_size);
}
