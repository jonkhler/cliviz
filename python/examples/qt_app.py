"""Run any Qt (PySide6) widget app in the terminal via cliviz.

Install: uv pip install ".[qt]"
Run:     uv run python python/examples/qt_app.py --app module:ClassName [args...]
Example: uv run python python/examples/qt_app.py --app mypackage.ui:MainWindow
Keys:    mouse click/scroll/hover, type text. Ctrl-Q quit.
Zoom:    Ctrl-Z drag to select region (Qt re-renders at higher res). Ctrl-Z again to exit.

The offscreen platform must be set before any Qt import, so this file does it
at module level before the PySide6 imports below.
"""

import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import io
import select
import sys
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Literal, NamedTuple

import numpy as np
from PIL import Image
from PySide6.QtCore import QEvent, QPoint, QPointF, Qt
from PySide6.QtGui import (
    QImage,
    QKeyEvent,
    QMouseEvent,
    QPointingDevice,
    QWheelEvent,
)
from PySide6.QtWidgets import QApplication, QWidget

import cliviz


# ── Input events (identical to browser.py) ──

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
        chunk = os.read(fd, 4096)
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


# ── Terminal mouse tracking ──

def enable_mouse() -> None:
    sys.stdout.buffer.write(b"\x1b[?1000h\x1b[?1003h\x1b[?1006h")
    sys.stdout.buffer.flush()

def disable_mouse() -> None:
    sys.stdout.buffer.write(b"\x1b[?1000l\x1b[?1003l\x1b[?1006l")
    sys.stdout.buffer.flush()


# ── Zoom state ──

class ZoomMode(Enum):
    NONE      = auto()
    SELECTING = auto()
    ACTIVE    = auto()

@dataclass
class Zoom:
    mode: ZoomMode = ZoomMode.NONE
    drag_start: tuple[int, int] | None = None
    drag_cur:   tuple[int, int] | None = None
    rect: tuple[int, int, int, int] | None = None  # (x0,y0,x1,y1) in pb pixels


@dataclass
class QtAppState:
    zoom: Zoom = field(default_factory=Zoom)
    show_help: bool = False


def cell_to_pixel(cx: int, cy: int) -> tuple[int, int]:
    return cx - 1, (cy - 1) * 2


def terminal_to_widget(
    cx: int, cy: int, pb: cliviz.PixelBuffer, layout_w: int, layout_h: int
) -> tuple[float, float]:
    return (cx - 1) / pb.width * layout_w, (cy - 1) * 2 / pb.height * layout_h


# ── Frame capture ──

def _qimage_to_numpy(img: QImage) -> np.ndarray:
    """QImage (any format) → (H, W, 3) uint8 RGB array."""
    rgb = img.convertToFormat(QImage.Format.Format_RGBA8888)
    arr = np.frombuffer(rgb.bits(), dtype=np.uint8).reshape(rgb.height(), rgb.width(), 4)
    return arr[:, :, :3].copy()


def grab_frame(
    window: QWidget,
    pb: cliviz.PixelBuffer,
    state: QtAppState,
    layout_w: int,
    layout_h: int,
    zoom_scale: int,
    app: QApplication,
) -> None:
    """Grab window pixels and write into pb, applying zoom if active."""
    zoom = state.zoom

    if zoom.mode == ZoomMode.ACTIVE and zoom.rect is not None:
        # Resize window to zoom_scale× so Qt re-renders at higher resolution.
        # This gives sharp SVG/QPainter output — no upscaling artifacts.
        x0, y0, x1, y1 = zoom.rect
        window.resize(layout_w * zoom_scale, layout_h * zoom_scale)
        app.processEvents()

        full = _qimage_to_numpy(window.grab().toImage())
        native_h, native_w = full.shape[:2]

        # Map pb-pixel zoom rect to the higher-res grab coords
        nx0 = int(x0 / pb.width  * native_w)
        ny0 = int(y0 / pb.height * native_h)
        nx1 = int(x1 / pb.width  * native_w)
        ny1 = int(y1 / pb.height * native_h)
        nx0, ny0 = max(0, nx0), max(0, ny0)
        nx1, ny1 = min(native_w, nx1), min(native_h, ny1)

        if nx1 > nx0 + 2 and ny1 > ny0 + 2:
            crop = full[ny0:ny1, nx0:nx1]
            pb.pixels[:] = np.array(
                Image.fromarray(crop).resize((pb.width, pb.height), Image.LANCZOS),
                dtype=np.uint8,
            )
            return

    # Normal render at layout size
    if zoom.mode != ZoomMode.ACTIVE:
        window.resize(layout_w, layout_h)
        app.processEvents()

    native = _qimage_to_numpy(window.grab().toImage())
    if native.shape[:2] == (pb.height, pb.width):
        pb.pixels[:] = native
    else:
        pb.pixels[:] = np.array(
            Image.fromarray(native).resize((pb.width, pb.height), Image.BILINEAR),
            dtype=np.uint8,
        )

    # Draw selection rect overlay while dragging
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


