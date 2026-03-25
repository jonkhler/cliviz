"""Interactive web browser in the terminal via headless Chromium.

Install: uv pip install ".[browser]" && playwright install chromium
Run:     uv run python python/examples/browser.py [url] [--proxy socks5://localhost:1080]
Keys:    mouse click/scroll, type text. Ctrl-Q quit.
Zoom:    Ctrl-Z enter zoom-select mode, drag to select rect, Ctrl-Z again to exit.
"""

import io
import os
import select
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Literal, NamedTuple
from urllib.parse import urlparse

import numpy as np
from PIL import Image
from playwright.sync_api import BrowserContext, CDPSession, Page, sync_playwright

import cliviz


# ── Terminal mouse tracking ──

def enable_mouse() -> None:
    # Click + scroll + all-motion tracking (SGR mode)
    sys.stdout.buffer.write(b"\x1b[?1000h\x1b[?1003h\x1b[?1006h")
    sys.stdout.buffer.flush()

def disable_mouse() -> None:
    sys.stdout.buffer.write(b"\x1b[?1000l\x1b[?1003l\x1b[?1006l")
    sys.stdout.buffer.flush()



# ── Input parsing ──

class KeyEvent(NamedTuple):
    ch: str


class MouseEvent(NamedTuple):
    button: int
    x: int
    y: int
    pressed: bool


class MotionEvent(NamedTuple):
    x: int
    y: int


class ScrollEvent(NamedTuple):
    direction: Literal["up", "down"]
    x: int
    y: int


Event = KeyEvent | MouseEvent | MotionEvent | ScrollEvent


def read_input(fd: int) -> list[Event]:
    events: list[Event] = []
    buf = b""
    while select.select([fd], [], [], 0)[0]:
        chunk = os.read(fd, 4096)  # large enough to drain motion event floods
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
                is_motion = bool(cb & 32)
                if cb & 64:
                    events.append(ScrollEvent("up" if button == 0 else "down", cx, cy))
                elif is_motion:
                    events.append(MotionEvent(cx, cy))
                else:
                    events.append(MouseEvent(button, cx, cy, not is_release))
            i = end + 1
        elif b == 0x1B:
            # Check for bare ESC (Ctrl-Esc) — no sequence follows quickly
            if i + 1 < len(buf) and buf[i+1] not in (ord("["), ord("O")):
                events.append(KeyEvent("\x1b"))
                i += 1
            else:
                i += 1
                while i < len(buf) and buf[i] not in range(0x40, 0x7F):
                    i += 1
                i += 1
        else:
            events.append(KeyEvent(chr(b)))
            i += 1
    return events


# ── URL bar state ──

@dataclass
class UrlBar:
    active: bool = False
    text: str = ""


# ── Zoom state ──

class ZoomMode(Enum):
    NONE      = auto()  # normal browsing
    SELECTING = auto()  # Ctrl-Z pressed, drag to define rect
    ACTIVE    = auto()  # rect selected, display is cropped+upscaled


@dataclass
class Zoom:
    mode: ZoomMode = ZoomMode.NONE
    drag_start: tuple[int, int] | None = None  # pb pixel coords
    drag_cur:   tuple[int, int] | None = None  # pb pixel coords
    rect: tuple[int, int, int, int] | None = None  # (x0,y0,x1,y1) pb pixels


@dataclass
class BrowserState:
    url_bar: UrlBar = field(default_factory=UrlBar)
    zoom: Zoom = field(default_factory=Zoom)
    navigation_pending: bool = False
    show_help: bool = False


def cell_to_pixel(cx: int, cy: int) -> tuple[int, int]:
    """Terminal cell (1-based) → pixel buffer coords."""
    return cx - 1, (cy - 1) * 2


# ── Browser setup ──

_INIT_SCRIPT = """
    // Open all links in current tab
    document.addEventListener('click', e => {
        const a = e.target.closest('a');
        if (a) a.removeAttribute('target');
    });
"""

_CHROMIUM_ARGS = ["--disable-features=FullscreenWithinTab"]


