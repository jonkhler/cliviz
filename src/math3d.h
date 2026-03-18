#pragma once

#include <cmath>
#include <cstdint>

namespace cliviz {

struct vec3 {
    float x, y, z;

    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
};

struct vec4 {
    float x, y, z, w;
};

inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(vec3 v) { float l = length(v); return v * (1.0f / l); }

inline vec3 cross(vec3 a, vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// Column-major 4×4 matrix. m[col][row].
struct mat4 {
    float m[4][4]{};

    static mat4 identity() {
        mat4 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    static mat4 translate(float tx, float ty, float tz) {
        mat4 r = identity();
        r.m[3][0] = tx;
        r.m[3][1] = ty;
        r.m[3][2] = tz;
        return r;
    }

    static mat4 rotate_x(float rad) {
        mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c;  r.m[2][1] = -s;
        r.m[1][2] = s;  r.m[2][2] = c;
        return r;
    }

    static mat4 rotate_y(float rad) {
        mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;  r.m[2][0] = s;
        r.m[0][2] = -s; r.m[2][2] = c;
        return r;
    }

    static mat4 rotate_z(float rad) {
        mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;  r.m[1][0] = -s;
        r.m[0][1] = s;  r.m[1][1] = c;
        return r;
    }

    // Standard OpenGL-style perspective projection.
    static mat4 perspective(float fov_y, float aspect, float near, float far) {
        float t = std::tan(fov_y * 0.5f);
        mat4 r{};
        r.m[0][0] = 1.0f / (aspect * t);
        r.m[1][1] = 1.0f / t;
        r.m[2][2] = -(far + near) / (far - near);
        r.m[2][3] = -1.0f;
        r.m[3][2] = -(2.0f * far * near) / (far - near);
        return r;
    }

    static mat4 look_at(vec3 eye, vec3 center, vec3 up) {
        vec3 f = normalize(center - eye);
        vec3 s = normalize(cross(f, up));
        vec3 u = cross(s, f);

        mat4 r = identity();
        r.m[0][0] = s.x;  r.m[1][0] = s.y;  r.m[2][0] = s.z;
        r.m[0][1] = u.x;  r.m[1][1] = u.y;  r.m[2][1] = u.z;
        r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
        r.m[3][0] = -dot(s, eye);
        r.m[3][1] = -dot(u, eye);
        r.m[3][2] = dot(f, eye);
        return r;
    }

    vec4 operator*(const vec4& v) const {
        return {
            m[0][0]*v.x + m[1][0]*v.y + m[2][0]*v.z + m[3][0]*v.w,
            m[0][1]*v.x + m[1][1]*v.y + m[2][1]*v.z + m[3][1]*v.w,
            m[0][2]*v.x + m[1][2]*v.y + m[2][2]*v.z + m[3][2]*v.w,
            m[0][3]*v.x + m[1][3]*v.y + m[2][3]*v.z + m[3][3]*v.w,
        };
    }

    mat4 operator*(const mat4& o) const {
        mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                r.m[c][row] =
                    m[0][row]*o.m[c][0] +
                    m[1][row]*o.m[c][1] +
                    m[2][row]*o.m[c][2] +
                    m[3][row]*o.m[c][3];
            }
        }
        return r;
    }
};

} // namespace cliviz
