// cliviz — Terminal 3D rendering engine
// Public C API for FFI (Python ctypes/cffi, etc.)
//
// Caller owns the frame loop. Typical usage:
//   cliviz_init()
//   pb = cliviz_pixbuf_create(cols, rows)
//   loop:
//     cliviz_pixbuf_clear(pb, r, g, b)
//     cliviz_pixbuf_set(pb, x, y, r, g, b)
//     cliviz_pixbuf_flush(pb)
//   cliviz_pixbuf_destroy(pb)
//   cliviz_shutdown()

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Terminal lifecycle ──

// Enter raw mode, alternate screen, hidden cursor. Returns 0 on success.
int cliviz_init(void);

// Restore terminal state. Safe to call multiple times.
void cliviz_shutdown(void);

// Query terminal dimensions. Returns 0 on success.
int cliviz_term_size(uint16_t* cols, uint16_t* rows);

// Returns 1 if terminal was resized since last call.
int cliviz_term_was_resized(void);

// ── Pixel buffer ──

typedef struct cliviz_pixbuf cliviz_pixbuf;

// Create a pixel buffer for the given terminal dimensions.
// Pixel height = term_rows * 2 (half-block sub-pixel resolution).
cliviz_pixbuf* cliviz_pixbuf_create(uint32_t term_cols, uint32_t term_rows);

void cliviz_pixbuf_destroy(cliviz_pixbuf* pb);

// Dimensions
uint32_t cliviz_pixbuf_width(const cliviz_pixbuf* pb);
uint32_t cliviz_pixbuf_height(const cliviz_pixbuf* pb);

// Direct pixel access — returns pointer to RGB pixel array.
// Layout: row-major, [height][width][3], uint8_t per channel.
uint8_t* cliviz_pixbuf_pixels(cliviz_pixbuf* pb);

// Set a single pixel.
void cliviz_pixbuf_set(cliviz_pixbuf* pb, uint32_t x, uint32_t y,
                       uint8_t r, uint8_t g, uint8_t b);

// Fill entire buffer with solid color.
void cliviz_pixbuf_clear(cliviz_pixbuf* pb, uint8_t r, uint8_t g, uint8_t b);

// Fill a rectangle.
void cliviz_pixbuf_fill_rect(cliviz_pixbuf* pb,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t r, uint8_t g, uint8_t b);

// Encode pixels → cells, diff against previous frame, write to terminal.
// This is the main "present frame" call. Returns number of cells emitted.
uint32_t cliviz_pixbuf_flush(cliviz_pixbuf* pb);

// Like flush, but marks all cells dirty first (use after writing every pixel).
uint32_t cliviz_pixbuf_flush_full(cliviz_pixbuf* pb);

// ── SDF raymarcher ──

// Function pointer type for SDF scenes.
typedef float (*cliviz_sdf_fn)(float x, float y, float z, float time);

// Render SDF scene into pixel buffer. Uses all CPU cores.
void cliviz_sdf_render(cliviz_pixbuf* pb, cliviz_sdf_fn scene, float time,
                       float eye_x, float eye_y, float eye_z,
                       float center_x, float center_y, float center_z,
                       int max_steps);

// Built-in SDF scene for testing.
float cliviz_sdf_default_scene(float x, float y, float z, float time);

#ifdef __cplusplus
}
#endif
