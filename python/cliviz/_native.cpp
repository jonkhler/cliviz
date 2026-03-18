#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include "framebuf.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "term.h"

namespace nb = nanobind;
using namespace nb::literals;
using namespace cliviz;

static OutputBuffer g_outbuf;

// ── Terminal wrapper ──

struct Terminal {
    uint16_t cols = 0;
    uint16_t rows = 0;
    bool active = false;
    std::string color_mode_override;

    bool init(const std::string& color_mode_str = "") {
        if (!term_init()) return false;
        active = true;
        auto ts = term_get_size();
        cols = ts.cols;
        rows = ts.rows;
        // Set color mode: explicit override or auto-detect
        if (color_mode_str == "truecolor" || color_mode_str == "24bit") {
            g_outbuf.color_mode = ColorMode::TrueColor;
        } else if (color_mode_str == "256") {
            g_outbuf.color_mode = ColorMode::Color256;
        } else {
            g_outbuf.color_mode = detect_color_mode();
        }
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

    void encode() { inner->encode(); }
    void encode_all() { inner->encode_all(); }

    uint32_t present() {
        g_outbuf.clear();
        uint32_t n = inner->fb->flush(g_outbuf);
        g_outbuf.flush();
        return n;
    }

    uint32_t present_nodiff() {
        g_outbuf.clear();
        uint32_t n = inner->fb->flush_nodiff(g_outbuf);
        g_outbuf.flush();
        return n;
    }

    uint32_t flush_full() {
        inner->encode_all();
        return present_nodiff();
    }
};

// ── Module ──

NB_MODULE(_native, mod) {
    mod.doc() = "cliviz: high-throughput terminal pixel display";

    nb::class_<Terminal>(mod, "Terminal")
        .def(nb::init<>())
        .def("__init__", [](Terminal& t, const std::string& color_mode) {
            new (&t) Terminal();
            t.color_mode_override = color_mode;
        }, "color_mode"_a = "")
        .def("init", &Terminal::init, "color_mode"_a = "")
        .def("shutdown", &Terminal::shutdown)
        .def("was_resized", &Terminal::was_resized)
        .def_ro("cols", &Terminal::cols)
        .def_ro("rows", &Terminal::rows)
        .def_ro("active", &Terminal::active)
        .def("__enter__", [](Terminal& t) -> Terminal& {
            if (!t.init(t.color_mode_override)) throw std::runtime_error("Failed to init terminal (not a TTY?)");
            return t;
        })
        .def("__exit__", [](Terminal& t, const nb::args&) {
            t.shutdown();
        });

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
        .def("encode", &PyPixelBuffer::encode, "Encode dirty pixel pairs into cells")
        .def("encode_all", &PyPixelBuffer::encode_all, "Encode all pixel pairs into cells")
        .def("present", &PyPixelBuffer::present, "Diff + write to terminal (call after encode + draw_text)")
        .def("present_nodiff", &PyPixelBuffer::present_nodiff, "Write all cells to terminal (no diff, for full redraws)")
        .def("flush", &PyPixelBuffer::flush, "Encode dirty cells and write to terminal")
        .def("flush_full", &PyPixelBuffer::flush_full, "Encode all cells and write to terminal")
        .def("draw_text", [](PyPixelBuffer& self, uint32_t col, uint32_t row,
                             const std::string& text,
                             uint8_t fg_r, uint8_t fg_g, uint8_t fg_b,
                             uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
            self.inner->draw_text(col, row, text.c_str(), fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
        }, "col"_a, "row"_a, "text"_a,
           "fg_r"_a, "fg_g"_a, "fg_b"_a,
           "bg_r"_a = 0, "bg_g"_a = 0, "bg_b"_a = 0);
}
