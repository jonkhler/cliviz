#include "sdf.h"

#include <algorithm>
#include <cmath>

namespace cliviz {

namespace {

inline float sdf_box(vec3 p, vec3 b) {
    vec3 d{std::abs(p.x) - b.x, std::abs(p.y) - b.y, std::abs(p.z) - b.z};
    vec3 clamped{std::max(d.x, 0.0f), std::max(d.y, 0.0f), std::max(d.z, 0.0f)};
    return length(clamped) + std::min(std::max({d.x, d.y, d.z}), 0.0f);
}

inline float op_smooth_union(float d1, float d2, float k) {
    float h = std::clamp(0.5f + 0.5f * (d2 - d1) / k, 0.0f, 1.0f);
    return d2 * (1.0f - h) + d1 * h - k * h * (1.0f - h);
}

// Estimate normal via central differences
inline vec3 calc_normal(SdfFn scene, vec3 p, float time) {
    constexpr float e = 0.001f;
    return normalize({
        scene({p.x + e, p.y, p.z}, time) - scene({p.x - e, p.y, p.z}, time),
        scene({p.x, p.y + e, p.z}, time) - scene({p.x, p.y - e, p.z}, time),
        scene({p.x, p.y, p.z + e}, time) - scene({p.x, p.y, p.z - e}, time),
    });
}

// Simple ambient occlusion approximation
inline float calc_ao(SdfFn scene, vec3 p, vec3 n, float time) {
    float occ = 0.0f;
    float scale = 1.0f;
    for (int i = 0; i < 5; ++i) {
        float h = 0.01f + 0.12f * static_cast<float>(i);
        float d = scene(p + n * h, time);
        occ += (h - d) * scale;
        scale *= 0.95f;
    }
    return std::clamp(1.0f - 3.0f * occ, 0.0f, 1.0f);
}

} // namespace

float sdf_sphere(vec3 pos, float /*time*/) {
    return length(pos) - 1.0f;
}

float sdf_rounded_cube(vec3 pos, float /*time*/) {
    return sdf_box(pos, {0.8f, 0.8f, 0.8f}) - 0.1f;
}

float sdf_scene_default(vec3 pos, float time) {
    // Rotating sphere + floor plane + secondary sphere
    float c = std::cos(time), s = std::sin(time);
    vec3 p1{pos.x * c - pos.z * s, pos.y, pos.x * s + pos.z * c};

    float sphere = length(p1) - 1.0f;
    float cube = sdf_box(p1 - vec3{0, 0, 0}, {0.75f, 0.75f, 0.75f}) - 0.05f;
    float main_shape = op_smooth_union(sphere, cube, 0.3f);

    // Orbiting smaller sphere
    float orbit_r = 2.0f;
    vec3 orbit_pos{orbit_r * std::cos(time * 1.5f), std::sin(time * 0.7f) * 0.5f,
                   orbit_r * std::sin(time * 1.5f)};
    float small_sphere = length(pos - orbit_pos) - 0.3f;

    // Floor
    float floor_dist = pos.y + 1.5f;

    float scene = std::min(main_shape, floor_dist);
    scene = std::min(scene, small_sphere);
    return scene;
}

namespace {

struct RenderParams {
    SdfFn scene;
    float time;
    vec3 eye, fwd, right, cam_up;
    float w, h, aspect;
    vec3 light_dir, light_col, sky_col;
    int max_steps;
};

void render_rows(PixelBuffer& pb, const RenderParams& rp,
                 uint32_t y_start, uint32_t y_end) {
    for (uint32_t py = y_start; py < y_end; ++py) {
        for (uint32_t px = 0; px < pb.width; ++px) {
            float u = (2.0f * (static_cast<float>(px) + 0.5f) / rp.w - 1.0f) * rp.aspect;
            float v = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / rp.h;

            vec3 rd = normalize(rp.fwd + rp.right * u + rp.cam_up * v);

            float t_ray = 0.0f;
            bool hit = false;
            for (int i = 0; i < rp.max_steps; ++i) {
                vec3 p = rp.eye + rd * t_ray;
                float d = rp.scene(p, rp.time);
                if (d < 0.001f) { hit = true; break; }
                if (t_ray > 50.0f) break;
                t_ray += d;
            }

            uint8_t r, g, b;
            if (hit) {
                vec3 p = rp.eye + rd * t_ray;
                vec3 n = calc_normal(rp.scene, p, rp.time);

                float diff = std::max(dot(n, rp.light_dir), 0.0f);

                float shadow = 1.0f;
                {
                    float st = 0.02f;
                    for (int i = 0; i < 32; ++i) {
                        float sd = rp.scene(p + rp.light_dir * st, rp.time);
                        if (sd < 0.001f) { shadow = 0.3f; break; }
                        st += sd;
                        if (st > 10.0f) break;
                    }
                }

                float ao = calc_ao(rp.scene, p, n, rp.time);

                vec3 mat{
                    std::abs(n.x) * 0.4f + 0.3f,
                    std::abs(n.y) * 0.4f + 0.3f,
                    std::abs(n.z) * 0.4f + 0.3f,
                };

                if (n.y > 0.9f) {
                    float check = std::fmod(std::floor(p.x) + std::floor(p.z), 2.0f);
                    if (check < 0.0f) check += 2.0f;
                    mat = check < 1.0f ? vec3{0.4f, 0.4f, 0.45f} : vec3{0.6f, 0.6f, 0.65f};
                }

                vec3 col = mat * (rp.light_col * diff * shadow + rp.sky_col * 0.15f) * ao;
                float fog = std::exp(-t_ray * 0.04f);
                col = col * fog + rp.sky_col * (1.0f - fog);

                r = static_cast<uint8_t>(std::clamp(col.x * 255.0f, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(col.y * 255.0f, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(col.z * 255.0f, 0.0f, 255.0f));
            } else {
                float sky_t = 0.5f * (rd.y + 1.0f);
                vec3 sky = rp.sky_col * sky_t + vec3{0.15f, 0.1f, 0.2f} * (1.0f - sky_t);
                r = static_cast<uint8_t>(std::clamp(sky.x * 255.0f, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(sky.y * 255.0f, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(sky.z * 255.0f, 0.0f, 255.0f));
            }

            // Write directly to pixel array (no dirty tracking needed — we mark all dirty)
            uint32_t idx = (py * pb.width + px) * 3;
            pb.pixels[idx + 0] = r;
            pb.pixels[idx + 1] = g;
            pb.pixels[idx + 2] = b;
        }
    }
}

RenderParams make_params(PixelBuffer& pb, SdfFn scene, float time,
                         vec3 eye, vec3 center, vec3 up, int max_steps) {
    vec3 fwd = normalize(center - eye);
    vec3 right_v = normalize(cross(fwd, up));
    vec3 cam_up = cross(right_v, fwd);
    auto w = static_cast<float>(pb.width);
    auto h = static_cast<float>(pb.height);
    return {scene, time, eye, fwd, right_v, cam_up, w, h, w / h,
            normalize({-0.5f, 0.8f, 0.6f}), {1.0f, 0.95f, 0.9f}, {0.4f, 0.5f, 0.7f},
            max_steps};
}

} // namespace

void sdf_render(PixelBuffer& pb, SdfFn scene, float time,
                vec3 eye, vec3 center, vec3 up) {
    RenderParams rp = make_params(pb, scene, time, eye, center, up, 80);
    render_rows(pb, rp, 0, pb.height);
    pb.fb->mark_all_dirty();
}

void sdf_render_parallel(PixelBuffer& pb, SdfFn scene, float time,
                         vec3 eye, vec3 center, vec3 up,
                         ThreadPool& pool, int max_steps) {
    RenderParams rp = make_params(pb, scene, time, eye, center, up, max_steps);
    pool.parallel_for([&](uint32_t id, uint32_t n) {
        uint32_t y_start = (pb.height * id) / n;
        uint32_t y_end = (pb.height * (id + 1)) / n;
        render_rows(pb, rp, y_start, y_end);
    });
    pb.fb->mark_all_dirty();
}

} // namespace cliviz
