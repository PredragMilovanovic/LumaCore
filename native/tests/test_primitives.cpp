#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "lumacore/geometry/intersection.hpp"
#include "lumacore/geometry/primitives.hpp"
#include "test_helpers.hpp"

using lc::Ray;
using lc::Vec3;

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
const Vec3 kMin{1.0, 2.0, 0.0};
const Vec3 kMax{3.0, 5.0, 1.5};
}  // namespace

TEST(Quad, SplitsIntoTwoCoplanarTriangles) {
    const auto tris = lc::quad_triangles(Vec3{0, 0, 0}, Vec3{2, 0, 0}, Vec3{2, 1, 0},
                                         Vec3{0, 1, 0});
    EXPECT_EQ(tris[0].normal(), (Vec3{0, 0, 1}));
    EXPECT_EQ(tris[1].normal(), (Vec3{0, 0, 1}));
    EXPECT_DOUBLE_EQ(tris[0].area() + tris[1].area(), 2.0);
}

TEST(Quad, RejectsNonPlanarQuad) {
    EXPECT_THROW((void)lc::quad_triangles(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 1},
                                          Vec3{0, 1, 0}),
                 std::invalid_argument);
}

TEST(Box, HasTwelveTrianglesWithCorrectSurfaceArea) {
    const auto tris = lc::box_triangles(kMin, kMax);
    ASSERT_EQ(tris.size(), 12u);
    double area = 0.0;
    for (const auto& t : tris) {
        area += t.area();
    }
    const Vec3 d = kMax - kMin;  // 2 x 3 x 1.5
    EXPECT_NEAR(area, 2.0 * (d.x * d.y + d.y * d.z + d.z * d.x), 1e-12);
}

TEST(Box, AllNormalsPointOutward) {
    const Vec3 center = (kMin + kMax) * 0.5;
    for (const auto& t : lc::box_triangles(kMin, kMax)) {
        EXPECT_GT(lc::dot(t.normal(), t.centroid() - center), 0.0);
    }
}

TEST(Box, RejectsInvalidBounds) {
    EXPECT_THROW((void)lc::box_triangles(kMax, kMin), std::invalid_argument);
    EXPECT_THROW((void)lc::box_triangles(Vec3{0, 0, 0}, Vec3{1, 1, 0}), std::invalid_argument);
    EXPECT_THROW((void)lc::box_triangles(Vec3{0, 0, 0}, Vec3{kInf, 1, 1}), std::invalid_argument);
}

TEST(Box, IsWatertight_Randomized) {
    // Every ray leaving the box interior must hit the surface exactly at the
    // analytic slab distance: no gaps along the shared triangle edges.
    const auto tris = lc::box_triangles(kMin, kMax);
    lc::test::Random rng;
    for (int i = 0; i < 2000; ++i) {
        const Vec3 origin{rng.uniform(kMin.x + 0.01, kMax.x - 0.01),
                          rng.uniform(kMin.y + 0.01, kMax.y - 0.01),
                          rng.uniform(kMin.z + 0.01, kMax.z - 0.01)};
        const Ray ray(origin, rng.unit());

        double nearest = kInf;
        for (const auto& t : tris) {
            if (const auto hit = lc::intersect(ray, t, 0.0, nearest)) {
                nearest = hit->t;
            }
        }
        ASSERT_LT(nearest, kInf) << "ray escaped the box (iteration " << i << ")";

        // Slab method: exit distance = min over axes of distance to the face ahead.
        const Vec3 d = ray.direction();
        double expected = kInf;
        const double o[3] = {origin.x, origin.y, origin.z};
        const double dir[3] = {d.x, d.y, d.z};
        const double lo[3] = {kMin.x, kMin.y, kMin.z};
        const double hi[3] = {kMax.x, kMax.y, kMax.z};
        for (int axis = 0; axis < 3; ++axis) {
            if (dir[axis] > 0.0) {
                expected = std::min(expected, (hi[axis] - o[axis]) / dir[axis]);
            } else if (dir[axis] < 0.0) {
                expected = std::min(expected, (lo[axis] - o[axis]) / dir[axis]);
            }
        }
        EXPECT_NEAR(nearest, expected, 1e-9);
    }
}
