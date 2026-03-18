#include "framebuf.h"

#include <cstdlib>
#include <cstring>

namespace cliviz {
namespace {

void* aligned_calloc(size_t alignment, size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    std::memset(ptr, 0, size);
    return ptr;
}

} // namespace

std::unique_ptr<Framebuffer> Framebuffer::create(uint32_t w, uint32_t h) {
    auto fb = std::unique_ptr<Framebuffer>(new Framebuffer());
    fb->width = w;
    fb->height = h;
    fb->cell_count = w * h;
    fb->mask_words = (fb->cell_count + 63) / 64;

    fb->front = static_cast<Cell*>(aligned_calloc(64, fb->cell_count * sizeof(Cell)));
    fb->back = static_cast<Cell*>(aligned_calloc(64, fb->cell_count * sizeof(Cell)));
    fb->dirty_mask = static_cast<uint64_t*>(aligned_calloc(64, fb->mask_words * sizeof(uint64_t)));

    if (!fb->front || !fb->back || !fb->dirty_mask) return nullptr;
    return fb;
}

Framebuffer::~Framebuffer() {
    std::free(front);
    std::free(back);
    std::free(dirty_mask);
}

void Framebuffer::set(uint32_t row, uint32_t col, Cell c) {
    uint32_t idx = row * width + col;
    back[idx] = c;
    dirty_mask[idx >> 6] |= (1ULL << (idx & 63));
}

void Framebuffer::mark_all_dirty() {
    std::memset(dirty_mask, 0xFF, mask_words * sizeof(uint64_t));
    // Clear any excess bits in the last word
    uint32_t excess = (mask_words * 64) - cell_count;
    if (excess > 0) {
        dirty_mask[mask_words - 1] &= (1ULL << (64 - excess)) - 1;
    }
}

void Framebuffer::clear_dirty() {
    std::memset(dirty_mask, 0, mask_words * sizeof(uint64_t));
}

uint32_t Framebuffer::flush(OutputBuffer& buf, uint8_t color_threshold) {
    buf.emit_sync_start();

    uint32_t emitted = 0;
    uint8_t last_fg[3] = {0, 0, 0};
    uint8_t last_bg[3] = {0, 0, 0};
    bool color_initialized = false;

    for (uint32_t word_idx = 0; word_idx < mask_words; ++word_idx) {
        uint64_t bits = dirty_mask[word_idx];
        while (bits != 0) {
            // Find next dirty bit
            uint32_t bit_pos = static_cast<uint32_t>(__builtin_ctzll(bits));
            bits &= bits - 1; // clear lowest set bit

            uint32_t idx = word_idx * 64 + bit_pos;
            if (idx >= cell_count) break;

            // Confirm actual change (skip false dirty)
            if (back[idx] == front[idx]) continue;

            // Perceptual delta skip: if only colors changed by < threshold, skip
            if (color_threshold > 0 && back[idx].ch == front[idx].ch) {
                auto abs_diff = [](uint8_t a, uint8_t b) -> uint8_t {
                    return a > b ? static_cast<uint8_t>(a - b) : static_cast<uint8_t>(b - a);
                };
                bool small_change = true;
                for (int ch = 0; ch < 3; ++ch) {
                    if (abs_diff(back[idx].fg[ch], front[idx].fg[ch]) >= color_threshold ||
                        abs_diff(back[idx].bg[ch], front[idx].bg[ch]) >= color_threshold) {
                        small_change = false;
                        break;
                    }
                }
                if (small_change) continue;
            }

            uint32_t row = idx / width;
            uint32_t col = idx % width;

            // Always emit cursor position — relying on implicit cursor
            // advance after character output is fragile across terminals
            // (some treat ▀ as width 2, breaking the tracking).
            buf.emit_cursor_to(static_cast<uint16_t>(row + 1),
                               static_cast<uint16_t>(col + 1));

            // Emit fg color if changed
            const Cell& c = back[idx];
            if (!color_initialized ||
                c.fg[0] != last_fg[0] || c.fg[1] != last_fg[1] || c.fg[2] != last_fg[2]) {
                buf.emit_fg(c.fg[0], c.fg[1], c.fg[2]);
                last_fg[0] = c.fg[0];
                last_fg[1] = c.fg[1];
                last_fg[2] = c.fg[2];
            }

            // Emit bg color if changed
            if (!color_initialized ||
                c.bg[0] != last_bg[0] || c.bg[1] != last_bg[1] || c.bg[2] != last_bg[2]) {
                buf.emit_bg(c.bg[0], c.bg[1], c.bg[2]);
                last_bg[0] = c.bg[0];
                last_bg[1] = c.bg[1];
                last_bg[2] = c.bg[2];
            }

            color_initialized = true;

            // Emit character
            if (c.ch < GLYPH_COUNT) {
                buf.emit_char(glyph_table[c.ch].utf8, glyph_table[c.ch].len);
            } else {
                buf.emit_char(" ", 1); // fallback
            }

            // Copy to front
            front[idx] = back[idx];

            ++emitted;
        }
    }

    buf.emit_sync_end();
    clear_dirty();

    return emitted;
}

} // namespace cliviz
