#include "outbuf.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cliviz {

uint8_t rgb_to_256(uint8_t r, uint8_t g, uint8_t b) {
    // Check if it's close to a grayscale value
    if (r == g && g == b) {
        if (r < 8) return 16;     // black end of cube
        if (r > 248) return 231;  // white end of cube
        // Map to grayscale ramp (232-255, 24 steps)
        return static_cast<uint8_t>(232 + std::lround((r - 8.0) / 247.0 * 23.0));
    }

    // Near-gray: if all channels within 10 of each other, use grayscale
    int max_diff = std::max({std::abs(r - g), std::abs(g - b), std::abs(r - b)});
    if (max_diff <= 10) {
        int avg = (r + g + b) / 3;
        if (avg < 8) return 16;
        if (avg > 248) return 231;
        return static_cast<uint8_t>(232 + std::lround((avg - 8.0) / 247.0 * 23.0));
    }

    // Map to 6×6×6 color cube (indices 16-231)
    auto to6 = [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(std::lround(v / 255.0 * 5.0));
    };
    return static_cast<uint8_t>(16 + 36 * to6(r) + 6 * to6(g) + to6(b));
}

ColorMode detect_color_mode() {
    // Check $COLORTERM first (most reliable)
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm) {
        if (std::strcmp(colorterm, "truecolor") == 0 ||
            std::strcmp(colorterm, "24bit") == 0) {
            return ColorMode::TrueColor;
        }
    }

    // Check $TERM_PROGRAM for known truecolor terminals
    const char* term_prog = std::getenv("TERM_PROGRAM");
    if (term_prog) {
        if (std::strcmp(term_prog, "ghostty") == 0 ||
            std::strcmp(term_prog, "WezTerm") == 0 ||
            std::strstr(term_prog, "iTerm") != nullptr) {
            return ColorMode::TrueColor;
        }
    }

    // Check $TERM for kitty (sets TERM=xterm-kitty)
    const char* term = std::getenv("TERM");
    if (term && std::strstr(term, "kitty") != nullptr) {
        return ColorMode::TrueColor;
    }

    // Default: 256-color (safe for Terminal.app and most terminals)
    return ColorMode::Color256;
}

} // namespace cliviz
