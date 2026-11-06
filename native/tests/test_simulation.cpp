#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "test_helpers.hpp"

using lc::MeasurementGrid;
using lc::PointLight;
using lc::Scene;
using lc::Vec3;

TEST(Stats, HandComputedValues) {
    const lc::IlluminanceStats s = lc::compute_stats({100.0, 400.0, 200.0, 300.0});
    EXPECT_DOUBLE_EQ(s.min_lux, 100.0);
    EXPECT_DOUBLE_EQ(s.max_lux, 400.0);
    EXPECT_DOUBLE_EQ(s.avg_lux, 250.0);
    EXPECT_DOUBLE_EQ(s.uniformity_u0, 0.4);
}

TEST(Stats, AllDarkGivesZeroUniformityAndEmptyThrows) {
    EXPECT_DOUBLE_EQ(lc::compute_stats({0.0, 0.0}).uniformity_u0, 0.0);
    EXPECT_THROW((void)lc::compute_stats({}), std::invalid_argument);
}

TEST(Stats, NotMeasuredPointsAreSkipped) {
    const double nan = std::nan("");
    const lc::IlluminanceStats s = lc::compute_stats({nan, 100.0, 300.0, nan});
    EXPECT_EQ(s.valid_points, 2u);
    EXPECT_DOUBLE_EQ(s.min_lux, 100.0);
    EXPECT_DOUBLE_EQ(s.avg_lux, 200.0);
    EXPECT_DOUBLE_EQ(s.uniformity_u0, 0.5);

    const lc::IlluminanceStats none = lc::compute_stats({nan, nan});
    EXPECT_EQ(none.valid_points, 0u);
    EXPECT_EQ(none.avg_lux, 0.0);
}

TEST(Simulation, PointsInsideSolidsAreNotMeasured) {
    // Work plane at 0.75 m cuts through a 2 m tall cabinet and a sphere.
    Scene scene;
    scene.add_light(PointLight(Vec3{2, 2, 2.8}, 800.0));
    scene.add_box(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 1.0, 2.0});
    scene.add_sphere(lc::Sphere(Vec3{3.0, 3.0, 0.75}, 0.3));
    const MeasurementGrid grid = MeasurementGrid::horizontal(0, 0, 4, 4, 0.75, 40, 40);
    lc::SequentialExecutor exec;
    const lc::SimulationResult r = lc::simulate({scene, grid, {}}, exec);

    std::size_t nan_count = 0;
    for (std::size_t j = 0; j < grid.ny(); ++j) {
        for (std::size_t i = 0; i < grid.nx(); ++i) {
            const double e = r.lux[grid.index(i, j)];
            EXPECT_EQ(std::isnan(e), scene.inside_solid(grid.point(i, j)));
            nan_count += std::isnan(e) ? 1u : 0u;
        }
    }
    // Box: 10 x 10 cells. Sphere (r = 0.3, cut through its center): cell-center offsets
    // a, b in {0.05, 0.15, 0.25}; a^2 + b^2 < 0.09 holds for 8 of the 9 pairs per
    // quadrant (only 0.25/0.25 fails), so 4 x 8 = 32 cells.
    EXPECT_EQ(nan_count, 100u + 32u);
    EXPECT_EQ(r.stats.valid_points, grid.point_count() - nan_count);
    EXPECT_FALSE(std::isnan(r.stats.min_lux));
    // Measured points may legitimately be 0 lx (in the full shadow of the only light),
    // but only if the light would reach them without occluders.
    for (std::size_t k = 0; k < r.lux.size(); ++k) {
        if (r.lux[k] == 0.0) {
            const std::size_t i = k % grid.nx(), j = k / grid.nx();
            EXPECT_GT(lc::illuminance_at(scene, grid.point(i, j), grid.normal(), false), 0.0);
        }
    }
}

TEST(Scene, InsideSolidIsStrict) {
    Scene scene;
    scene.add_box(Vec3{0, 0, 0}, Vec3{1, 1, 1});
    scene.add_sphere(lc::Sphere(Vec3{5, 5, 5}, 1.0));
    EXPECT_TRUE(scene.inside_solid(Vec3{0.5, 0.5, 0.5}));
    EXPECT_FALSE(scene.inside_solid(Vec3{0.5, 0.5, 1.0}));  // on the top face: measurable
    EXPECT_TRUE(scene.inside_solid(Vec3{5.5, 5, 5}));
    EXPECT_FALSE(scene.inside_solid(Vec3{6.0, 5, 5}));      // on the sphere surface
    scene.add_triangle(lc::Triangle(Vec3{2, 0, 0}, Vec3{3, 0, 0}, Vec3{2, 1, 0}));
    EXPECT_FALSE(scene.inside_solid(Vec3{2.2, 0.2, 0.0}));  // open surfaces have no inside
}

