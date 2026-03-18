# Terminal 3D Rendering Engine — Implementation Plan

## Project Overview

A layered, high-performance terminal rendering engine in C++20 that treats the terminal as a pixel display. Optimized for minimum output latency using SIMD, multi-core parallelism, and minimal ANSI byte emission. The architecture is bottom-up: each stage is independently testable and benchmarkable before the next is built on top.

**Target performance**: 60+ FPS at 200×50 terminal (400×100 effective pixel resolution via half-block encoding), with <2ms CPU time per frame for typical scenes.

**Compiler**: Clang or GCC with `-O3 -march=native -mavx2` (AVX2 assumed as baseline; SSE4.1 fallback path for portability).

**Build system**: CMake. Single static library + test/demo binaries.

**Dependencies**: None beyond libc and POSIX. No ncurses, no frameworks.

---

## Stage 0 — Project Skeleton & Terminal Primitives

**Goal**: Raw terminal I/O at maximum throughput. No abstraction yet — just prove we can blast bytes to stdout fast and measure it.

### 0.1 — Terminal Setup / Teardown

```
src/
  term.h / term.cpp     — terminal raw mode, cleanup
  main.cpp              — test harness
CMakeLists.txt
```

- Enter raw mode via `tcsetattr()` (disable canonical mode, echo, line buffering).
- Disable stdout buffering: `setvbuf(stdout, NULL, _IONBF, 0)`. We manage our own buffer.
- Register `atexit()` handler to restore terminal state, handle SIGINT/SIGTERM gracefully.
- Query terminal size via `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` and `SIGWINCH` handler for resize.
- Hide cursor (`\e[?25l`), restore on exit (`\e[?25h`).
- Alternate screen buffer (`\e[?1049h` / `\e[?1049l`) so we don't trash scrollback.

### 0.2 — Output Buffer & Write Engine

```
src/
  outbuf.h / outbuf.cpp — static output buffer + write primitives
```

Core data structure:

```cpp
struct OutputBuffer {
    alignas(64) char data[1 << 18]; // 256KB, cache-line aligned
    uint32_t len = 0;

    void append(const char* s, uint32_t n);  // memcpy, no bounds check in hot path
    void append_byte(char c);
    void flush();  // single write(STDOUT_FILENO, data, len); len = 0;
};
```

**Critical**: `flush()` is a single `write()` syscall. If `len > PIPE_BUF` (4096 on Linux), this is not atomic, but that's fine — the synchronized output escape handles tearing.

Write helpers (all append to buffer, no I/O):
- `emit_sync_start()` → appends `\e[?2026h`
- `emit_sync_end()` → appends `\e[?2026l`
- `emit_cursor_to(row, col)` → `\e[{row};{col}H` with hand-rolled int-to-ascii
- `emit_fg(r, g, b)` → `\e[38;2;{r};{g};{b}m`
- `emit_bg(r, g, b)` → `\e[48;2;{r};{g};{b}m`
- `emit_char(const char* utf8, uint8_t len)` → raw bytes

**Key optimization** — Hand-rolled `uint8_to_str()`:

```cpp
// Avoid sprintf/snprintf entirely. A lookup table of "0\0\0" .. "255" is 768 bytes,
// fits in L1. For each R, G, B value, copy 1-3 bytes + ';' separator.
// This alone is 5-10× faster than sprintf for ANSI color codes.
static const char digits[256][4]; // precomputed, includes length
```

### 0.3 — Benchmark: Raw Throughput

Test harness that fills the entire screen with random-color cells every frame, measures:
1. Time to build the output buffer (CPU-side).
2. Time for `write()` syscall to return.
3. Bytes emitted per frame.

This gives us the floor — worst-case full-screen redraw cost. Expect ~150-200KB per full frame at 200×50.

**Deliverables**: `term_init()`, `term_shutdown()`, `OutputBuffer`, throughput benchmark printing FPS and bytes/frame to stderr.

---

## Stage 1 — Cell Framebuffer & Diff Engine

**Goal**: Double-buffered cell grid with differential output. Only changed cells are emitted. This is the core engine.

### 1.1 — Cell Definition & Framebuffer

```
src/
  cell.h         — Cell struct, comparison
  framebuf.h/cpp — Framebuffer: alloc, set, diff, flush
```

Cell layout (packed for SIMD-friendly comparison):

