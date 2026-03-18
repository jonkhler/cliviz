"""SDF raymarcher demo — morphing shapes with shadows and ambient occlusion.

Run: uv run python python/examples/sdf_demo.py
Keys: WASD/arrows orbit, +/- zoom, space toggle rotation, q quit
"""

import math
import os
import select
import sys
import time

import numpy as np

import cliviz


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
            return {65: ord("k"), 66: ord("j"), 67: ord("l"), 68: ord("h")}.get(
                b2[1], 0
            )
        return 0
    return c


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)

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
            eye = np.array(
                [distance * cx * sy, distance * sx, distance * cx * cy],
                dtype=np.float32,
            )
            center = np.zeros(3, dtype=np.float32)

            cliviz.sdf_render(pb, t, eye, center, max_steps=40)
            pb.flush_full()

            time.sleep(max(0, 0.016 - (time.monotonic() - now)))


if __name__ == "__main__":
    main()
