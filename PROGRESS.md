# Progress

## Current state

Terminal 3D rendering engine in C++20. All plan stages implemented.
94 tests, all passing. 21 commits.

### Implemented stages

**Stage 0 — Terminal Primitives**: Raw mode, alternate screen, signal-safe cleanup,
256KB output buffer with hand-rolled uint8→ASCII LUT, ANSI escape helpers.

**Stage 1 — Cell Framebuffer & Diff Engine**: 8-byte packed Cell (uint64 compare),
double-buffered framebuffer with bitmask dirty tracking, diff-only flush with cursor
and color state optimization.

**Stage 2 — NEON SIMD**: ARM NEON-accelerated pixel buffer clear (48-byte RGB pattern
fill) and encode_all() fast path (4 cells at a time via vextq_u8 shuffle).

**Stage 3 — Pixel Buffer & Half-Block Encoding**: Sub-pixel vertical resolution via ▀,
dirty propagation from pixel to cell level, encode only dirty regions.

**Stage 4 — Multi-Core Parallelism**: Fork-join thread pool with row-band
partitioning. SDF raymarcher uses all cores for embarrassingly parallel rendering.

**Stage 5 — 3D Rasterizer**: vec3/vec4/mat4 math, perspective projection, scanline
triangle rasterizer with backface culling, z-buffer, flat and Gouraud shading.
Procedural cube and icosphere generators.

**Stage 6.1 — Gouraud Shading**: Per-vertex normal interpolation, directional light
with ambient term, smooth color gradients on the icosphere.

**Stage 6.3 — SDF Raymarcher**: Per-pixel signed distance field raymarcher with
smooth union CSG, diffuse lighting, shadow rays, ambient occlusion, distance fog.

**Stage 6.4 — Perceptual Delta Skip**: Framebuffer flush skips cells where all RGB
channels differ by less than a configurable threshold, reducing ANSI output bytes.

**Stage 6.5 — Adaptive Quality**: SDF march step count dynamically adjusts (20-80)
based on frame time to maintain 60fps responsiveness.

**Stage 6.6 — Input/Resize Handling**: SIGWINCH handler enables live terminal resize
with automatic buffer reallocation.

### Build & run
```
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
cmake --preset conan-release
cmake --build build
./build/cliviz              # interactive demo
ctest --test-dir build      # 94 tests
```

### Controls
- WASD / arrow keys: orbit camera
- +/-: zoom in/out
- 1: cube (flat shading), 2: sphere (Gouraud), 3: SDF raymarcher
- Space: toggle auto-rotation
- q: quit

### Architecture
```
src/
  outbuf.h          — 256KB output buffer + ANSI escape helpers
  cell.h            — 8-byte Cell struct + glyph table
  math3d.h          — vec3/vec4/mat4 linear algebra
  term.h/.cpp       — terminal raw mode, SIGWINCH
  framebuf.h/.cpp   — double-buffered cell framebuffer, diff engine
  pixbuf.h/.cpp     — pixel buffer, half-block encode, NEON paths
  raster.h/.cpp     — triangle rasterizer, z-buffer, Gouraud
  sdf.h/.cpp        — SDF raymarcher, parallel + adaptive
  threadpool.h/.cpp — fork-join thread pool
  main.cpp          — interactive demo
tests/
  9 test files, 94 tests
```

## Remaining (nice-to-have, not in critical path)

- Texture mapping (UV interpolation)
- 256-color terminal fallback
- Mouse input support
