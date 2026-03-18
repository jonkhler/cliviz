#include <gtest/gtest.h>

#include "cliviz.h"

TEST(CAPI, CreateAndDestroyPixbuf) {
    auto* pb = cliviz_pixbuf_create(10, 5);
    ASSERT_NE(pb, nullptr);
    EXPECT_EQ(cliviz_pixbuf_width(pb), 10u);
    EXPECT_EQ(cliviz_pixbuf_height(pb), 10u); // 5 rows × 2
    cliviz_pixbuf_destroy(pb);
}

TEST(CAPI, PixelAccessAndClear) {
    auto* pb = cliviz_pixbuf_create(4, 2);
    ASSERT_NE(pb, nullptr);

    cliviz_pixbuf_clear(pb, 128, 64, 32);

    uint8_t* pixels = cliviz_pixbuf_pixels(pb);
    EXPECT_EQ(pixels[0], 128u);
    EXPECT_EQ(pixels[1], 64u);
    EXPECT_EQ(pixels[2], 32u);

    cliviz_pixbuf_destroy(pb);
}

TEST(CAPI, SetPixel) {
    auto* pb = cliviz_pixbuf_create(10, 5);
    cliviz_pixbuf_set(pb, 3, 4, 255, 0, 0);

    uint8_t* pixels = cliviz_pixbuf_pixels(pb);
    uint32_t idx = (4 * 10 + 3) * 3;
    EXPECT_EQ(pixels[idx], 255u);
    EXPECT_EQ(pixels[idx + 1], 0u);
    EXPECT_EQ(pixels[idx + 2], 0u);

    cliviz_pixbuf_destroy(pb);
}

TEST(CAPI, FillRect) {
    auto* pb = cliviz_pixbuf_create(10, 5);
    cliviz_pixbuf_fill_rect(pb, 2, 2, 5, 6, 200, 100, 50);

    uint8_t* pixels = cliviz_pixbuf_pixels(pb);
    uint32_t idx_in = (3 * 10 + 3) * 3;
    EXPECT_EQ(pixels[idx_in], 200u);

    uint32_t idx_out = 0;
    EXPECT_EQ(pixels[idx_out], 0u);

    cliviz_pixbuf_destroy(pb);
}

static float test_sdf_sphere(float x, float y, float z, float /*time*/) {
    return std::sqrt(x * x + y * y + z * z) - 1.0f;
}

TEST(CAPI, SdfDefaultSceneCallable) {
    float d = cliviz_sdf_default_scene(0, 0, 0, 0);
    EXPECT_LT(d, 0.0f); // inside the scene
}

TEST(CAPI, SdfRenderWithCustomScene) {
    auto* pb = cliviz_pixbuf_create(8, 4);
    cliviz_pixbuf_clear(pb, 0, 0, 0);

    cliviz_sdf_render(pb, test_sdf_sphere, 0.0f,
                      0, 0, 3, 0, 0, 0, 40);

    // Some pixels should be non-black
    uint8_t* pixels = cliviz_pixbuf_pixels(pb);
    uint32_t w = cliviz_pixbuf_width(pb);
    uint32_t h = cliviz_pixbuf_height(pb);
    bool any_color = false;
    for (uint32_t i = 0; i < w * h * 3; i += 3) {
        if (pixels[i] > 0 || pixels[i + 1] > 0 || pixels[i + 2] > 0) {
            any_color = true;
            break;
        }
    }
    EXPECT_TRUE(any_color);

    cliviz_pixbuf_destroy(pb);
}
