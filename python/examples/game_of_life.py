"""Conway's Game of Life — demonstrates diff engine efficiency.

Most cells don't change each frame, so the diff engine emits minimal output.
Run: uv run python python/examples/game_of_life.py
Keys: space=pause/resume, r=randomize, c=clear, q=quit
"""

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


def step(grid: np.ndarray) -> np.ndarray:
    """One generation of Game of Life using numpy convolution."""
    neighbors = (
        np.roll(grid, 1, 0) + np.roll(grid, -1, 0)
        + np.roll(grid, 1, 1) + np.roll(grid, -1, 1)
        + np.roll(np.roll(grid, 1, 0), 1, 1)
        + np.roll(np.roll(grid, 1, 0), -1, 1)
        + np.roll(np.roll(grid, -1, 0), 1, 1)
        + np.roll(np.roll(grid, -1, 0), -1, 1)
    )
    return ((grid == 1) & ((neighbors == 2) | (neighbors == 3)) |
            (grid == 0) & (neighbors == 3)).astype(np.uint8)


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels
        h, w = pixels.shape[:2]

        # Random initial state
        grid = (np.random.random((h, w)) > 0.7).astype(np.uint8)
        generation = 0
        running = True
        last = time.monotonic()

        # Color palette: dead=dark blue, alive=green with age fade
        age = np.zeros((h, w), dtype=np.float32)

        while True:
            now = time.monotonic()
            dt = now - last
            last = now

            key = read_key(sys.stdin.fileno())
            while key:
                if key == ord("q"):
                    return
                elif key == ord(" "):
                    running = not running
                elif key == ord("r"):
                    grid = (np.random.random((h, w)) > 0.7).astype(np.uint8)
                    age[:] = 0
                    generation = 0
                elif key == ord("c"):
                    grid[:] = 0
                    age[:] = 0
                    generation = 0
                key = read_key(sys.stdin.fileno())

            if running:
                new_grid = step(grid)
                # Track age for color
                age = np.where(new_grid == 1, np.minimum(age + 0.05, 1.0), age * 0.85)
                grid = new_grid
                generation += 1

            # Render: alive cells get green tint, dead cells dark
            alive = grid.astype(np.float32)
            pixels[:, :, 0] = (alive * 50 + (1 - alive) * 10 + age * 30).astype(np.uint8)
            pixels[:, :, 1] = (alive * 200 + (1 - alive) * 15 + age * 55).astype(np.uint8)
            pixels[:, :, 2] = (alive * 80 + (1 - alive) * 30).astype(np.uint8)

            pb.encode_all()
            fps = 1.0 / dt if dt > 0 else 0
            status = f"{fps:.0f}fps  gen:{generation}  {'running' if running else 'paused'}  [space]pause [r]andom [c]lear [q]uit"
            pb.draw_text(1, 0, status, 255, 255, 255, 0, 40, 0)
            pb.present()

            time.sleep(max(0, 0.033 - (time.monotonic() - now)))  # ~30fps


if __name__ == "__main__":
    main()
