#include "pixbuf.h"

#include <cstdlib>
#include <cstring>

namespace cliviz {

std::unique_ptr<PixelBuffer> PixelBuffer::create(uint32_t term_cols, uint32_t term_rows) {
    auto pb = std::unique_ptr<PixelBuffer>(new PixelBuffer());
    pb->width = term_cols;
    pb->height = term_rows * 2;
    pb->fb = Framebuffer::create(term_cols, term_rows);
    if (!pb->fb) return nullptr;

    size_t pixel_bytes = static_cast<size_t>(pb->width) * pb->height * 3;
    void* mem = nullptr;
    if (posix_memalign(&mem, 64, pixel_bytes) != 0) return nullptr;
    std::memset(mem, 0, pixel_bytes);
    pb->pixels = static_cast<uint8_t*>(mem);

    return pb;
}

PixelBuffer::~PixelBuffer() {
    std::free(pixels);
}

void PixelBuffer::set(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t idx = (y * width + x) * 3;
    pixels[idx + 0] = r;
    pixels[idx + 1] = g;
    pixels[idx + 2] = b;

    uint32_t cell_row = y >> 1;
    uint32_t cell_col = x;
    uint32_t cell_idx = cell_row * fb->width + cell_col;
    fb->dirty_mask[cell_idx >> 6] |= (1ULL << (cell_idx & 63));
}

uint8_t* PixelBuffer::row(uint32_t y) {
    return pixels + y * width * 3;
}

void PixelBuffer::mark_row_dirty(uint32_t y) {
    uint32_t cell_row = y >> 1;
    for (uint32_t col = 0; col < width; ++col) {
        uint32_t cell_idx = cell_row * fb->width + col;
        fb->dirty_mask[cell_idx >> 6] |= (1ULL << (cell_idx & 63));
    }
}

void PixelBuffer::mark_rect_dirty(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    uint32_t cell_row0 = y0 >> 1;
    uint32_t cell_row1 = (y1 > 0 ? y1 - 1 : 0) >> 1;
    for (uint32_t r = cell_row0; r <= cell_row1; ++r) {
        for (uint32_t c = x0; c < x1; ++c) {
            uint32_t cell_idx = r * fb->width + c;
            fb->dirty_mask[cell_idx >> 6] |= (1ULL << (cell_idx & 63));
        }
    }
}

void PixelBuffer::encode() {
    for (uint32_t word_idx = 0; word_idx < fb->mask_words; ++word_idx) {
        uint64_t bits = fb->dirty_mask[word_idx];
        while (bits != 0) {
            uint32_t bit_pos = static_cast<uint32_t>(__builtin_ctzll(bits));
            bits &= bits - 1;

            uint32_t cell_idx = word_idx * 64 + bit_pos;
            if (cell_idx >= fb->cell_count) break;

            uint32_t cell_row = cell_idx / fb->width;
            uint32_t cell_col = cell_idx % fb->width;

            uint32_t top_y = cell_row * 2;
            uint32_t bot_y = top_y + 1;

            const uint8_t* top = pixels + (top_y * width + cell_col) * 3;
            const uint8_t* bot = pixels + (bot_y * width + cell_col) * 3;

            Cell c;
            c.fg[0] = top[0]; c.fg[1] = top[1]; c.fg[2] = top[2];
            c.bg[0] = bot[0]; c.bg[1] = bot[1]; c.bg[2] = bot[2];
            c.ch = GLYPH_HALF_UPPER;

            fb->back[cell_idx] = c;
        }
    }
}

void PixelBuffer::clear(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t total_pixels = width * height;
    for (uint32_t i = 0; i < total_pixels; ++i) {
        pixels[i * 3 + 0] = r;
        pixels[i * 3 + 1] = g;
        pixels[i * 3 + 2] = b;
    }
    fb->mark_all_dirty();
}

void PixelBuffer::fill_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t r, uint8_t g, uint8_t b) {
    uint32_t clamped_x1 = x1 < width ? x1 : width;
    uint32_t clamped_y1 = y1 < height ? y1 : height;

    for (uint32_t y = y0; y < clamped_y1; ++y) {
        for (uint32_t x = x0; x < clamped_x1; ++x) {
            uint32_t idx = (y * width + x) * 3;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
        }
    }
    mark_rect_dirty(x0, y0, clamped_x1, clamped_y1);
}

} // namespace cliviz