def setup_browser(
    pw, layout_w: int, layout_h: int, proxy: str | None, profile: str | None
) -> tuple[BrowserContext, "Page", "Callable"]:
    """Returns (ctx, page, cleanup).

    When profile is set, uses a persistent context (cookies/logins survive
    between sessions). Otherwise uses an ephemeral context.
    """
    proxy_opts = {"proxy": {"server": proxy}} if proxy else {}

    if profile:
        ctx = pw.chromium.launch_persistent_context(
            profile,
            headless=True,
            viewport={"width": layout_w, "height": layout_h},
            args=_CHROMIUM_ARGS,
            **proxy_opts,
        )
        ctx.add_init_script(_INIT_SCRIPT)
        page = ctx.pages[0] if ctx.pages else ctx.new_page()
        return ctx, page, ctx.close
    else:
        browser = pw.chromium.launch(headless=True, args=_CHROMIUM_ARGS, **proxy_opts)
        opts: dict = {"viewport": {"width": layout_w, "height": layout_h}, **proxy_opts}
        ctx = browser.new_context(**opts)
        ctx.add_init_script(_INIT_SCRIPT)
        page = ctx.new_page()
        return ctx, page, browser.close


def set_screen_metrics(cdp: CDPSession, w: int, h: int) -> None:
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
    return (cx - 1) / pb.width * layout_w, (cy - 1) * 2 / pb.height * layout_h



def layout_height(layout_w: int, pb: cliviz.PixelBuffer) -> int:
    return max(100, int(layout_w * pb.height / pb.width))


# ── Screenshot + zoom rendering ──

_CONSTRAIN_VIDEOS_JS = """
    document.querySelectorAll('video').forEach(v => {
        v.style.setProperty('max-width',  '100vw',   'important');
        v.style.setProperty('max-height', '100vh',   'important');
        v.style.setProperty('width',      '100%',    'important');
        v.style.setProperty('height',     'auto',    'important');
        v.style.setProperty('object-fit', 'contain', 'important');
    });
"""


def take_screenshot(page: Page) -> np.ndarray | None:
    """Constrain videos + screenshot → numpy array at native browser resolution."""
    try:
        page.evaluate(_CONSTRAIN_VIDEOS_JS)
    except Exception:
        pass
    try:
        vp = page.viewport_size
        jpg = page.screenshot(
            type="jpeg", quality=85,
            clip={"x": 0, "y": 0, "width": vp["width"], "height": vp["height"]},
        )
        return np.array(Image.open(io.BytesIO(jpg)).convert("RGB"), dtype=np.uint8)
    except Exception as exc:
        print(f"screenshot error: {exc}", file=sys.stderr)
        return None


def render_frame(pb: cliviz.PixelBuffer, native: np.ndarray, zoom: Zoom) -> None:
    """Downscale native screenshot into pb.pixels, using zoom rect if active.

    Zoom crops from the full-resolution native array for maximum quality —
    the crop is at browser rendering resolution, not the downscaled pb resolution.
    """
    native_h, native_w = native.shape[:2]

    if zoom.mode == ZoomMode.ACTIVE and zoom.rect is not None:
        # Map zoom rect from pb pixel space to native array space
        x0, y0, x1, y1 = zoom.rect
        nx0 = int(x0 / pb.width  * native_w)
        ny0 = int(y0 / pb.height * native_h)
        nx1 = int(x1 / pb.width  * native_w)
        ny1 = int(y1 / pb.height * native_h)
        nx0, ny0 = max(0, nx0), max(0, ny0)
        nx1, ny1 = min(native_w, nx1), min(native_h, ny1)
        if nx1 > nx0 + 2 and ny1 > ny0 + 2:
            crop = native[ny0:ny1, nx0:nx1]
            pb.pixels[:] = np.array(
                Image.fromarray(crop).resize((pb.width, pb.height), Image.LANCZOS),
                dtype=np.uint8,
            )
            return

    # Normal: downscale full frame to pb dimensions
    if native.shape[:2] == (pb.height, pb.width):
        pb.pixels[:] = native
    else:
        pb.pixels[:] = np.array(
            Image.fromarray(native).resize((pb.width, pb.height), Image.BILINEAR),
            dtype=np.uint8,
        )

    if zoom.mode == ZoomMode.SELECTING and zoom.drag_start and zoom.drag_cur:
        sx, sy = zoom.drag_start
        ex, ey = zoom.drag_cur
        x0, x1 = max(0, min(sx, ex)), min(pb.width,  max(sx, ex))
        y0, y1 = max(0, min(sy, ey)), min(pb.height, max(sy, ey))
        if x1 > x0 and y1 > y0:
            region = pb.pixels[y0:y1, x0:x1].astype(np.float32)
            pb.pixels[y0:y1, x0:x1] = np.clip(
                region * 0.5 + np.array([0, 80, 200], dtype=np.float32) * 0.5,
                0, 255,
            ).astype(np.uint8)


