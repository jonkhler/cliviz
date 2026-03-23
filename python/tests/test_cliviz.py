import gc
import time

import numpy as np
import os
import pytest

import cliviz
from cliviz.framepace import FramePacer


def test_pixelbuffer_create():
    pb = cliviz.PixelBuffer(10, 5)
    assert pb.width == 10
    assert pb.height == 10  # 5 rows × 2 (half-block)


def test_pixelbuffer_pixels_is_numpy():
    pb = cliviz.PixelBuffer(10, 5)
    pixels = pb.pixels
    assert isinstance(pixels, np.ndarray)
    assert pixels.shape == (10, 10, 3)
    assert pixels.dtype == np.uint8


def test_pixelbuffer_clear():
    pb = cliviz.PixelBuffer(4, 2)
    pb.clear(128, 64, 32)
    pixels = pb.pixels
    assert pixels[0, 0, 0] == 128
    assert pixels[0, 0, 1] == 64
    assert pixels[0, 0, 2] == 32


def test_pixelbuffer_set():
    pb = cliviz.PixelBuffer(10, 5)
    pb.set(3, 4, 255, 0, 0)
    pixels = pb.pixels
    assert pixels[4, 3, 0] == 255
    assert pixels[4, 3, 1] == 0
    assert pixels[4, 3, 2] == 0


def test_numpy_write_through():
    """Writing to the numpy array is visible to the engine (zero-copy)."""
    pb = cliviz.PixelBuffer(10, 5)
    pixels = pb.pixels
    pixels[2, 3] = [200, 100, 50]
    pixels2 = pb.pixels
    assert pixels2[2, 3, 0] == 200
    assert pixels2[2, 3, 1] == 100
    assert pixels2[2, 3, 2] == 50


def test_fill_rect():
    pb = cliviz.PixelBuffer(10, 5)
    pb.fill_rect(2, 2, 5, 6, 255, 128, 0)
    pixels = pb.pixels
    assert pixels[3, 3, 0] == 255  # inside
    assert pixels[0, 0, 0] == 0  # outside


def test_draw_text():
    pb = cliviz.PixelBuffer(20, 5)
    pb.clear(0, 0, 0)
    pb.draw_text(2, 1, "Hi", 255, 255, 255)
    # Verify via flush — should not crash and should emit cells
    # (can't easily inspect cells from Python, but we can verify it doesn't error)


def test_draw_text_with_bg():
    pb = cliviz.PixelBuffer(20, 5)
    pb.draw_text(0, 0, "AB", 255, 0, 0, 0, 0, 128)


def test_present_threshold_accepts_parameter():
    pb = cliviz.PixelBuffer(10, 5)
    pb.clear(100, 100, 100)
    pb.encode_all()
    # Should not crash — threshold is forwarded to C++ diff engine
    pb.present(color_threshold=8)


def test_terminal_accepts_color_mode():
    t = cliviz.Terminal(color_mode="256")
    # Should not crash — color mode stored for init


def test_pixelbuffer_term_rows():
    pb = cliviz.PixelBuffer(10, 5)
    assert pb.term_rows == 5
    assert pb.height == 10  # pixel rows = term_rows * 2


def test_terminal_was_interrupted_false_by_default():
    t = cliviz.Terminal()
    assert not t.was_interrupted()


def test_pixels_keeps_buffer_alive():
    """numpy array returned by .pixels must keep the PixelBuffer alive."""
    pb = cliviz.PixelBuffer(4, 2)
    pb.clear(42, 0, 0)
    pixels = pb.pixels
    del pb  # PixelBuffer ref dropped; array must still hold it alive
    gc.collect()
    # Array contents must still be readable (no use-after-free)
    assert pixels[0, 0, 0] == 42


def test_terminal_context_manager_init_fails_gracefully():
    """__enter__ raises RuntimeError (not a TTY in test env), not crash."""
    if os.isatty(1):
        pytest.skip("stdout is a TTY")
    t = cliviz.Terminal()
    with pytest.raises(RuntimeError):
        t.__enter__()


def test_terminal_fails_gracefully_in_test():
    t = cliviz.Terminal()
    if not os.isatty(1):
        assert not t.init()
        assert not t.active
    else:
        pytest.skip("stdout is a TTY")


# ── FramePacer ──

def test_framepaper_pace_is_monotonic():
    pacer = FramePacer(target_fps=200.0)  # very fast target so test doesn't sleep long
    t0 = time.monotonic()
    dt = pacer.pace()
    t1 = time.monotonic()
    assert dt > 0, "pace() must return a positive elapsed time"
    assert dt <= t1 - t0 + 0.01, "returned dt must not exceed wall time"


def test_framepaper_fps_property_after_pace():
    pacer = FramePacer(target_fps=200.0)
    pacer.pace()
    # fps is 0 until at least one pace() establishes a frame time
    pacer.pace()
    assert pacer.fps > 0, "fps must be positive after two pace() calls"


def test_framepaper_adapts_to_slow_terminal():
    """dt increases when frame_time exceeds 1.5× target."""
    pacer = FramePacer(target_fps=1000.0, min_fps=10.0)
    initial_dt = pacer.dt
    # Simulate a very slow frame by advancing _last backward
    pacer._last -= 0.1  # 100ms ago — way beyond any target
    pacer.pace()
    # After a slow frame, dt should have backed off (increased toward min_dt)
    assert pacer.dt >= initial_dt


# ── present_nodiff vs present behavioral difference ──

def test_present_nodiff_emits_all_cells():
    """present_nodiff writes every cell even without encode/dirty."""
    pb = cliviz.PixelBuffer(4, 2)
    # No encode — just call present_nodiff directly on the cell buffer
    # It must return cell_count = width * term_rows = 4 * 2 = 8
    count = pb.present_nodiff()
    assert count == 8


def test_present_only_emits_dirty_cells():
    """present after encode() only writes cells with actual changes."""
    pb = cliviz.PixelBuffer(4, 2)
    pb.clear(10, 20, 30)
    pb.encode_all()
    pb.present()  # establish front buffer

    # set() writes the pixel AND marks the cell dirty; numpy write alone does not
    pb.set(0, 0, 11, 21, 31)
    pb.encode()
    count = pb.present()
    assert count == 1, f"expected 1 changed cell, got {count}"
