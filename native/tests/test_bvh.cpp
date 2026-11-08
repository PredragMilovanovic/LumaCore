#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <vector>

#include "lumacore/geometry/bvh.hpp"
#include "lumacore/geometry/intersection.hpp"
#include "lumacore/geometry/primitives.hpp"
#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/parallel/threaded_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "test_helpers.hpp"

using lc::Ray;
using lc::Triangle;
using lc::TriangleBvh;
using lc::Vec3;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

bool brute_force(const std::vector<Triangle>& tris, const Ray& ray, double t_max) {
    for (const Triangle& t : tris) {
        if (lc::intersect(ray, t, 0.0, t_max)) return true;
    }
    return false;
}

std::vector<Triangle> random_soup(lc::test::Random& rng, int n, double spread, double size) {
    std::vector<Triangle> tris;
    while (static_cast<int>(tris.size()) < n) {
        const Vec3 c = rng.vec(-spread, spread);
        const Vec3 a = c + rng.vec(-size, size), b = c + rng.vec(-size, size), d = c + rng.vec(-size, size);
        if (lc::length(lc::cross(b - a, d - a)) > 1e-6) tris.emplace_back(a, b, d);
    }
    return tris;
}

}  // namespace

TEST(Bvh, AnyHitMatchesBruteForce_Randomized) {
    lc::test::Random rng(2026);
    const auto tris = random_soup(rng, 1500, 5.0, 0.6);
    const TriangleBvh bvh(tris);
    int hits = 0;
    for (int k = 0; k < 20000; ++k) {
        const Ray ray(rng.vec(-7, 7), rng.unit());
        const double t_max = (k % 4 == 0) ? kInf : rng.uniform(0.1, 15.0);
        const bool expected = brute_force(tris, ray, t_max);
        ASSERT_EQ(bvh.any_hit(ray, t_max), expected) << "ray " << k;
        hits += expected ? 1 : 0;
    }
    EXPECT_GT(hits, 2000);   // both outcomes are exercised
    EXPECT_LT(hits, 18000);
}

TEST(Bvh, EdgeAndVertexRaysMatchBruteForce) {
    // Rays aimed exactly at box corners and edge midpoints: the hardest cases for a
    // bounding-box test, because the hit lies on the boundary of a node.
    std::vector<Triangle> tris;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const auto box = lc::box_triangles(Vec3{i * 1.0, j * 1.0, 0.0}, Vec3{i + 0.5, j + 0.5, 0.5});
            tris.insert(tris.end(), box.begin(), box.end());
        }
    }
    const TriangleBvh bvh(tris);
    lc::test::Random rng(7);
    int checked = 0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            for (const Vec3 target : {Vec3{i * 1.0, j * 1.0, 0.5}, Vec3{i + 0.5, j + 0.5, 0.5},
                                      Vec3{i + 0.25, j * 1.0, 0.5}, Vec3{i + 0.5, j + 0.25, 0.0}}) {
                for (int k = 0; k < 10; ++k) {
                    const Vec3 origin = target + rng.unit() * rng.uniform(0.5, 6.0);
                    const Ray ray(origin, target - origin);
                    ASSERT_EQ(bvh.any_hit(ray, kInf), brute_force(tris, ray, kInf));
                    ++checked;
                }
            }
        }
    }
    EXPECT_EQ(checked, 640);
}

TEST(Bvh, AxisParallelRaysMatchBruteForce) {
    // Direction components of exactly 0 make the slab test divide by zero (±inf, and NaN when
    // the origin lies on a slab plane). The test must stay conservative.
    std::vector<Triangle> tris;
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            const auto quad = lc::quad_triangles(Vec3{i * 0.5, j * 0.5, 0}, Vec3{i * 0.5 + 0.5, j * 0.5, 0},
                                                 Vec3{i * 0.5 + 0.5, j * 0.5 + 0.5, 0}, Vec3{i * 0.5, j * 0.5 + 0.5, 0});
            tris.insert(tris.end(), quad.begin(), quad.end());
        }
    }
    const TriangleBvh bvh(tris);
    for (int i = 0; i <= 20; ++i) {
        for (int j = 0; j <= 20; ++j) {
            const Vec3 p{i * 0.25, j * 0.25, 1.0};  // includes points exactly above edges and corners
            const Ray down = Ray::from_unit_direction(p, Vec3{0, 0, -1});
            ASSERT_EQ(bvh.any_hit(down, kInf), brute_force(tris, down, kInf)) << i << "," << j;
            const Ray along = Ray::from_unit_direction(Vec3{-1, j * 0.25, 0.0}, Vec3{1, 0, 0});
            ASSERT_EQ(bvh.any_hit(along, kInf), brute_force(tris, along, kInf));  // in the plane
        }
    }
}

