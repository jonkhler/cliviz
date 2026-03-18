import numpy as np
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
    """Writing to the numpy array should be visible to the engine."""
    pb = cliviz.PixelBuffer(10, 5)
    pixels = pb.pixels
    pixels[2, 3] = [200, 100, 50]
    # Read back via a fresh numpy view
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


def test_make_cube():
    mesh = cliviz.make_cube()
    assert mesh.n_tris == 12
    assert mesh.n_verts == 8


def test_make_icosphere():
    mesh = cliviz.make_icosphere(2)
    assert mesh.n_tris > 100
    assert mesh.n_verts > 40


def test_mat4_perspective():
    m = cliviz.perspective(1.0, 1.5, 0.1, 100.0)
    assert m.shape == (4, 4)
    assert m.dtype == np.float32


def test_mat4_look_at():
    eye = np.array([0, 0, 5], dtype=np.float32)
    center = np.array([0, 0, 0], dtype=np.float32)
    up = np.array([0, 1, 0], dtype=np.float32)
    m = cliviz.look_at(eye, center, up)
    assert m.shape == (4, 4)


def test_rasterize_cube():
    pb = cliviz.PixelBuffer(20, 10)
    zb = cliviz.ZBuffer(pb.width, pb.height)
    mesh = cliviz.make_cube()

    eye = np.array([0, 2, 5], dtype=np.float32)
    center = np.array([0, 0, 0], dtype=np.float32)
    up = np.array([0, 1, 0], dtype=np.float32)
    view = cliviz.look_at(eye, center, up)
    proj = cliviz.perspective(1.0, pb.width / pb.height, 0.1, 100.0)
    mvp = proj @ view

    pb.clear(0, 0, 0)
    zb.clear()
    tris = cliviz.rasterize(pb, zb, mesh, mvp.astype(np.float32))
    assert tris > 0

    # Some pixels should be non-black
    pixels = pb.pixels
    assert pixels.max() > 0


def test_sdf_render():
    pb = cliviz.PixelBuffer(10, 5)
    eye = np.array([0, 2, 5], dtype=np.float32)
    center = np.array([0, 0, 0], dtype=np.float32)
    cliviz.sdf_render(pb, 0.0, eye, center, max_steps=20)

    pixels = pb.pixels
    assert pixels.max() > 0


def test_terminal_fails_gracefully_in_test():
    """Terminal init should fail in non-TTY test runner."""
    t = cliviz.Terminal()
    # In a test runner, stdout is not a TTY
    import os

    if not os.isatty(1):
        assert not t.init()
        assert not t.active
    else:
        pytest.skip("stdout is a TTY")
