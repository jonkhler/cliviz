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

    # Hexagonal pattern instead of checkerboard
    v1 = ti.sin(tx * 6.0) * ti.sin(ty * 3.0)
    v2 = ti.sin(tx * 3.0 + ty * 6.0 + t)
    pattern = ti.math.clamp(v1 * 0.5 + v2 * 0.3 + 0.5, 0.0, 1.0)

    # Center glow + depth glow
    center_glow = 0.03 / (radius * radius + 0.03)
    depth_pulse = ti.sin(ty * 0.5 - t * 3.0) * 0.5 + 0.5

    base = palette(ty * 0.08 + t * 0.05,
                   ti.math.vec3(0.5), ti.math.vec3(0.5),
                   ti.math.vec3(1.0, 0.7, 0.4), ti.math.vec3(0.0, 0.15, 0.2))

    col = base * (pattern * 0.6 + 0.2)
    # Specular-like highlights on the tunnel walls
    spec = ti.pow(ti.max(ti.sin(tx * 12.0 + t * 2.0), 0.0), 8.0) * depth_pulse
    col += ti.math.vec3(0.8, 0.9, 1.0) * spec * 0.3 / (radius + 0.1)
    col += ti.math.vec3(center_glow * 0.6, center_glow * 0.3, center_glow * 0.8)
    return col


# ── Effect 2: 3D Raymarched Metaballs ──

@ti.func
def smooth_min(a: float, b: float, k: float) -> float:
    h = ti.math.clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


@ti.func
def blob_pos(i: int, t: float) -> ti.math.vec3:
    fi = float(i)
    return ti.math.vec3(
        ti.sin(t * (0.7 + fi * 0.13) + fi * 1.3) * 1.2,
        ti.cos(t * (0.5 + fi * 0.17) + fi * 2.1) * 0.6,
        ti.sin(t * (0.3 + fi * 0.19) + fi * 0.7) * 1.0,
    )


@ti.func
def sdf_metaballs(p: ti.math.vec3, t: float) -> float:
    d = (p - blob_pos(0, t)).norm() - 0.55  # blob 0: glass
    sd1 = (p - blob_pos(1, t)).norm() - 0.55  # blob 1: mirror
    d = smooth_min(d, sd1, 0.05)  # tiny k — almost hard union, keeps them distinct
    for i in range(2, 5):
        sd = (p - blob_pos(i, t)).norm() - (0.4 + 0.1 * ti.sin(t + float(i)))
        d = smooth_min(d, sd, 0.4)
    return d


@ti.func
def closest_blob_id(p: ti.math.vec3, t: float) -> int:
    """Return 0 for glass blob, 1 for mirror blob, 2+ for opaque."""
    best_id = 0
    best_d = (p - blob_pos(0, t)).norm()
    for i in range(1, 5):
        dd = (p - blob_pos(i, t)).norm()
        if dd < best_d:
            best_d = dd
            best_id = i
    return best_id


@ti.func
def sdf_meta_scene(p: ti.math.vec3, t: float) -> float:
    return ti.min(sdf_metaballs(p, t), p.y + 1.5)


@ti.func
def meta_normal(p: ti.math.vec3, t: float) -> ti.math.vec3:
    e = 0.001
    return ti.math.normalize(ti.math.vec3(
        sdf_meta_scene(p + ti.math.vec3(e, 0, 0), t) - sdf_meta_scene(p - ti.math.vec3(e, 0, 0), t),
        sdf_meta_scene(p + ti.math.vec3(0, e, 0), t) - sdf_meta_scene(p - ti.math.vec3(0, e, 0), t),
        sdf_meta_scene(p + ti.math.vec3(0, 0, e), t) - sdf_meta_scene(p - ti.math.vec3(0, 0, e), t),
    ))


@ti.func
def refract_ray(rd: ti.math.vec3, n: ti.math.vec3, eta: float) -> ti.math.vec3:
    """Snell's law refraction. Returns zero vector for total internal reflection."""
    cos_i = -ti.math.dot(n, rd)
    sin2_t = eta * eta * (1.0 - cos_i * cos_i)
    cos_t = ti.sqrt(ti.max(1.0 - sin2_t, 0.0))
    return rd * eta + n * (eta * cos_i - cos_t)


