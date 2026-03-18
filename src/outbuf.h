#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <unistd.h>

namespace cliviz {

// Pre-computed lookup table: uint8_t → decimal ASCII digits + length.
// Fits in L1 cache (1024 bytes). Avoids sprintf entirely in the hot path.
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

// 256KB output buffer, cache-line aligned. Single write() syscall per flush.
struct OutputBuffer {
    static constexpr uint32_t CAPACITY = 1u << 18; // 256KB

    alignas(64) char data[CAPACITY];
    uint32_t len = 0;

    [[nodiscard]] uint32_t size() const { return len; }
    [[nodiscard]] std::string_view view() const { return {data, len}; }

    void clear() { len = 0; }

    void append(const char* s, uint32_t n) {
        std::memcpy(data + len, s, n);
        len += n;
    }

    void append_byte(char c) { data[len++] = c; }

    void append_uint8(uint8_t v) {
        const auto& e = digit_table.entries[v];
        std::memcpy(data + len, e.chars, e.len);
        len += e.len;
    }

    // For row/col values that can exceed 255 (wide terminals)
    void append_uint16(uint16_t v) {
        if (v <= 255) {
            append_uint8(static_cast<uint8_t>(v));
            return;
        }
        // 256..65535: decompose into digits
        char tmp[5];
        int i = 0;
        uint16_t rem = v;
        do {
            tmp[i++] = static_cast<char>('0' + rem % 10);
            rem /= 10;
        } while (rem > 0);
        // Reverse into buffer
        for (int j = i - 1; j >= 0; --j) {
            data[len++] = tmp[j];
        }
    }

    // Single write() syscall — the only I/O in the hot path
    void flush() {
        if (len > 0) {
            ::write(STDOUT_FILENO, data, len);
            len = 0;
        }
    }

    // ── ANSI escape helpers (append only, no I/O) ──

    void emit_sync_start() { append("\x1b[?2026h", 8); }
    void emit_sync_end() { append("\x1b[?2026l", 8); }

    void emit_cursor_to(uint16_t row, uint16_t col) {
        append("\x1b[", 2);
        append_uint16(row);
        append_byte(';');
        append_uint16(col);
        append_byte('H');
    }

    void emit_fg(uint8_t r, uint8_t g, uint8_t b) {
        append("\x1b[38;2;", 7);
        append_uint8(r);
        append_byte(';');
        append_uint8(g);
        append_byte(';');
        append_uint8(b);
        append_byte('m');
    }

    void emit_bg(uint8_t r, uint8_t g, uint8_t b) {
        append("\x1b[48;2;", 7);
        append_uint8(r);
        append_byte(';');
        append_uint8(g);
        append_byte(';');
        append_uint8(b);
        append_byte('m');
    }

    void emit_char(const char* utf8, uint8_t n) { append(utf8, n); }
};

} // namespace cliviz
