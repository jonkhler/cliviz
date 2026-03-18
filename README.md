# cliviz

High-throughput terminal pixel engine. Treats your terminal as a pixel display using Unicode half-block characters (`▀`) for 2x vertical sub-pixel resolution. The C++ core handles terminal I/O, differential rendering, and ANSI escape generation. You bring the pixels — from numpy, GPU compute (Taichi/wgpu), or any source.

## Install

```bash
uv pip install .

# With GPU example support (Taichi)
uv pip install ".[gpu]"
```

## Quick start

```python
import cliviz
import numpy as np

with cliviz.Terminal() as term:
    pb = cliviz.PixelBuffer(term.cols, term.rows)

    # Write pixels however you want
    pixels = pb.pixels  # numpy (H, W, 3) uint8 — zero-copy view into C++ buffer
    pixels[10:20, 10:30] = [255, 0, 0]  # red rectangle via numpy

    pb.flush_full()  # encode + diff + write to terminal
    input()
```

## GPU-accelerated rendering (Taichi example)

```bash
uv pip install ".[gpu]"
uv run python python/examples/sdf_taichi.py
```

A Taichi `@ti.kernel` renders an SDF scene on the GPU (Metal), writing directly into `pb.pixels`. The C++ engine handles only terminal output. Zero CPU pixel computation.

## Python API

```python
import cliviz

# Terminal lifecycle
with cliviz.Terminal() as term:
    term.cols, term.rows       # terminal dimensions
    term.was_resized()         # poll for SIGWINCH

# Pixel buffer — the core abstraction
pb = cliviz.PixelBuffer(cols, rows)
pb.pixels                      # numpy (H, W, 3) uint8, zero-copy
pb.set(x, y, r, g, b)         # single pixel
pb.clear(r, g, b)             # fill all
pb.fill_rect(x0, y0, x1, y1, r, g, b)

# Text overlay — uses the terminal's native font
pb.draw_text(col, row, "hello", fg_r, fg_g, fg_b, bg_r, bg_g, bg_b)

# Frame output pipeline
pb.flush()                     # encode dirty + diff + write
pb.flush_full()                # encode all + diff + write

# For text overlays: split encode and present
pb.encode_all()                # pixels → cells
pb.draw_text(0, 0, "60fps")   # text on top (after encode, before present)
pb.present()                   # diff + write to terminal
```

## Architecture

```
┌──────────────────────────────────────────┐
│           Your code (Python)             │
│  numpy, Taichi, wgpu, PIL, matplotlib…   │
│         writes RGB pixels into           │
└──────────────┬───────────────────────────┘
               │  pb.pixels (numpy, zero-copy)
               ▼
┌──────────────────────────────────────────┐
│        cliviz C++ core (~500 LOC)        │
│                                          │
│  PixelBuffer ──encode──▶ Framebuffer     │
│  (RGB array)    ▀▀▀     (8-byte cells)  │
│                            │             │
│                        diff engine       │
│                        (dirty bitmask)   │
│                            │             │
│                      OutputBuffer        │
│                   (ANSI escape stream)   │
│                            │             │
│                     write(STDOUT)        │
│                  (single syscall,        │
│                   synchronized output)   │
└──────────────────────────────────────────┘
               │
               ▼
          Terminal (Ghostty, Kitty, etc.)
```

The C++ core does one thing: convert a pixel array into minimal ANSI output at maximum throughput. It owns:

- **Half-block encoding** — pixel pairs → terminal cells with `▀` + fg/bg colors
- **Double-buffered diff** — only changed cells are emitted
- **ANSI serialization** — hand-rolled uint8→ASCII, cursor positioning, color escapes
- **Single `write()` per frame** — 256KB buffer, synchronized output wrapping

Everything above the pixel buffer (rendering, compute, scene logic) lives in Python/user code.

## Why C++ instead of pure Python?

The diff engine + ANSI serialization is the one path where Python is too slow. At 120fps with ~8K cells, building ~170KB of variable-length ANSI escape sequences per frame requires sub-millisecond serialization. The C++ core is ~500 lines — the thinnest possible native layer. Pixel computation (the expensive part) stays in Python/GPU.

## Project structure

```
pyproject.toml               Python packaging (scikit-build-core + nanobind)
CMakeLists.txt               C++ build (also used by Python wheel build)
csrc/                        C++ terminal engine
  include/cliviz/            public headers (outbuf, cell, framebuf, pixbuf, term)
  src/                       implementation
  tests/                     GTest suite
python/
  cliviz/                    Python package
    __init__.py
    _native.cpp              nanobind bindings
  examples/
    sdf_taichi.py            GPU SDF raymarcher demo
  tests/                     pytest suite
```

## Development

```bash
# Python tests
uv run python -m pytest

# C++ tests
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## License

MIT — Jonas Köhler