@ti.func
def metaballs(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0
    p.y = -p.y  # flip Y so floor is at bottom

    # Camera — orbit, look slightly down at the blobs
    cam_angle = t * 0.4
    eye = ti.math.vec3(ti.sin(cam_angle) * 4.0, 2.0, ti.cos(cam_angle) * 4.0)
    target = ti.math.vec3(0.0, 0.0, 0.0)
    fwd = ti.math.normalize(target - eye)
    right = ti.math.normalize(ti.math.cross(fwd, ti.math.vec3(0, 1, 0)))
    up = ti.math.cross(right, fwd)
    rd = ti.math.normalize(fwd + right * p.x * 0.8 + up * p.y * 0.5)

    # Raymarch
    ray_t = 0.0
    hit = False
    for _ in range(80):
        pos = eye + rd * ray_t
        d = sdf_meta_scene(pos, t)
        if d < 0.003:
            hit = True
            break
        if ray_t > 25.0:
            break
        ray_t += d

    bg_col = ti.math.vec3(0.08, 0.05, 0.15)
    col = bg_col
    if hit:
        pos = eye + rd * ray_t
        n = meta_normal(pos, t)
        light = ti.math.normalize(ti.math.vec3(1.0, 1.5, -1.0))
        diff = ti.max(ti.math.dot(n, light), 0.0)
        half_v = ti.math.normalize(light - rd)
        spec = ti.pow(ti.max(ti.math.dot(n, half_v), 0.0), 64.0)
        rim = ti.pow(1.0 - ti.max(ti.math.dot(n, -rd), 0.0), 3.0)
        fresnel = 0.04 + 0.96 * ti.pow(1.0 - ti.max(ti.math.dot(n, -rd), 0.0), 5.0)

        is_floor = ti.math.clamp((-(pos.y + 1.4)) * 100.0, 0.0, 1.0)
        blob_id = closest_blob_id(pos, t)

        # Floor: checkerboard
        check = ti.math.mod(ti.floor(pos.x * 2.0) + ti.floor(pos.z * 2.0), 2.0)
        if check < 0.0:
            check += 2.0
        floor_col = ti.math.vec3(0.12, 0.12, 0.18) + ti.math.vec3(0.08) * check

        if is_floor > 0.5:
            # Floor
            col = floor_col * (diff * 0.6 + 0.25) + ti.math.vec3(spec * 0.3)
        elif blob_id == 0:
            # GLASS BLOB — refract through to floor
            refr_rd = refract_ray(rd, n, 1.0 / 1.5)
            floor_t = (-1.5 - pos.y) / (refr_rd.y - 0.0001)
            refr_col = bg_col * 0.5 + ti.math.vec3(0.05, 0.08, 0.12)
            if floor_t > 0.0:
                fp = pos + refr_rd * floor_t
                fc = ti.math.mod(ti.floor(fp.x * 2.0) + ti.floor(fp.z * 2.0), 2.0)
                if fc < 0.0:
                    fc += 2.0
                refr_col = ti.math.vec3(0.1, 0.15, 0.22) + ti.math.vec3(0.1) * fc
            # Fresnel: edges reflect, center refracts
            col = refr_col * (1.0 - fresnel) + ti.math.vec3(0.4, 0.6, 0.9) * fresnel
            col += ti.math.vec3(spec * 2.0)  # bright specular highlight
            col += ti.math.vec3(0.15, 0.25, 0.4) * rim  # blue rim
        elif blob_id == 1:
            # MIRROR BLOB — reflect and march again
            refl_rd = rd - n * 2.0 * ti.math.dot(rd, n)
            refl_col = bg_col
            refl_t = 0.05
            for _ in range(40):
                rp = pos + refl_rd * refl_t
                rd_refl = sdf_meta_scene(rp, t)
                if rd_refl < 0.005:
                    rn = meta_normal(rp, t)
                    rdiff = ti.max(ti.math.dot(rn, light), 0.0)
                    # Reflected hit — color based on what we hit
                    r_floor = ti.math.clamp((-(rp.y + 1.4)) * 100.0, 0.0, 1.0)
                    rc = ti.math.mod(ti.floor(rp.x * 2.0) + ti.floor(rp.z * 2.0), 2.0)
                    if rc < 0.0:
                        rc += 2.0
                    r_mat = ti.math.vec3(0.12, 0.12, 0.18) + ti.math.vec3(0.08) * rc
                    r_blob = palette(ti.math.dot(rn, ti.math.vec3(1, 0.5, 0.3)) * 0.5 + t * 0.1,
                                     ti.math.vec3(0.5), ti.math.vec3(0.5),
                                     ti.math.vec3(1, 0.7, 0.4), ti.math.vec3(0, 0.15, 0.2))
                    refl_col = r_mat * r_floor + r_blob * (1.0 - r_floor)
                    refl_col *= (rdiff * 0.6 + 0.2)
                    break
                refl_t += rd_refl
                if refl_t > 15.0:
                    break
            # Mirror: tinted slightly chrome
            col = refl_col * 0.85 + ti.math.vec3(0.1, 0.1, 0.12) * 0.15
            col += ti.math.vec3(spec * 1.5)
            col += ti.math.vec3(0.8, 0.85, 0.9) * rim * 0.15
        else:
            # OPAQUE BLOBS — iridescent
            mat = palette(ti.math.dot(n, ti.math.vec3(1, 0.5, 0.3)) * 0.5 + t * 0.1,
                          ti.math.vec3(0.5), ti.math.vec3(0.5),
                          ti.math.vec3(1, 0.7, 0.4), ti.math.vec3(0, 0.15, 0.2))
            col = mat * (diff * 0.6 + 0.15) + ti.math.vec3(spec) * 0.6
            col += ti.math.vec3(0.2, 0.4, 0.8) * rim * 0.3

        fog = ti.exp(-ray_t * 0.06)
        col = col * fog + bg_col * (1.0 - fog)
    return col


# ── Effect 3: Raymarched terrain ──

@ti.func
def terrain_height(p: ti.math.vec2) -> float:
    # Multi-octave noise for more interesting terrain
    h = ti.sin(p.x * 0.3) * ti.cos(p.y * 0.2) * 1.0
    h += ti.sin(p.x * 0.7 + p.y * 0.5) * 0.5
    h += ti.sin(p.x * 1.5 - p.y * 1.2) * 0.25
    h += ti.sin(p.x * 3.1 + p.y * 2.7) * 0.1
    return h


@ti.func
def terrain(uv: ti.math.vec2, t: float) -> ti.math.vec3:
    p = uv * 2.0 - 1.0
    p.y = -p.y

    # Camera — fly forward, look slightly down
    ro = ti.math.vec3(t * 1.5, 3.0 + ti.sin(t * 0.3) * 0.5, t * 1.0)
    rd = ti.math.normalize(ti.math.vec3(p.x * 0.7, p.y * 0.4 - 0.25, 1.0))

    sky_col = ti.math.vec3(0.25, 0.15, 0.4)
    sun_dir = ti.math.normalize(ti.math.vec3(0.5, 0.8, -0.3))
    col = sky_col

    # Sky gradient with warm horizon
    sky_t = ti.math.clamp(rd.y * 3.0 + 0.3, 0.0, 1.0)
    horizon = ti.math.vec3(0.6, 0.3, 0.2)
    col = horizon * (1.0 - sky_t) + sky_col * sky_t

    # Fixed-step terrain march — uniform steps avoid horizon artifacts
    hit_t = 0.0
    prev_d = 1e10
    dt_step = 0.15  # fixed step size
    for _ in range(200):
        pos = ro + rd * hit_t
        h = terrain_height(ti.math.vec2(pos.x, pos.z))
        d = pos.y - h
        # Crossed the surface (sign change) — interpolate for smooth hit
        if d < 0.0:
            # Interpolate back to surface
            hit_t -= dt_step * d / (d - prev_d + 0.0001)
            pos = ro + rd * hit_t
            h = terrain_height(ti.math.vec2(pos.x, pos.z))

            n_eps = 0.02
            nx = terrain_height(ti.math.vec2(pos.x + n_eps, pos.z)) - terrain_height(ti.math.vec2(pos.x - n_eps, pos.z))
            nz = terrain_height(ti.math.vec2(pos.x, pos.z + n_eps)) - terrain_height(ti.math.vec2(pos.x, pos.z - n_eps))
            normal = ti.math.normalize(ti.math.vec3(-nx, 2.0 * n_eps, -nz))

            diff = ti.max(ti.math.dot(normal, sun_dir), 0.0)
            half_v = ti.math.normalize(sun_dir - rd)
            spec = ti.pow(ti.max(ti.math.dot(normal, half_v), 0.0), 32.0)

            terrain_col = palette(h * 0.15 + 0.5,
                                  ti.math.vec3(0.2, 0.35, 0.1), ti.math.vec3(0.25, 0.3, 0.15),
                                  ti.math.vec3(1.0, 0.8, 0.5), ti.math.vec3(0.0, 0.1, 0.15))

            col = terrain_col * (diff * 0.7 + 0.25) + ti.math.vec3(1.0, 0.9, 0.7) * spec * 0.3

            fog = ti.exp(-hit_t * 0.05)
            col = col * fog + horizon * (1.0 - fog)
            break
        prev_d = d
        hit_t += dt_step
        if hit_t > 30.0:
            break

    # Sun disc + halo
    sun_dot = ti.max(ti.math.dot(rd, sun_dir), 0.0)
    col += ti.math.vec3(1.0, 0.7, 0.3) * ti.pow(sun_dot, 64.0) * 1.0
    col += ti.math.vec3(1.0, 0.5, 0.2) * ti.pow(sun_dot, 8.0) * 0.15
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
        pacer = cliviz.FramePacer(target_fps=60)

        while True:
            dt = pacer.pace()
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
            name = EFFECT_NAMES[current]
            label = f" {pacer.fps:.0f}fps  {name}  [1-4]select [space]auto [q]uit "
            pb.draw_text(1, 0, label, 220, 220, 220, 20, 20, 30)
            pb.present()


if __name__ == "__main__":
    main()