# ── Qt event injection ──

_POINTING_DEVICE = None  # initialised after QApplication exists

def _device() -> QPointingDevice:
    global _POINTING_DEVICE
    if _POINTING_DEVICE is None:
        _POINTING_DEVICE = QPointingDevice.primaryPointingDevice()
    return _POINTING_DEVICE


_CHAR_TO_KEY: dict[str, Qt.Key] = {
    "\r": Qt.Key.Key_Return,
    "\n": Qt.Key.Key_Return,
    "\t": Qt.Key.Key_Tab,
    "\x7f": Qt.Key.Key_Backspace,
    "\x1b": Qt.Key.Key_Escape,
}


def _target_and_local(window: QWidget, pos: QPointF) -> tuple[QWidget, QPointF]:
    """Find the child widget under pos and return it with pos in its local coords.

    Qt does not hit-test child widgets for synthesized events — sendEvent()
    on the top-level window delivers to the window, not to children. We must
    find the correct child ourselves and remap the coordinates.
    """
    ipos = pos.toPoint()
    child = window.childAt(ipos)
    if child is None:
        return window, pos
    local = QPointF(child.mapFrom(window, ipos))
    return child, local


def _send_mouse(
    window: QWidget,
    event_type: QEvent.Type,
    pos: QPointF,
    button: Qt.MouseButton,
) -> None:
    target, local = _target_and_local(window, pos)
    global_pos = target.mapToGlobal(local.toPoint()).toPointF()
    ev = QMouseEvent(event_type, local, global_pos, button, button,
                     Qt.KeyboardModifier.NoModifier, _device())
    QApplication.sendEvent(target, ev)


def _send_wheel(window: QWidget, pos: QPointF, delta_y: int) -> None:
    target, local = _target_and_local(window, pos)
    global_pos = target.mapToGlobal(local.toPoint()).toPointF()
    ev = QWheelEvent(
        local, global_pos,
        QPoint(0, 0),           # pixelDelta (unavailable here)
        QPoint(0, delta_y),     # angleDelta: positive = up
        Qt.MouseButton.NoButton,
        Qt.KeyboardModifier.NoModifier,
        Qt.ScrollPhase.NoScrollPhase,
        False,
    )
    QApplication.sendEvent(target, ev)


def forward_to_qt(
    event: Event,
    window: QWidget,
    pb: cliviz.PixelBuffer,
    layout_w: int,
    layout_h: int,
) -> None:
    match event:
        case MouseEvent(button, cx, cy, pressed):
            wx, wy = terminal_to_widget(cx, cy, pb, layout_w, layout_h)
            pos = QPointF(wx, wy)
            qt_btn = Qt.MouseButton.RightButton if button == 2 else Qt.MouseButton.LeftButton
            ev_type = QEvent.Type.MouseButtonPress if pressed else QEvent.Type.MouseButtonRelease
            _send_mouse(window, ev_type, pos, qt_btn)

        case MotionEvent(cx, cy):
            wx, wy = terminal_to_widget(cx, cy, pb, layout_w, layout_h)
            try:
                _send_mouse(window, QEvent.Type.MouseMove, QPointF(wx, wy),
                            Qt.MouseButton.NoButton)
            except Exception as exc:
                print(f"hover error: {exc}", file=sys.stderr)

        case ScrollEvent(direction, cx, cy):
            wx, wy = terminal_to_widget(cx, cy, pb, layout_w, layout_h)
            _send_wheel(window, QPointF(wx, wy), 120 if direction == "up" else -120)

        case KeyEvent(ch):
            qt_key = _CHAR_TO_KEY.get(ch, Qt.Key.Key_unknown)
            if qt_key == Qt.Key.Key_unknown and len(ch) == 1 and ch.isprintable():
                qt_key = Qt.Key(ord(ch.upper()))
            press = QKeyEvent(QEvent.Type.KeyPress, qt_key,
                              Qt.KeyboardModifier.NoModifier, ch)
            release = QKeyEvent(QEvent.Type.KeyRelease, qt_key,
                                Qt.KeyboardModifier.NoModifier, ch)
            QApplication.sendEvent(window, press)
            QApplication.sendEvent(window, release)


# ── HUD ──

_HELP_TEXT = (
    "  Ctrl-Z: zoom (drag to select, again to exit)"
    "  scroll/click/type: forwarded  Ctrl-Q: quit  ?: hide help"
)

