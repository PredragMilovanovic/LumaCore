#include <gtest/gtest.h>

#include <stdexcept>

#include "lumacore/scene/measurement_grid.hpp"
#include "test_helpers.hpp"

using lc::MeasurementGrid;
using lc::Vec3;

TEST(MeasurementGrid, PointsAreCellCenters) {
    const auto g = MeasurementGrid::horizontal(0.0, 0.0, 4.0, 2.0, 0.75, 4, 2);  // 1 m cells
    EXPECT_EQ(g.point_count(), 8u);
    EXPECT_VEC3_NEAR((Vec3{0.5, 0.5, 0.75}), g.point(0, 0), 1e-15);
    EXPECT_VEC3_NEAR((Vec3{3.5, 0.5, 0.75}), g.point(3, 0), 1e-15);
    EXPECT_VEC3_NEAR((Vec3{3.5, 1.5, 0.75}), g.point(3, 1), 1e-15);
    EXPECT_DOUBLE_EQ(g.cell_area(), 1.0);
}

TEST(MeasurementGrid, RowMajorIndex) {
    const auto g = MeasurementGrid::horizontal(0.0, 0.0, 1.0, 1.0, 0.0, 5, 3);
    EXPECT_EQ(g.index(0, 0), 0u);
    EXPECT_EQ(g.index(4, 0), 4u);
    EXPECT_EQ(g.index(0, 1), 5u);
    EXPECT_EQ(g.index(4, 2), 14u);
}

TEST(MeasurementGrid, NormalFollowsEdgeOrder) {
    const auto up = MeasurementGrid::horizontal(0.0, 0.0, 1.0, 1.0, 0.0, 2, 2);
    EXPECT_EQ(up.normal(), (Vec3{0, 0, 1}));
    // Swapping the edges flips the normal: a ceiling-facing (downward) grid.
    const MeasurementGrid down(Vec3{0, 0, 2}, Vec3{0, 1, 0}, Vec3{1, 0, 0}, 2, 2);
    EXPECT_EQ(down.normal(), (Vec3{0, 0, -1}));
    // Vertical wall grid facing +Y.
    const MeasurementGrid wall(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 0, 0}, 2, 2);
    EXPECT_VEC3_NEAR((Vec3{0, 1, 0}), wall.normal(), 1e-15);
}

TEST(MeasurementGrid, CellAreaTimesCountIsTotalArea) {
    const MeasurementGrid g(Vec3{1, 2, 3}, Vec3{3, 0, 0}, Vec3{0, 0, 2}, 7, 11);
    EXPECT_NEAR(g.cell_area() * static_cast<double>(g.point_count()), 6.0, 1e-12);
}

TEST(MeasurementGrid, RejectsInvalidInput) {
    EXPECT_THROW(MeasurementGrid(Vec3{}, Vec3{}, Vec3{0, 1, 0}, 2, 2), std::invalid_argument);
    EXPECT_THROW(MeasurementGrid(Vec3{}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, 2, 2),
                 std::invalid_argument);  // not perpendicular
    EXPECT_THROW(MeasurementGrid(Vec3{}, Vec3{1, 0, 0}, Vec3{0, 1, 0}, 0, 2),
                 std::invalid_argument);
    EXPECT_THROW(MeasurementGrid(Vec3{}, Vec3{1, 0, 0}, Vec3{0, 1, 0}, 100'000, 100'000),
                 std::invalid_argument);  // above kMaxPoints
    EXPECT_THROW((void)MeasurementGrid::horizontal(1.0, 0.0, 0.0, 1.0, 0.0, 2, 2),
                 std::invalid_argument);
}
