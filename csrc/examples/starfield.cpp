// 3D starfield — flying through space.
//
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
// Run:   ./build/starfield
// Keys:  +/- speed, q=quit

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>

#include "outbuf.h"
#include "pixbuf.h"
#include "term.h"

using namespace cliviz;

namespace {

struct Star {
    float x, y, z;
};

int read_key() {
    struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) return 0;
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    return c;
}

float randf() { return static_cast<float>(std::rand()) / RAND_MAX; }

} // namespace

int main() {
    if (!term_init()) return 1;
    TermSize ts = term_get_size();
    if (ts.cols == 0 || ts.rows == 0) { term_shutdown(); return 1; }

    auto pb = PixelBuffer::create(ts.cols, ts.rows);
    OutputBuffer outbuf;

    auto w = static_cast<float>(pb->width);
    auto h = static_cast<float>(pb->height);
    float cx = w * 0.5f;
    float cy = h * 0.5f;

    constexpr int NUM_STARS = 800;
    Star stars[NUM_STARS];
    for (auto& s : stars) {
        s.x = (randf() - 0.5f) * 20.0f;
        s.y = (randf() - 0.5f) * 20.0f;
        s.z = randf() * 10.0f + 0.1f;
    }

    float speed = 3.0f;
    auto last = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        int key = read_key();
        if (key == 'q') break;
        if (key == '+' || key == '=') speed = std::min(20.0f, speed + 1.0f);
        if (key == '-') speed = std::max(0.5f, speed - 1.0f);

        // Clear to black
        pb->clear(0, 0, 4);

        // Update and render stars
        for (auto& s : stars) {
            s.z -= speed * dt;
            if (s.z <= 0.1f) {
                s.x = (randf() - 0.5f) * 20.0f;
                s.y = (randf() - 0.5f) * 20.0f;
                s.z = 10.0f;
            }

            // Project to screen
            float px = (s.x / s.z) * cx + cx;
            float py = (s.y / s.z) * cy + cy;

            if (px < 0 || px >= w || py < 0 || py >= h) continue;

            // Brightness based on distance
            float brightness = 1.0f - (s.z / 10.0f);
            brightness = std::clamp(brightness, 0.0f, 1.0f);
            auto b = static_cast<uint8_t>(brightness * 255.0f);
            auto b2 = static_cast<uint8_t>(brightness * brightness * 200.0f);

            // Streak length based on speed and distance
            float streak = speed * 0.3f * (1.0f - s.z / 10.0f);
            for (float t = 0; t < streak && py + t < h; t += 1.0f) {
                float fade = 1.0f - t / streak;
                auto fb = static_cast<uint8_t>(b * fade);
                auto fb2 = static_cast<uint8_t>(b2 * fade);
                pb->set(static_cast<uint32_t>(px), static_cast<uint32_t>(py + t),
                        fb, fb, static_cast<uint8_t>(std::min(255, fb + fb2)));
            }
        }

        pb->encode_all();

        char status[64];
        float fps = dt > 0 ? 1.0f / dt : 0;
        std::snprintf(status, sizeof(status), "%.0ffps  speed:%.0f  starfield  [+-]speed [q]uit", fps, speed);
        pb->draw_text(1, 0, status, 180, 180, 255, 0, 0, 20);

        outbuf.clear();
        pb->fb->flush(outbuf);
        outbuf.flush();

        usleep(16000);
    }

    term_shutdown();
    return 0;
}
