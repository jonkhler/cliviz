#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>

#include "cell.h"
#include "framebuf.h"
#include "math3d.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "raster.h"
#include "sdf.h"
#include "term.h"
#include "threadpool.h"

namespace nb = nanobind;
using namespace nb::literals;
using namespace cliviz;

// ── Global state ──

static OutputBuffer g_outbuf;
static std::unique_ptr<ThreadPool> g_pool;

// ── Terminal wrapper ──

struct Terminal {
    uint16_t cols = 0;
    uint16_t rows = 0;
    bool active = false;

    bool init() {
        if (!term_init()) return false;
        active = true;
        auto ts = term_get_size();
        cols = ts.cols;
        rows = ts.rows;
        if (!g_pool) g_pool = std::make_unique<ThreadPool>();
        return true;
    }

    void shutdown() {
        if (active) {
            term_shutdown();
            active = false;
        }
    }

    bool was_resized() {
        if (!term_was_resized()) return false;
        auto ts = term_get_size();
        cols = ts.cols;
        rows = ts.rows;
        return true;
    }

    ~Terminal() { shutdown(); }
};

// ── PixelBuffer wrapper with numpy view ──

struct PyPixelBuffer {
    std::unique_ptr<PixelBuffer> inner;

    PyPixelBuffer(uint32_t term_cols, uint32_t term_rows)
        : inner(PixelBuffer::create(term_cols, term_rows)) {
        if (!inner) throw std::runtime_error("Failed to create PixelBuffer");
    }

    uint32_t width() const { return inner->width; }
    uint32_t height() const { return inner->height; }

    // Zero-copy numpy view into the pixel array
    nb::ndarray<nb::numpy, uint8_t> pixels() {
        size_t shape[3] = {inner->height, inner->width, 3};
        return nb::ndarray<nb::numpy, uint8_t>(
            inner->pixels, 3, shape, nb::handle());
    }

    void set(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
        inner->set(x, y, r, g, b);
    }

    void clear(uint8_t r, uint8_t g, uint8_t b) {
        inner->clear(r, g, b);
    }

    void fill_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                   uint8_t r, uint8_t g, uint8_t b) {
        inner->fill_rect(x0, y0, x1, y1, r, g, b);
    }

    uint32_t flush() {
        inner->encode();
        g_outbuf.clear();
        uint32_t n = inner->fb->flush(g_outbuf);
        g_outbuf.flush();
        return n;
    }

    uint32_t flush_full() {
        inner->encode_all();
        g_outbuf.clear();
        uint32_t n = inner->fb->flush(g_outbuf);
        g_outbuf.flush();
        return n;
    }
};

// ── ZBuffer wrapper ──

struct PyZBuffer {
    ZBuffer inner;
    PyZBuffer(uint32_t w, uint32_t h) : inner(w, h) {}
    void clear() { inner.clear(); }
};

// ── mat4 helpers ──

using Mat4Array = nb::ndarray<float, nb::numpy, nb::shape<4, 4>>;

mat4 ndarray_to_mat4(Mat4Array a) {
    mat4 m{};
    auto v = a.view();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            m.m[col][row] = v(row, col); // numpy is row-major, mat4 is col-major
    return m;
}

nb::ndarray<nb::numpy, float> mat4_to_ndarray(const mat4& m) {
    float* data = new float[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            data[row * 4 + col] = m.m[col][row];
    size_t shape[2] = {4, 4};
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<float*>(p); });
    return nb::ndarray<nb::numpy, float>(data, 2, shape, owner);
}

// ── Module definition ──

