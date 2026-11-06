#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "lumacore/geometry/intersection.hpp"
#include "test_helpers.hpp"

using lc::Ray;
using lc::Triangle;
using lc::Vec3;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
// Right triangle in the z = 0 plane, counter-clockwise seen from +Z.
const Triangle kTri(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0});

Ray down_at(double x, double y) { return Ray(Vec3{x, y, 2.0}, Vec3{0, 0, -1}); }
}  // namespace

TEST(Triangle, NormalFollowsRightHandRule) {
    EXPECT_EQ(kTri.normal(), (Vec3{0, 0, 1}));
    const Triangle flipped(Vec3{0, 0, 0}, Vec3{0, 1, 0}, Vec3{1, 0, 0});
    EXPECT_EQ(flipped.normal(), (Vec3{0, 0, -1}));
    EXPECT_DOUBLE_EQ(kTri.area(), 0.5);
    EXPECT_VEC3_NEAR((Vec3{1.0 / 3.0, 1.0 / 3.0, 0.0}), kTri.centroid(), 1e-15);
}

TEST(Triangle, RejectsDegenerateTriangles) {
    EXPECT_THROW(Triangle(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{2, 2, 2}), std::invalid_argument);
    EXPECT_THROW(Triangle(Vec3{1, 2, 3}, Vec3{1, 2, 3}, Vec3{0, 0, 0}), std::invalid_argument);
    EXPECT_THROW(Triangle(Vec3{0, 0, 0}, Vec3{kInf, 0, 0}, Vec3{0, 1, 0}),
                 std::invalid_argument);
}

TEST(RayTriangle, HitInteriorPoint) {
    const auto hit = lc::intersect(down_at(0.25, 0.25), kTri, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 2.0);
    EXPECT_VEC3_NEAR((Vec3{0.25, 0.25, 0.0}), hit->point, 1e-15);
    EXPECT_EQ(hit->normal, (Vec3{0, 0, 1}));
}

TEST(RayTriangle, MissOutside) {
    EXPECT_FALSE(lc::intersect(down_at(0.6, 0.6), kTri, 0.0, kInf));  // beyond hypotenuse
    EXPECT_FALSE(lc::intersect(down_at(-0.1, 0.5), kTri, 0.0, kInf));
    EXPECT_FALSE(lc::intersect(down_at(0.5, -0.1), kTri, 0.0, kInf));
}

TEST(RayTriangle, EdgesAndVerticesAreInside) {
    EXPECT_TRUE(lc::intersect(down_at(0.5, 0.0), kTri, 0.0, kInf));  // edge a-b
    EXPECT_TRUE(lc::intersect(down_at(0.0, 0.5), kTri, 0.0, kInf));  // edge a-c
    EXPECT_TRUE(lc::intersect(down_at(0.0, 0.0), kTri, 0.0, kInf));  // vertex a
    EXPECT_TRUE(lc::intersect(down_at(1.0, 0.0), kTri, 0.0, kInf));  // vertex b
}

TEST(RayTriangle, ParallelRayMisses) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{-1, 0.2, 0}, Vec3{1, 0, 0}), kTri, 0.0, kInf));
    EXPECT_FALSE(lc::intersect(Ray(Vec3{-1, 0.2, 1}, Vec3{1, 0, 0}), kTri, 0.0, kInf));
}

TEST(RayTriangle, IsTwoSided) {
    const auto hit = lc::intersect(Ray(Vec3{0.2, 0.2, -3}, Vec3{0, 0, 1}), kTri, 0.0, kInf);
    ASSERT_TRUE(hit.has_value());
    EXPECT_DOUBLE_EQ(hit->t, 3.0);
    EXPECT_EQ(hit->normal, (Vec3{0, 0, 1}));
}

