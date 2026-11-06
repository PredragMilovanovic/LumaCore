#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "lumacore/geometry/intersection.hpp"
#include "test_helpers.hpp"

using lc::Ray;
using lc::Sphere;
using lc::Vec3;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
const Sphere kUnit(Vec3{0, 0, 0}, 1.0);
}  // namespace

TEST(Sphere, RejectsInvalidRadiusAndCenter) {
    EXPECT_THROW(Sphere(Vec3{}, 0.0), std::invalid_argument);
    EXPECT_THROW(Sphere(Vec3{}, -1.0), std::invalid_argument);
    EXPECT_THROW(Sphere(Vec3{}, std::nan("")), std::invalid_argument);
    EXPECT_THROW(Sphere(Vec3{}, kInf), std::invalid_argument);
    EXPECT_THROW(Sphere(Vec3{kInf, 0, 0}, 1.0), std::invalid_argument);
}

TEST(RaySphere, HitFromOutsideReturnsNearSurface) {
    const auto hit = lc::intersect(Ray(Vec3{0, 0, 5}, Vec3{0, 0, -1}), kUnit, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 4.0);
    EXPECT_VEC3_NEAR((Vec3{0, 0, 1}), hit->point, 1e-15);
    EXPECT_VEC3_NEAR((Vec3{0, 0, 1}), hit->normal, 1e-15);
}

TEST(RaySphere, Miss) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{2, 0, 5}, Vec3{0, 0, -1}), kUnit, 0.0, kInf));
}

TEST(RaySphere, TangentRayTouchesOnce) {
    const auto hit = lc::intersect(Ray(Vec3{1, 0, 5}, Vec3{0, 0, -1}), kUnit, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 5.0, 1e-12);
    EXPECT_VEC3_NEAR((Vec3{1, 0, 0}), hit->normal, 1e-12);
}

TEST(RaySphere, FromInsideReturnsExitWithOutwardNormal) {
    const auto hit = lc::intersect(Ray(Vec3{0, 0, 0}, Vec3{1, 0, 0}), kUnit, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 1.0);
    EXPECT_VEC3_NEAR((Vec3{1, 0, 0}), hit->normal, 1e-15);
}

TEST(RaySphere, SphereBehindOriginMisses) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{0, 0, 5}, Vec3{0, 0, 1}), kUnit, 0.0, kInf));
}

TEST(RaySphere, TMinSkipsNearRootAndReturnsFarRoot) {
    const Ray r(Vec3{0, 0, 5}, Vec3{0, 0, -1});  // roots at t = 4 and t = 6
    const auto hit = lc::intersect(r, kUnit, 4.5, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 6.0);
    EXPECT_FALSE(lc::intersect(r, kUnit, 0.0, 3.9));  // both roots beyond t_max
}

TEST(RaySphere, NumericallyStableAtLargeDistance) {
    // Naive (-b - sqrt(disc)) / 2a loses digits for the far root here; the stable
    // form keeps both roots accurate to ~1 ulp of their magnitude.
    const Sphere far_sphere(Vec3{0, 0, -1e6}, 1.0);
    const Ray r(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    const auto near_hit = lc::intersect(r, far_sphere, 0.0, kInf);
    ASSERT_TRUE(near_hit.has_value());
    EXPECT_NEAR(near_hit->t, 1e6 - 1.0, 1e-9);
    const auto far_hit = lc::intersect(r, far_sphere, near_hit->t, kInf);
    ASSERT_TRUE(far_hit.has_value());
    EXPECT_NEAR(far_hit->t, 1e6 + 1.0, 1e-9);
}

TEST(RaySphere, HitPointLiesOnSurface_Randomized) {
    // Rays start outside the sphere and aim at a random point in a cube of half-size
    // 2r around the center, so roughly a fifth hit and the rest miss nearby.
    lc::test::Random rng;
    int hits = 0;
    int misses = 0;
    for (int i = 0; i < 2000; ++i) {
        const Sphere s(rng.vec(-3, 3), rng.uniform(0.1, 2.0));
        const Vec3 origin = s.center() + rng.unit() * (s.radius() + rng.uniform(0.5, 10.0));
        const Vec3 aim = s.center() + rng.vec(-2.0, 2.0) * s.radius();
        const Ray ray(origin, aim - origin);
        const auto hit = lc::intersect(ray, s, 0.0, kInf);
        // Independent check: from outside, the ray hits iff its closest approach to the
        // center (restricted to t >= 0) is <= r.
        const double along = std::max(0.0, lc::dot(s.center() - origin, ray.direction()));
        const double closest = lc::distance(ray.at(along), s.center());
        if (!hit) {
            ++misses;
            EXPECT_GT(closest, s.radius() - 1e-9);
            continue;
        }
        ++hits;
        EXPECT_LT(closest, s.radius() + 1e-9);
        EXPECT_NEAR(lc::distance(hit->point, s.center()), s.radius(), 1e-9);
        EXPECT_NEAR(lc::length(hit->normal), 1.0, 1e-12);
    }
    // Both branches must actually be exercised by the sample.
    EXPECT_GT(hits, 200);
    EXPECT_GT(misses, 200);
}

TEST(RaySphere, AimedRaysAlwaysHit_Randomized) {
    lc::test::Random rng;
    for (int i = 0; i < 1000; ++i) {
        const Sphere s(rng.vec(-3, 3), rng.uniform(0.1, 2.0));
        const Vec3 origin = s.center() + rng.unit() * (s.radius() + rng.uniform(0.1, 10.0));
        const auto hit = lc::intersect(Ray(origin, s.center() - origin), s, 0.0, kInf);
        ASSERT_TRUE(hit.has_value());
        EXPECT_NEAR(hit->t, lc::distance(origin, s.center()) - s.radius(), 1e-9);
    }
}