TEST(Bvh, StructureAndEmptyTree) {
    const TriangleBvh empty(std::vector<Triangle>{});
    EXPECT_FALSE(empty.any_hit(Ray(Vec3{}, Vec3{0, 0, 1}), kInf));
    EXPECT_EQ(empty.node_count(), 0u);

    lc::test::Random rng(1);
    const TriangleBvh bvh(random_soup(rng, 4096, 10.0, 0.3));
    EXPECT_EQ(bvh.triangle_count(), 4096u);
    EXPECT_LT(bvh.node_count(), 2u * 4096u / TriangleBvh::kLeafSize + 1u);
    EXPECT_LE(bvh.depth(), 12u);  // median split: about log2(4096 / 4) + 1 = 11
    EXPECT_FALSE(bvh.any_hit(Ray(Vec3{}, Vec3{0, 0, 1}), 0.0));  // empty interval
}

TEST(SceneAcceleration, SimulationIsBitIdenticalWithAndWithoutBvh) {
    lc::Scene brute;
    brute.set_room(6, 4, 3);
    for (const double x : {1.0, 3.0, 5.0}) {
        for (const double y : {1.0, 3.0}) brute.add_light(lc::PointLight(Vec3{x, y, 2.8}, 800.0));
    }
    lc::test::Random rng(99);
    for (int k = 0; k < 150; ++k) {  // 1800 box triangles: desks, shelves, clutter
        const Vec3 lo{rng.uniform(0.2, 5.5), rng.uniform(0.2, 3.5), rng.uniform(0.0, 1.5)};
        brute.add_box(lo, lo + Vec3{rng.uniform(0.05, 0.4), rng.uniform(0.05, 0.4), rng.uniform(0.05, 0.6)});
    }
    brute.add_triangles(random_soup(rng, 500, 1.0, 0.2));  // a "plant": loose triangles
    brute.add_sphere(lc::Sphere(Vec3{4.5, 2.6, 1.9}, 0.25));

    lc::Scene fast = brute;
    fast.build_acceleration();
    ASSERT_TRUE(fast.has_acceleration());
    ASSERT_FALSE(brute.has_acceleration());

    const auto grid = lc::MeasurementGrid::horizontal(0.5, 0.5, 5.5, 3.5, 0.75, 100, 60);
    lc::SequentialExecutor sequential;
    const lc::ThreadedExecutor threaded(8);
    const auto a = lc::simulate({brute, grid, {}}, sequential);
    const auto b = lc::simulate({fast, grid, {}}, sequential);
    const auto c = lc::simulate({fast, grid, {}}, threaded);
    ASSERT_EQ(a.lux.size(), b.lux.size());
    EXPECT_EQ(std::memcmp(a.lux.data(), b.lux.data(), a.lux.size() * sizeof(double)), 0);
    EXPECT_EQ(std::memcmp(a.lux.data(), c.lux.data(), a.lux.size() * sizeof(double)), 0);
    EXPECT_LT(a.stats.min_lux, a.stats.max_lux);  // the clutter really casts shadows
}

TEST(SceneAcceleration, GeometryChangesDiscardTheBvh) {
    lc::Scene s;
    s.add_box(Vec3{0, 0, 0}, Vec3{1, 1, 1});
    s.build_acceleration();
    EXPECT_TRUE(s.has_acceleration());
    s.add_triangle(Triangle(Vec3{2, 0, 0}, Vec3{3, 0, 0}, Vec3{2, 1, 0}));
    EXPECT_FALSE(s.has_acceleration());  // stale tree would miss the new triangle
    s.build_acceleration();
    s.add_light(lc::PointLight(Vec3{0, 0, 3}, 1.0));
    s.clear_lights();
    EXPECT_TRUE(s.has_acceleration());  // lights do not affect the geometry
}
