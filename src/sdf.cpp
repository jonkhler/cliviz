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

void sdf_render(PixelBuffer& pb, SdfFn scene, float time,
                vec3 eye, vec3 center, vec3 up) {
    vec3 fwd = normalize(center - eye);
    vec3 right = normalize(cross(fwd, up));
    vec3 cam_up = cross(right, fwd);

    auto w = static_cast<float>(pb.width);
    auto h = static_cast<float>(pb.height);
    float aspect = w / h;

    // Light direction
    vec3 light_dir = normalize({-0.5f, 0.8f, 0.6f});
    vec3 light_col{1.0f, 0.95f, 0.9f};
    vec3 sky_col{0.4f, 0.5f, 0.7f};

    for (uint32_t py = 0; py < pb.height; ++py) {
        for (uint32_t px = 0; px < pb.width; ++px) {
            // NDC coords
            float u = (2.0f * (static_cast<float>(px) + 0.5f) / w - 1.0f) * aspect;
            float v = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / h;

            vec3 rd = normalize(fwd + right * u + cam_up * v);

            // Raymarch
            float t_ray = 0.0f;
            bool hit = false;
            for (int i = 0; i < 80; ++i) {
                vec3 p = eye + rd * t_ray;
                float d = scene(p, time);
                if (d < 0.001f) { hit = true; break; }
                if (t_ray > 50.0f) break;
                t_ray += d;
            }

            uint8_t r, g, b;
            if (hit) {
                vec3 p = eye + rd * t_ray;
                vec3 n = calc_normal(scene, p, time);

                // Diffuse lighting
                float diff = std::max(dot(n, light_dir), 0.0f);

                // Shadow ray
                float shadow = 1.0f;
                {
                    float st = 0.02f;
                    for (int i = 0; i < 32; ++i) {
                        float sd = scene(p + light_dir * st, time);
                        if (sd < 0.001f) { shadow = 0.3f; break; }
                        st += sd;
                        if (st > 10.0f) break;
                    }
                }

                float ao = calc_ao(scene, p, n, time);

                // Material color based on normal
                vec3 mat{
                    std::abs(n.x) * 0.4f + 0.3f,
                    std::abs(n.y) * 0.4f + 0.3f,
                    std::abs(n.z) * 0.4f + 0.3f,
                };

                // Floor gets checkerboard
                if (n.y > 0.9f) {
                    float check = std::fmod(std::floor(p.x) + std::floor(p.z), 2.0f);
                    if (check < 0.0f) check += 2.0f;
                    mat = check < 1.0f ? vec3{0.4f, 0.4f, 0.45f} : vec3{0.6f, 0.6f, 0.65f};
                }

                // Combine
                vec3 col = mat * (light_col * diff * shadow + sky_col * 0.15f) * ao;

                // Fog
                float fog = std::exp(-t_ray * 0.04f);
                col = col * fog + sky_col * (1.0f - fog);

                r = static_cast<uint8_t>(std::clamp(col.x * 255.0f, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(col.y * 255.0f, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(col.z * 255.0f, 0.0f, 255.0f));
            } else {
                // Sky gradient
                float sky_t = 0.5f * (rd.y + 1.0f);
                vec3 sky = sky_col * sky_t + vec3{0.15f, 0.1f, 0.2f} * (1.0f - sky_t);
                r = static_cast<uint8_t>(std::clamp(sky.x * 255.0f, 0.0f, 255.0f));
                g = static_cast<uint8_t>(std::clamp(sky.y * 255.0f, 0.0f, 255.0f));
                b = static_cast<uint8_t>(std::clamp(sky.z * 255.0f, 0.0f, 255.0f));
            }
            pb.set(px, py, r, g, b);
        }
    }
}

} // namespace cliviz
