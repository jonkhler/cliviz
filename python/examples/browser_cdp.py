"""High-performance terminal browser using CDP screencast.

Chromium pushes frames via CDP instead of polling screenshots.
Only sends frames when the page actually renders something new.

Install: uv pip install ".[browser]" && playwright install chromium
Run:     uv run python python/examples/browser_cdp.py [url] [--proxy ...]
Keys:    mouse click, scroll, type text. Ctrl-Q=quit
"""

import base64
import io
import os
import select
import sys
import threading

import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

import cliviz


# ── Mouse tracking ──


def enable_mouse():
    sys.stdout.buffer.write(b"\x1b[?1000h\x1b[?1006h")
    sys.stdout.buffer.flush()


def disable_mouse():
    sys.stdout.buffer.write(b"\x1b[?1000l\x1b[?1006l")
    sys.stdout.buffer.flush()


def read_input(fd: int) -> list:
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
            i += 1
            while i < len(buf) and buf[i] not in range(0x40, 0x7F):
                i += 1
            i += 1
        elif b < 0x20:
            events.append(("key", chr(b)))
            i += 1
        else:
            events.append(("key", chr(b)))
            i += 1
    return events


# ── Main ──


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="CDP screencast terminal browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280,
                        help="Layout width in CSS pixels (default 1280)")
    args = parser.parse_args()

    url = args.url

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=60)

        def make_context(browser, layout_w: int, px_w: int, px_h: int):
            scale = px_w / layout_w
            layout_h = int(px_h / scale)
            return browser.new_context(
                viewport={"width": layout_w, "height": layout_h},
                device_scale_factor=scale,
            ), layout_w, layout_h, scale

        launch_opts: dict = {"headless": True}
        if args.proxy:
            launch_opts["proxy"] = {"server": args.proxy}

        browser = pw.chromium.launch(**launch_opts)
        ctx, layout_w, layout_h, scale = make_context(
            browser, args.width, pb.width, pb.height)
        page = ctx.new_page()

        # CDP screencast: browser pushes frames at terminal pixel dimensions
        cdp = page.context.new_cdp_session(page)
        latest_frame = {"data": None}
        lock = threading.Lock()

        def on_frame(params: dict) -> None:
            with lock:
                latest_frame["data"] = params["data"]
            cdp.send("Page.screencastFrameAck", {"sessionId": params["sessionId"]})

        cdp.on("Page.screencastFrame", on_frame)
        cdp.send("Page.startScreencast", {
            "format": "jpeg",
            "quality": 50,
            "maxWidth": pb.width,
            "maxHeight": pb.height,
            "everyNthFrame": 1,
        })

        page.goto(url, wait_until="domcontentloaded")

        # Force videos to stay within viewport (prevent fullscreen overflow)
        page.add_style_tag(content="""
            video, iframe { max-width: 100vw !important; max-height: 100vh !important; }
            *:fullscreen { width: 100vw !important; height: 100vh !important; }
        """)
        enable_mouse()

        try:
            while True:
                pacer.pace()

                # Resize handling (font size change → different terminal cols/rows)
                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)
                    ctx, layout_w, layout_h, scale = make_context(
                        browser, args.width, pb.width, pb.height)
                    page = ctx.new_page()
                    page.goto(page.url, wait_until="domcontentloaded")
                    cdp = page.context.new_cdp_session(page)
                    cdp.on("Page.screencastFrame", on_frame)
                    cdp.send("Page.startScreencast", {
                        "format": "jpeg", "quality": 50,
                        "maxWidth": pb.width, "maxHeight": pb.height,
                        "everyNthFrame": 1,
                    })

                # Input
                for event in read_input(sys.stdin.fileno()):
                    if event[0] == "key":
                        ch = event[1]
                        if ch == "\x11":  # Ctrl-Q
                            return
                        elif ch == "\r":
                            page.keyboard.press("Enter")
                        elif ch == "\x7f":
                            page.keyboard.press("Backspace")
                        elif ch == "\t":
                            page.keyboard.press("Tab")
                        else:
                            page.keyboard.type(ch)

                    elif event[0] == "mouse":
                        _, button, cx, cy, pressed = event
                        if pressed and button == 0:
                            px = (cx - 1)
                            py = (cy - 1) * 2
                            page.mouse.click(px / scale, py / scale)

                    elif event[0] == "scroll":
                        _, direction, cx, cy = event
                        page.mouse.wheel(0, -60 if direction == "up" else 60)

                # Pump Playwright's event loop so CDP events dispatch
                try:
                    page.evaluate("0")
                except Exception:
                    pass

                # Render latest frame from CDP
                with lock:
                    frame_data = latest_frame["data"]
                    latest_frame["data"] = None

                if frame_data:
                    try:
                        jpg_bytes = base64.b64decode(frame_data)
                        arr = np.array(Image.open(io.BytesIO(jpg_bytes)).convert("RGB"),
                                       dtype=np.uint8)
                        h = min(arr.shape[0], pb.height)
                        w = min(arr.shape[1], pb.width)
                        pb.pixels[:h, :w] = arr[:h, :w]
                    except Exception:
                        pass

                pb.encode_all()
                pb.draw_text(1, 0,
                             f" {pacer.fps:.0f}fps  {page.url[:60]}  Ctrl-Q=quit ",
                             255, 255, 255, 30, 30, 50)
                pb.present()

        finally:
            cdp.send("Page.stopScreencast")
            cdp.detach()
            disable_mouse()
            browser.close()


if __name__ == "__main__":
    main()