TEST(Simulation, ResultShapeAndStatsMatchGrid) {
    Scene scene;
    scene.add_light(PointLight(Vec3{1, 1, 2}, 500.0));
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(0, 0, 2, 1, 0, 8, 3), {}};
    lc::SequentialExecutor exec;
    const lc::SimulationResult r = lc::simulate(job, exec);
    EXPECT_EQ(r.nx, 8u);
    EXPECT_EQ(r.ny, 3u);
    ASSERT_EQ(r.lux.size(), 24u);
    EXPECT_GE(r.elapsed_ms, 0.0);
    const lc::IlluminanceStats expected = lc::compute_stats(r.lux);
    EXPECT_EQ(r.stats.avg_lux, expected.avg_lux);
    // Each value equals the kernel evaluated at that cell center.
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_EQ(r.lux[job.grid.index(i, j)],
                      lc::illuminance_at(scene, job.grid.point(i, j), job.grid.normal(), true));
        }
    }
}

TEST(Simulation, CenteredLightGivesSymmetricField) {
    Scene scene;
    scene.add_light(PointLight(Vec3{2, 2, 2.75}, 1000.0));
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(0, 0, 4, 4, 0.75, 40, 40),
                                {}};
    lc::SequentialExecutor exec;
    const lc::SimulationResult r = lc::simulate(job, exec);
    for (std::size_t j = 0; j < 40; ++j) {
        for (std::size_t i = 0; i < 40; ++i) {
            const double e = r.lux[job.grid.index(i, j)];
            EXPECT_NEAR(e, r.lux[job.grid.index(39 - i, j)], 1e-12 * e);  // mirror in x
            EXPECT_NEAR(e, r.lux[job.grid.index(i, 39 - j)], 1e-12 * e);  // mirror in y
            EXPECT_NEAR(e, r.lux[job.grid.index(j, i)], 1e-12 * e);       // diagonal
        }
    }
    // Brightest at the four cells around the center, darkest in the corners (those
    // four-way ties are exact only mathematically, hence the relative tolerance).
    EXPECT_NEAR(r.stats.max_lux, r.lux[job.grid.index(19, 19)], 1e-12 * r.stats.max_lux);
    EXPECT_NEAR(r.stats.min_lux, r.lux[job.grid.index(0, 0)], 1e-12 * r.stats.min_lux);
}

TEST(Simulation, FluxThroughSquareMatchesSolidAngle) {
    // Flux from an isotropic source onto a surface = I * (solid angle it subtends).
    // Square of half-side a, centered 1 m below the source: Omega = 4 asin(a^2 / (a^2 + h^2)).
    // Summing E * cell_area over the grid is a midpoint-rule integral of that flux.
    const double intensity = 1000.0, h = 1.0, a = 2.0;
    Scene scene;
    scene.add_light(PointLight(Vec3{0, 0, h}, intensity));
    const lc::SimulationJob job{scene, MeasurementGrid::horizontal(-a, -a, a, a, 0.0, 500, 500),
                                {}};
    lc::SequentialExecutor exec;
    const lc::SimulationResult r = lc::simulate(job, exec);

    double flux_lm = 0.0;
    for (const double e : r.lux) {
        flux_lm += e * job.grid.cell_area();
    }
    const double omega = 4.0 * std::asin(a * a / (a * a + h * h));
    EXPECT_NEAR(flux_lm, intensity * omega, 1e-4 * intensity * omega);
}

TEST(Simulation, ShadowOptionChangesOnlyOccludedPoints) {
    Scene scene;
    scene.add_light(PointLight(Vec3{2, 2, 3}, 1000.0));
    scene.add_box(Vec3{1.8, 1.8, 1.0}, Vec3{2.2, 2.2, 1.2});
    const MeasurementGrid grid = MeasurementGrid::horizontal(0, 0, 4, 4, 0.0, 20, 20);
    lc::SequentialExecutor exec;
    const auto with = lc::simulate({scene, grid, lc::SimulationOptions{true}}, exec);
    const auto without = lc::simulate({scene, grid, lc::SimulationOptions{false}}, exec);
    int shadowed = 0;
    for (std::size_t k = 0; k < with.lux.size(); ++k) {
        if (with.lux[k] == 0.0) {
            ++shadowed;
            EXPECT_GT(without.lux[k], 0.0);
        } else {
            EXPECT_EQ(with.lux[k], without.lux[k]);
        }
    }
    EXPECT_GT(shadowed, 0);
    EXPECT_LT(with.stats.min_lux, without.stats.min_lux);
}

TEST(Simulation, InvalidSceneThrowsBeforeRunning) {
    Scene scene;
    scene.set_room(2.0, 2.0, 2.0);
    scene.add_light(PointLight(Vec3{5, 5, 5}, 100.0));
    lc::SequentialExecutor exec;
    EXPECT_THROW(
        (void)lc::simulate({scene, MeasurementGrid::horizontal(0, 0, 2, 2, 0, 2, 2), {}}, exec),
        std::invalid_argument);
}
