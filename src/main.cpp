#include <chrono>
#include <cmath>
#include <cstdio>
#include <poll.h>
#include <unistd.h>

#include "math3d.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "raster.h"
#include "sdf.h"
#include "term.h"
#include "threadpool.h"

using namespace cliviz;
using Clock = std::chrono::steady_clock;

namespace {

struct Camera {
    float yaw = 0.0f;
    float pitch = 0.3f;
    float distance = 4.0f;

    mat4 view_matrix() const {
        float cx = std::cos(pitch), sx = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        vec3 eye{
            distance * cx * sy,
            distance * sx,
            distance * cx * cy,
        };
        return mat4::look_at(eye, {0, 0, 0}, {0, 1, 0});
    }
};

// Non-blocking stdin read. Returns 0 if no input available.
// Uses a short timeout for escape sequence continuation bytes
// to distinguish bare ESC from arrow keys (ESC [ A/B/C/D).
int read_key() {
    struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) return 0;
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;

    if (c == '\x1b') {
        // Wait up to 30ms for escape sequence continuation
        struct pollfd esc_pfd{STDIN_FILENO, POLLIN, 0};
        if (poll(&esc_pfd, 1, 30) <= 0) return 0; // bare ESC → ignore
        char seq[2]{};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 0;
        if (seq[0] != '[') return 0;
        if (poll(&esc_pfd, 1, 30) <= 0) return 0;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 0;
        switch (seq[1]) {
        case 'A': return 'k'; // up
        case 'B': return 'j'; // down
        case 'C': return 'l'; // right
        case 'D': return 'h'; // left
        }
        return 0;
    }
    return c;
}

} // namespace

int main() {
    if (!term_init()) {
        std::fprintf(stderr, "Failed to initialize terminal (not a TTY?)\n");
        return 1;
    }

    TermSize ts = term_get_size();
    if (ts.cols == 0 || ts.rows == 0) {
        term_shutdown();
        std::fprintf(stderr, "Cannot determine terminal size\n");
        return 1;
    }
    // Clamp to reasonable limits to prevent excessive allocation
    ts.cols = std::min(ts.cols, static_cast<uint16_t>(1000));
    ts.rows = std::min(ts.rows, static_cast<uint16_t>(500));

    auto pb = PixelBuffer::create(ts.cols, ts.rows - 1); // leave 1 row for status
    ZBuffer zb(pb->width, pb->height);
    OutputBuffer outbuf;

    ThreadPool pool;
    Mesh cube = make_cube();
    Mesh sphere = make_icosphere(2);
    Mesh* active_mesh = &cube;

    Camera cam;
    float aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);

    enum class Mode { Raster, SDF } mode = Mode::Raster;

    float angle = 0.0f;
    bool auto_rotate = true;
    bool running = true;
    int sdf_quality = 80; // adaptive: 80 (full), 40 (fast), 20 (ultra-fast)

    auto last_frame = Clock::now();

    while (running) {
        auto frame_start = Clock::now();
        float dt = std::chrono::duration<float>(frame_start - last_frame).count();
        last_frame = frame_start;

        // Input — drain all pending keys
        for (int key = read_key(); key != 0; key = read_key()) {
            switch (key) {
            case 'q': running = false; break;
            case 'h': case 'a': cam.yaw -= 0.1f; break;
            case 'l': case 'd': cam.yaw += 0.1f; break;
            case 'k': case 'w': cam.pitch += 0.05f; break;
            case 'j': case 's': cam.pitch -= 0.05f; break;
            case '+': case '=': cam.distance = std::max(1.5f, cam.distance - 0.3f); break;
            case '-': cam.distance = std::min(20.0f, cam.distance + 0.3f); break;
            case ' ': auto_rotate = !auto_rotate; break;
            case '1': active_mesh = &cube; mode = Mode::Raster; break;
            case '2': active_mesh = &sphere; mode = Mode::Raster; break;
            case '3': mode = Mode::SDF; break;
            default: break;
            }
        }

        cam.pitch = std::clamp(cam.pitch, -1.4f, 1.4f);

        // Handle terminal resize
        if (term_was_resized()) {
            ts = term_get_size();
            ts.cols = std::min(ts.cols, static_cast<uint16_t>(1000));
            ts.rows = std::min(ts.rows, static_cast<uint16_t>(500));
            if (ts.cols > 0 && ts.rows > 1) {
                pb = PixelBuffer::create(ts.cols, ts.rows - 1);
                zb = ZBuffer(pb->width, pb->height);
                aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
                proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);
                // Clear screen after resize
                outbuf.clear();
                outbuf.append("\x1b[2J", 4);
                outbuf.flush();
            }
        }

        if (auto_rotate) {
            angle += dt * 0.8f;
        }

        // Render
        uint32_t tris_drawn = 0;
        if (mode == Mode::SDF) {
            float cx = std::cos(cam.pitch), sx = std::sin(cam.pitch);
            float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
            vec3 eye{cam.distance * cx * sy, cam.distance * sx, cam.distance * cx * cy};
            sdf_render_parallel(*pb, sdf_scene_default, angle, eye, {0, 0, 0}, {0, 1, 0}, pool, sdf_quality);
            pb->encode_all(); // fast path: SDF writes every pixel
        } else {
            pb->clear(15, 15, 25);
            zb.clear();
            mat4 model = mat4::rotate_y(angle) * mat4::rotate_x(angle * 0.3f);
            mat4 mvp = proj * cam.view_matrix() * model;
            tris_drawn = rasterize(*active_mesh, mvp, *pb, zb);
            pb->encode();
        }

        outbuf.clear();
        uint32_t cells_emitted = pb->fb->flush(outbuf);

        // Status bar
        auto frame_end = Clock::now();
        float frame_ms = std::chrono::duration<float, std::milli>(frame_end - frame_start).count();
        float fps = dt > 0 ? 1.0f / dt : 0.0f;

        outbuf.emit_cursor_to(ts.rows, 1);
        outbuf.append("\x1b[0m\x1b[7m", 8); // reset + inverse
        char status[128];
        const char* mode_name = mode == Mode::SDF ? "sdf" :
                                 (active_mesh == &cube ? "cube" : "sphere");
        int n;
        if (mode == Mode::SDF) {
            n = std::snprintf(status, sizeof(status),
                " %s q:%d | cells:%u | %.1fms | %.0ffps | WASD:cam +-:zoom 1/2/3:mode q:quit ",
                mode_name, sdf_quality, cells_emitted, frame_ms, fps);
        } else {
            n = std::snprintf(status, sizeof(status),
                " %s | tris:%u cells:%u | %.1fms | %.0ffps | WASD:cam +-:zoom 1/2/3:mode q:quit ",
                mode_name, tris_drawn, cells_emitted, frame_ms, fps);
        }
        // Pad to terminal width
        for (int i = n; i < ts.cols; ++i) status[i] = ' ';
        outbuf.append(status, static_cast<uint32_t>(std::min(static_cast<int>(ts.cols), static_cast<int>(sizeof(status)))));
        outbuf.append("\x1b[0m", 4); // reset

        outbuf.flush();

        // Adaptive quality for SDF mode
        if (mode == Mode::SDF) {
            if (frame_ms > 20.0f && sdf_quality > 20) {
                sdf_quality = std::max(20, sdf_quality - 10);
            } else if (frame_ms < 12.0f && sdf_quality < 80) {
                sdf_quality = std::min(80, sdf_quality + 5);
            }
        }

        // No artificial frame limiter — synchronized output (\e[?2026h/l)
        // lets the terminal handle pacing. This maximizes responsiveness.
    }

    term_shutdown();
    return 0;
}
