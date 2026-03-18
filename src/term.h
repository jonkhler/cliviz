#pragma once

#include <cstdint>

namespace cliviz {

struct TermSize {
    uint16_t cols;
    uint16_t rows;
};

// Query current terminal dimensions. Returns {0,0} if not a terminal.
TermSize term_get_size();

// Enter raw mode: alternate screen, hidden cursor, raw input, no buffering.
// Registers atexit + signal handlers for clean restore.
// Returns false if stdout is not a terminal.
bool term_init();

// Restore terminal to original state. Safe to call multiple times.
void term_shutdown();

// True after successful term_init(), false after term_shutdown().
bool term_is_active();

// Returns true if the terminal was resized since the last call.
// Cleared after each call (edge-triggered).
bool term_was_resized();

} // namespace cliviz
