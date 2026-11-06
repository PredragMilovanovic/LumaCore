#include <gtest/gtest.h>

#include <limits>

#include "lumacore/math/vec3.hpp"
#include "test_helpers.hpp"

using lc::Vec3;

namespace {

constexpr Vec3 kX{1.0, 0.0, 0.0};
constexpr Vec3 kY{0.0, 1.0, 0.0};
constexpr Vec3 kZ{0.0, 0.0, 1.0};

// Compile-time checks: the core operations are usable in constant expressions.
static_assert(lc::dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == 32.0);
static_assert(lc::cross(kX, kY) == kZ);
static_assert(Vec3{1, 2, 3} + Vec3{1, 1, 1} == Vec3{2, 3, 4});

}  // namespace

TEST(Vec3, DefaultIsZero) { EXPECT_EQ(Vec3{}, (Vec3{0.0, 0.0, 0.0})); }

TEST(Vec3, AdditionAndSubtraction) {
    const Vec3 a{1.0, -2.0, 3.5};
    const Vec3 b{0.5, 4.0, -1.0};
    EXPECT_EQ(a + b, (Vec3{1.5, 2.0, 2.5}));
    EXPECT_EQ(a - b, (Vec3{0.5, -6.0, 4.5}));
    EXPECT_EQ(-a, (Vec3{-1.0, 2.0, -3.5}));
}

TEST(Vec3, ScalarMultiplicationAndDivision) {
    const Vec3 v{1.0, -2.0, 4.0};
    EXPECT_EQ(v * 2.0, (Vec3{2.0, -4.0, 8.0}));
    EXPECT_EQ(2.0 * v, v * 2.0);
    EXPECT_EQ(v / 2.0, (Vec3{0.5, -1.0, 2.0}));
}

TEST(Vec3, CompoundAssignment) {
    Vec3 v{1.0, 1.0, 1.0};
    v += Vec3{1.0, 2.0, 3.0};
    EXPECT_EQ(v, (Vec3{2.0, 3.0, 4.0}));
    v -= Vec3{1.0, 1.0, 1.0};
    EXPECT_EQ(v, (Vec3{1.0, 2.0, 3.0}));
    v *= 3.0;
    EXPECT_EQ(v, (Vec3{3.0, 6.0, 9.0}));
}

TEST(Vec3, DotProductKnownValues) {
    EXPECT_DOUBLE_EQ(lc::dot(Vec3{1, 2, 3}, Vec3{4, -5, 6}), 12.0);
    EXPECT_DOUBLE_EQ(lc::dot(kX, kY), 0.0);
    EXPECT_DOUBLE_EQ(lc::dot(kZ, kZ), 1.0);
}

TEST(Vec3, CrossProductOfBasisIsRightHanded) {
    EXPECT_EQ(lc::cross(kX, kY), kZ);
    EXPECT_EQ(lc::cross(kY, kZ), kX);
    EXPECT_EQ(lc::cross(kZ, kX), kY);
    EXPECT_EQ(lc::cross(kY, kX), -kZ);
}

TEST(Vec3, CrossProductOfParallelVectorsIsZero) {
    const Vec3 v{1.5, -2.0, 0.25};
    EXPECT_EQ(lc::cross(v, v * 3.0), Vec3{});
}

TEST(Vec3, CrossProductProperties_Randomized) {
    lc::test::Random rng;
    for (int i = 0; i < 1000; ++i) {
        const Vec3 a = rng.vec(-10.0, 10.0);
        const Vec3 b = rng.vec(-10.0, 10.0);
        const Vec3 c = lc::cross(a, b);
        const double scale = lc::length(a) * lc::length(b);
        // Perpendicular to both operands.
        EXPECT_NEAR(lc::dot(c, a), 0.0, 1e-12 * scale * lc::length(a));
        EXPECT_NEAR(lc::dot(c, b), 0.0, 1e-12 * scale * lc::length(b));
        // Anti-commutative.
        EXPECT_EQ(lc::cross(b, a), -c);
        // Lagrange identity: |a x b|^2 = |a|^2 |b|^2 - (a.b)^2.
        const double ab = lc::dot(a, b);
        EXPECT_NEAR(lc::length_squared(c),
                    lc::length_squared(a) * lc::length_squared(b) - ab * ab, 1e-10 * scale * scale);
    }
}

TEST(Vec3, LengthAndDistance) {
    EXPECT_DOUBLE_EQ(lc::length(Vec3{3.0, 4.0, 0.0}), 5.0);
    EXPECT_DOUBLE_EQ(lc::length_squared(Vec3{1.0, 2.0, 2.0}), 9.0);
    EXPECT_DOUBLE_EQ(lc::distance(Vec3{1, 1, 1}, Vec3{1, 4, 5}), 5.0);
}

TEST(Vec3, NormalizedHasUnitLengthAndSameDirection_Randomized) {
    lc::test::Random rng;
    for (int i = 0; i < 1000; ++i) {
        const Vec3 v = rng.vec(-100.0, 100.0);
        const auto n = lc::normalized(v);
        ASSERT_TRUE(n.has_value());
        EXPECT_NEAR(lc::length(*n), 1.0, 1e-14);
        EXPECT_NEAR(lc::dot(*n, v), lc::length(v), 1e-12 * lc::length(v));
    }
}

TEST(Vec3, NormalizeRejectsZeroTinyAndNonFinite) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(lc::normalized(Vec3{}).has_value());
    EXPECT_FALSE(lc::normalized(Vec3{1e-14, 0.0, 0.0}).has_value());
    EXPECT_FALSE(lc::normalized(Vec3{inf, 0.0, 0.0}).has_value());
    EXPECT_FALSE(lc::normalized(Vec3{nan, 1.0, 0.0}).has_value());
}

TEST(Vec3, IsFinite) {
    EXPECT_TRUE(lc::is_finite(Vec3{1.0, -2.0, 1e300}));
    EXPECT_FALSE(lc::is_finite(Vec3{0.0, std::numeric_limits<double>::infinity(), 0.0}));
    EXPECT_FALSE(lc::is_finite(Vec3{0.0, 0.0, std::numeric_limits<double>::quiet_NaN()}));
}

TEST(Vec3, ApproxEqualUsesAbsoluteTolerance) {
    EXPECT_TRUE(lc::approx_equal(Vec3{1.0, 2.0, 3.0}, Vec3{1.0 + 1e-10, 2.0, 3.0}, 1e-9));
    EXPECT_FALSE(lc::approx_equal(Vec3{1.0, 2.0, 3.0}, Vec3{1.0, 2.0, 3.1}, 1e-9));
}
