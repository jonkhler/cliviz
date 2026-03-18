"""Spinning cube demo — Python driving the cliviz C++ rendering engine."""

import math
import os
import sys
import time

import numpy as np

import cliviz


def read_key(fd: int) -> int:
    """Non-blocking key read from raw stdin. Returns 0 if nothing available."""
    import select

    if not select.select([fd], [], [], 0)[0]:
        return 0
    b = os.read(fd, 1)
    if not b:
        return 0
    c = b[0]
    if c == 0x1B:  # escape sequence
        if not select.select([fd], [], [], 0.03)[0]:
            return 0
        b2 = os.read(fd, 2)
        if len(b2) == 2 and b2[0] == ord("["):
            return {65: ord("k"), 66: ord("j"), 67: ord("l"), 68: ord("h")}.get(
                b2[1], 0
            )
        return 0
    return c


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows - 1)
        zb = cliviz.ZBuffer(pb.width, pb.height)
        mesh = cliviz.make_cube()

        aspect = pb.width / pb.height
        proj = cliviz.perspective(math.pi / 3, aspect, 0.1, 100.0)

        yaw, pitch, distance = 0.0, 0.3, 4.0
        angle = 0.0
        auto_rotate = True
        last = time.monotonic()

        while True:
            now = time.monotonic()
            dt = now - last
            last = now

            # Input
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
                elif key == ord(" "):
                    auto_rotate = not auto_rotate
                key = read_key(sys.stdin.fileno())

            pitch = max(-1.4, min(1.4, pitch))
            if auto_rotate:
                angle += dt * 0.8

            # Camera
            cx, sx = math.cos(pitch), math.sin(pitch)
            cy, sy = math.cos(yaw), math.sin(yaw)
            eye = np.array(
                [distance * cx * sy, distance * sx, distance * cx * cy],
                dtype=np.float32,
            )
            view = cliviz.look_at(
                eye,
                np.zeros(3, dtype=np.float32),
                np.array([0, 1, 0], dtype=np.float32),
            )

            model = cliviz.rotate_y(angle) @ cliviz.rotate_x(angle * 0.3)
            mvp = (proj @ view @ model).astype(np.float32)

            # Render
            pb.clear(15, 15, 25)
            zb.clear()
            tris = cliviz.rasterize(pb, zb, mesh, mvp)
            pb.flush()

            time.sleep(max(0, 0.016 - (time.monotonic() - now)))


if __name__ == "__main__":
    main()