```cpp
// 8 bytes total — fits in a single 64-bit compare
struct __attribute__((packed)) Cell {
    uint8_t fg[3];   // foreground RGB
    uint8_t bg[3];   // background RGB
    uint16_t ch;     // character index (map to UTF-8 at emit time)
                     // 0 = space, 1 = ▀, 2 = ▄, etc.
                     // 16-bit allows future glyph extension
};
static_assert(sizeof(Cell) == 8);
```

**Why 8 bytes**: Comparing two cells is a single `uint64_t != uint64_t`. No per-field comparison needed. SIMD can compare 4 cells at once with a 256-bit AVX2 register, 8 cells with AVX-512.

Framebuffer:

```cpp
struct Framebuffer {
    uint32_t width, height;           // terminal cols × rows
    Cell* front;                      // what's on screen (cache-line aligned alloc)
    Cell* back;                       // what we want on screen
    uint64_t* dirty_mask;             // 1 bit per cell, packed into uint64_t[]
                                      // (200×50 = 10000 cells = 157 uint64_t)
};
```

### 1.2 — Cell Set + Dirty Tracking

```cpp
inline void fb_set(Framebuffer* fb, uint32_t row, uint32_t col, Cell c) {
    uint32_t idx = row * fb->width + col;
    fb->back[idx] = c;
    // Set dirty bit (branchless)
    fb->dirty_mask[idx >> 6] |= (1ULL << (idx & 63));
}
```

The dirty bit is always set on write. The diff pass checks `back != front` to confirm actual change (handles the case where a cell is set to its current value).

### 1.3 — Diff + Flush (The Critical Path)

This is the performance-critical function. Algorithm:

```
emit_sync_start()

track: last_emitted_fg[3], last_emitted_bg[3], cursor_row, cursor_col

for row in 0..height:
    scan dirty_mask for this row's bits
    for each dirty cell (idx) in this row:
        if back[idx] == front[idx]: continue (false dirty)
        decide cursor movement:
            if cursor is already at (row, col): no-op (cheapest)
            if cursor is at (row, col - N) for small N: emit N spaces or \e[{N}C
            else: emit \e[row;colH
        if fg != last_emitted_fg: emit_fg(), update last_emitted_fg
        if bg != last_emitted_bg: emit_bg(), update last_emitted_bg
        emit character UTF-8 bytes
        copy back[idx] → front[idx]
        advance logical cursor position

emit_sync_end()
outbuf.flush()
clear dirty_mask (memset 0, or SIMD zero — trivial cost)
```

**SIMD in the diff scan**: The dirty mask scan uses `__builtin_ctzll()` (count trailing zeros) to jump to the next dirty bit in O(1) per dirty cell, skipping clean regions entirely. For AVX2, the `back != front` confirmation can be done 4 cells at a time.

### 1.4 — Cursor Movement Heuristic

Emitting `\e[row;colH` costs 6-10 bytes. Alternatives:
- `\e[C` (cursor right 1) = 3 bytes. `\e[{N}C` = 4-6 bytes.
- `\r` (carriage return) + `\e[{N}C` = cheaper than absolute for same-row jumps.
- Writing space characters (with correct bg color) to advance = 1 byte per cell if bg matches.

Heuristic: if gap between two dirty cells on the same row is ≤ 4, rewrite the gap. If gap > 4, emit cursor-jump.

For cross-row jumps: always emit `\e[row;colH` (or `\n` + column-move if moving down exactly 1).

### 1.5 — Benchmark: Diff Engine

Test harness with scenarios:
1. **Full dirty** — every cell changed. Measure vs Stage 0 raw blast (should be ~same bytes, small overhead for diff logic).
2. **1% dirty** — 100 random cells changed. Measure bytes emitted. Should be ~100× less than full frame.
3. **Scrolling scene** — every cell shifts left by 1. ~100% dirty but with long same-color runs.
4. **Static scene** — nothing changed. Measure flush cost (should be near-zero: dirty mask scan + sync escapes only).

**Deliverables**: `Framebuffer` with `fb_set()`, `fb_flush()`, diff benchmarks.

---

## Stage 2 — SIMD-Accelerated Internals

**Goal**: Vectorize the hot paths identified in Stage 1 benchmarks. This stage is about optimization, not new features.

### 2.1 — SIMD Cell Comparison

The diff scan's `back[idx] != front[idx]` can be widened:

