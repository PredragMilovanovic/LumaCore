#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "lumacore/lighting/illuminance.hpp"
#include "lumacore/math/constants.hpp"
#include "test_helpers.hpp"

using lc::PointLight;
using lc::Vec3;

namespace {
const Vec3 kUp{0.0, 0.0, 1.0};
}  // namespace

TEST(PointLight, RejectsInvalidInput) {
    EXPECT_THROW(PointLight(Vec3{}, -1.0), std::invalid_argument);
    EXPECT_THROW(PointLight(Vec3{}, std::nan("")), std::invalid_argument);
    EXPECT_THROW(PointLight(Vec3{}, std::numeric_limits<double>::infinity()),
                 std::invalid_argument);
    EXPECT_THROW(PointLight(Vec3{std::nan(""), 0, 0}, 100.0), std::invalid_argument);
    EXPECT_NO_THROW(PointLight(Vec3{}, 0.0));  // switched-off luminaire is valid
}

TEST(PointIlluminance, DirectlyBelowIsInverseSquare) {
    // E = I / h^2: 1000 cd at 2 m -> 250 lx.
    const PointLight light(Vec3{0, 0, 2}, 1000.0);
    EXPECT_NEAR(lc::point_illuminance(light, Vec3{0, 0, 0}, kUp), 250.0, 1e-12);
    // Doubling the distance quarters the illuminance.
    const PointLight higher(Vec3{0, 0, 4}, 1000.0);
    EXPECT_NEAR(lc::point_illuminance(higher, Vec3{0, 0, 0}, kUp), 62.5, 1e-12);
}

TEST(PointIlluminance, HorizontalOffsetMatchesClosedForm_Randomized) {
    // Horizontal plane: cos(theta) = h / r, so E = I h / (h^2 + d^2)^(3/2).
    lc::test::Random rng;
    for (int k = 0; k < 1000; ++k) {
        const double intensity = rng.uniform(1.0, 5000.0);
        const double h = rng.uniform(0.1, 10.0);
        const double dx = rng.uniform(-10.0, 10.0);
        const double dy = rng.uniform(-10.0, 10.0);
        const PointLight light(Vec3{dx, dy, h}, intensity);
        const double d2 = dx * dx + dy * dy;
        const double expected = intensity * h / std::pow(h * h + d2, 1.5);
        EXPECT_NEAR(lc::point_illuminance(light, Vec3{0, 0, 0}, kUp), expected, 1e-12 * expected);
    }
}

TEST(PointIlluminance, TiltedSurfaceScalesWithCosine) {
    const PointLight light(Vec3{0, 0, 2}, 1000.0);
    for (const double deg : {0.0, 30.0, 45.0, 60.0, 89.0}) {
        const double a = deg * lc::kPi / 180.0;
        const Vec3 n{std::sin(a), 0.0, std::cos(a)};
        EXPECT_NEAR(lc::point_illuminance(light, Vec3{0, 0, 0}, n), 250.0 * std::cos(a), 1e-12)
            << deg << " degrees";
    }
}

TEST(PointIlluminance, ZeroWhenLightIsBehindOrLevelWithSurface) {
    const PointLight below(Vec3{0, 0, -2}, 1000.0);
    EXPECT_EQ(lc::point_illuminance(below, Vec3{0, 0, 0}, kUp), 0.0);
    const PointLight level(Vec3{3, 0, 0}, 1000.0);  // grazing: cos(theta) = 0
    EXPECT_EQ(lc::point_illuminance(level, Vec3{0, 0, 0}, kUp), 0.0);
}

TEST(PointIlluminance, ZeroInsideMinimumDistanceAndForDarkSource) {
    const PointLight near(Vec3{0, 0, 0.5 * lc::kMinLightDistance}, 1000.0);
    EXPECT_EQ(lc::point_illuminance(near, Vec3{0, 0, 0}, kUp), 0.0);
    const PointLight off(Vec3{0, 0, 2}, 0.0);
    EXPECT_EQ(lc::point_illuminance(off, Vec3{0, 0, 0}, kUp), 0.0);
}

TEST(PointIlluminance, LinearInIntensity) {
    const Vec3 p{0.3, -0.7, 0.0};
    const double e1 = lc::point_illuminance(PointLight(Vec3{1, 2, 3}, 100.0), p, kUp);
    const double e3 = lc::point_illuminance(PointLight(Vec3{1, 2, 3}, 300.0), p, kUp);
    EXPECT_NEAR(e3, 3.0 * e1, 1e-12 * e3);
}
