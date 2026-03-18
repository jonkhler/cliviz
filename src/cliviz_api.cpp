#include "cliviz.h"

#include "framebuf.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "sdf.h"
#include "term.h"
#include "threadpool.h"

using namespace cliviz;

// Global state shared across C API calls
namespace {
OutputBuffer g_outbuf;
ThreadPool* g_pool = nullptr;
} // namespace

// ── Terminal lifecycle ──

int cliviz_init(void) {
    if (!term_init()) return -1;
    if (!g_pool) g_pool = new ThreadPool();
    return 0;
}

void cliviz_shutdown(void) {
    term_shutdown();
    delete g_pool;
    g_pool = nullptr;
}

int cliviz_term_size(uint16_t* cols, uint16_t* rows) {
    TermSize ts = term_get_size();
    if (ts.cols == 0 || ts.rows == 0) return -1;
    *cols = ts.cols;
    *rows = ts.rows;
    return 0;
}

int cliviz_term_was_resized(void) {
    return term_was_resized() ? 1 : 0;
}

// ── Pixel buffer ──

struct cliviz_pixbuf {
    std::unique_ptr<PixelBuffer> inner;
};

cliviz_pixbuf* cliviz_pixbuf_create(uint32_t term_cols, uint32_t term_rows) {
    auto inner = PixelBuffer::create(term_cols, term_rows);
    if (!inner) return nullptr;
    auto* pb = new cliviz_pixbuf{std::move(inner)};
    return pb;
}

void cliviz_pixbuf_destroy(cliviz_pixbuf* pb) {
    delete pb;
}

uint32_t cliviz_pixbuf_width(const cliviz_pixbuf* pb) {
    return pb->inner->width;
}

uint32_t cliviz_pixbuf_height(const cliviz_pixbuf* pb) {
    return pb->inner->height;
}

uint8_t* cliviz_pixbuf_pixels(cliviz_pixbuf* pb) {
    return pb->inner->pixels;
}

void cliviz_pixbuf_set(cliviz_pixbuf* pb, uint32_t x, uint32_t y,
                       uint8_t r, uint8_t g, uint8_t b) {
    pb->inner->set(x, y, r, g, b);
}

void cliviz_pixbuf_clear(cliviz_pixbuf* pb, uint8_t r, uint8_t g, uint8_t b) {
    pb->inner->clear(r, g, b);
}

void cliviz_pixbuf_fill_rect(cliviz_pixbuf* pb,
                             uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                             uint8_t r, uint8_t g, uint8_t b) {
    pb->inner->fill_rect(x0, y0, x1, y1, r, g, b);
}

uint32_t cliviz_pixbuf_flush(cliviz_pixbuf* pb) {
    pb->inner->encode();
    g_outbuf.clear();
    uint32_t emitted = pb->inner->fb->flush(g_outbuf);
    g_outbuf.flush();
    return emitted;
}

uint32_t cliviz_pixbuf_flush_full(cliviz_pixbuf* pb) {
    pb->inner->encode_all();
    g_outbuf.clear();
    uint32_t emitted = pb->inner->fb->flush(g_outbuf);
    g_outbuf.flush();
    return emitted;
}

// ── SDF raymarcher ──

namespace {

// Adapter: C function pointer → cliviz::SdfFn
thread_local cliviz_sdf_fn g_sdf_fn = nullptr;

float sdf_c_adapter(vec3 pos, float time) {
    return g_sdf_fn(pos.x, pos.y, pos.z, time);
}

} // namespace

void cliviz_sdf_render(cliviz_pixbuf* pb, cliviz_sdf_fn scene, float time,
                       float eye_x, float eye_y, float eye_z,
                       float center_x, float center_y, float center_z,
                       int max_steps) {
    g_sdf_fn = scene;
    vec3 eye{eye_x, eye_y, eye_z};
    vec3 center{center_x, center_y, center_z};
    if (g_pool) {
        sdf_render_parallel(*pb->inner, sdf_c_adapter, time,
                            eye, center, {0, 1, 0}, *g_pool, max_steps);
    } else {
        sdf_render(*pb->inner, sdf_c_adapter, time, eye, center, {0, 1, 0});
    }
}

float cliviz_sdf_default_scene(float x, float y, float z, float time) {
    return sdf_scene_default({x, y, z}, time);
}