```cpp
// Compare 4 cells at once (4 × 8 bytes = 32 bytes = one AVX2 register)
__m256i b = _mm256_load_si256((__m256i*)(back + i));
__m256i f = _mm256_load_si256((__m256i*)(front + i));
__m256i cmp = _mm256_cmpeq_epi64(b, f);  // per-64-bit (per-cell) equality
int mask = _mm256_movemask_epi8(cmp);     // extract comparison result
// mask == 0xFFFFFFFF means all 4 cells are identical → skip
```

This replaces the dirty-mask pre-pass for full-frame redraws (when you know most cells are dirty anyway). For partial updates, the dirty mask + `ctzll` approach is still faster because it skips entire 64-cell blocks.

Provide both paths; select based on dirty cell count heuristic (if >50% dirty, use SIMD sweep; else use bitmask).

### 2.2 — SIMD ANSI Escape Generation

The `emit_fg(r, g, b)` escape `\e[38;2;R;G;Bm` can be partially vectorized:
- Precompute a 256-entry lookup table mapping `uint8_t` → the 1-3 ASCII digit chars + their length.
- Use SSSE3 `pshufb` to rearrange bytes from the LUT into the output buffer in one shuffle.
- Minor win per-escape (~2 bytes of instruction savings), but across thousands of color changes per frame it adds up.

### 2.3 — SIMD Dirty Mask Clear

After flush, clearing `dirty_mask[]` with `_mm256_store_si256` of zero vectors, 32 bytes at a time. Trivial to implement, avoids `memset` overhead.

### 2.4 — Benchmark Comparison

Re-run Stage 1 benchmarks before/after SIMD. Publish numbers. SIMD is worth it if it saves >10% on the diff+emit path for the full-dirty case.

**Deliverables**: SIMD-optimized `fb_flush()`, comparative benchmarks.

---

## Stage 3 — Pixel Buffer & Half-Block Encoding

**Goal**: Layer 2 of the architecture. Exposes a pixel-level API that maps onto the cell framebuffer via half-block encoding.

### 3.1 — Pixel Buffer

```cpp
struct PixelBuffer {
    uint32_t width;          // = terminal cols
    uint32_t height;         // = terminal rows × 2 (sub-pixel vertical resolution)
    uint8_t* pixels;         // RGB, row-major, [height][width][3]
                             // cache-line aligned allocation
    Framebuffer* fb;         // underlying cell framebuffer
};
```

### 3.2 — px_set and Dirty Propagation

```cpp
inline void px_set(PixelBuffer* pb, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t idx = (y * pb->width + x) * 3;
    pb->pixels[idx + 0] = r;
    pb->pixels[idx + 1] = g;
    pb->pixels[idx + 2] = b;
    // Mark the corresponding cell dirty
    uint32_t cell_row = y >> 1;  // y / 2
    uint32_t cell_col = x;
    uint32_t cell_idx = cell_row * pb->fb->width + cell_col;
    pb->fb->dirty_mask[cell_idx >> 6] |= (1ULL << (cell_idx & 63));
}
```

Also provide a raw pointer access mode for rasterizers that want to write directly into the pixel array (avoid function call overhead per pixel):

```cpp
uint8_t* px_row(PixelBuffer* pb, uint32_t y);  // returns pointer to row y's RGB data
void px_mark_row_dirty(PixelBuffer* pb, uint32_t y);  // marks all cells in this row dirty
void px_mark_rect_dirty(PixelBuffer* pb, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
```

### 3.3 — Half-Block Encode (The Pixel→Cell Bridge)

Before `fb_flush()`, sweep all dirty cells and encode pixel pairs into cells:

```cpp
void px_encode(PixelBuffer* pb) {
    for each dirty cell (row, col):
        uint8_t* top = &pixels[(2 * row) * width * 3 + col * 3];
        uint8_t* bot = &pixels[(2 * row + 1) * width * 3 + col * 3];
        Cell c;
        c.fg = {top[0], top[1], top[2]};  // ▀ foreground = top pixel
        c.bg = {bot[0], bot[1], bot[2]};  // ▀ background = bottom pixel
        c.ch = GLYPH_HALF_BLOCK_UPPER;    // ▀
        fb->back[row * width + col] = c;
}
```

