// Doom-style fire effect — classic pixel buffer demo.
//
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
// Run:   ./build/fire
// Keys:  q=quit

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <unistd.h>

#include "outbuf.h"
#include "pixbuf.h"
#include "term.h"

using namespace cliviz;

namespace {

// Fire palette: black → red → orange → yellow → white
struct RGB { uint8_t r, g, b; };

constexpr RGB fire_palette[] = {
    {0,0,0}, {7,7,7}, {31,7,7}, {47,15,7}, {71,15,7}, {87,23,7},
    {103,31,7}, {119,31,7}, {143,39,7}, {159,47,7}, {175,63,7},
    {191,71,7}, {199,71,7}, {223,79,7}, {223,87,7}, {223,87,7},
    {215,95,7}, {215,103,15}, {207,111,15}, {207,119,15}, {207,127,15},
    {207,135,23}, {199,135,23}, {199,143,23}, {199,151,31}, {191,159,31},
    {191,159,31}, {191,167,39}, {191,167,39}, {191,175,47}, {183,175,47},
    {183,183,47}, {183,183,55}, {207,207,111}, {223,223,159},
    {239,239,199}, {255,255,255},
};
constexpr int PALETTE_SIZE = sizeof(fire_palette) / sizeof(fire_palette[0]);

int read_key() {
    struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) return 0;
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    return c;
}

} // namespace

int main() {
    if (!term_init()) return 1;
    TermSize ts = term_get_size();
    if (ts.cols == 0 || ts.rows == 0) { term_shutdown(); return 1; }

    auto pb = PixelBuffer::create(ts.cols, ts.rows);
    OutputBuffer outbuf;

    uint32_t w = pb->width;
    uint32_t h = pb->height;

    // Fire buffer — one intensity value per pixel
    auto* fire = new uint8_t[w * h];
    std::memset(fire, 0, w * h);

    auto last = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        if (read_key() == 'q') break;

        // Set bottom row to max intensity (fire source)
        for (uint32_t x = 0; x < w; ++x) {
            fire[(h - 1) * w + x] = PALETTE_SIZE - 1;
        }

        // Propagate fire upward with random cooling
        for (uint32_t y = 0; y < h - 1; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t src = (y + 1) * w + x;
                uint32_t decay = static_cast<uint32_t>(std::rand()) % 3;
                int jitter = (static_cast<int>(std::rand()) % 3) - 1;
                uint32_t dst_x = static_cast<uint32_t>(
                    std::clamp(static_cast<int>(x) + jitter, 0, static_cast<int>(w) - 1));
                int val = static_cast<int>(fire[src]) - static_cast<int>(decay);
                fire[y * w + dst_x] = static_cast<uint8_t>(std::max(val, 0));
            }
        }

        // Render fire buffer to pixels
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                int idx = fire[y * w + x];
                idx = std::clamp(idx, 0, PALETTE_SIZE - 1);
                pb->set(x, y, fire_palette[idx].r, fire_palette[idx].g, fire_palette[idx].b);
            }
        }

        pb->encode_all();

        // FPS overlay
        char status[64];
        float fps = dt > 0 ? 1.0f / dt : 0;
        std::snprintf(status, sizeof(status), "%.0ffps  fire  [q]uit", fps);
        pb->draw_text(1, 0, status, 255, 255, 255, 50, 10, 0);

        outbuf.clear();
        pb->fb->flush(outbuf);
        outbuf.flush();

        usleep(16000);
    }

    delete[] fire;
    term_shutdown();
    return 0;
}