NB_MODULE(_native, mod) {
    mod.doc() = "cliviz: Terminal 3D rendering engine";

    // Terminal
    nb::class_<Terminal>(mod, "Terminal")
        .def(nb::init<>())
        .def("init", &Terminal::init)
        .def("shutdown", &Terminal::shutdown)
        .def("was_resized", &Terminal::was_resized)
        .def_ro("cols", &Terminal::cols)
        .def_ro("rows", &Terminal::rows)
        .def_ro("active", &Terminal::active)
        .def("__enter__", [](Terminal& t) -> Terminal& {
            if (!t.init()) throw std::runtime_error("Failed to init terminal (not a TTY?)");
            return t;
        })
        .def("__exit__", [](Terminal& t, nb::object, nb::object, nb::object) {
            t.shutdown();
        });

    // PixelBuffer
    nb::class_<PyPixelBuffer>(mod, "PixelBuffer")
        .def(nb::init<uint32_t, uint32_t>(), "term_cols"_a, "term_rows"_a)
        .def_prop_ro("width", &PyPixelBuffer::width)
        .def_prop_ro("height", &PyPixelBuffer::height)
        .def_prop_ro("pixels", &PyPixelBuffer::pixels,
                     "Zero-copy numpy view (height, width, 3) into pixel data")
        .def("set", &PyPixelBuffer::set, "x"_a, "y"_a, "r"_a, "g"_a, "b"_a)
        .def("clear", &PyPixelBuffer::clear, "r"_a, "g"_a, "b"_a)
        .def("fill_rect", &PyPixelBuffer::fill_rect,
             "x0"_a, "y0"_a, "x1"_a, "y1"_a, "r"_a, "g"_a, "b"_a)
        .def("flush", &PyPixelBuffer::flush, "Encode dirty cells and write to terminal")
        .def("flush_full", &PyPixelBuffer::flush_full, "Encode all cells and write to terminal");

    // ZBuffer
    nb::class_<PyZBuffer>(mod, "ZBuffer")
        .def(nb::init<uint32_t, uint32_t>(), "width"_a, "height"_a)
        .def("clear", &PyZBuffer::clear);

    // mat4 helpers
    mod.def("perspective", [](float fov, float aspect, float near, float far) {
        return mat4_to_ndarray(mat4::perspective(fov, aspect, near, far));
    }, "fov_y"_a, "aspect"_a, "near"_a, "far"_a);

    mod.def("look_at", [](nb::ndarray<float, nb::numpy, nb::shape<3>> eye,
                          nb::ndarray<float, nb::numpy, nb::shape<3>> center,
                          nb::ndarray<float, nb::numpy, nb::shape<3>> up) {
        auto e = eye.view(); auto c = center.view(); auto u = up.view();
        return mat4_to_ndarray(mat4::look_at(
            {e(0), e(1), e(2)}, {c(0), c(1), c(2)}, {u(0), u(1), u(2)}));
    }, "eye"_a, "center"_a, "up"_a);

    mod.def("rotate_y", [](float rad) {
        return mat4_to_ndarray(mat4::rotate_y(rad));
    }, "radians"_a);

    mod.def("rotate_x", [](float rad) {
        return mat4_to_ndarray(mat4::rotate_x(rad));
    }, "radians"_a);

    // Mesh
    nb::class_<Mesh>(mod, "Mesh")
        .def_ro("n_verts", &Mesh::n_verts)
        .def_ro("n_tris", &Mesh::n_tris);

    mod.def("make_cube", &make_cube);
    mod.def("make_icosphere", &make_icosphere, "subdivisions"_a = 2);

    // Rasterize
    mod.def("rasterize", [](PyPixelBuffer& pb, PyZBuffer& zb,
                            const Mesh& mesh, Mat4Array mvp_arr) {
        mat4 mvp = ndarray_to_mat4(mvp_arr);
        return rasterize(mesh, mvp, *pb.inner, zb.inner);
    }, "pb"_a, "zb"_a, "mesh"_a, "mvp"_a);

    // SDF
    mod.def("sdf_render", [](PyPixelBuffer& pb, float time,
                             nb::ndarray<float, nb::numpy, nb::shape<3>> eye_arr,
                             nb::ndarray<float, nb::numpy, nb::shape<3>> center_arr,
                             int max_steps) {
        auto e = eye_arr.view(); auto c = center_arr.view();
        vec3 eye{e(0), e(1), e(2)};
        vec3 center{c(0), c(1), c(2)};
        if (g_pool) {
            sdf_render_parallel(*pb.inner, sdf_scene_default, time,
                                eye, center, {0, 1, 0}, *g_pool, max_steps);
        } else {
            sdf_render(*pb.inner, sdf_scene_default, time,
                       eye, center, {0, 1, 0});
        }
    }, "pb"_a, "time"_a, "eye"_a, "center"_a, "max_steps"_a = 40);
}
