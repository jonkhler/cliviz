#include <gtest/gtest.h>

#include <cmath>

#include "math3d.h"

using namespace cliviz;

constexpr float EPS = 1e-5f;

// ── vec3 ──

TEST(Vec3, Add) {
    vec3 a{1, 2, 3};
    vec3 b{4, 5, 6};
    auto c = a + b;
    EXPECT_NEAR(c.x, 5, EPS);
    EXPECT_NEAR(c.y, 7, EPS);
    EXPECT_NEAR(c.z, 9, EPS);
}

TEST(Vec3, Sub) {
    vec3 a{5, 3, 1};
    vec3 b{1, 2, 3};
    auto c = a - b;
    EXPECT_NEAR(c.x, 4, EPS);
    EXPECT_NEAR(c.y, 1, EPS);
    EXPECT_NEAR(c.z, -2, EPS);
}

TEST(Vec3, ScalarMul) {
    vec3 a{1, 2, 3};
    auto c = a * 2.0f;
    EXPECT_NEAR(c.x, 2, EPS);
    EXPECT_NEAR(c.y, 4, EPS);
    EXPECT_NEAR(c.z, 6, EPS);
}

TEST(Vec3, Dot) {
    vec3 a{1, 0, 0};
    vec3 b{0, 1, 0};
    EXPECT_NEAR(dot(a, b), 0, EPS);

    EXPECT_NEAR(dot(a, a), 1, EPS);
}

TEST(Vec3, Cross) {
    vec3 x{1, 0, 0};
    vec3 y{0, 1, 0};
    auto z = cross(x, y);
    EXPECT_NEAR(z.x, 0, EPS);
    EXPECT_NEAR(z.y, 0, EPS);
    EXPECT_NEAR(z.z, 1, EPS);
}

TEST(Vec3, Normalize) {
    vec3 a{3, 4, 0};
    auto n = normalize(a);
    EXPECT_NEAR(n.x, 0.6f, EPS);
    EXPECT_NEAR(n.y, 0.8f, EPS);
    EXPECT_NEAR(n.z, 0, EPS);
}

TEST(Vec3, Length) {
    vec3 a{3, 4, 0};
    EXPECT_NEAR(length(a), 5.0f, EPS);
}

// ── mat4 ──

TEST(Mat4, IdentityTimesVec) {
    mat4 m = mat4::identity();
    vec4 v{1, 2, 3, 1};
    auto r = m * v;
    EXPECT_NEAR(r.x, 1, EPS);
    EXPECT_NEAR(r.y, 2, EPS);
    EXPECT_NEAR(r.z, 3, EPS);
    EXPECT_NEAR(r.w, 1, EPS);
}

TEST(Mat4, Translation) {
    mat4 m = mat4::translate(10, 20, 30);
    vec4 v{1, 2, 3, 1};
    auto r = m * v;
    EXPECT_NEAR(r.x, 11, EPS);
    EXPECT_NEAR(r.y, 22, EPS);
    EXPECT_NEAR(r.z, 33, EPS);
}

TEST(Mat4, TranslationDoesNotAffectDirections) {
    mat4 m = mat4::translate(10, 20, 30);
    vec4 v{1, 0, 0, 0}; // direction, w=0
    auto r = m * v;
    EXPECT_NEAR(r.x, 1, EPS);
    EXPECT_NEAR(r.y, 0, EPS);
    EXPECT_NEAR(r.z, 0, EPS);
}

TEST(Mat4, RotateY_90Deg) {
    mat4 m = mat4::rotate_y(static_cast<float>(M_PI / 2.0));
    vec4 v{1, 0, 0, 1};
    auto r = m * v;
    EXPECT_NEAR(r.x, 0, EPS);
    EXPECT_NEAR(r.y, 0, EPS);
    EXPECT_NEAR(r.z, -1, EPS);
}

TEST(Mat4, Multiply) {
    mat4 a = mat4::translate(1, 0, 0);
    mat4 b = mat4::translate(0, 2, 0);
    mat4 c = a * b;
    vec4 v{0, 0, 0, 1};
    auto r = c * v;
    EXPECT_NEAR(r.x, 1, EPS);
    EXPECT_NEAR(r.y, 2, EPS);
    EXPECT_NEAR(r.z, 0, EPS);
}

TEST(Mat4, Perspective) {
    // A point at (0,0,-1) in front of the camera with near=0.1, far=100
    // should map to NDC z somewhere in [-1, 1]
    float fov = static_cast<float>(M_PI / 2.0); // 90 degrees
    mat4 p = mat4::perspective(fov, 1.0f, 0.1f, 100.0f);
    vec4 v{0, 0, -1, 1};
    auto r = p * v;
    // After perspective divide
    float ndc_z = r.z / r.w;
    EXPECT_GT(ndc_z, -1.0f);
    EXPECT_LT(ndc_z, 1.0f);
}

TEST(Mat4, LookAt) {
    // Camera at (0,0,5) looking at origin
    mat4 view = mat4::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    vec4 origin{0, 0, 0, 1};
    auto r = view * origin;
    // Origin should be at z=-5 in view space
    EXPECT_NEAR(r.x, 0, EPS);
    EXPECT_NEAR(r.y, 0, EPS);
    EXPECT_NEAR(r.z, -5, EPS);
}
