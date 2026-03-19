"""Interactive web browser in the terminal via headless Chromium.

Install: uv pip install ".[browser]" && playwright install chromium
Run:     uv run python python/examples/browser.py [url] [--proxy socks5://localhost:1080]
Keys:    mouse click, scroll, type text. Ctrl-Q=quit
"""

import io
import os
import re
import select
import sys

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

import cliviz


def enable_mouse():
    """Enable SGR mouse tracking (click + scroll + motion)."""
    sys.stdout.buffer.write(b"\x1b[?1000h\x1b[?1006h")
    sys.stdout.buffer.flush()


def disable_mouse():
    sys.stdout.buffer.write(b"\x1b[?1000l\x1b[?1006l")
    sys.stdout.buffer.flush()


def read_input(fd: int) -> list:
    """Read all pending input, return list of events.

    Events: ('key', char), ('mouse', button, x, y, pressed), ('scroll', direction, x, y)
    """
    events = []
    buf = b""

    while select.select([fd], [], [], 0)[0]:
        chunk = os.read(fd, 256)
        if not chunk:
            break
        buf += chunk

    i = 0
    while i < len(buf):
        b = buf[i]

        if b == 0x1B and i + 2 < len(buf) and buf[i + 1] == ord("[") and buf[i + 2] == ord("<"):
            # SGR mouse: \e[<Cb;Cx;Cy M/m
            end = buf.find(ord("M"), i + 3)
            is_release = False
            if end == -1:
                end = buf.find(ord("m"), i + 3)
                is_release = True
            if end == -1:
                i += 1
                continue
            parts = buf[i + 3:end].decode("ascii", errors="ignore").split(";")
            if len(parts) == 3:
                cb, cx, cy = int(parts[0]), int(parts[1]), int(parts[2])
                button = cb & 0x03
                is_motion = bool(cb & 32)
                is_wheel = bool(cb & 64)
                if is_wheel:
                    direction = "up" if button == 0 else "down"
                    events.append(("scroll", direction, cx, cy))
                elif not is_motion:
                    events.append(("mouse", button, cx, cy, not is_release))
            i = end + 1
        elif b == 0x1B:
            # Other escape sequences — skip
            i += 1
            while i < len(buf) and buf[i] not in range(0x40, 0x7F):
                i += 1
            i += 1
        elif b < 0x20:
            # Control character
            events.append(("key", chr(b)))
            i += 1
        else:
            # Regular character
            events.append(("key", chr(b)))
            i += 1

    return events


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Terminal web browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280,
                        help="Layout width in CSS pixels (default 1280). "
                             "Browser renders at this resolution; scale factor maps to terminal.")
    args = parser.parse_args()

    url = args.url

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=30)

        def make_context(browser, layout_w: int, px_w: int, px_h: int):
            """Create a browser context with deviceScaleFactor so screenshots
            arrive at terminal pixel dimensions with no resizing needed."""
            # Clamp scale: Chromium rejects values outside [0.1, 4.0]
            scale = max(0.1, min(4.0, px_w / layout_w))
            layout_h = int(px_h / scale)
            return browser.new_context(
                viewport={"width": layout_w, "height": layout_h},
                device_scale_factor=scale,
            ), layout_w, layout_h, scale

        def copy_screenshot(jpg: bytes, pb: "cliviz.PixelBuffer") -> None:
            """Decode screenshot and write into pixel buffer.
            Handles size mismatches: resizes if needed, clears first."""
            arr = np.array(Image.open(io.BytesIO(jpg)).convert("RGB"), dtype=np.uint8)
            pb.pixels[:] = 0  # clear stale content
            if arr.shape[0] == pb.height and arr.shape[1] == pb.width:
                pb.pixels[:] = arr
            else:
                img = Image.fromarray(arr).resize((pb.width, pb.height), Image.BILINEAR)
                pb.pixels[:] = np.array(img, dtype=np.uint8)

        launch_opts: dict = {"headless": True}
        if args.proxy:
            launch_opts["proxy"] = {"server": args.proxy}

        browser = pw.chromium.launch(**launch_opts)
        ctx, layout_w, layout_h, scale = make_context(
            browser, args.width, pb.width, pb.height)
        page = ctx.new_page()
        page.goto(url, wait_until="domcontentloaded")

        enable_mouse()
        needs_refresh = True

        try:
            while True:
                pacer.pace()

                # Handle terminal resize (font size change → different cols/rows)
                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)
                    # Recompute scale for new terminal size; layout stays fixed
                    scale = pb.width / layout_w
                    needs_refresh = True

                for event in read_input(sys.stdin.fileno()):
                    if event[0] == "key":
                        ch = event[1]
                        if ch == "\x11":  # Ctrl-Q
                            return
                        elif ch == "\x0c":  # Ctrl-L — navigate
                            disable_mouse()
                            # Can't easily prompt in raw mode, so just go to a fixed URL
                            # In a real app you'd implement a URL bar
                            pass
                        elif ch == "\r":
                            page.keyboard.press("Enter")
                            needs_refresh = True
                        elif ch == "\x7f":  # backspace
                            page.keyboard.press("Backspace")
                            needs_refresh = True
                        elif ch == "\t":
                            page.keyboard.press("Tab")
                            needs_refresh = True
                        else:
                            page.keyboard.type(ch)
                            needs_refresh = True

                    elif event[0] == "mouse":
                        _, button, cx, cy, pressed = event
                        if pressed and button == 0:
                            # Terminal cell (1-based) → pixel → CSS layout coords
                            px = (cx - 1)          # pixel x
                            py = (cy - 1) * 2      # pixel y (2 pixel rows per cell)
                            page.mouse.click(px / scale, py / scale)
                            needs_refresh = True

                    elif event[0] == "scroll":
                        _, direction, cx, cy = event
                        delta = -60 if direction == "up" else 60
                        page.mouse.wheel(0, delta)
                        needs_refresh = True

                if needs_refresh:
                    try:
                        jpg = page.screenshot(type="jpeg", quality=60)
                        copy_screenshot(jpg, pb)
                    except Exception as e:
                        pb.draw_text(0, 1, f"err:{e}"[:pb.width], 255, 80, 80, 0, 0, 0)

                    needs_refresh = False
                    pb.encode_all()
                    pb.draw_text(1, 0,
                                 f" {pacer.fps:.0f}fps  {page.url[:60]}  Ctrl-Q=quit ",
                                 255, 255, 255, 30, 30, 50)
                    pb.present()

        finally:
            disable_mouse()
            browser.close()


if __name__ == "__main__":
    main()
