#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "lumacore/geometry/intersection.hpp"
#include "test_helpers.hpp"

using lc::Plane;
using lc::Ray;
using lc::Vec3;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
const Plane kFloor(Vec3{0, 0, 0}, Vec3{0, 0, 1});  // z = 0, normal +Z
}  // namespace

TEST(Plane, NormalizesNormalAndComputesOffset) {
    const Plane p(Vec3{0, 0, 2}, Vec3{0, 0, 10});
    EXPECT_EQ(p.normal(), (Vec3{0, 0, 1}));
    EXPECT_DOUBLE_EQ(p.offset(), 2.0);
    EXPECT_DOUBLE_EQ(p.signed_distance(Vec3{5, 5, 3}), 1.0);
    EXPECT_DOUBLE_EQ(p.signed_distance(Vec3{0, 0, -1}), -3.0);
}

TEST(Plane, RejectsInvalidInput) {
    EXPECT_THROW(Plane(Vec3{}, Vec3{}), std::invalid_argument);
    EXPECT_THROW(Plane(Vec3{kInf, 0, 0}, Vec3{0, 0, 1}), std::invalid_argument);
}

TEST(RayPlane, PerpendicularHit) {
    const auto hit = lc::intersect(Ray(Vec3{1, 2, 3}, Vec3{0, 0, -1}), kFloor, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 3.0);
    EXPECT_VEC3_NEAR((Vec3{1, 2, 0}), hit->point, 1e-15);
    EXPECT_EQ(hit->normal, (Vec3{0, 0, 1}));
}

TEST(RayPlane, ObliqueHitAt45Degrees) {
    const auto hit = lc::intersect(Ray(Vec3{0, 0, 2}, Vec3{1, 0, -1}), kFloor, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 2.0 * std::sqrt(2.0), 1e-14);
    EXPECT_VEC3_NEAR((Vec3{2, 0, 0}), hit->point, 1e-14);
}

TEST(RayPlane, ParallelRayMisses) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{0, 0, 1}, Vec3{1, 0, 0}), kFloor, 0.0, kInf));
    // A ray lying inside the plane is also reported as no hit.
    EXPECT_FALSE(lc::intersect(Ray(Vec3{0, 0, 0}, Vec3{0, 1, 0}), kFloor, 0.0, kInf));
}

TEST(RayPlane, PlaneBehindOriginMisses) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{0, 0, 1}, Vec3{0, 0, 1}), kFloor, 0.0, kInf));
}

TEST(RayPlane, HitFromBelowReturnsGeometricNormal) {
    const auto hit = lc::intersect(Ray(Vec3{0, 0, -2}, Vec3{0, 0, 1}), kFloor, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 2.0);
    EXPECT_EQ(hit->normal, (Vec3{0, 0, 1}));  // not flipped toward the ray
}

TEST(RayPlane, RespectsOpenParameterInterval) {
    const Ray r(Vec3{0, 0, 3}, Vec3{0, 0, -1});  // hit at t = 3
    EXPECT_FALSE(lc::intersect(r, kFloor, 0.0, 3.0));        // t_max is exclusive
    EXPECT_FALSE(lc::intersect(r, kFloor, 3.0, kInf));       // t_min is exclusive
    EXPECT_TRUE(lc::intersect(r, kFloor, 2.999, 3.001));
    EXPECT_FALSE(lc::intersect(r, kFloor, 5.0, 1.0));        // empty interval
    EXPECT_FALSE(lc::intersect(r, kFloor, std::nan(""), kInf));
}

TEST(RayPlane, HitPointLiesOnPlane_Randomized) {
    lc::test::Random rng;
    for (int i = 0; i < 1000; ++i) {
        const Plane plane(rng.vec(-5, 5), rng.unit());
        const Ray ray(rng.vec(-5, 5), rng.unit());
        const auto hit = lc::intersect(ray, plane, 0.0, kInf);
        if (hit) {
            EXPECT_GT(hit->t, 0.0);
            EXPECT_NEAR(plane.signed_distance(hit->point), 0.0, 1e-9);
        } else {
            // A miss means the plane is behind the origin or the ray is (near) parallel.
            const double denom = lc::dot(plane.normal(), ray.direction());
            const bool behind = plane.signed_distance(ray.origin()) * denom > 0.0;
            EXPECT_TRUE(behind || std::abs(denom) < lc::kParallelEpsilon);
        }
    }
}
