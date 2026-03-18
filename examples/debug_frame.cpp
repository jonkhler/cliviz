#include <cstdio>
#include <cmath>
#include "math3d.h"
#include "outbuf.h"
#include "pixbuf.h"
#include "raster.h"
#include "term.h"

using namespace cliviz;

int main() {
    // Render one frame to a file for inspection
    auto pb = PixelBuffer::create(20, 5); // small: 20 cols, 5 rows
    ZBuffer zb(pb->width, pb->height);
    OutputBuffer outbuf;

    Mesh cube = make_cube();
    mat4 view = mat4::look_at({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
    float aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);
    mat4 mvp = proj * view;

    pb->clear(15, 15, 25);
    zb.clear();
    rasterize(cube, mvp, *pb, zb);
    pb->encode();

    outbuf.clear();
    uint32_t emitted = pb->fb->flush(outbuf);

    // Dump raw bytes to file
    FILE* f = fopen("frame_dump.bin", "wb");
    fwrite(outbuf.view().data(), 1, outbuf.size(), f);
    fclose(f);

    std::fprintf(stderr, "Emitted %u cells, %u bytes\n", emitted, outbuf.size());

    // Also print hex of first 200 bytes
    auto v = outbuf.view();
    for (size_t i = 0; i < std::min(v.size(), size_t(500)); ++i) {
        auto c = static_cast<unsigned char>(v[i]);
        if (c == 0x1b) std::fprintf(stderr, "\nESC");
        else if (c >= 0x20 && c < 0x7f) std::fprintf(stderr, "%c", c);
        else std::fprintf(stderr, "\\x%02x", c);
    }
    std::fprintf(stderr, "\n");
    return 0;
}
