#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "cell.h"
#include "framebuf.h"

namespace cliviz {

struct PixelBuffer {
    uint32_t width = 0;    // terminal cols
    uint32_t height = 0;   // terminal rows × 2 (sub-pixel vertical resolution)

    uint8_t* pixels = nullptr; // RGB, row-major, [height][width][3]
    std::unique_ptr<Framebuffer> fb;

    // Create a pixel buffer for the given terminal dimensions.
    // Pixel height = term_rows × 2 (half-block encoding).
    static std::unique_ptr<PixelBuffer> create(uint32_t term_cols, uint32_t term_rows);

    // Set a pixel and mark the corresponding cell dirty.
    void set(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);

    // Get pointer to a pixel row's RGB data.
    uint8_t* row(uint32_t y);

    // Mark all cells overlapping a pixel row dirty.
    void mark_row_dirty(uint32_t y);

    // Mark all cells overlapping a pixel rect dirty.
    void mark_rect_dirty(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);

    // Encode dirty pixel pairs → cells in the framebuffer back buffer.
    void encode();

    // Fast path: encode all cells (skip dirty mask scan).
    // Use when you know the entire buffer was rewritten (e.g., after SDF render).
    void encode_all();

    // Fill entire pixel buffer with solid color, mark all dirty.
    void clear(uint8_t r, uint8_t g, uint8_t b);

    // Fill a rectangle of pixels, mark affected cells dirty.
    void fill_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                   uint8_t r, uint8_t g, uint8_t b);

    ~PixelBuffer();

private:
    PixelBuffer() = default;
};

} // namespace cliviz
