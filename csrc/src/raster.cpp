#include "raster.h"

#include <algorithm>
#include <cmath>

namespace cliviz {

Mesh make_cube() {
    Mesh m;
    m.positions = {
        {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1}, // back
        {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}, // front
    };
    m.indices = {
        // front
        4, 5, 6,  4, 6, 7,
        // back
        1, 0, 3,  1, 3, 2,
        // left
        0, 4, 7,  0, 7, 3,
        // right
        5, 1, 2,  5, 2, 6,
        // top
        7, 6, 2,  7, 2, 3,
        // bottom
        0, 1, 5,  0, 5, 4,
    };
    m.colors = {
        {0.2f, 0.6f, 1.0f}, {0.2f, 0.6f, 1.0f}, // front (blue)
        {1.0f, 0.3f, 0.3f}, {1.0f, 0.3f, 0.3f}, // back (red)
        {0.3f, 1.0f, 0.3f}, {0.3f, 1.0f, 0.3f}, // left (green)
        {1.0f, 1.0f, 0.3f}, {1.0f, 1.0f, 0.3f}, // right (yellow)
        {1.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}, // top (orange)
        {0.5f, 0.3f, 1.0f}, {0.5f, 0.3f, 1.0f}, // bottom (purple)
    };
    m.n_verts = 8;
    m.n_tris = 12;
    return m;
}

Mesh make_icosphere(uint32_t subdivisions) {
    // Start with icosahedron
    float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    Mesh m;
    m.positions = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };
    // Normalize to unit sphere
    for (auto& p : m.positions) {
        p = normalize(p);
    }

    m.indices = {
        0,11, 5,  0, 5, 1,  0, 1, 7,  0, 7,10,  0,10,11,
        1, 5, 9,  5,11, 4, 11,10, 2, 10, 7, 6,  7, 1, 8,
        3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
        4, 9, 5,  2, 4,11,  6, 2,10,  8, 6, 7,  9, 8, 1,
    };

    // Subdivide
    for (uint32_t s = 0; s < subdivisions; ++s) {
        std::vector<uint32_t> new_indices;
        auto midpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
            vec3 mid = normalize((m.positions[a] + m.positions[b]) * 0.5f);
            auto idx = static_cast<uint32_t>(m.positions.size());
            m.positions.push_back(mid);
            return idx;
        };
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            uint32_t v0 = m.indices[i];
            uint32_t v1 = m.indices[i + 1];
            uint32_t v2 = m.indices[i + 2];
            uint32_t a = midpoint(v0, v1);
            uint32_t b = midpoint(v1, v2);
            uint32_t c = midpoint(v2, v0);
            new_indices.insert(new_indices.end(), {v0, a, c});
            new_indices.insert(new_indices.end(), {v1, b, a});
            new_indices.insert(new_indices.end(), {v2, c, b});
            new_indices.insert(new_indices.end(), {a, b, c});
        }
        m.indices = std::move(new_indices);
    }

    m.n_verts = static_cast<uint32_t>(m.positions.size());
    m.n_tris = static_cast<uint32_t>(m.indices.size()) / 3;

    // Per-vertex normals (on a unit sphere, normal == position)
    m.normals.resize(m.n_verts);
    m.vertex_colors.resize(m.n_verts);
    for (uint32_t i = 0; i < m.n_verts; ++i) {
        m.normals[i] = normalize(m.positions[i]);
        // Material color from normal direction
        vec3 n = m.normals[i];
        m.vertex_colors[i] = {
            std::abs(n.x) * 0.5f + 0.5f,
            std::abs(n.y) * 0.5f + 0.5f,
            std::abs(n.z) * 0.5f + 0.5f,
        };
    }

    // Per-face colors (fallback for flat mode)
    m.colors.resize(m.n_tris);
    for (uint32_t i = 0; i < m.n_tris; ++i) {
        vec3 center = (m.positions[m.indices[i * 3]] +
                       m.positions[m.indices[i * 3 + 1]] +
                       m.positions[m.indices[i * 3 + 2]]) * (1.0f / 3.0f);
        vec3 n = normalize(center);
        m.colors[i] = {
            std::abs(n.x) * 0.5f + 0.5f,
            std::abs(n.y) * 0.5f + 0.5f,
            std::abs(n.z) * 0.5f + 0.5f,
        };
    }

    return m;
}

namespace {

struct ScreenVert {
    float x, y, z; // screen-space x,y; NDC z for depth
};

// Edge function: positive if (px,py) is on the left side of edge (v0→v1)
inline float edge_fn(float v0x, float v0y, float v1x, float v1y, float px, float py) {
    return (v1x - v0x) * (py - v0y) - (v1y - v0y) * (px - v0x);
}

} // namespace