**SIMD opportunity**: encode 4, 8, or 16 cells at once. The pixel→cell transform is just a shuffle + pack. Each cell needs 6 bytes from two pixel rows — this is a gather operation, ideal for `vpgatherdd` (AVX2) or manual shuffle sequences.

Wider approach: process an entire row of pixel pairs (200 columns = 200 cells). Load top row pixels and bottom row pixels as two contiguous arrays, then interleave/pack into the Cell array with vector shuffles. At 200 cells × 8 bytes = 1600 bytes, this is 50 AVX2 stores.

### 3.4 — Encode Fusion Optimization

Instead of a separate encode sweep, the encode can be fused with the diff scan in `fb_flush()`:
- For each dirty bit, encode on-the-fly, compare against front, emit if different.
- Avoids writing to `back[]` at all — the pixel buffer IS the back buffer, and encoding is part of the flush.
- Trade-off: slightly more complex flush logic, but eliminates one memcpy per dirty cell.

Implement both; benchmark to decide. Fused is likely faster for high-dirty-% frames, separate is simpler and better when dirty% is low (encode only what's needed, diff skips the rest).

### 3.5 — Pixel Buffer Bulk Operations

For the rasterizer layer above:
- `px_clear(pb, r, g, b)` — fill entire pixel buffer, mark all dirty. Use `memset` if solid color, SIMD fill otherwise.
- `px_blit(pb, src, x, y, w, h)` — copy a rectangle from source buffer into pixel buffer. Row-major `memcpy` per row, mark affected cells dirty.
- `px_fill_rect(pb, x0, y0, x1, y1, r, g, b)` — filled rectangle. SIMD fill, mark dirty.

### 3.6 — Benchmark: Pixel Layer

Scenarios:
1. **Random pixel noise** — every pixel random color every frame. Measures worst-case encode + full flush.
2. **Moving rectangle** — a filled rect moves across screen. Measures partial dirty encode efficiency.
3. **Gradient fill** — full-screen gradient. Measures color-run encoding (many consecutive distinct colors).

**Deliverables**: `PixelBuffer`, `px_set()`, `px_encode()`, `px_flush()` (encode + fb_flush combined), pixel-level benchmarks.

---

## Stage 4 — Multi-Core Parallelism

**Goal**: Parallelize the rasterization and encode paths. The diff+emit path stays single-threaded (it writes to a single output buffer sequentially), but everything feeding into it can be parallel.

### 4.1 — Threading Model

Use a **thread pool** (std::jthread + work-stealing queue, or a simple barrier-based fork-join).

Do NOT use `std::async` or create/destroy threads per frame — the overhead kills you at 60fps.

```
                ┌──────────────────────────────┐
                │   Main Thread (Frame Loop)    │
                │                               │
                │  1. Kick off parallel work     │
                │  2. Barrier-wait for workers   │
                │  3. fb_flush() (sequential)    │
                │  4. write() to terminal        │
                └──────────────────────────────┘
                         │
            ┌────────────┼────────────┐
            ▼            ▼            ▼
        Worker 0     Worker 1     Worker 2
        rows 0-16    rows 17-33   rows 34-49
        (rasterize   (rasterize   (rasterize
         + encode)    + encode)    + encode)
```

Each worker owns a horizontal band of the pixel buffer and the corresponding cells. No shared writes, no locks, no false sharing (ensure row boundaries are cache-line aligned).

### 4.2 — Row-Band Partitioning

Partition by terminal rows (not pixel rows). Each worker processes `height / N_workers` rows. Since each terminal row maps to 2 pixel rows, the pixel-level parallelism follows automatically.

Padding: ensure each band's starting Cell address is 64-byte aligned. Pad the Cell array if necessary.

### 4.3 — Parallel Encode

The `px_encode()` pass is embarrassingly parallel — each cell depends only on its own two pixels. Partition across workers. Each worker encodes its band of cells and writes to its section of `fb->back[]`. After barrier, main thread runs `fb_flush()`.

### 4.4 — Parallel Rasterization (Prep for Stage 5)

The rasterizer (Stage 5) can either:
1. **Row-parallel**: each worker rasterizes its band of the screen. Simple for raymarching/SDF (per-pixel, no dependencies). For triangle rasterization, requires splitting triangles at band boundaries.
2. **Tile-parallel**: divide screen into rectangular tiles, workers pull tiles from a queue. Better load balancing, more complex.

For now, implement row-band parallelism. It's simpler and maps directly onto the encode partitioning (same band = same worker for both rasterize and encode, excellent cache locality).

### 4.5 — Benchmark: Parallel Encode

Compare single-threaded vs 2/4/8 threads for the encode path. At 200×50 terminal, the encode work is ~10μs single-threaded — parallelism may not help (thread dispatch overhead dominates). But at larger terminals (e.g., 400×100) or with expensive rasterization, it will.

Determine the crossover point: below some resolution, single-threaded is faster. Above it, parallel wins. Make the thread count adaptive.

**Deliverables**: Thread pool, parallel `px_encode()`, adaptive single/multi-thread selection.

---

## Stage 5 — 3D Rasterizer

**Goal**: A minimal but functional 3D software rasterizer that writes into the pixel buffer. This is the first stage that actually draws something interesting.

### 5.1 — Math Primitives

```
src/
  math3d.h — vec3, vec4, mat4, basic ops
```

All inline, all `float`. Use SSE for mat4×vec4 multiply (4 `dpps` or `mulps+haddps`). Keep it minimal:
- `vec3`: add, sub, mul, dot, cross, normalize
- `mat4`: identity, translate, rotate_{x,y,z}, perspective, lookat, multiply, transform_point, transform_dir
- Frustum: 6 planes extracted from MVP matrix

### 5.2 — Triangle Rasterizer Core

Scanline rasterizer with:
- **Z-buffer** (float, same resolution as pixel buffer, cache-line aligned)
- **Flat shading** (one color per triangle — skip interpolation for v1, add Gouraud later)
- **Backface culling** (cross product of screen-space edges)
- **Viewport clipping** (clip to pixel buffer bounds, guard-band optional)

Hot loop: for each triangle → compute bounding box → for each pixel in bbox → edge function test → z-test → write pixel.

**SIMD in rasterizer**: evaluate edge functions for 4 or 8 pixels simultaneously using AVX2. Each edge function is `E(x,y) = a*x + b*y + c` — increment across pixels with `_mm256_add_ps`. Test sign of all 3 edges + z-compare in one `_mm256_movemask_ps`.

### 5.3 — Simple Mesh Format

For demo purposes:
```cpp
struct Mesh {
    vec3* positions;     // vertex positions
    uint32_t* indices;   // triangle indices (3 per tri)
    vec3* colors;        // per-face color
    uint32_t n_verts, n_tris;
};
```

Hardcoded test meshes: cube, icosphere, torus. Generate procedurally at startup.

### 5.4 — Render Pipeline

Per frame:
1. Clear z-buffer (`memset` to float-max, or SIMD broadcast).
2. Clear pixel buffer (background color).
3. Transform vertices: MVP × position → clip space → NDC → screen space. (SIMD: transform 4 vertices at once.)
4. For each triangle: backface cull → rasterize → z-test → `px_set`.
5. `px_encode()` + `fb_flush()`.

### 5.5 — Camera

Simple orbit camera: yaw/pitch/distance, controlled by keyboard (WASD + arrow keys, read from raw stdin in non-blocking mode). Compute view matrix from spherical coords.

### 5.6 — Benchmark: Rasterizer

Scenes:
1. **Spinning cube** (12 triangles) — baseline, should be trivially fast.
2. **Icosphere** (320 triangles at 3 subdivisions) — moderate.
3. **1K triangles** (procedural mesh) — stress test.
4. **Overdraw stress** (many overlapping triangles) — tests z-buffer throughput.

Measure: total frame time, rasterizer-only time, encode+flush time.

**Deliverables**: Software rasterizer, z-buffer, orbit camera, spinning cube demo.

---

## Stage 6 — Polish & Features

These are independent improvements, implementable in any order after Stage 5.

### 6.1 — Gouraud Shading

Interpolate vertex colors (or normals + per-pixel lighting) across triangles. The scanline loop interpolates RGB via fixed-point or float barycentric coords. SIMD: interpolate 4–8 pixels at once.

### 6.2 — Texture Mapping

UV interpolation + texture lookup. Textures are small (terminal resolution is tiny), so even nearest-neighbor sampling looks fine. Perspective-correct interpolation via 1/w.

### 6.3 — SDF Raymarcher (Alternative Renderer)

Drop-in replacement for the triangle rasterizer. Per-pixel raymarching against signed distance fields. Embarrassingly parallel. Produces stunning visuals at terminal resolution (smooth surfaces, ambient occlusion, soft shadows — all cheap at 20K pixels).

Implement as an alternative render backend that writes into the same `PixelBuffer`.

### 6.4 — Color Quantization & Escape Compression

- **Perceptual delta skip**: if a cell's fg color changed by <4 in each channel (imperceptible on terminal), skip the fg escape. Saves ~20 bytes per skipped cell.
- **256-color fallback**: detect if terminal supports truecolor. If not, quantize to 256-color palette. Shorter escapes (`\e[38;5;Nm` = 8–11 bytes vs `\e[38;2;R;G;Bm` = 15–19 bytes).
- **Run-length color encoding**: when many consecutive cells share a color, emit the color escape once, then all characters. (The diff engine already does this implicitly via color state tracking.)

### 6.5 — Frame Timing & Adaptive Quality

- Measure time from flush-start to write-return.
- If consistently over budget (>16ms), reduce effective resolution (render at half-res pixel buffer, upscale to cell grid via nearest-neighbor).
- vsync: if terminal supports it, sync to terminal refresh. Otherwise, use `usleep()` / `clock_nanosleep()` to cap framerate.

### 6.6 — Input Handling

Non-blocking stdin read in the main loop. Parse:
- Arrow keys, WASD for camera.
- Escape sequences for mouse events (`\e[?1003h` for mouse tracking).
- Resize events (re-allocate buffers on `SIGWINCH`).

---

## File Structure (Final)

```
terminal-render/
├── CMakeLists.txt
├── src/
│   ├── term.h / .cpp           — terminal setup/teardown, raw mode
│   ├── outbuf.h / .cpp         — output buffer, ANSI escape helpers
│   ├── cell.h                  — Cell struct (header-only, trivial)
│   ├── framebuf.h / .cpp       — cell framebuffer, diff engine, flush
│   ├── pixbuf.h / .cpp         — pixel buffer, half-block encode
│   ├── threadpool.h / .cpp     — fork-join thread pool
│   ├── math3d.h                — vec3/vec4/mat4 (header-only, inline)
│   ├── raster.h / .cpp         — triangle rasterizer, z-buffer
│   ├── sdf.h / .cpp            — SDF raymarcher (Stage 6)
│   ├── camera.h / .cpp         — orbit camera, input
│   └── main.cpp                — demo / entry point
├── bench/
│   ├── bench_outbuf.cpp        — Stage 0 throughput
│   ├── bench_diff.cpp          — Stage 1 diff scenarios
│   ├── bench_encode.cpp        — Stage 3 pixel encode
│   └── bench_raster.cpp        — Stage 5 rasterizer
└── README.md
```

---

## Implementation Order Summary

| Stage | Deliverable | Key Metric |
|-------|------------|------------|
| 0 | Terminal I/O + output buffer | bytes/sec to stdout |
| 1 | Cell framebuffer + diff engine | bytes emitted at N% dirty |
| 2 | SIMD-optimized diff + encode | μs per flush (before vs after) |
| 3 | Pixel buffer + half-block layer | pixel-level API working, FPS |
| 4 | Multi-core encode + rasterize | speedup at various resolutions |
| 5 | 3D software rasterizer | spinning cube at 60fps |
| 6 | Shading, SDF, polish | visual quality + adaptive perf |

Each stage has its own benchmarks. Do not proceed to the next stage until the current stage's benchmarks are satisfactory. If a stage's performance is worse than expected, profile and fix before moving on.

---

## Design Principles

1. **Measure everything.** Every stage has explicit benchmarks. Profile-guided decisions only.
2. **Single write() per frame.** Never call write() more than once per frame. Buffer everything.
3. **Minimize bytes, not CPU cycles.** At terminal resolution, CPU is cheap. Output bytes are expensive (they pass through PTY, terminal VT parser, GPU text renderer). Every byte saved in ANSI escapes is a direct latency win.
4. **Allocation-free hot path.** All buffers are pre-allocated at startup or resize. The frame loop does zero malloc/free/new/delete.
5. **Data-oriented layout.** Cell arrays are flat, contiguous, cache-line aligned. No pointers-to-pointers, no linked lists, no virtual dispatch in the hot path.
6. **SIMD as optimization, not architecture.** Write scalar first, verify correctness, then vectorize. Keep scalar fallback for debugging and portability.
