# Progress

## Current state

Terminal pixel engine: C++20 core + Python bindings via nanobind.

### Core (csrc/)
- Terminal raw mode, alternate screen, SIGWINCH resize
- 256KB output buffer with hand-rolled ANSI escape generation
- 8-byte packed Cell, double-buffered framebuffer with bitmask dirty tracking
- Half-block pixel encoding (▀), NEON-accelerated encode + clear
- Text overlay via terminal's native font (ASCII 32-126)
- Perceptual delta skip for color changes below threshold

### Python (python/cliviz/)
- nanobind bindings: Terminal, PixelBuffer with zero-copy numpy access
- `encode_all()` / `draw_text()` / `present()` pipeline for text overlays
- Taichi GPU SDF raymarcher example

### Build
- `uv pip install .` — builds everything (scikit-build-core + nanobind)
- `cmake -B build && cmake --build build` — C++ only (FetchContent for GTest)
- No Conan dependency

### Tests
- 68 C++ tests (GTest)
- 9 Python tests (pytest)
