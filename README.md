# cliviz

Terminal 3D rendering engine. Treats your terminal as a pixel display using Unicode half-block characters (`▀`) for 2x vertical resolution. Ships as a C++20 library with Python bindings.

## Features

- **Half-block pixel display** — each terminal cell encodes two pixels vertically via `▀` with independent fg/bg colors
- **Double-buffered diff engine** — only changed cells emit ANSI escapes, minimizing output bytes
- **Triangle rasterizer** — perspective projection, z-buffer, backface culling, Gouraud shading
- **SDF raymarcher** — signed distance fields with shadows, ambient occlusion, distance fog
- **Multi-threaded** — parallel SDF rendering across all CPU cores
- **Python bindings** — numpy zero-copy pixel access, all rendering stays in C++
- **Zero dependencies** — no ncurses, no frameworks. Just POSIX + a C++20 compiler

## Install

```bash
# Python (requires C++20 compiler + CMake)
uv pip install .

# C++ only
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Quick start (Python)

```python
import cliviz
import numpy as np

with cliviz.Terminal() as term:
    pb = cliviz.PixelBuffer(term.cols, term.rows - 1)
    zb = cliviz.ZBuffer(pb.width, pb.height)
    mesh = cliviz.make_cube()

    eye = np.array([0, 2, 5], dtype=np.float32)
    center = np.zeros(3, dtype=np.float32)
    up = np.array([0, 1, 0], dtype=np.float32)

    proj = cliviz.perspective(1.0, pb.width / pb.height, 0.1, 100.0)
    view = cliviz.look_at(eye, center, up)
    mvp = (proj @ view).astype(np.float32)

    pb.clear(0, 0, 0)
    zb.clear()
    cliviz.rasterize(pb, zb, mesh, mvp)
    pb.flush()

    input()  # wait before exiting
```

## Quick start (C++)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/cliviz_demo
```

Keys: `1` cube, `2` sphere, `3` SDF raymarcher, `WASD`/arrows orbit, `+`/`-` zoom, `space` toggle rotation, `q` quit.

## Python API

```python
import cliviz

# Terminal lifecycle (context manager)
with cliviz.Terminal() as term:
    term.cols, term.rows   # terminal dimensions
    term.was_resized()     # poll for SIGWINCH

# Pixel buffer
pb = cliviz.PixelBuffer(cols, rows)
pb.pixels                  # numpy (H, W, 3) uint8 — zero-copy view
pb.set(x, y, r, g, b)
pb.clear(r, g, b)
pb.fill_rect(x0, y0, x1, y1, r, g, b)
pb.flush()                 # diff + encode + write to terminal
pb.flush_full()            # full redraw (after writing every pixel)

# 3D rasterization (C++ speed)
mesh = cliviz.make_cube()
mesh = cliviz.make_icosphere(subdivisions=2)
zb = cliviz.ZBuffer(w, h)
cliviz.rasterize(pb, zb, mesh, mvp)  # mvp is 4x4 numpy float32

# Matrix helpers
cliviz.perspective(fov_y, aspect, near, far)
cliviz.look_at(eye, center, up)       # eye/center/up are numpy float32[3]
cliviz.rotate_y(radians)
cliviz.rotate_x(radians)

# SDF raymarcher (multi-threaded)
cliviz.sdf_render(pb, time, eye, center, max_steps=40)
```

## Architecture

```
include/cliviz/          C++ public headers
  outbuf.h               256KB output buffer + ANSI escape helpers
  cell.h                 8-byte packed Cell struct + glyph table
  math3d.h               vec3/vec4/mat4 linear algebra
  term.h                 terminal raw mode, SIGWINCH
  framebuf.h             double-buffered cell framebuffer, diff engine
  pixbuf.h               pixel buffer, half-block encode, NEON paths
  raster.h               triangle rasterizer, z-buffer, Gouraud
  sdf.h                  SDF raymarcher, parallel rendering
  threadpool.h           fork-join thread pool
include/cliviz.h         flat C API (extern "C") for FFI
src/                     C++ implementation
python/cliviz/           Python package (nanobind bindings)
examples/                C++ demo + Python examples
tests/                   108 C++ tests (GTest)
python/tests/            13 Python tests (pytest)
```

## How it works

1. **Pixel buffer** (`PixelBuffer`): a 2D RGB array at 2x terminal vertical resolution
2. **Half-block encode**: pairs of pixel rows map to terminal cells — top pixel becomes fg color, bottom becomes bg color, character is `▀`
3. **Cell framebuffer** (`Framebuffer`): double-buffered 8-byte cells with bitmask dirty tracking
4. **Diff engine**: compares back vs front buffer, emits only changed cells as ANSI cursor-position + color + character sequences
5. **Single write()**: entire frame buffered in a 256KB `OutputBuffer`, flushed with one `write()` syscall wrapped in synchronized output (`\e[?2026h/l`)

## Tests

```bash
# Python
uv run python -m pytest

# C++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## License

MIT
