import numpy as np
import os
import pytest

import cliviz


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


def test_terminal_fails_gracefully_in_test():
    t = cliviz.Terminal()
    if not os.isatty(1):
        assert not t.init()
        assert not t.active
    else:
        pytest.skip("stdout is a TTY")
