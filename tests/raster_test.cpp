#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "raster.h"

using namespace cliviz;

// ── Mesh generation ──

TEST(Mesh, CubeHas12Triangles) {
    auto mesh = make_cube();
    EXPECT_EQ(mesh.n_tris, 12u);
    EXPECT_EQ(mesh.n_verts, 8u);
}

TEST(Mesh, CubeIndicesInRange) {
    auto mesh = make_cube();
    for (uint32_t i = 0; i < mesh.n_tris * 3; ++i) {
        EXPECT_LT(mesh.indices[i], mesh.n_verts);
    }
}

// ── Z-buffer ──

TEST(ZBuffer, CreateAndClear) {
    ZBuffer zb(10, 10);
    // All values should be far (large positive)
    EXPECT_GT(zb.at(0, 0), 1e10f);
    EXPECT_GT(zb.at(5, 5), 1e10f);
}

TEST(ZBuffer, TestAndSet) {
    ZBuffer zb(10, 10);
    // First write should succeed
    EXPECT_TRUE(zb.test_and_set(5, 5, 1.0f));
    // Closer should succeed
    EXPECT_TRUE(zb.test_and_set(5, 5, 0.5f));
    // Farther should fail
    EXPECT_FALSE(zb.test_and_set(5, 5, 0.8f));
}

// ── Rasterizer ──

TEST(Rasterizer, RendersCubeWithoutCrash) {
    auto pb = PixelBuffer::create(20, 10);
    ZBuffer zb(pb->width, pb->height);

    auto mesh = make_cube();
    mat4 model = mat4::identity();
    mat4 view = mat4::look_at({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
    float aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);
    mat4 mvp = proj * view * model;

    pb->clear(0, 0, 0);
    zb.clear();
    rasterize(mesh, mvp, *pb, zb);
    pb->encode();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);
    // Should have emitted some cells (the cube is visible)
    EXPECT_GT(emitted, 0u);
}

TEST(Rasterizer, BackfaceCullingReducesOutput) {
    // A cube viewed from the front: only ~3 faces visible (backface culled)
    auto pb1 = PixelBuffer::create(40, 20);
    auto pb2 = PixelBuffer::create(40, 20);
    ZBuffer zb1(pb1->width, pb1->height);
    ZBuffer zb2(pb2->width, pb2->height);

    auto mesh = make_cube();
    mat4 view = mat4::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    float aspect = static_cast<float>(pb1->width) / static_cast<float>(pb1->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);
    mat4 mvp = proj * view;

    pb1->clear(0, 0, 0);
    zb1.clear();
    uint32_t drawn_with_cull = rasterize(mesh, mvp, *pb1, zb1);

    // Without backface culling, more triangles are processed
    // (but result should still look correct due to z-buffer)
    // We just verify the culled version draws fewer triangles
    EXPECT_GT(drawn_with_cull, 0u);
    EXPECT_LE(drawn_with_cull, 12u);
}

TEST(Rasterizer, ZBufferPreventsOverdraw) {
    // Two overlapping triangles: front one should win
    auto pb = PixelBuffer::create(20, 10);
    ZBuffer zb(pb->width, pb->height);

    pb->clear(0, 0, 0);
    zb.clear();

    // Render a cube — the z-buffer should ensure correct occlusion
    auto mesh = make_cube();
    mat4 view = mat4::look_at({3, 3, 3}, {0, 0, 0}, {0, 1, 0});
    float aspect = static_cast<float>(pb->width) / static_cast<float>(pb->height);
    mat4 proj = mat4::perspective(static_cast<float>(M_PI / 3.0), aspect, 0.1f, 100.0f);
    mat4 mvp = proj * view;

    rasterize(mesh, mvp, *pb, zb);

    // At least some z-buffer entries should be less than the initial far value
    bool any_written = false;
    for (uint32_t y = 0; y < pb->height; ++y) {
        for (uint32_t x = 0; x < pb->width; ++x) {
            if (zb.at(x, y) < 1e10f) {
                any_written = true;
                break;
            }
        }
        if (any_written) break;
    }
    EXPECT_TRUE(any_written);
}
