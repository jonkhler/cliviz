"""Interactive web browser in the terminal via headless Chromium.

Install: uv pip install ".[browser]" && playwright install chromium
Run:     uv run python python/examples/browser.py [url] [--proxy socks5://localhost:1080]
Keys:    mouse click, scroll, type text. Ctrl-Q=quit
"""

import io
import os
import select
import sys

import numpy as np
from PIL import Image
from playwright.sync_api import BrowserContext, CDPSession, Page, sync_playwright

import cliviz


# ── Terminal mouse tracking ──

def enable_mouse() -> None:
    sys.stdout.buffer.write(b"\x1b[?1000h\x1b[?1006h")
    sys.stdout.buffer.flush()

def disable_mouse() -> None:
    sys.stdout.buffer.write(b"\x1b[?1000l\x1b[?1006l")
    sys.stdout.buffer.flush()


# ── Input parsing ──

def read_input(fd: int) -> list:
    """Return list of ('key', ch), ('mouse', btn, cx, cy, pressed), ('scroll', dir, cx, cy)."""
    events: list = []
    buf = b""
    while select.select([fd], [], [], 0)[0]:
        chunk = os.read(fd, 256)
        if not chunk:
            break
        buf += chunk

    i = 0
    while i < len(buf):
        b = buf[i]
        if b == 0x1B and i + 2 < len(buf) and buf[i+1] == ord("[") and buf[i+2] == ord("<"):
            end = buf.find(ord("M"), i + 3)
            is_release = False
            if end == -1:
                end = buf.find(ord("m"), i + 3)
                is_release = True
            if end == -1:
                i += 1
                continue
            parts = buf[i+3:end].decode("ascii", errors="ignore").split(";")
            if len(parts) == 3:
                cb, cx, cy = int(parts[0]), int(parts[1]), int(parts[2])
                button = cb & 0x03
                if cb & 64:
                    events.append(("scroll", "up" if button == 0 else "down", cx, cy))
                elif not (cb & 32):
                    events.append(("mouse", button, cx, cy, not is_release))
            i = end + 1
        elif b == 0x1B:
            i += 1
            while i < len(buf) and buf[i] not in range(0x40, 0x7F):
                i += 1
            i += 1
        else:
            events.append(("key", chr(b)))
            i += 1
    return events


# ── Browser setup ──

def make_context(browser, layout_w: int, layout_h: int, proxy: str | None) -> BrowserContext:
    """Create browser context with fullscreen blocking and video constraints."""
    opts: dict = {"viewport": {"width": layout_w, "height": layout_h}}
    if proxy:
        opts["proxy"] = {"server": proxy}
    ctx = browser.new_context(**opts)
    ctx.add_init_script("""
        // Block fullscreen API
        Object.defineProperty(document, 'fullscreenEnabled', {get: () => false});
        Object.defineProperty(document, 'fullscreen', {get: () => false});
        document.documentElement.requestFullscreen = () => Promise.resolve();
        Element.prototype.requestFullscreen = () => Promise.resolve();
        document.exitFullscreen = () => Promise.resolve();

        // Open all links in same tab
        document.addEventListener('click', e => {
            const a = e.target.closest('a');
            if (a) a.removeAttribute('target');
        });
    """)
    return ctx


def set_screen_metrics(cdp: CDPSession, w: int, h: int) -> None:
    """Tell Chromium the screen is exactly our layout size (prevents fullscreen overflow)."""
    cdp.send("Emulation.setDeviceMetricsOverride", {
        "width": w, "height": h,
        "screenWidth": w, "screenHeight": h,
        "deviceScaleFactor": 1, "mobile": False,
    })


# ── Coordinate mapping ──

def terminal_to_css(
    cx: int, cy: int, pb: cliviz.PixelBuffer,
    layout_w: int, layout_h: int,
) -> tuple[float, float]:
    """Map terminal cell (1-based) to browser CSS pixel coordinates."""
    return (cx - 1) / pb.width * layout_w, (cy - 1) * 2 / pb.height * layout_h


def layout_height(layout_w: int, pb: cliviz.PixelBuffer) -> int:
    """Browser layout height matching terminal aspect ratio."""
    return max(100, int(layout_w * pb.height / pb.width))


# ── Screenshot ──

_CONSTRAIN_VIDEOS_JS = """
    document.querySelectorAll('video').forEach(v => {
        v.style.setProperty('max-width',  '100vw',   'important');
        v.style.setProperty('max-height', '100vh',   'important');
        v.style.setProperty('width',      '100%',    'important');
        v.style.setProperty('height',     'auto',    'important');
        v.style.setProperty('object-fit', 'contain', 'important');
    });
"""


def capture(page: Page, pb: cliviz.PixelBuffer) -> None:
    """Constrain videos then take a clipped screenshot into the pixel buffer."""
    try:
        page.evaluate(_CONSTRAIN_VIDEOS_JS)
    except Exception:
        pass
    try:
        vp = page.viewport_size
        jpg = page.screenshot(
            type="jpeg", quality=60,
            clip={"x": 0, "y": 0, "width": vp["width"], "height": vp["height"]},
        )
        arr = np.array(Image.open(io.BytesIO(jpg)).convert("RGB"), dtype=np.uint8)
        pb.pixels[:] = 0
        if arr.shape[:2] == (pb.height, pb.width):
            pb.pixels[:] = arr
        else:
            img = Image.fromarray(arr).resize((pb.width, pb.height), Image.BILINEAR)
            pb.pixels[:] = np.array(img, dtype=np.uint8)
    except Exception:
        pass


# ── Main ──

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Terminal web browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280,
                        help="Layout width in CSS pixels (default 1280)")
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
        page = ctx.new_page()

        cdp = ctx.new_cdp_session(page)
        set_screen_metrics(cdp, layout_w, lh)

        page.goto(args.url, wait_until="domcontentloaded")
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

                capture(page, pb)
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
