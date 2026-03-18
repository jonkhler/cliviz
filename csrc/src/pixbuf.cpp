#include "pixbuf.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

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

void PixelBuffer::encode_all() {
    uint32_t stride = width * 3;
    for (uint32_t cell_row = 0; cell_row < fb->height; ++cell_row) {
        const uint8_t* top_row = pixels + (cell_row * 2) * stride;
        const uint8_t* bot_row = pixels + (cell_row * 2 + 1) * stride;
        Cell* dst = fb->back + cell_row * fb->width;

        uint32_t col = 0;
#ifdef __ARM_NEON
        // Process 4 cells at a time: vld1q loads 16 bytes, we need 12 (4×RGB).
        // Ensure at least 16 bytes remain in the row to avoid OOB read.
        uint32_t neon_limit = (stride >= 16) ? (stride - 16) / 3 + 1 : 0;
        neon_limit = std::min(neon_limit, fb->width);
        // Round down to multiple of 4
        neon_limit = (neon_limit / 4) * 4;
        for (; col < neon_limit; col += 4) {
            uint8x16_t top_v = vld1q_u8(top_row + col * 3);
            uint8x16_t bot_v = vld1q_u8(bot_row + col * 3);

            // Pack into 4 Cell structs (8 bytes each = 32 bytes total)
            // Cell layout: fg[3] bg[3] ch[2]
            // We need: top[0..2] bot[0..2] glyph, top[3..5] bot[3..5] glyph, ...
            for (uint32_t i = 0; i < 4; ++i) {
                Cell c;
                c.fg[0] = vgetq_lane_u8(top_v, 0);
                c.fg[1] = vgetq_lane_u8(top_v, 1);
                c.fg[2] = vgetq_lane_u8(top_v, 2);
                c.bg[0] = vgetq_lane_u8(bot_v, 0);
                c.bg[1] = vgetq_lane_u8(bot_v, 1);
                c.bg[2] = vgetq_lane_u8(bot_v, 2);
                c.ch = GLYPH_HALF_UPPER;
                dst[col + i] = c;
                // Shift left by 3 bytes for next pixel
                if (i < 3) {
                    top_v = vextq_u8(top_v, top_v, 3);
                    bot_v = vextq_u8(bot_v, bot_v, 3);
                }
            }
        }
#endif
        // Scalar fallback for remaining cells
        for (; col < fb->width; ++col) {
            const uint8_t* top = top_row + col * 3;
            const uint8_t* bot = bot_row + col * 3;
            Cell c;
            c.fg[0] = top[0]; c.fg[1] = top[1]; c.fg[2] = top[2];
            c.bg[0] = bot[0]; c.bg[1] = bot[1]; c.bg[2] = bot[2];
            c.ch = GLYPH_HALF_UPPER;
            dst[col] = c;
        }
    }
    fb->mark_all_dirty();
}

void PixelBuffer::clear(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t total_bytes = width * height * 3;
    uint32_t i = 0;

#ifdef __ARM_NEON
    // Fill pattern: repeating RGB triplets packed into 48 bytes (lcm of 3 and 16)
    uint8_t pattern[48];
    for (int j = 0; j < 48; j += 3) {
        pattern[j] = r; pattern[j + 1] = g; pattern[j + 2] = b;
    }
    uint8x16_t v0 = vld1q_u8(pattern);
    uint8x16_t v1 = vld1q_u8(pattern + 16);
    uint8x16_t v2 = vld1q_u8(pattern + 32);

    for (; i + 48 <= total_bytes; i += 48) {
        vst1q_u8(pixels + i, v0);
        vst1q_u8(pixels + i + 16, v1);
        vst1q_u8(pixels + i + 32, v2);
    }
#endif

    for (; i < total_bytes; i += 3) {
        pixels[i] = r; pixels[i + 1] = g; pixels[i + 2] = b;
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
