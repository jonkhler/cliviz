"""Type stubs for cliviz._native (C++ nanobind extension)."""

from __future__ import annotations

from typing import Any

import numpy as np


class Terminal:
    """Raw-mode terminal context manager.

    Usage::

        with Terminal() as term:
            while not term.was_interrupted():
                ...

    Attributes:
        cols: Terminal width in columns (set after init).
        rows: Terminal height in rows (set after init).
        active: True while the terminal is in raw mode.
    """

    cols: int
    rows: int
    active: bool

    def __init__(self, color_mode: str = "") -> None:
        """
        Args:
            color_mode: ``"truecolor"`` / ``"24bit"`` / ``"256"`` or ``""``
                        for auto-detection.
        """

    def __enter__(self) -> Terminal: ...
    def __exit__(self, *args: Any) -> None: ...
    def init(self, color_mode: str = "") -> bool:
        """Enter raw mode. Returns False if stdout is not a TTY."""

    def shutdown(self) -> None:
        """Restore terminal state. Safe to call multiple times."""

    def was_resized(self) -> bool:
        """True if the terminal was resized since the last call (edge-triggered)."""

    def was_interrupted(self) -> bool:
        """True if SIGINT/SIGTERM was received since the last call (edge-triggered).

        Call ``term.shutdown()`` and exit when this returns True.
        """


class PixelBuffer:
    """RGB pixel buffer backed by a terminal cell framebuffer.

    Pixel coordinates: ``x`` ∈ [0, width), ``y`` ∈ [0, height).
    Terminal coordinates: ``col`` ∈ [0, width), ``row`` ∈ [0, term_rows)``.

    ``height == term_rows * 2`` — each terminal row stores two pixel rows
    via the half-block character (▀).

    Usage::

        pb = PixelBuffer(term.cols, term.rows)
        pb.pixels[y, x] = [r, g, b]   # write via numpy (zero-copy)
        pb.encode_all()                # pixel pairs → cells
        pb.present()                   # diff + write to terminal
    """

    width: int     # terminal columns
    height: int    # pixel rows (= term_rows * 2)
    term_rows: int # terminal rows (= height // 2)

    pixels: np.ndarray[Any, np.dtype[np.uint8]]
    """Zero-copy numpy view of shape ``(height, width, 3)``, dtype ``uint8``."""

    def __init__(self, term_cols: int, term_rows: int) -> None: ...

    def set(
        self, x: int, y: int, r: int, g: int, b: int
    ) -> None:
        """Set a single pixel. Out-of-range coordinates are silently ignored."""

    def clear(self, r: int, g: int, b: int) -> None:
        """Fill entire buffer with a solid color and mark all cells dirty."""

    def fill_rect(
        self,
        x0: int,
        y0: int,
        x1: int,
        y1: int,
        r: int,
        g: int,
        b: int,
    ) -> None:
        """Fill pixel rectangle ``[x0, x1) × [y0, y1)`` with a solid color."""

    def encode(self) -> None:
        """Encode dirty pixel pairs into framebuffer cells."""

    def encode_all(self) -> None:
        """Encode all pixel pairs into cells (fast path; skips dirty-mask scan)."""

    def present(self, color_threshold: int = 0) -> int:
        """Diff back vs front and write changed cells to the terminal.

        Args:
            color_threshold: Skip cells where every RGB channel changed by less
                than this value (absorbs JPEG compression noise). 0 = exact diff.

        Returns:
            Number of cells actually written.
        """

    def present_nodiff(self) -> int:
        """Write every cell unconditionally (no diff). Use for full redraws.

        Returns:
            Number of cells written (always ``width * term_rows``).
        """

    def flush(self) -> int:
        """Encode dirty cells then write to terminal (encode + present combined)."""

    def flush_full(self) -> int:
        """Encode all cells then write to terminal (encode_all + present_nodiff)."""

    def draw_text(
        self,
        col: int,
        row: int,
        text: str,
        fg_r: int,
        fg_g: int,
        fg_b: int,
        bg_r: int = 0,
        bg_g: int = 0,
        bg_b: int = 0,
    ) -> None:
        """Draw ASCII text at terminal coordinates (0-based col/row).

        Clips at the right edge. Overwrites pixel data at that row.
        """

    def draw_text_fg(
        self,
        col: int,
        row: int,
        text: str,
        fg_r: int,
        fg_g: int,
        fg_b: int,
    ) -> None:
        """Draw text preserving the existing background color."""