TEST(RayTriangle, BehindOriginAndIntervalBounds) {
    EXPECT_FALSE(lc::intersect(Ray(Vec3{0.2, 0.2, 1}, Vec3{0, 0, 1}), kTri, 0.0, kInf));
    EXPECT_FALSE(lc::intersect(down_at(0.2, 0.2), kTri, 0.0, 2.0));  // hit exactly at t_max
    EXPECT_TRUE(lc::intersect(down_at(0.2, 0.2), kTri, 1.0, 2.5));
}

TEST(RayTriangle, ObliqueHitMatchesPlaneIntersection) {
    // Tilted triangle in the plane z = (x + y) / 2.
    const Triangle tilted(Vec3{0, 0, 0}, Vec3{2, 0, 1}, Vec3{0, 2, 1});
    const lc::Plane plane(tilted.a(), tilted.normal());
    const Vec3 origin{0.5, 0.5, 5};

    // Aimed at the interior point a + 0.3 e1 + 0.3 e2 = (0.6, 0.6, 0.6).
    const Vec3 target{0.6, 0.6, 0.6};
    const Ray ray(origin, target - origin);
    const auto tri_hit = lc::intersect(ray, tilted, 0.0, kInf);
    const auto plane_hit = lc::intersect(ray, plane, 0.0, kInf);
    ASSERT_TRUE(tri_hit.has_value());
    ASSERT_TRUE(plane_hit.has_value());
    EXPECT_NEAR(tri_hit->t, plane_hit->t, 1e-12);
    EXPECT_NEAR(tri_hit->t, lc::distance(origin, target), 1e-12);
    EXPECT_VEC3_NEAR(target, tri_hit->point, 1e-12);
}

TEST(RayTriangle, ObliqueRayOutsideTriangleMissesButHitsPlane) {
    // Worked by hand: this ray meets the plane at (0.891, 1.283, 1.087), i.e.
    // barycentric u = 0.446, v = 0.641, u + v = 1.087 > 1  ->  outside the triangle.
    const Triangle tilted(Vec3{0, 0, 0}, Vec3{2, 0, 1}, Vec3{0, 2, 1});
    const Ray ray(Vec3{0.5, 0.5, 5}, Vec3{0.1, 0.2, -1});
    EXPECT_FALSE(lc::intersect(ray, tilted, 0.0, kInf));
    const auto plane_hit = lc::intersect(ray, lc::Plane(tilted.a(), tilted.normal()), 0.0, kInf);
    ASSERT_TRUE(plane_hit.has_value());
    EXPECT_NEAR(plane_hit->point.x, 0.5 + 0.1 * 9.0 / 2.3, 1e-12);
    EXPECT_NEAR(plane_hit->point.y, 0.5 + 0.2 * 9.0 / 2.3, 1e-12);
}

TEST(RayTriangle, BarycentricSamplesInsideHitAndOutsideMiss_Randomized) {
    lc::test::Random rng;
    for (int i = 0; i < 1000; ++i) {
        const Vec3 a = rng.vec(-5, 5), b = rng.vec(-5, 5), c = rng.vec(-5, 5);
        if (lc::length(lc::cross(b - a, c - a)) < 0.5) {
            continue;  // skip slivers: their tolerance analysis is a different test
        }
        const Triangle tri(a, b, c);
        const Vec3 origin = tri.centroid() + tri.normal() * rng.uniform(1.0, 5.0) +
                            rng.vec(-0.5, 0.5);

        // Strictly interior target point.
        const double u = rng.uniform(0.05, 0.9);
        const double v = rng.uniform(0.05, 0.95 - u);
        const Vec3 inside = a + (b - a) * u + (c - a) * v;
        const auto hit = lc::intersect(Ray(origin, inside - origin), tri, 0.0, kInf);
        ASSERT_TRUE(hit.has_value());
        EXPECT_VEC3_NEAR(inside, hit->point, 1e-9);

        // Clearly exterior point in the same plane (u < 0).
        const Vec3 outside = a + (b - a) * -0.2 + (c - a) * 0.3;
        EXPECT_FALSE(lc::intersect(Ray(origin, outside - origin), tri, 0.0, kInf));
    }
}
