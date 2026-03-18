"""Demoscene — multi-effect showcase with smooth transitions.

Effects cycle automatically: tunnel, metaballs, terrain, vortex.
Run: uv pip install ".[gpu]" && uv run python python/examples/demoscene.py
Keys: 1-4 jump to effect, space pause, q quit
"""

import math
import os
import select
import sys
import time

import numpy as np
import taichi as ti

import cliviz

ti.init(arch=ti.metal)

effect_id = ti.field(dtype=ti.i32, shape=())
global_time = ti.field(dtype=ti.f32, shape=())


@ti.func
def palette(t: float, a: ti.math.vec3, b: ti.math.vec3,
            c: ti.math.vec3, d: ti.math.vec3) -> ti.math.vec3:
    """Inigo Quilez cosine palette."""
    return a + b * ti.cos(6.28318 * (c * t + d))


@ti.func
def hash21(p: ti.math.vec2) -> float:
    """Pseudo-random hash."""
    q = ti.math.fract(p * ti.math.vec2(123.34, 456.21))
    q += ti.math.dot(q, q + 45.32)
    return ti.math.fract(q.x * q.y)


# ── Effect 1: Infinite tunnel ──

@ti.func
def tunnel(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0
    angle = ti.atan2(p.y, p.x)
    radius = p.norm()

    # Tunnel coordinates
    tx = angle / 3.14159 * 2.0 + t * 0.3
    ty = 1.0 / (radius + 0.001) + t * 2.0

    # Checkerboard pattern with glow
    check = ti.math.mod(ti.floor(tx * 4.0) + ti.floor(ty * 2.0), 2.0)
    glow = 0.02 / (radius * radius + 0.02)

    base = palette(ty * 0.1 + t * 0.1,
                   ti.math.vec3(0.5), ti.math.vec3(0.5),
                   ti.math.vec3(1.0, 1.0, 0.5), ti.math.vec3(0.0, 0.1, 0.2))
    return base * (check * 0.5 + 0.3) + ti.math.vec3(glow * 0.5, glow * 0.3, glow)


# ── Effect 2: Metaballs ──

@ti.func
def metaballs(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0
    p.x *= 1.5  # aspect correction approximation

    energy = 0.0
    for i in range(6):
        fi = float(i)
        bx = ti.sin(t * (0.7 + fi * 0.13) + fi * 1.3) * 0.6
        by = ti.cos(t * (0.5 + fi * 0.17) + fi * 2.1) * 0.6
        bp = ti.math.vec2(bx, by)
        d = (p - bp).norm()
        energy += 0.03 / (d * d + 0.001)

    # Threshold and color
    v = ti.math.clamp(energy - 1.0, 0.0, 3.0) / 3.0
    col = palette(v + t * 0.05,
                  ti.math.vec3(0.5, 0.5, 0.5), ti.math.vec3(0.5, 0.5, 0.5),
                  ti.math.vec3(2.0, 1.0, 0.0), ti.math.vec3(0.5, 0.2, 0.25))
    # Edge glow
    edge = ti.exp(-ti.abs(energy - 1.0) * 5.0) * 2.0
    col += ti.math.vec3(edge * 0.3, edge * 0.5, edge)
    return col * v + ti.math.vec3(0.02, 0.01, 0.04) * (1.0 - v)


# ── Effect 3: Raymarched terrain ──

@ti.func
def terrain_height(p: ti.math.vec2) -> float:
    h = ti.sin(p.x * 0.5) * ti.cos(p.y * 0.3) * 0.5
    h += ti.sin(p.x * 1.3 + p.y * 0.7) * 0.25
    h += ti.sin(p.x * 3.1 - p.y * 2.3) * 0.1
    return h


@ti.func
def terrain(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0

    # Camera
    ro = ti.math.vec3(t * 2.0, 2.0, t * 1.5)
    rd = ti.math.normalize(ti.math.vec3(p.x * 0.8, p.y * 0.5 - 0.3, 1.0))

    # Raymarch terrain
    col = ti.math.vec3(0.1, 0.05, 0.15)  # sky
    hit_t = 0.0
    for _ in range(64):
        pos = ro + rd * hit_t
        h = terrain_height(ti.math.vec2(pos.x, pos.z))
        d = pos.y - h
        if d < 0.01:
            # Hit terrain
            n_eps = 0.05
            nx = terrain_height(ti.math.vec2(pos.x + n_eps, pos.z)) - terrain_height(ti.math.vec2(pos.x - n_eps, pos.z))
            nz = terrain_height(ti.math.vec2(pos.x, pos.z + n_eps)) - terrain_height(ti.math.vec2(pos.x, pos.z - n_eps))
            normal = ti.math.normalize(ti.math.vec3(-nx, 2.0 * n_eps, -nz))
            light = ti.math.normalize(ti.math.vec3(0.5, 0.8, -0.3))
            diff = ti.max(ti.math.dot(normal, light), 0.0)

            terrain_col = palette(h * 0.5 + 0.5,
                                  ti.math.vec3(0.2, 0.4, 0.1), ti.math.vec3(0.3, 0.3, 0.2),
                                  ti.math.vec3(1.0, 0.7, 0.4), ti.math.vec3(0.0, 0.05, 0.2))
            col = terrain_col * (diff * 0.7 + 0.3)

            # Fog
            fog = ti.exp(-hit_t * 0.03)
            col = col * fog + ti.math.vec3(0.1, 0.05, 0.15) * (1.0 - fog)
            break
        hit_t += ti.max(d * 0.5, 0.1)
        if hit_t > 50.0:
            break

    # Sun
    sun_dir = ti.math.normalize(ti.math.vec3(0.5, 0.8, -0.3))
    sun = ti.max(ti.math.dot(rd, sun_dir), 0.0)
    col += ti.math.vec3(1.0, 0.7, 0.3) * ti.pow(sun, 32.0) * 0.5
    return col


# ── Effect 4: Spiral vortex ──

@ti.func
def vortex(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0
    angle = ti.atan2(p.y, p.x)
    radius = p.norm()

    # Spiral distortion
    twist = radius * 5.0 - t * 3.0
    spiral_angle = angle + twist

    # Layered rings
    v1 = ti.sin(spiral_angle * 3.0) * 0.5 + 0.5
    v2 = ti.sin(radius * 10.0 - t * 4.0) * 0.5 + 0.5
    v3 = ti.sin(angle * 5.0 + t * 2.0) * 0.5 + 0.5

    # Combine with distance fade
    intensity = ti.exp(-radius * 0.8)
    col = palette(v1 * 0.3 + v2 * 0.3 + v3 * 0.3 + t * 0.05,
                  ti.math.vec3(0.5), ti.math.vec3(0.5),
                  ti.math.vec3(1.0, 0.7, 0.4), ti.math.vec3(0.0, 0.15, 0.2))

    # Center glow
    glow = 0.05 / (radius * radius + 0.01)
    col = col * intensity + ti.math.vec3(glow * 0.3, glow * 0.1, glow * 0.5)
    return col


# ── Compositor ──

@ti.kernel
def render(pixels: ti.types.ndarray(dtype=ti.u8, ndim=3), w: int, h: int):
    t = global_time[None]
    eid = effect_id[None]

    for py, px in ti.ndrange(h, w):
        uv = ti.math.vec2(float(px) / float(w), float(py) / float(h))

        col = ti.math.vec3(0.0)
        if eid == 0:
            col = tunnel(uv, t)
        elif eid == 1:
            col = metaballs(uv, t)
        elif eid == 2:
            col = terrain(uv, t)
        else:
            col = vortex(uv, t)

        # Scanline effect
        scanline = 0.95 + 0.05 * ti.sin(float(py) * 3.14159)
        col *= scanline

        # Vignette
        vc = (uv - 0.5) * 2.0
        vignette = 1.0 - vc.norm() * 0.4
        col *= vignette

        pixels[py, px, 0] = int(ti.math.clamp(col.x * 255.0, 0.0, 255.0))
        pixels[py, px, 1] = int(ti.math.clamp(col.y * 255.0, 0.0, 255.0))
        pixels[py, px, 2] = int(ti.math.clamp(col.z * 255.0, 0.0, 255.0))


# ── Main ──

EFFECT_NAMES = ["tunnel", "metaballs", "terrain", "vortex"]
EFFECT_DURATION = 8.0  # seconds per effect


def read_key(fd: int) -> int:
    if not select.select([fd], [], [], 0)[0]:
        return 0
    b = os.read(fd, 1)
    if not b:
        return 0
    c = b[0]
    if c == 0x1B:
        if not select.select([fd], [], [], 0.03)[0]:
            return 0
        os.read(fd, 2)
        return 0
    return c


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels

        t = 0.0
        current = 0
        auto_cycle = True
        last = time.monotonic()

        while True:
            now = time.monotonic()
            dt = now - last
            last = now
            t += dt

            key = read_key(sys.stdin.fileno())
            while key:
                if key == ord("q"):
                    return
                elif key in (ord("1"), ord("2"), ord("3"), ord("4")):
                    current = key - ord("1")
                    auto_cycle = False
                elif key == ord(" "):
                    auto_cycle = not auto_cycle
                key = read_key(sys.stdin.fileno())

            if auto_cycle:
                current = int(t / EFFECT_DURATION) % len(EFFECT_NAMES)

            effect_id[None] = current
            global_time[None] = t

            render(pixels, pb.width, pb.height)

            pb.encode_all()
            fps = 1.0 / dt if dt > 0 else 0
            name = EFFECT_NAMES[current]
            label = f" {fps:.0f}fps  {name}  [1-4]select [space]auto [q]uit "
            pb.draw_text(1, 0, label, 220, 220, 220, 20, 20, 30)
            pb.present_nodiff()

            time.sleep(max(0, 0.008 - (time.monotonic() - now)))


if __name__ == "__main__":
    main()
