#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "lumacore/math/ray.hpp"
#include "test_helpers.hpp"

using lc::Ray;
using lc::Vec3;

TEST(Ray, NormalizesDirection) {
    const Ray r(Vec3{1, 2, 3}, Vec3{0.0, 0.0, -5.0});
    EXPECT_EQ(r.origin(), (Vec3{1, 2, 3}));
    EXPECT_EQ(r.direction(), (Vec3{0.0, 0.0, -1.0}));
}

TEST(Ray, AtReturnsPointAtDistance) {
    const Ray r(Vec3{0, 0, 0}, Vec3{3.0, 4.0, 0.0});
    EXPECT_VEC3_NEAR((Vec3{0.6, 0.8, 0.0}), r.at(1.0), 1e-15);
    EXPECT_VEC3_NEAR((Vec3{3.0, 4.0, 0.0}), r.at(5.0), 1e-14);
    EXPECT_EQ(r.at(0.0), r.origin());
}

TEST(Ray, RejectsDegenerateDirection) {
    EXPECT_THROW(Ray(Vec3{}, Vec3{}), std::invalid_argument);
    EXPECT_THROW(Ray(Vec3{}, Vec3{0.0, 1e-15, 0.0}), std::invalid_argument);
    EXPECT_THROW(Ray(Vec3{}, Vec3{std::numeric_limits<double>::infinity(), 0.0, 0.0}),
                 std::invalid_argument);
}

TEST(Ray, RejectsNonFiniteOrigin) {
    EXPECT_THROW(Ray(Vec3{std::numeric_limits<double>::quiet_NaN(), 0, 0}, Vec3{1, 0, 0}),
                 std::invalid_argument);
}

TEST(Ray, FromUnitDirectionKeepsInputs) {
    const Ray r = Ray::from_unit_direction(Vec3{1, 1, 1}, Vec3{0, 1, 0});
    EXPECT_EQ(r.origin(), (Vec3{1, 1, 1}));
    EXPECT_EQ(r.direction(), (Vec3{0, 1, 0}));
}