def safe_url(url: str, show_full: bool = False) -> str:
    if show_full:
        return url
    try:
        parsed = urlparse(url)
        if not parsed.scheme or not parsed.netloc:
            return url
        return f"{parsed.scheme}://{parsed.netloc}{parsed.path}"
    except Exception:
        return url


_HELP_TEXT = (
    "  Ctrl-L: url  Ctrl-Z: zoom (drag to select, again to exit)"
    "  scroll/click/type: forwarded  Ctrl-Q: quit  ?: hide help"
)

def render_hud(
    pb: cliviz.PixelBuffer,
    state: BrowserState,
    url: str,
    fps: float,
    show_full_url: bool,
) -> None:
    if state.url_bar.active:
        bar = f" Go: {state.url_bar.text}_"
        pb.draw_text(1, 0, bar.ljust(pb.width - 1), 255, 255, 200, 20, 60, 20)
    else:
        mode_hint = "  [Ctrl-Z]zoom-select " if state.zoom.mode == ZoomMode.SELECTING else \
                    "  [Ctrl-Z]exit-zoom " if state.zoom.mode == ZoomMode.ACTIVE else ""
        pb.draw_text(
            1, 0,
            f" {fps:.0f}fps  {safe_url(url, show_full=show_full_url)[:50]}  "
            f"{mode_hint}Ctrl-L=url  Ctrl-Q=quit  ?=help ",
            255, 255, 255, 30, 30, 50,
        )
    if state.show_help:
        pb.draw_text(
            1, pb.term_rows - 1,
            _HELP_TEXT.ljust(pb.width - 1),
            220, 220, 180, 20, 40, 20,
        )


# ── Main ──

