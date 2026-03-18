"""Trippy SDF with domain warping, repetition, and fractal folding.

Impossible with triangle rasterizers — SDF domain manipulation creates
infinite mirrored tunnels, melting geometry, and organic structures.

Install: uv pip install ".[gpu]"
Run:     uv run python python/examples/sdf_warp.py
Keys:    WASD orbit, 1-4 switch scenes, q quit
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


# ── SDF primitives ──


@ti.func
def sd_sphere(p: ti.math.vec3, r: float) -> float:
    return p.norm() - r


@ti.func
def sd_box(p: ti.math.vec3, b: ti.math.vec3) -> float:
    d = ti.abs(p) - b
    return ti.math.length(ti.max(d, 0.0)) + ti.min(ti.max(d.x, ti.max(d.y, d.z)), 0.0)


@ti.func
def sd_torus(p: ti.math.vec3, t1: float, t2: float) -> float:
    q = ti.math.vec2(ti.math.length(ti.math.vec2(p.x, p.z)) - t1, p.y)
    return q.norm() - t2


@ti.func
def smooth_min(a: float, b: float, k: float) -> float:
    h = ti.math.clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


@ti.func
def op_rep(p: ti.math.vec3, s: ti.math.vec3) -> ti.math.vec3:
    """Infinite repetition via domain modulo."""
    return ti.math.mod(p + s * 0.5, s) - s * 0.5


@ti.func
def op_twist(p: ti.math.vec3, k: float) -> ti.math.vec3:
    """Twist along Y axis."""
    c, s = ti.cos(k * p.y), ti.sin(k * p.y)
    return ti.math.vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z)


@ti.func
def op_fold(p: ti.math.vec3) -> ti.math.vec3:
    """Mirror fold — creates kaleidoscopic symmetry."""
    return ti.abs(p)


# ── Scenes ──

scene_id = ti.field(dtype=ti.i32, shape=())


@ti.func
def scene_infinite_spheres(p: ti.math.vec3, t: float) -> float:
    """Infinite grid of pulsing spheres."""
    rp = op_rep(p, ti.math.vec3(3.0))
    r = 0.8 + 0.3 * ti.sin(p.x * 0.5 + t) * ti.sin(p.z * 0.5 + t * 0.7)
    return sd_sphere(rp, r)


@ti.func
def scene_twisted_torus(p: ti.math.vec3, t: float) -> float:
    """Torus with animated twist and domain warping."""
    tp = op_twist(p, ti.sin(t * 0.3) * 2.0)
    torus = sd_torus(tp, 1.5, 0.4)
    # Add organic noise via domain displacement
    displacement = ti.sin(p.x * 3.0 + t) * ti.sin(p.y * 3.0 + t * 0.7) * ti.sin(p.z * 3.0 + t * 1.1) * 0.15
    return torus + displacement


@ti.func
def scene_fractal_fold(p: ti.math.vec3, t: float) -> float:
    """Fractal box folding — creates Menger-sponge-like structures."""
    q = p
    d = sd_box(q, ti.math.vec3(1.0))
    scale = 1.0
    for _ in range(4):
        q = op_fold(q)
        q = q * 3.0 - ti.math.vec3(2.0 + 0.3 * ti.sin(t * 0.5))
        scale *= 3.0
        d = ti.min(d, sd_box(q, ti.math.vec3(1.1)) / scale)
    return d


@ti.func
def scene_melting(p: ti.math.vec3, t: float) -> float:
    """Shapes melting into each other with domain warping."""
    c, s = ti.cos(t * 0.4), ti.sin(t * 0.4)
    rp = ti.math.vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z)
    sphere = sd_sphere(rp - ti.math.vec3(0, 0.5 * ti.sin(t), 0), 1.0)
    box = sd_box(rp, ti.math.vec3(0.7)) - 0.1
    torus = sd_torus(rp + ti.math.vec3(0, 0.3, 0), 1.2, 0.3)
    blend = ti.sin(t * 0.3) * 0.5 + 0.5  # 0..1 oscillation
    k = 0.3 + blend * 0.7
    d = smooth_min(sphere, box, k)
    d = smooth_min(d, torus, k * 0.8)
    floor = p.y + 2.0
    return ti.min(d, floor)


@ti.func
def scene(p: ti.math.vec3, t: float) -> float:
    sid = scene_id[None]
    d = 100.0
    if sid == 0:
        d = scene_infinite_spheres(p, t)
    elif sid == 1:
        d = scene_twisted_torus(p, t)
    elif sid == 2:
        d = scene_fractal_fold(p, t)
    else:
        d = scene_melting(p, t)
    return d


@ti.func
def calc_normal(p: ti.math.vec3, t: float) -> ti.math.vec3:
    e = 0.001
    return ti.math.normalize(ti.math.vec3(
        scene(p + ti.math.vec3(e, 0, 0), t) - scene(p - ti.math.vec3(e, 0, 0), t),
        scene(p + ti.math.vec3(0, e, 0), t) - scene(p - ti.math.vec3(0, e, 0), t),
        scene(p + ti.math.vec3(0, 0, e), t) - scene(p - ti.math.vec3(0, 0, e), t),
    ))


@ti.kernel
def render(pixels: ti.types.ndarray(dtype=ti.u8, ndim=3),
           w: int, h: int, t: float,
           eye_x: float, eye_y: float, eye_z: float):
    eye = ti.math.vec3(eye_x, eye_y, eye_z)
    fwd = ti.math.normalize(-eye)
    right = ti.math.normalize(ti.math.cross(fwd, ti.math.vec3(0, 1, 0)))
    up = ti.math.cross(right, fwd)
    aspect = float(w) / float(h)
    light_dir = ti.math.normalize(ti.math.vec3(-0.5, 0.8, 0.6))

    for py, px in ti.ndrange(h, w):
        u = (2.0 * (float(px) + 0.5) / float(w) - 1.0) * aspect
        v = 1.0 - 2.0 * (float(py) + 0.5) / float(h)
        rd = ti.math.normalize(fwd + right * u + up * v)

        t_ray = 0.0
        hit = False
        for _ in range(80):
            p = eye + rd * t_ray
            d = scene(p, t)
            if d < 0.003:
                hit = True
                break
            if t_ray > 30.0:
                break
            t_ray += d

        r, g, b = 0, 0, 0
        if hit:
            p = eye + rd * t_ray
            n = calc_normal(p, t)
            diff = ti.max(ti.math.dot(n, light_dir), 0.0)

            # Iridescent coloring from normal direction
            mat = ti.math.vec3(
                0.5 + 0.5 * ti.sin(n.x * 3.0 + t),
                0.5 + 0.5 * ti.sin(n.y * 3.0 + t * 0.7 + 2.0),
                0.5 + 0.5 * ti.sin(n.z * 3.0 + t * 1.3 + 4.0),
            )
            col = mat * (diff * 0.8 + 0.2)
            fog = ti.exp(-t_ray * 0.06)
            sky = ti.math.vec3(0.05, 0.05, 0.12)
            col = col * fog + sky * (1.0 - fog)

            r = int(ti.math.clamp(col.x * 255.0, 0.0, 255.0))
            g = int(ti.math.clamp(col.y * 255.0, 0.0, 255.0))
            b = int(ti.math.clamp(col.z * 255.0, 0.0, 255.0))
        else:
            sky_t = 0.5 * (rd.y + 1.0)
            sky = ti.math.vec3(0.05, 0.05, 0.12) * sky_t + ti.math.vec3(0.02, 0.01, 0.05) * (1.0 - sky_t)
            r = int(ti.math.clamp(sky.x * 255.0, 0.0, 255.0))
            g = int(ti.math.clamp(sky.y * 255.0, 0.0, 255.0))
            b = int(ti.math.clamp(sky.z * 255.0, 0.0, 255.0))

        pixels[py, px, 0] = r
        pixels[py, px, 1] = g
        pixels[py, px, 2] = b


# ── Input + main loop ──

SCENE_NAMES = ["infinite spheres", "twisted torus", "fractal fold", "melting shapes"]


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
        b2 = os.read(fd, 2)
        if len(b2) == 2 and b2[0] == ord("["):
            return {65: ord("k"), 66: ord("j"), 67: ord("l"), 68: ord("h")}.get(b2[1], 0)
        return 0
    return c


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels

        yaw, pitch, distance = 0.4, 0.3, 5.0
        t = 0.0
        current_scene = 0
        scene_id[None] = current_scene
        pacer = cliviz.FramePacer(target_fps=60)

        while True:
            dt = pacer.pace()
            t += dt * 0.6

            key = read_key(sys.stdin.fileno())
            while key:
                if key == ord("q"):
                    return
                elif key in (ord("a"), ord("h")):
                    yaw -= 0.1
                elif key in (ord("d"), ord("l")):
                    yaw += 0.1
                elif key in (ord("w"), ord("k")):
                    pitch += 0.05
                elif key in (ord("s"), ord("j")):
                    pitch -= 0.05
                elif key in (ord("1"), ord("2"), ord("3"), ord("4")):
                    current_scene = key - ord("1")
                    scene_id[None] = current_scene
                key = read_key(sys.stdin.fileno())

            pitch = max(-1.4, min(1.4, pitch))
            cx, sx = math.cos(pitch), math.sin(pitch)
            cy, sy = math.cos(yaw), math.sin(yaw)

            render(pixels, pb.width, pb.height, t,
                   distance * cx * sy, distance * sx, distance * cx * cy)

            pb.encode_all()
            name = SCENE_NAMES[current_scene]
            pb.draw_text(1, 0, f"{pacer.fps:.0f}fps  {name}  [1-4]scenes [wasd]orbit [q]uit",
                         255, 255, 255, 20, 20, 30)
            pb.present()


if __name__ == "__main__":
    main()
