#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/parallel/threaded_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "test_helpers.hpp"

using lc::MeasurementGrid;
using lc::PointLight;
using lc::Scene;
using lc::Vec3;

namespace {

// Office-like scene: several lights, box and sphere occluders, and a work plane that cuts
// through a box so some cells are NaN (not measured).
Scene office_like_scene() {
    Scene s;
    s.set_room(6.0, 4.0, 3.0);
    for (const double y : {1.0, 3.0}) {
        for (const double x : {1.0, 3.0, 5.0}) {
            s.add_light(PointLight(Vec3{x, y, 2.8}, 800.0));
        }
    }
    s.add_box(Vec3{0.05, 2.6, 0.0}, Vec3{0.65, 3.95, 2.0});
    s.add_box(Vec3{2.9, 0.2, 0.0}, Vec3{3.1, 1.6, 1.6});
    s.add_sphere(lc::Sphere(Vec3{4.5, 2.6, 1.9}, 0.25));
    return s;
}

bool bit_identical(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
}

}  // namespace

TEST(ThreadedExecutor, BitIdenticalToSequentialForManyThreadCounts) {
    const Scene scene = office_like_scene();
    // 97 x 61 cells: neither dimension divides evenly by the thread counts below.
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(0, 0, 6, 4, 0.75, 97, 61), {}};
    lc::SequentialExecutor sequential;
    const lc::SimulationResult reference = lc::simulate(job, sequential);

    std::size_t nan_cells = 0;
    for (const double e : reference.lux) nan_cells += std::isnan(e) ? 1u : 0u;
    ASSERT_GT(nan_cells, 0u) << "test scene should include not-measured cells";

    for (const std::size_t threads : {1u, 2u, 3u, 7u, 24u, 64u}) {
        const lc::ThreadedExecutor threaded(threads);
        const lc::SimulationResult r = lc::simulate(job, threaded);
        EXPECT_TRUE(bit_identical(r.lux, reference.lux)) << threads << " threads";
        EXPECT_EQ(r.stats.avg_lux, reference.stats.avg_lux) << threads << " threads";
        EXPECT_EQ(r.stats.valid_points, reference.stats.valid_points);
    }
}

TEST(ThreadedExecutor, RepeatedRunsAreIdentical) {
    // A data race would typically show up as run-to-run differences.
    const Scene scene = office_like_scene();
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(0, 0, 6, 4, 0.75, 120, 80), {}};
    const lc::ThreadedExecutor threaded(8);
    const lc::SimulationResult first = lc::simulate(job, threaded);
    for (int k = 0; k < 20; ++k) {
        EXPECT_TRUE(bit_identical(lc::simulate(job, threaded).lux, first.lux)) << "run " << k;
    }
}

TEST(ThreadedExecutor, WritesEveryCellExactlyOnce) {
    Scene scene;
    scene.add_light(PointLight(Vec3{1, 1, 2}, 100.0));
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(0, 0, 2, 2, 0, 13, 29), {}};
    std::vector<double> out(job.grid.point_count(), -1.0);  // sentinel: no valid lux is < 0
    lc::ThreadedExecutor(5).run(job, out.data());
    for (std::size_t k = 0; k < out.size(); ++k) {
        EXPECT_GT(out[k], 0.0) << "cell " << k << " not written";
    }
}

TEST(ThreadedExecutor, MoreThreadsThanRowsAndSingleCell) {
    Scene scene;
    scene.add_light(PointLight(Vec3{0.5, 0.5, 1}, 100.0));
    lc::SequentialExecutor sequential;
    const lc::ThreadedExecutor threaded(24);
    for (const auto& grid : {MeasurementGrid::horizontal(0, 0, 1, 1, 0, 50, 2),
                             MeasurementGrid::horizontal(0, 0, 1, 1, 0, 1, 1)}) {
        const lc::SimulationJob job{scene, grid, {}};
        EXPECT_TRUE(bit_identical(lc::simulate(job, threaded).lux,
                                  lc::simulate(job, sequential).lux));
    }
}

TEST(ThreadedExecutor, ThreadCountResolution) {
    const unsigned hw = std::thread::hardware_concurrency();
    EXPECT_EQ(lc::ThreadedExecutor(0).thread_count(), hw == 0 ? 1u : hw);
    EXPECT_EQ(lc::ThreadedExecutor(3).thread_count(), 3u);
    EXPECT_THROW(lc::ThreadedExecutor(lc::ThreadedExecutor::kMaxThreads + 1),
                 std::invalid_argument);
}
