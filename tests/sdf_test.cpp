#include <gtest/gtest.h>

#include <cmath>

#include "sdf.h"

using namespace cliviz;

TEST(SDF, SphereAtOriginIsNegative) {
    // Inside the sphere, distance should be negative
    EXPECT_LT(sdf_sphere({0, 0, 0}, 0), 0.0f);
}

TEST(SDF, SphereOnSurface) {
    // On the surface (radius 1), distance should be ~0
    EXPECT_NEAR(sdf_sphere({1, 0, 0}, 0), 0.0f, 0.01f);
}

TEST(SDF, SphereOutside) {
    EXPECT_GT(sdf_sphere({2, 0, 0}, 0), 0.0f);
}

TEST(SDF, RoundedCubeAtOriginIsNegative) {
    EXPECT_LT(sdf_rounded_cube({0, 0, 0}, 0), 0.0f);
}

TEST(SDF, DefaultSceneAtOriginIsNegative) {
    EXPECT_LT(sdf_scene_default({0, 0, 0}, 0), 0.0f);
}

TEST(SDF, DefaultSceneFarAwayIsPositive) {
    EXPECT_GT(sdf_scene_default({100, 100, 100}, 0), 0.0f);
}

TEST(SDF, RenderProducesPixels) {
    auto pb = PixelBuffer::create(10, 5);
    sdf_render(*pb, sdf_scene_default, 0.0f,
               {0, 2, 5}, {0, 0, 0}, {0, 1, 0});

    // At least some pixels should be non-black
    bool any_color = false;
    for (uint32_t y = 0; y < pb->height && !any_color; ++y) {
        for (uint32_t x = 0; x < pb->width; ++x) {
            uint32_t idx = (y * pb->width + x) * 3;
            if (pb->pixels[idx] > 0 || pb->pixels[idx + 1] > 0 || pb->pixels[idx + 2] > 0) {
                any_color = true;
                break;
            }
        }
    }
    EXPECT_TRUE(any_color);
}

TEST(SDF, RenderFullPipeline) {
    auto pb = PixelBuffer::create(8, 4);
    sdf_render(*pb, sdf_sphere, 0.0f,
               {0, 0, 3}, {0, 0, 0}, {0, 1, 0});
    pb->encode();

    OutputBuffer buf;
    uint32_t emitted = pb->fb->flush(buf);
    EXPECT_GT(emitted, 0u);
}
