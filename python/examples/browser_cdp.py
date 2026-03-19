"""Terminal browser with CDP for navigation events + synchronous screenshots.

Uses a CDP session to keep navigation/event state in sync, while taking
synchronous screenshots (with video constraints applied) for correct rendering.

Install: uv pip install ".[browser]" && playwright install chromium
Run:     uv run python python/examples/browser_cdp.py [url] [--proxy ...]
Keys:    mouse click, scroll, type text. Ctrl-Q=quit
"""

import os
import select
import sys

import numpy as np
from PIL import Image
from playwright.sync_api import CDPSession, sync_playwright

import cliviz
from browser import (  # shared helpers
    apply_page_styles, copy_screenshot, disable_mouse, enable_mouse,
    layout_height, make_context, read_input, terminal_to_css,
)


def set_screen_metrics(cdp: CDPSession, w: int, h: int) -> None:
    """Override Chromium's screen dimensions so fullscreen stays within layout."""
    cdp.send("Emulation.setDeviceMetricsOverride", {
        "width": w, "height": h,
        "screenWidth": w, "screenHeight": h,
        "deviceScaleFactor": 1, "mobile": False,
    })


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="CDP terminal browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280, help="Layout width in CSS pixels")
    args = parser.parse_args()

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=30)
        layout_w = args.width

        browser = pw.chromium.launch(
            headless=True,
            args=["--disable-features=FullscreenWithinTab", "--kiosk"],
            **({"proxy": {"server": args.proxy}} if args.proxy else {}),
        )
        lh = layout_height(layout_w, pb)
        ctx = make_context(browser, layout_w, lh, args.proxy)
        ctx.add_init_script("document.addEventListener('click', e => { const a = e.target.closest('a'); if (a) a.removeAttribute('target'); })")
        page = ctx.new_page()

        cdp = ctx.new_cdp_session(page)
        set_screen_metrics(cdp, layout_w, lh)

        page.goto(args.url, wait_until="domcontentloaded")
        apply_page_styles(page)
        enable_mouse()

        try:
            while True:
                pacer.pace()

                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)
                    lh = layout_height(layout_w, pb)
                    page.set_viewport_size({"width": layout_w, "height": lh})
                    set_screen_metrics(cdp, layout_w, lh)

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
                            bx, by = terminal_to_css(cx, cy, pb, layout_w,
                                                     page.viewport_size["height"])
                            page.mouse.click(bx, by)
                    elif event[0] == "scroll":
                        _, direction, _, _ = event
                        page.mouse.wheel(0, -60 if direction == "up" else 60)

                # Constrain then screenshot synchronously
                try:
                    apply_page_styles(page)
                    vp = page.viewport_size
                    jpg = page.screenshot(
                        type="jpeg", quality=50,
                        clip={"x": 0, "y": 0, "width": vp["width"], "height": vp["height"]},
                    )
                    copy_screenshot(jpg, pb)
                except Exception:
                    pass

                pb.encode_all()
                pb.draw_text(1, 0,
                             f" {pacer.fps:.0f}fps  {page.url[:60]}  Ctrl-Q=quit ",
                             255, 255, 255, 30, 30, 50)
                pb.present()

        finally:
            cdp.detach()
            disable_mouse()
            browser.close()


if __name__ == "__main__":
    main()
