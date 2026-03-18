"""2D plasma effect — pure numpy, no 3D, no GPU.

Classic demoscene color math rendered into cliviz's pixel buffer.
Run: uv run python python/examples/plasma.py
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
    return b[0] if b else 0


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels
        h, w = pixels.shape[:2]

        # Precompute coordinate grids
        y_grid, x_grid = np.mgrid[0:h, 0:w].astype(np.float32)
        x_norm = x_grid / w * 8.0
        y_norm = y_grid / h * 8.0

        t = 0.0
        last = time.monotonic()

        while True:
            now = time.monotonic()
            dt = now - last
            last = now
            t += dt * 0.8

            if read_key(sys.stdin.fileno()) == ord("q"):
                return

            # Plasma function — sum of sines at different frequencies
            v1 = np.sin(x_norm + t)
            v2 = np.sin(y_norm * 0.7 + t * 1.3)
            v3 = np.sin((x_norm + y_norm + t) * 0.5)
            cx = x_norm + 0.5 * np.sin(t * 0.3)
            cy = y_norm + 0.5 * np.cos(t * 0.5)
            v4 = np.sin(np.sqrt(cx * cx + cy * cy + 1.0) * 2.0)

            v = (v1 + v2 + v3 + v4) * 0.25  # [-1, 1]

            # Color mapping — smooth HSV-like palette
            r = np.clip((np.sin(v * math.pi * 2.0 + 0.0) * 0.5 + 0.5) * 255, 0, 255)
            g = np.clip((np.sin(v * math.pi * 2.0 + 2.1) * 0.5 + 0.5) * 255, 0, 255)
            b = np.clip((np.sin(v * math.pi * 2.0 + 4.2) * 0.5 + 0.5) * 255, 0, 255)

            pixels[:, :, 0] = r.astype(np.uint8)
            pixels[:, :, 1] = g.astype(np.uint8)
            pixels[:, :, 2] = b.astype(np.uint8)

            pb.encode_all()
            fps = 1.0 / dt if dt > 0 else 0
            pb.draw_text(1, 0, f" {fps:.0f}fps  plasma  q=quit ", 255, 255, 255, 40, 0, 40)
            pb.present()

            time.sleep(max(0, 0.008 - (time.monotonic() - now)))  # cap ~120fps


if __name__ == "__main__":
    main()
