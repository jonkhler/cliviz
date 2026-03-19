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
from playwright.sync_api import CDPSession, Page, sync_playwright

import cliviz
from browser import (  # shared helpers
    apply_page_styles, copy_screenshot, disable_mouse, enable_mouse,
    layout_height, make_context, read_input, terminal_to_css,
)


def start_screencast(cdp: CDPSession, pb: cliviz.PixelBuffer) -> None:
    cdp.send("Page.startScreencast", {
        "format": "jpeg",
        "quality": 50,
        "maxWidth": pb.width,
        "maxHeight": pb.height,
        "everyNthFrame": 1,
    })


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="CDP screencast terminal browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280, help="Layout width in CSS pixels")
    args = parser.parse_args()

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=60)
        layout_w = args.width

        browser = pw.chromium.launch(
            headless=True,
            args=["--disable-features=FullscreenWithinTab", "--kiosk"],
            **({"proxy": {"server": args.proxy}} if args.proxy else {}),
        )
        ctx = make_context(browser, layout_w, layout_height(layout_w, pb), args.proxy)
        page = ctx.new_page()

        def handle_popup(popup) -> None:
            url = popup.url
            popup.close()
            if url and url != "about:blank":
                page.goto(url, wait_until="domcontentloaded")

        page.on("popup", handle_popup)

        cdp = ctx.new_cdp_session(page)
        latest_frame: dict[str, bytes | None] = {"data": None}
        lock = threading.Lock()

        def on_frame(params: dict) -> None:
            with lock:
                latest_frame["data"] = params["data"]
            cdp.send("Page.screencastFrameAck", {"sessionId": params["sessionId"]})

        cdp.on("Page.screencastFrame", on_frame)
        start_screencast(cdp, pb)

        page.goto(args.url, wait_until="domcontentloaded")
        apply_page_styles(page)
        enable_mouse()
        screenshot_w, screenshot_h = layout_w, layout_height(layout_w, pb)

        try:
            while True:
                pacer.pace()

                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)
                    new_h = layout_height(layout_w, pb)
                    page.set_viewport_size({"width": layout_w, "height": new_h})
                    screenshot_w, screenshot_h = layout_w, new_h
                    cdp.send("Page.stopScreencast")
                    start_screencast(cdp, pb)

                for event in read_input(sys.stdin.fileno()):
                    if event[0] == "key":
                        ch = event[1]
                        if ch == "\x11":
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
                        _, btn, cx, cy, pressed = event
                        if pressed and btn == 0:
                            bx, by = terminal_to_css(cx, cy, pb, screenshot_w, screenshot_h)
                            page.mouse.click(bx, by)
                    elif event[0] == "scroll":
                        _, direction, _, _ = event
                        page.mouse.wheel(0, -60 if direction == "up" else 60)

                # Pump Playwright event loop so CDP callbacks fire
                try:
                    page.evaluate("0")
                except Exception:
                    pass

                with lock:
                    frame_data = latest_frame["data"]
                    latest_frame["data"] = None

                if frame_data:
                    try:
                        screenshot_w, screenshot_h = copy_screenshot(
                            base64.b64decode(frame_data), pb)
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
