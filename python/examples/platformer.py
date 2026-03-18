"""Minimal 2D platformer — pure numpy, no GPU.

Run: uv run python python/examples/platformer.py
Keys: A/D or arrows=move, W/space=jump, q=quit
"""

import math
import os
import select
import sys

import numpy as np

import cliviz

# ── Input ──


def read_keys(fd: int) -> set[int]:
    """Read all pending keys, return as a set."""
    keys = set()
    while select.select([fd], [], [], 0)[0]:
        b = os.read(fd, 1)
        if not b:
            break
        c = b[0]
        if c == 0x1B:
            if not select.select([fd], [], [], 0.03)[0]:
                continue
            b2 = os.read(fd, 2)
            if len(b2) == 2 and b2[0] == ord("["):
                keys.add({65: ord("w"), 66: ord("s"), 67: ord("d"), 68: ord("a")}.get(b2[1], 0))
        else:
            keys.add(c)
    return keys


# ── Game state ──

GRAVITY = 600.0
JUMP_VEL = -250.0
MOVE_SPEED = 120.0
PLAYER_W = 3
PLAYER_H = 5


def generate_platforms(w: int, h: int) -> list[tuple[int, int, int, int]]:
    """Generate a set of platforms. Each is (x, y, width, height)."""
    rng = np.random.default_rng(42)
    platforms = []
    # Ground
    platforms.append((0, h - 4, w, 4))
    # Floating platforms
    for i in range(15):
        px = int(rng.integers(10, w - 30))
        py = int(rng.integers(h // 4, h - 15))
        pw = int(rng.integers(15, 40))
        platforms.append((px, py, pw, 3))
    return platforms


def generate_coins(platforms: list, h: int) -> list[tuple[float, float, bool]]:
    """Place coins above platforms."""
    rng = np.random.default_rng(123)
    coins = []
    for px, py, pw, _ in platforms[1:]:  # skip ground
        cx = px + pw // 2
        cy = py - 6
        if 0 < cy < h:
            coins.append([float(cx), float(cy), True])
    return coins


# ── Rendering ──


def draw_rect(pixels: np.ndarray, x: int, y: int, w: int, h: int,
              r: int, g: int, b: int) -> None:
    """Draw a filled rectangle, clipped to pixel bounds."""
    ph, pw = pixels.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(pw, x + w), min(ph, y + h)
    if x0 < x1 and y0 < y1:
        pixels[y0:y1, x0:x1] = [r, g, b]


def draw_player(pixels: np.ndarray, px: float, py: float, facing_right: bool) -> None:
    """Draw a tiny character."""
    x, y = int(px), int(py)
    # Body
    draw_rect(pixels, x, y, PLAYER_W, PLAYER_H, 80, 200, 80)
    # Head
    draw_rect(pixels, x, y - 2, PLAYER_W, 2, 220, 180, 140)
    # Eye
    ex = x + 2 if facing_right else x
    draw_rect(pixels, ex, y - 2, 1, 1, 40, 40, 40)


def draw_coin(pixels: np.ndarray, cx: float, cy: float, t: float) -> None:
    """Draw a bouncing coin."""
    y_off = int(math.sin(t * 4 + cx * 0.1) * 2)
    x, y = int(cx), int(cy) + y_off
    draw_rect(pixels, x - 1, y - 1, 3, 3, 255, 220, 50)
    draw_rect(pixels, x, y, 1, 1, 200, 170, 30)


def draw_background(pixels: np.ndarray, camera_x: float, t: float) -> None:
    """Gradient sky + parallax stars."""
    h, w = pixels.shape[:2]
    # Sky gradient
    for y in range(h):
        frac = y / h
        r = int(20 + frac * 15)
        g = int(15 + frac * 25)
        b = int(50 - frac * 20)
        pixels[y, :] = [r, g, b]

    # Parallax dots (distant stars/particles)
    rng = np.random.default_rng(7)
    for _ in range(40):
        sx = int(rng.integers(0, w * 3))
        sy = int(rng.integers(0, h))
        parallax = 0.2
        screen_x = int((sx - camera_x * parallax) % w)
        if 0 <= screen_x < w and 0 <= sy < h:
            brightness = int(60 + 40 * math.sin(t * 2 + sx * 0.3))
            pixels[sy, screen_x] = [brightness, brightness, brightness + 20]


# ── Main ──


def main() -> None:
    with cliviz.Terminal() as term:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pixels = pb.pixels
        h, w = pixels.shape[:2]

        platforms = generate_platforms(w, h)
        coins = generate_coins(platforms, h)

        # Player state
        px, py = 20.0, float(h - 15)
        vx, vy = 0.0, 0.0
        on_ground = False
        facing_right = True
        score = 0

        # Camera
        camera_x = 0.0

        pacer = cliviz.FramePacer(target_fps=60)
        t = 0.0

        while True:
            dt = pacer.pace()
            t += dt

            # Input
            keys = read_keys(sys.stdin.fileno())
            if ord("q") in keys:
                return

            # Movement
            vx = 0.0
            if ord("a") in keys:
                vx = -MOVE_SPEED
                facing_right = False
            if ord("d") in keys:
                vx = MOVE_SPEED
                facing_right = True
            if (ord("w") in keys or ord(" ") in keys) and on_ground:
                vy = JUMP_VEL
                on_ground = False

            # Physics
            vy += GRAVITY * dt
            px += vx * dt
            py += vy * dt

            # Platform collision
            on_ground = False
            player_bottom = py + PLAYER_H
            for plat_x, plat_y, plat_w, plat_h in platforms:
                # Check if player is above platform and falling
                if (px + PLAYER_W > plat_x and px < plat_x + plat_w and
                        player_bottom >= plat_y and player_bottom <= plat_y + plat_h + vy * dt + 2 and
                        vy >= 0):
                    py = plat_y - PLAYER_H
                    vy = 0
                    on_ground = True

            # World bounds
            px = max(0, min(px, w - PLAYER_W))
            if py > h:
                py = 0.0
                vy = 0.0

            # Coin collection
            for coin in coins:
                if not coin[2]:
                    continue
                cx, cy = coin[0], coin[1]
                if abs(px + PLAYER_W / 2 - cx) < 4 and abs(py + PLAYER_H / 2 - cy) < 4:
                    coin[2] = False
                    score += 1

            # Camera follow
            target_cam = px - w * 0.3
            camera_x += (target_cam - camera_x) * 3.0 * dt

            # Render
            draw_background(pixels, camera_x, t)

            cam = int(camera_x)

            # Platforms
            for plat_x, plat_y, plat_w, plat_h in platforms:
                sx = plat_x - cam
                draw_rect(pixels, sx, plat_y, plat_w, plat_h, 60, 100, 60)
                # Platform top highlight
                draw_rect(pixels, sx, plat_y, plat_w, 1, 80, 140, 80)

            # Coins
            for coin in coins:
                if coin[2]:
                    draw_coin(pixels, coin[0] - cam, coin[1], t)

            # Player
            draw_player(pixels, px - cam, py, facing_right)

            # HUD
            pb.encode_all()
            hud = f" score:{score}  {pacer.fps:.0f}fps  [AD]move [W/space]jump [q]uit "
            pb.draw_text(1, 0, hud, 255, 255, 255, 20, 20, 40)
            pb.present()


if __name__ == "__main__":
    main()
