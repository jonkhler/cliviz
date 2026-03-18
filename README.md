# cliviz

High-throughput terminal pixel engine. Treats your terminal as a pixel display using Unicode half-block characters (`▀`) for 2x vertical sub-pixel resolution. The C++ core handles terminal I/O, differential rendering, and ANSI escape generation. You bring the pixels — from numpy, GPU compute (Taichi/wgpu), or any source.

## Install

```bash
uv pip install .

# With GPU examples (Taichi on Metal/Vulkan/CUDA)
uv pip install ".[gpu]"
```

## Examples

```bash
# GPU-accelerated demoscene (4 effects with auto-cycling)
uv run python python/examples/demoscene.py

# Individual demos
uv run python python/examples/plasma.py          # 2D plasma (pure numpy)
uv run python python/examples/game_of_life.py    # Conway's GoL
uv run python python/examples/sdf_taichi.py      # SDF raymarcher
uv run python python/examples/sdf_warp.py        # Domain warping (4 scenes)

# C++ examples
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/fire                                      # Doom-style fire
./build/starfield                                 # 3D starfield
```

### Demoscene showcase

The `demoscene.py` demo cycles through four GPU-rendered effects:

1. **Tunnel** — infinite hexagonal tunnel with depth-pulsing specular highlights
2. **Metaballs** — 5 raymarched blobs with per-blob materials:
   - Glass blob with Fresnel refraction (see distorted floor through it)
   - Chrome mirror blob (reflects other blobs and floor via secondary ray march)
   - 3 iridescent opaque blobs with cosine palette coloring
3. **Terrain** — raymarched procedural landscape with multi-octave heightmap, fixed-step marching with surface interpolation, sun disc, specular sheen, distance fog
4. **Vortex** — spiral with twisted layered rings and center glow

All effects include scanline + vignette post-processing.

Keys: `1`-`4` select effect, `space` toggle auto-cycle, `WASD` orbit (SDF demos), `q` quit.

## Quick start

```python
import cliviz
import numpy as np

with cliviz.Terminal() as term:
    pb = cliviz.PixelBuffer(term.cols, term.rows)

    # Write pixels however you want
    pixels = pb.pixels  # numpy (H, W, 3) uint8 — zero-copy into C++ buffer
    pixels[10:20, 10:30] = [255, 0, 0]  # red rectangle

    pb.flush_full()
    input()
```

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

# Frame output
pb.flush()                     # encode dirty + diff + write (partial updates)
pb.flush_full()                # encode all + write (full redraws)

# For text overlays on full redraws: split encode and present
pb.encode_all()                # pixels → cells
pb.draw_text(0, 0, "60fps")   # text on top (after encode, before present)
pb.present_nodiff()            # write all cells to terminal
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
│                            │             │
│                   draw_text overlay      │
│                  (terminal native font)  │
└──────────────────────────────────────────┘
               │
               ▼
          Terminal (Ghostty, Kitty, etc.)
```

## Why C++ instead of pure Python?

The diff engine + ANSI serialization is the one path where Python is too slow. At 120fps with ~8K cells, building variable-length ANSI escape sequences per frame requires sub-millisecond serialization. The C++ core is ~500 lines — the thinnest possible native layer. Pixel computation (the expensive part) stays in Python/GPU.

## Project structure

```
pyproject.toml               Python packaging (scikit-build-core + nanobind)
CMakeLists.txt               C++ build
csrc/
  include/cliviz/            public C++ headers
  src/                       implementation (~500 LOC)
  examples/                  C++ demos (fire, starfield)
  tests/                     GTest suite (68 tests)
python/
  cliviz/                    Python package
    __init__.py
    _native.cpp              nanobind bindings
  examples/                  Python demos
    demoscene.py             GPU multi-effect showcase
    plasma.py                2D plasma (numpy)
    game_of_life.py          Conway's GoL
    sdf_taichi.py            SDF raymarcher (Taichi Metal)
    sdf_warp.py              Domain warping (4 scenes)
  tests/                     pytest suite (9 tests)
```

## Development

```bash
# Python
uv pip install -e ".[test]"
uv run python -m pytest

# C++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## License

MIT — Jonas Köhler
