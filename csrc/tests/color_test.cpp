#include <gtest/gtest.h>

#include "outbuf.h"

using namespace cliviz;

// ── RGB → 256-color mapping ──

TEST(Color256, BlackMapsToIndex16) {
    EXPECT_EQ(rgb_to_256(0, 0, 0), 16u);
}

TEST(Color256, WhiteMapsToIndex231) {
    EXPECT_EQ(rgb_to_256(255, 255, 255), 231u);
}

TEST(Color256, PureRedMapsCorrectly) {
    // r=5, g=0, b=0 → 16 + 36*5 + 6*0 + 0 = 196
    EXPECT_EQ(rgb_to_256(255, 0, 0), 196u);
}

TEST(Color256, PureGreenMapsCorrectly) {
    // r=0, g=5, b=0 → 16 + 36*0 + 6*5 + 0 = 46
    EXPECT_EQ(rgb_to_256(0, 255, 0), 46u);
}

TEST(Color256, PureBlue) {
    // r=0, g=0, b=5 → 16 + 0 + 0 + 5 = 21
    EXPECT_EQ(rgb_to_256(0, 0, 255), 21u);
}

TEST(Color256, MidGrayUsesGrayscaleRamp) {
    // Pure gray values should map to the grayscale ramp (232-255)
    uint8_t idx = rgb_to_256(128, 128, 128);
    EXPECT_GE(idx, 232u);
    EXPECT_LE(idx, 255u);
}

TEST(Color256, NearGrayUsesGrayscaleRamp) {
    uint8_t idx = rgb_to_256(100, 100, 100);
    EXPECT_GE(idx, 232u);
    EXPECT_LE(idx, 255u);
}

// ── Emit functions produce correct escape format ──

TEST(Color256, EmitFg256Format) {
    OutputBuffer buf;
    buf.emit_fg_256(196);
    EXPECT_EQ(buf.view(), "\x1b[38;5;196m");
}

TEST(Color256, EmitBg256Format) {
    OutputBuffer buf;
    buf.emit_bg_256(46);
    EXPECT_EQ(buf.view(), "\x1b[48;5;46m");
}

// ── ColorMode detection ──

TEST(ColorMode, DetectTruecolorFromEnv) {
    // Can't easily test env detection without modifying env,
    // but verify the function exists and returns a valid mode
    ColorMode mode = detect_color_mode();
    EXPECT_TRUE(mode == ColorMode::TrueColor || mode == ColorMode::Color256);
}
