#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "math3d.h"
#include "pixbuf.h"

namespace cliviz {

struct Mesh {
    std::vector<vec3> positions;
    std::vector<uint32_t> indices; // 3 per triangle
    std::vector<vec3> colors;     // per-face color (RGB, 0-1 range)
    uint32_t n_verts = 0;
    uint32_t n_tris = 0;
};

// Procedural mesh generators
Mesh make_cube();
Mesh make_icosphere(uint32_t subdivisions = 2);

struct ZBuffer {
    uint32_t width, height;
    std::vector<float> data;

    ZBuffer(uint32_t w, uint32_t h)
        : width(w), height(h), data(w * h, std::numeric_limits<float>::max()) {}

    void clear() {
        std::fill(data.begin(), data.end(), std::numeric_limits<float>::max());
    }

    float at(uint32_t x, uint32_t y) const { return data[y * width + x]; }

    // Returns true if z is closer than current value, and updates
    bool test_and_set(uint32_t x, uint32_t y, float z) {
        float& cur = data[y * width + x];
        if (z < cur) { cur = z; return true; }
        return false;
    }
};

// Rasterize mesh into pixel buffer with z-testing.
// Returns number of triangles drawn (after backface culling).
uint32_t rasterize(const Mesh& mesh, const mat4& mvp,
                   PixelBuffer& pb, ZBuffer& zb);

} // namespace cliviz
