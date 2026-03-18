# Progress

## Current state

Terminal 3D rendering engine in C++20. Stages 0, 1, 3, 5 complete.

### Implemented stages

**Stage 0 — Terminal Primitives**: Raw mode, alternate screen, signal-safe cleanup,
256KB output buffer with hand-rolled uint8→ASCII LUT, ANSI escape helpers.

**Stage 1 — Cell Framebuffer & Diff Engine**: 8-byte packed Cell (uint64 compare),
double-buffered framebuffer with bitmask dirty tracking, diff-only flush with cursor
and color state optimization.

**Stage 3 — Pixel Buffer & Half-Block Encoding**: Sub-pixel vertical resolution via ▀,
dirty propagation from pixel to cell level, encode only dirty regions.

**Stage 5 — 3D Rasterizer**: vec3/vec4/mat4 math, perspective projection, scanline
triangle rasterizer with backface culling, z-buffer, flat shading. Procedural cube
and icosphere generators. Interactive demo with orbit camera.

### Build & run
```
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
cmake --preset conan-release
cmake --build build
./build/cliviz              # interactive demo
ctest --test-dir build      # 76 tests
```

### Architecture
```
src/
  outbuf.h        — output buffer + ANSI escape helpers (header-only)
  cell.h          — 8-byte Cell struct + glyph table (header-only)
  math3d.h        — vec3/vec4/mat4 linear algebra (header-only)
  term.h/.cpp     — terminal raw mode, setup/teardown
  framebuf.h/.cpp — double-buffered cell framebuffer, diff engine
  pixbuf.h/.cpp   — pixel buffer, half-block encode
  raster.h/.cpp   — triangle rasterizer, z-buffer, mesh generators
  main.cpp        — spinning cube/sphere demo
tests/
  7 test files, 76 tests total
```

## Next (plan.md stages not yet done)

- **Stage 2**: SIMD optimization (NEON on ARM, not AVX2)
- **Stage 4**: Multi-core parallelism (thread pool, row-band partitioning)
- **Stage 6**: Gouraud shading, textures, SDF raymarcher, color quantization,
  adaptive quality, input handling improvements
