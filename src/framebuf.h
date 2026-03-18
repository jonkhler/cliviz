#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "cell.h"
#include "outbuf.h"

namespace cliviz {

struct Framebuffer {
    uint32_t width = 0;
    uint32_t height = 0;

    // Double-buffered: front = what's on screen, back = what we want
    Cell* front = nullptr;
    Cell* back = nullptr;

    // 1 bit per cell, packed. Used to skip clean regions.
    uint64_t* dirty_mask = nullptr;

    uint32_t cell_count = 0;
    uint32_t mask_words = 0; // ceil(cell_count / 64)

    // Allocate buffers for the given dimensions.
    // All allocations are cache-line aligned (64-byte).
    static std::unique_ptr<Framebuffer> create(uint32_t w, uint32_t h);

    // Set a cell in the back buffer and mark it dirty.
    void set(uint32_t row, uint32_t col, Cell c);

    // Diff back vs front, emit minimal ANSI to the output buffer, swap.
    // Returns the number of cells actually emitted.
    uint32_t flush(OutputBuffer& buf);

    // Mark all cells dirty (for full redraws).
    void mark_all_dirty();

    // Clear dirty mask (all clean).
    void clear_dirty();

    ~Framebuffer();

private:
    Framebuffer() = default;
};

} // namespace cliviz
