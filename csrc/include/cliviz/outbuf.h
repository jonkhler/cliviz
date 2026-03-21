#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unistd.h>

namespace cliviz {

// ── Color mode ──

enum class ColorMode : uint8_t {
    TrueColor, // 24-bit: \e[38;2;R;G;Bm
    Color256,  // 8-bit:  \e[38;5;Nm
};

// Auto-detect from $COLORTERM, $TERM_PROGRAM, $TERM
ColorMode detect_color_mode();

// Map RGB → nearest 256-color index (6×6×6 cube + 24 grayscale)
uint8_t rgb_to_256(uint8_t r, uint8_t g, uint8_t b);

// Pre-computed lookup table: uint8_t → decimal ASCII digits + length.
struct DigitEntry {
    char chars[3];
    uint8_t len;
};

inline constexpr auto build_digit_table() {
    struct Table {
        DigitEntry entries[256]{};
    } t{};
    for (int i = 0; i < 256; ++i) {
        if (i < 10) {
            t.entries[i] = {
                {static_cast<char>('0' + i), '\0', '\0'}, 1};
        } else if (i < 100) {
            t.entries[i] = {
                {static_cast<char>('0' + i / 10),
                 static_cast<char>('0' + i % 10), '\0'},
                2};
        } else {
            t.entries[i] = {
                {static_cast<char>('0' + i / 100),
                 static_cast<char>('0' + (i / 10) % 10),
                 static_cast<char>('0' + i % 10)},
                3};
        }
    }
    return t;
}

inline constexpr auto digit_table = build_digit_table();

// Dynamic output buffer — heap-allocated, sized per terminal.
// Single write() syscall per flush; auto-flushes when full.
struct OutputBuffer {
    // Default capacity for non-terminal uses (C++ demos, tests).
    static constexpr uint32_t DEFAULT_CAPACITY = 1u << 20; // 1MB

    // Worst-case bytes per cell: cursor(12) + fg(19) + bg(19) + char(3) = 53
    static constexpr uint32_t BYTES_PER_CELL = 53u;

    // Compute capacity needed for a given cell count.
    static constexpr uint32_t capacity_for_cells(uint32_t cell_count) {
        return cell_count * BYTES_PER_CELL + 16u; // +16 for sync markers
    }

    char*    data;
    uint32_t capacity;
    uint32_t len = 0;
    ColorMode color_mode = ColorMode::TrueColor;

    explicit OutputBuffer(uint32_t cap = DEFAULT_CAPACITY)
        : data(new char[cap]), capacity(cap) {}

    ~OutputBuffer() { delete[] data; }

    OutputBuffer(const OutputBuffer&)            = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;

    OutputBuffer(OutputBuffer&& o) noexcept
        : data(o.data), capacity(o.capacity), len(o.len), color_mode(o.color_mode) {
        o.data = nullptr; o.capacity = 0; o.len = 0;
    }

    [[nodiscard]] uint32_t size() const { return len; }
    [[nodiscard]] std::string_view view() const { return {data, len}; }
    [[nodiscard]] uint32_t remaining() const { return capacity - len; }

    void clear() { len = 0; }

    void append(const char* s, uint32_t n) {
        if (len + n > capacity) flush();
        std::memcpy(data + len, s, n);
        len += n;
    }

    void append_byte(char c) {
        if (len >= capacity) flush();
        data[len++] = c;
    }

    void append_uint8(uint8_t v) {
        const auto& e = digit_table.entries[v];
        std::memcpy(data + len, e.chars, e.len);
        len += e.len;
    }

    void append_uint16(uint16_t v) {
        if (v <= 255) {
            append_uint8(static_cast<uint8_t>(v));
            return;
        }
        char tmp[5];
        int i = 0;
        uint16_t rem = v;
        do {
            tmp[i++] = static_cast<char>('0' + rem % 10);
            rem /= 10;
        } while (rem > 0);
        for (int j = i - 1; j >= 0; --j) {
            data[len++] = tmp[j];
        }
    }

    void flush() {
        if (len > 0) {
            ::write(STDOUT_FILENO, data, len);
            len = 0;
        }
    }

    // ── ANSI escape helpers ──

    void emit_sync_start() { append("\x1b[?2026h", 8); }
    void emit_sync_end()   { append("\x1b[?2026l", 8); }

    void emit_cursor_to(uint16_t row, uint16_t col) {
        append("\x1b[", 2);
        append_uint16(row);
        append_byte(';');
        append_uint16(col);
        append_byte('H');
    }

    void emit_fg(uint8_t r, uint8_t g, uint8_t b) {
        if (color_mode == ColorMode::Color256) {
            emit_fg_256(rgb_to_256(r, g, b));
        } else {
            append("\x1b[38;2;", 7);
            append_uint8(r); append_byte(';');
            append_uint8(g); append_byte(';');
            append_uint8(b); append_byte('m');
        }
    }

    void emit_bg(uint8_t r, uint8_t g, uint8_t b) {
        if (color_mode == ColorMode::Color256) {
            emit_bg_256(rgb_to_256(r, g, b));
        } else {
            append("\x1b[48;2;", 7);
            append_uint8(r); append_byte(';');
            append_uint8(g); append_byte(';');
            append_uint8(b); append_byte('m');
        }
    }

    void emit_fg_256(uint8_t idx) {
        append("\x1b[38;5;", 7);
        append_uint8(idx);
        append_byte('m');
    }

    void emit_bg_256(uint8_t idx) {
        append("\x1b[48;5;", 7);
        append_uint8(idx);
        append_byte('m');
    }

    void emit_char(const char* utf8, uint8_t n) { append(utf8, n); }
};

} // namespace cliviz