def render_hud(
    pb: cliviz.PixelBuffer,
    state: QtAppState,
    title: str,
    fps: float,
) -> None:
    mode_hint = (
        "  [Ctrl-Z]zoom-select "  if state.zoom.mode == ZoomMode.SELECTING else
        "  [Ctrl-Z]exit-zoom "    if state.zoom.mode == ZoomMode.ACTIVE    else
        ""
    )
    pb.draw_text(
        1, 0,
        f" {fps:.0f}fps  {title[:40]}  {mode_hint}Ctrl-Q=quit  ?=help ",
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
    parser = argparse.ArgumentParser(description="Run a Qt app in the terminal")
    parser.add_argument(
        "--app", required=True, metavar="MODULE:CLASS",
        help="Widget to run, e.g. mypackage.module:MyWidget",
    )
    parser.add_argument("--width",      type=int, default=1280)
    parser.add_argument("--height",     type=int, default=800)
    parser.add_argument("--zoom-scale", type=int, default=3,
                        help="Render multiplier when zoomed (default 3)")
    args = parser.parse_args()

    module_name, class_name = args.app.rsplit(":", 1)
    import importlib
    widget_class: type[QWidget] = getattr(importlib.import_module(module_name), class_name)

    app = QApplication([sys.argv[0]])
    print(f"Qt platform: {app.platformName()}", file=sys.stderr)

    window: QWidget = widget_class()
    window.resize(args.width, args.height)
    window.show()
    app.processEvents()

    with cliviz.Terminal() as term:
        pb    = cliviz.PixelBuffer(term.cols, term.rows)
        pacer = cliviz.FramePacer(target_fps=30)
        state = QtAppState()
        layout_w, layout_h = args.width, args.height

        enable_mouse()
        try:
            while not term.was_interrupted():
                app.processEvents()

                if term.was_resized():
                    pb = cliviz.PixelBuffer(term.cols, term.rows)

                for event in read_input(sys.stdin.fileno()):
                    def exit_zoom() -> None:
                        state.zoom = Zoom()
                        window.resize(layout_w, layout_h)
                        app.processEvents()

                    if isinstance(event, KeyEvent):
                        ch = event.ch
                        if ch == "\x11":    # Ctrl-Q
                            return
                        elif ch == "?":
                            state.show_help = not state.show_help
                        elif ch == "\x1a":  # Ctrl-Z: toggle zoom
                            if state.zoom.mode == ZoomMode.NONE:
                                state.zoom = Zoom(mode=ZoomMode.SELECTING)
                            else:
                                exit_zoom()
                        elif ch == "\x1b":  # ESC: exit zoom
                            exit_zoom()
                        elif state.zoom.mode != ZoomMode.SELECTING:
                            forward_to_qt(event, window, pb, layout_w, layout_h)

                    elif isinstance(event, MotionEvent):
                        if state.zoom.mode == ZoomMode.SELECTING and state.zoom.drag_start:
                            state.zoom.drag_cur = cell_to_pixel(event.x, event.y)
                        elif state.zoom.mode != ZoomMode.SELECTING:
                            forward_to_qt(event, window, pb, layout_w, layout_h)

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
                                    state.zoom = Zoom(mode=ZoomMode.ACTIVE,
                                                      rect=(x0, y0, x1, y1))
                                else:
                                    exit_zoom()

                        elif state.zoom.mode == ZoomMode.ACTIVE:
                            if event.pressed and state.zoom.rect:
                                x0, y0, x1, y1 = state.zoom.rect
                                # Map click within the zoomed region to widget coords
                                frac_x = (px - x0) / max(x1 - x0, 1)
                                frac_y = (py - y0) / max(y1 - y0, 1)
                                wx = x0 / pb.width * layout_w + frac_x * (x1 - x0) / pb.width * layout_w
                                wy = y0 / pb.height * layout_h + frac_y * (y1 - y0) / pb.height * layout_h
                                qt_btn = Qt.MouseButton.RightButton if event.button == 2 else Qt.MouseButton.LeftButton
                                _send_mouse(window, QEvent.Type.MouseButtonPress,  QPointF(wx, wy), qt_btn)
                                _send_mouse(window, QEvent.Type.MouseButtonRelease, QPointF(wx, wy), qt_btn)
                        else:
                            forward_to_qt(event, window, pb, layout_w, layout_h)

                    elif isinstance(event, ScrollEvent):
                        forward_to_qt(event, window, pb, layout_w, layout_h)

                grab_frame(window, pb, state, layout_w, layout_h, args.zoom_scale, app)
                pb.encode_all()
                render_hud(pb, state, window.windowTitle(), pacer.fps)
                pb.present(color_threshold=4)
                pacer.pace()

        finally:
            disable_mouse()


if __name__ == "__main__":
    main()
