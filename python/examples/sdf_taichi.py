"""GPU-accelerated SDF raymarcher using Taichi → cliviz terminal display.

Demonstrates the architecture: Taichi renders pixels on the GPU,
cliviz handles terminal output. Zero CPU pixel computation.

Install: uv pip install ".[gpu]"
Run:     uv run python python/examples/sdf_taichi.py
Keys:    WASD/arrows orbit, +/- zoom, space toggle rotation, q quit
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


# ── SDF scene (runs on GPU) ──


@ti.func
def sdf_sphere(p: ti.math.vec3, r: float) -> float:
    return p.norm() - r


@ti.func
def sdf_box(p: ti.math.vec3, b: ti.math.vec3) -> float:
    d = ti.abs(p) - b
    return ti.math.length(ti.max(d, 0.0)) + ti.min(ti.max(d.x, ti.max(d.y, d.z)), 0.0)


@ti.func
def smooth_union(d1: float, d2: float, k: float) -> float:
    h = ti.math.clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0)
    return d2 * (1.0 - h) + d1 * h - k * h * (1.0 - h)


@ti.func
def scene(p: ti.math.vec3, t: float) -> float:
    # Rotating main shape
    c, s = ti.cos(t), ti.sin(t)
    rp = ti.math.vec3(p.x * c - p.z * s, p.y, p.x * s + p.z * c)
    sphere = sdf_sphere(rp, 1.0)
    cube = sdf_box(rp, ti.math.vec3(0.75)) - 0.05
    main = smooth_union(sphere, cube, 0.3)

    # Orbiting satellite
    orbit_pos = ti.math.vec3(2.0 * ti.cos(t * 1.5), ti.sin(t * 0.7) * 0.5, 2.0 * ti.sin(t * 1.5))
    satellite = sdf_sphere(p - orbit_pos, 0.3)

    # Floor
    floor = p.y + 1.5

    return ti.min(ti.min(main, floor), satellite)


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

        # Raymarch
        t_ray = 0.0
        hit = False
        for _ in range(60):
            p = eye + rd * t_ray
            d = scene(p, t)
            if d < 0.005:
                hit = True
                break
            if t_ray > 20.0:
                break
            t_ray += d

        r, g, b = 0, 0, 0
        if hit:
            p = eye + rd * t_ray
            n = calc_normal(p, t)
            diff = ti.max(ti.math.dot(n, light_dir), 0.0)

            # Shadow
            shadow = 1.0
            st = 0.05
            for _ in range(8):
                sd = scene(p + light_dir * st, t)
                if sd < 0.005:
                    shadow = 0.3
                    break
                st += ti.max(sd, 0.1)
                if st > 5.0:
                    break

            # Material
            mat = ti.math.vec3(ti.abs(n.x) * 0.4 + 0.3,
                               ti.abs(n.y) * 0.4 + 0.3,
                               ti.abs(n.z) * 0.4 + 0.3)
            if n.y > 0.9:
                check = ti.math.mod(ti.floor(p.x) + ti.floor(p.z), 2.0)
                if check < 0.0:
                    check += 2.0
                if check < 1.0:
                    mat = ti.math.vec3(0.4, 0.4, 0.45)
                else:
                    mat = ti.math.vec3(0.6, 0.6, 0.65)

            col = mat * (diff * shadow + 0.15)
            fog = ti.exp(-t_ray * 0.04)
            sky = ti.math.vec3(0.4, 0.5, 0.7)
            col = col * fog + sky * (1.0 - fog)

            r = int(ti.math.clamp(col.x * 255.0, 0.0, 255.0))
            g = int(ti.math.clamp(col.y * 255.0, 0.0, 255.0))
            b = int(ti.math.clamp(col.z * 255.0, 0.0, 255.0))
        else:
            sky_t = 0.5 * (rd.y + 1.0)
            sky = ti.math.vec3(0.4, 0.5, 0.7) * sky_t + ti.math.vec3(0.15, 0.1, 0.2) * (1.0 - sky_t)
            r = int(ti.math.clamp(sky.x * 255.0, 0.0, 255.0))
            g = int(ti.math.clamp(sky.y * 255.0, 0.0, 255.0))
            b = int(ti.math.clamp(sky.z * 255.0, 0.0, 255.0))

        pixels[py, px, 0] = r
        pixels[py, px, 1] = g
        pixels[py, px, 2] = b


# ── Input ──


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


# ── Main loop ──


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels  # numpy view into C++ buffer

        yaw, pitch, distance = 0.4, 0.3, 5.0
        t = 0.0
        auto_rotate = True
        last = time.monotonic()

        while True:
            now = time.monotonic()
            dt = now - last
            last = now

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
                elif key == ord("+") or key == ord("="):
                    distance = max(2.0, distance - 0.3)
                elif key == ord("-"):
                    distance = min(15.0, distance + 0.3)
                elif key == ord(" "):
                    auto_rotate = not auto_rotate
                key = read_key(sys.stdin.fileno())

            pitch = max(-1.4, min(1.4, pitch))
            if auto_rotate:
                t += dt * 0.6

            cx, sx = math.cos(pitch), math.sin(pitch)
            cy, sy = math.cos(yaw), math.sin(yaw)
            eye_x = distance * cx * sy
            eye_y = distance * sx
            eye_z = distance * cx * cy

            # GPU renders directly into the pixel buffer's numpy array
            render(pixels, pb.width, pb.height, t, eye_x, eye_y, eye_z)

            pb.flush_full()

            time.sleep(max(0, 0.016 - (time.monotonic() - now)))


if __name__ == "__main__":
    main()
