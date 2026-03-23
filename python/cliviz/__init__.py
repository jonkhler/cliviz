"""cliviz — high-throughput terminal pixel display.

Core types:
  Terminal     — raw-mode context manager; yields cols/rows and resize events.
  PixelBuffer  — RGB pixel array backed by a terminal cell framebuffer.
                 height == term_rows * 2 (half-block sub-pixel encoding).
  FramePacer   — adaptive frame-rate limiter.
"""

from cliviz._native import Terminal, PixelBuffer
from cliviz.framepace import FramePacer

__all__ = ["Terminal", "PixelBuffer", "FramePacer"]