uint32_t rasterize(const Mesh& mesh, const mat4& mvp,
                   PixelBuffer& pb, ZBuffer& zb,
                   const Light& light) {
    auto w = static_cast<float>(pb.width);
    auto h = static_cast<float>(pb.height);
    float half_w = w * 0.5f;
    float half_h = h * 0.5f;

    // Transform all vertices to clip space
    std::vector<vec4> clip(mesh.n_verts);
    for (uint32_t i = 0; i < mesh.n_verts; ++i) {
        const vec3& p = mesh.positions[i];
        clip[i] = mvp * vec4{p.x, p.y, p.z, 1.0f};
    }

    uint32_t drawn = 0;

    for (uint32_t tri = 0; tri < mesh.n_tris; ++tri) {
        uint32_t i0 = mesh.indices[tri * 3];
        uint32_t i1 = mesh.indices[tri * 3 + 1];
        uint32_t i2 = mesh.indices[tri * 3 + 2];

        const vec4& c0 = clip[i0];
        const vec4& c1 = clip[i1];
        const vec4& c2 = clip[i2];

        // Simple near-plane clip: reject if any vertex is behind camera
        if (c0.w <= 0.0f || c1.w <= 0.0f || c2.w <= 0.0f) continue;

        // Perspective divide → NDC
        float inv_w0 = 1.0f / c0.w, inv_w1 = 1.0f / c1.w, inv_w2 = 1.0f / c2.w;
        ScreenVert s0{(c0.x * inv_w0 + 1.0f) * half_w, (1.0f - c0.y * inv_w0) * half_h, c0.z * inv_w0};
        ScreenVert s1{(c1.x * inv_w1 + 1.0f) * half_w, (1.0f - c1.y * inv_w1) * half_h, c1.z * inv_w1};
        ScreenVert s2{(c2.x * inv_w2 + 1.0f) * half_w, (1.0f - c2.y * inv_w2) * half_h, c2.z * inv_w2};

        // Backface culling (screen-space winding)
        float area = edge_fn(s0.x, s0.y, s1.x, s1.y, s2.x, s2.y);
        if (area <= 0.0f) continue;

        float inv_area = 1.0f / area;

        // Bounding box (clamped to screen)
        float min_x = std::max(0.0f, std::floor(std::min({s0.x, s1.x, s2.x})));
        float min_y = std::max(0.0f, std::floor(std::min({s0.y, s1.y, s2.y})));
        float max_x = std::min(w - 1.0f, std::ceil(std::max({s0.x, s1.x, s2.x})));
        float max_y = std::min(h - 1.0f, std::ceil(std::max({s0.y, s1.y, s2.y})));

        if (min_x > max_x || min_y > max_y) continue;

        bool gouraud = mesh.has_gouraud();
        vec3 light_dir = normalize(light.direction);

        // Flat shading fallback color
        uint8_t flat_r = 0, flat_g = 0, flat_b = 0;
        if (!gouraud) {
            const vec3& col = mesh.colors[tri];
            flat_r = static_cast<uint8_t>(std::clamp(col.x * 255.0f, 0.0f, 255.0f));
            flat_g = static_cast<uint8_t>(std::clamp(col.y * 255.0f, 0.0f, 255.0f));
            flat_b = static_cast<uint8_t>(std::clamp(col.z * 255.0f, 0.0f, 255.0f));
        }

        // Rasterize
        for (float py = min_y + 0.5f; py <= max_y + 0.5f; py += 1.0f) {
            for (float px = min_x + 0.5f; px <= max_x + 0.5f; px += 1.0f) {
                float w0 = edge_fn(s1.x, s1.y, s2.x, s2.y, px, py);
                float w1 = edge_fn(s2.x, s2.y, s0.x, s0.y, px, py);
                float w2 = edge_fn(s0.x, s0.y, s1.x, s1.y, px, py);

                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                float b0 = w0 * inv_area;
                float b1 = w1 * inv_area;
                float b2 = w2 * inv_area;
                float z = b0 * s0.z + b1 * s1.z + b2 * s2.z;

                auto ix = static_cast<uint32_t>(px);
                auto iy = static_cast<uint32_t>(py);

                if (ix < pb.width && iy < pb.height && zb.test_and_set(ix, iy, z)) {
                    if (gouraud) {
                        // Interpolate normal
                        vec3 n = normalize(
                            mesh.normals[i0] * b0 +
                            mesh.normals[i1] * b1 +
                            mesh.normals[i2] * b2);
                        // Interpolate vertex color
                        vec3 vc = mesh.vertex_colors[i0] * b0 +
                                  mesh.vertex_colors[i1] * b1 +
                                  mesh.vertex_colors[i2] * b2;
                        // Diffuse + ambient
                        float diff = std::max(dot(n, light_dir), 0.0f);
                        vec3 col = vc * (light.color * diff + light.color * light.ambient);
                        pb.set(ix, iy,
                            static_cast<uint8_t>(std::clamp(col.x * 255.0f, 0.0f, 255.0f)),
                            static_cast<uint8_t>(std::clamp(col.y * 255.0f, 0.0f, 255.0f)),
                            static_cast<uint8_t>(std::clamp(col.z * 255.0f, 0.0f, 255.0f)));
                    } else {
                        pb.set(ix, iy, flat_r, flat_g, flat_b);
                    }
                }
            }
        }

        ++drawn;
    }

    return drawn;
}

} // namespace cliviz