def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description="Terminal web browser")
    parser.add_argument("url", nargs="?", default="https://news.ycombinator.com")
    parser.add_argument("--proxy", help="Proxy server (e.g. socks5://localhost:1080)")
    parser.add_argument("--width", type=int, default=1280,
                        help="Layout width in CSS pixels (default 1280)")
    parser.add_argument("--profile", metavar="DIR",
                        help="Browser profile directory for persistent sessions "
                             "(cookies/logins survive between runs)")
    parser.add_argument("--no-cdp", action="store_true",
                        help="Disable CDP session (better login compatibility, "
                             "disables fullscreen video fix)")
    parser.add_argument("--show-full-url", action="store_true",
                        help="Show full URLs in HUD and terminal title")
    args = parser.parse_args()

    with cliviz.Terminal() as term, sync_playwright() as pw:
        pb = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=30)
        layout_w = args.width
        state = BrowserState()

        lh = layout_height(layout_w, pb)
        ctx, page, cleanup = setup_browser(pw, layout_w, lh, args.proxy, args.profile)
        cdp = ctx.new_cdp_session(page) if not args.no_cdp else None
        if cdp:
            set_screen_metrics(cdp, layout_w, lh)
        def _on_navigated(_: object) -> None:
            state.navigation_pending = True

        page.on("framenavigated", _on_navigated)

        page.goto(args.url, wait_until="domcontentloaded")
        enable_mouse()

        try:
            while True:
                pacer.pace()

                # Exit zoom on navigation (deferred from callback)
                if state.navigation_pending:
                    state.navigation_pending = False
                    state.zoom = Zoom()

                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)
                    lh = layout_height(layout_w, pb)
                    page.set_viewport_size({"width": layout_w, "height": lh})
                    if cdp:
                        set_screen_metrics(cdp, layout_w, lh)
                    state.zoom = Zoom()


                for event in read_input(sys.stdin.fileno()):
                    def exit_zoom() -> None:
                        state.zoom = Zoom()

                    if isinstance(event, KeyEvent):
                        ch = event.ch

                        # URL bar mode — absorb all keys except Enter/Esc
                        if state.url_bar.active:
                            if ch == "\r":
                                url = state.url_bar.text.strip()
                                state.url_bar.active = False
                                state.url_bar.text = ""
                                if url:
                                    if "://" not in url:
                                        url = "https://" + url
                                    page.goto(url, wait_until="domcontentloaded")
                            elif ch in ("\x1b", "\x11"):  # ESC or Ctrl-Q cancel
                                state.url_bar.active = False
                                state.url_bar.text = ""
                                if ch == "\x11":
                                    return
                            elif ch == "\x7f":
                                state.url_bar.text = state.url_bar.text[:-1]
                            elif ch >= " ":
                                state.url_bar.text += ch
                            continue  # don't process further

                        if ch == "\x11":  # Ctrl-Q
                            return
                        elif ch == "?":
                            state.show_help = not state.show_help
                        elif ch == "\x0c":  # Ctrl-L → open URL bar
                            state.url_bar.active = True
                            state.url_bar.text = page.url
                            exit_zoom()
                        elif ch == "\x1a":  # Ctrl-Z → toggle zoom
                            if state.zoom.mode == ZoomMode.NONE:
                                state.zoom = Zoom(mode=ZoomMode.SELECTING)
                            else:
                                exit_zoom()
                        elif ch == "\x1b":  # ESC → exit zoom
                            exit_zoom()
                        elif state.zoom.mode != ZoomMode.SELECTING:
                            if ch == "\r":
                                page.keyboard.press("Enter")
                            elif ch == "\x7f":
                                page.keyboard.press("Backspace")
                            elif ch == "\t":
                                page.keyboard.press("Tab")
                            else:
                                page.keyboard.type(ch)

                    elif isinstance(event, MotionEvent):
                        if state.zoom.mode == ZoomMode.SELECTING and state.zoom.drag_start:
                            state.zoom.drag_cur = cell_to_pixel(event.x, event.y)
                        else:
                            # Forward hover to browser for tooltips/menus
                            bx, by = terminal_to_css(event.x, event.y, pb, layout_w, lh)
                            try:
                                page.mouse.move(bx, by)
                            except Exception as exc:
                                print(f"hover error: {exc}", file=sys.stderr)

                    elif isinstance(event, MouseEvent):
                        px, py = cell_to_pixel(event.x, event.y)

                        if state.zoom.mode == ZoomMode.SELECTING:
                            if event.pressed and event.button == 0:
                                state.zoom.drag_start = (px, py)
                                state.zoom.drag_cur   = (px, py)
                            elif not event.pressed and event.button == 0 and state.zoom.drag_start:
                                x0 = min(state.zoom.drag_start[0], px)
                                y0 = min(state.zoom.drag_start[1], py)
                                x1 = max(state.zoom.drag_start[0], px)
                                y1 = max(state.zoom.drag_start[1], py)
                                if x1 - x0 > 4 and y1 - y0 > 4:
                                    state.zoom = Zoom(mode=ZoomMode.ACTIVE, rect=(x0, y0, x1, y1))
                                else:
                                    state.zoom = Zoom()

                        elif state.zoom.mode == ZoomMode.ACTIVE:
                            if event.pressed and state.zoom.rect:
                                x0, y0, x1, y1 = state.zoom.rect
                                bx = (event.x - 1) / pb.width  * (x1 - x0) / pb.width  * layout_w + x0 / pb.width  * layout_w
                                by = (event.y - 1) * 2 / pb.height * (y1 - y0) / pb.height * lh + y0 / pb.height * lh
                                pw_btn = "right" if event.button == 2 else "left"
                                page.mouse.click(bx, by, button=pw_btn)

                        else:  # NONE
                            if event.pressed:
                                bx, by = terminal_to_css(event.x, event.y, pb, layout_w, lh)
                                pw_btn = "right" if event.button == 2 else "left"
                                page.mouse.click(bx, by, button=pw_btn)

                    elif isinstance(event, ScrollEvent):
                        page.mouse.wheel(0, -60 if event.direction == "up" else 60)

                frame = take_screenshot(page)
                if frame is not None:
                    render_frame(pb, frame, state.zoom)

                # encode → text overlay → nodiff present
                # present_nodiff avoids stale cells from JPEG producing same
                # quantized color as the previous frame (causes torn-frame artifacts)
                pb.encode_all()
                render_hud(pb, state, page.url, pacer.fps, args.show_full_url)
                # Terminal title: resolution info for debugging
                vp = page.viewport_size
                title_url = safe_url(page.url, show_full=args.show_full_url)
                sys.stdout.buffer.write(
                    f"\x1b]0;cliviz {pb.width}×{pb.height}px "
                    f"| browser {vp['width']}×{vp['height']}css "
                    f"| {pacer.fps:.0f}fps | {title_url}\x07".encode()
                )
                sys.stdout.buffer.flush()
                pb.present(color_threshold=8)

        finally:
            if cdp:
                cdp.detach()
            disable_mouse()
            cleanup()


if __name__ == "__main__":
    main()
