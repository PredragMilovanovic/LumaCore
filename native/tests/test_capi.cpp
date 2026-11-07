// Tests of the C API exactly as a C caller (or P/Invoke) sees it: through lumacore.dll.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "lumacore_c.h"

namespace {

std::string last_error() {
    char buffer[512];
    (void)lc_last_error_message(buffer, sizeof buffer);
    return buffer;
}

/// RAII owner for tests, the same pattern the C# SafeHandle implements.
struct ScenePtr {
    LcScene* p = nullptr;
    ScenePtr() { EXPECT_EQ(lc_scene_create(&p), LC_OK); }
    ~ScenePtr() { lc_scene_destroy(p); }
    ScenePtr(const ScenePtr&) = delete;
    ScenePtr& operator=(const ScenePtr&) = delete;
};

// 1 x 1 m grid at z = 0 centred on the origin, one cell: its centre is exactly (0, 0, 0).
LcGridDesc one_cell_at_origin() {
    return LcGridDesc{{-0.5, -0.5, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1, 1};
}

const LcSimOptions kSequential{LC_BACKEND_SEQUENTIAL, 0, 1, 0};

}  // namespace

TEST(CApi, VersionInformation) {
    EXPECT_EQ(lc_api_version(), LC_API_VERSION);
    EXPECT_STREQ(lc_version_string(), "0.1.0");
    EXPECT_STREQ(lc_status_name(LC_ERR_BUFFER_TOO_SMALL), "LC_ERR_BUFFER_TOO_SMALL");
    EXPECT_STREQ(lc_status_name(12345), "LC_UNKNOWN_STATUS");
}

TEST(CApi, CreateDestroyAndNullSafety) {
    EXPECT_EQ(lc_scene_create(nullptr), LC_ERR_NULL_ARGUMENT);
    LcScene* s = reinterpret_cast<LcScene*>(0x1);  // must be overwritten
    ASSERT_EQ(lc_scene_create(&s), LC_OK);
    ASSERT_NE(s, nullptr);
    lc_scene_destroy(s);
    lc_scene_destroy(nullptr);  // no-op
}

TEST(CApi, EveryFunctionRejectsNullScene) {
    const LcVec3 v{0, 0, 0};
    double lux = 0.0;
    const LcGridDesc grid = one_cell_at_origin();
    EXPECT_EQ(lc_scene_set_room(nullptr, 1, 1, 1), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_scene_add_point_light(nullptr, v, 1.0), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_scene_add_box(nullptr, v, v), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_scene_add_sphere(nullptr, v, 1.0), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_scene_add_triangle(nullptr, v, v, v), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_scene_get_counts(nullptr, nullptr, nullptr), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_simulate(nullptr, &grid, nullptr, &lux, 1, nullptr), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(last_error(), "scene is NULL");
}

TEST(CApi, InvalidArgumentsBecomeStatusAndMessage) {
    ScenePtr s;
    EXPECT_EQ(lc_scene_add_point_light(s.p, LcVec3{0, 0, 2}, -5.0), LC_ERR_INVALID_ARGUMENT);
    EXPECT_NE(last_error().find("intensity"), std::string::npos) << last_error();

    EXPECT_EQ(lc_scene_add_box(s.p, LcVec3{1, 1, 1}, LcVec3{0, 0, 0}), LC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(lc_scene_add_sphere(s.p, LcVec3{0, 0, 0}, 0.0), LC_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(lc_scene_add_triangle(s.p, LcVec3{0, 0, 0}, LcVec3{1, 1, 1}, LcVec3{2, 2, 2}),
              LC_ERR_INVALID_ARGUMENT);  // degenerate
    EXPECT_EQ(lc_scene_set_room(s.p, std::nan(""), 1, 1), LC_ERR_INVALID_ARGUMENT);

    // Failed calls must not have changed the scene.
    std::uint32_t lights = 99, occluders = 99;
    ASSERT_EQ(lc_scene_get_counts(s.p, &lights, &occluders), LC_OK);
    EXPECT_EQ(lights, 0u);
    EXPECT_EQ(occluders, 0u);
}

TEST(CApi, InverseSquareThroughTheDll) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_add_point_light(s.p, LcVec3{0, 0, 2}, 1000.0), LC_OK);
    const LcGridDesc grid = one_cell_at_origin();
    double lux = -1.0;
    LcSimStats stats{};
    ASSERT_EQ(lc_simulate(s.p, &grid, &kSequential, &lux, 1, &stats), LC_OK) << last_error();
    EXPECT_NEAR(lux, 250.0, 1e-12);  // E = I / h^2
    EXPECT_NEAR(stats.avg_lux, 250.0, 1e-12);
    EXPECT_EQ(stats.valid_points, 1u);
    EXPECT_GE(stats.elapsed_ms, 0.0);
}

TEST(CApi, ThreadedMatchesSequentialBitForBit) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_set_room(s.p, 6, 4, 3), LC_OK);
    for (const double x : {1.0, 3.0, 5.0}) {
        ASSERT_EQ(lc_scene_add_point_light(s.p, LcVec3{x, 2, 2.8}, 800.0), LC_OK);
    }
    ASSERT_EQ(lc_scene_add_box(s.p, LcVec3{2.5, 1.5, 0}, LcVec3{3.5, 2.5, 1.5}), LC_OK);
    ASSERT_EQ(lc_scene_add_sphere(s.p, LcVec3{4.5, 2.6, 1.9}, 0.25), LC_OK);
    const LcGridDesc grid{{0, 0, 0.75}, {6, 0, 0}, {0, 4, 0}, 61, 43};
    const std::size_t n = 61u * 43u;

    std::vector<double> seq(n), thr(n);
    ASSERT_EQ(lc_simulate(s.p, &grid, &kSequential, seq.data(), n, nullptr), LC_OK);
    const LcSimOptions threaded{LC_BACKEND_THREADED, 7, 1, 0};
    ASSERT_EQ(lc_simulate(s.p, &grid, &threaded, thr.data(), n, nullptr), LC_OK);
    EXPECT_EQ(std::memcmp(seq.data(), thr.data(), n * sizeof(double)), 0);

    // NULL options = threaded with shadows: same numbers again.
    std::vector<double> dflt(n);
    ASSERT_EQ(lc_simulate(s.p, &grid, nullptr, dflt.data(), n, nullptr), LC_OK);
    EXPECT_EQ(std::memcmp(seq.data(), dflt.data(), n * sizeof(double)), 0);

    // The box cuts the work plane: those cells are NaN (not measured).
    EXPECT_TRUE(std::isnan(seq[static_cast<std::size_t>(21) * 61 + 30]));
}

TEST(CApi, SimulationArgumentChecks) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_add_point_light(s.p, LcVec3{0, 0, 2}, 100.0), LC_OK);
    LcGridDesc grid{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 4, 3};
    std::vector<double> lux(12);

    EXPECT_EQ(lc_simulate(s.p, nullptr, nullptr, lux.data(), 12, nullptr), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, nullptr, 12, nullptr), LC_ERR_NULL_ARGUMENT);
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, lux.data(), 11, nullptr), LC_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, lux.data(), 100, nullptr), LC_OK);  // larger is fine

    const LcSimOptions bad_backend{7, 0, 1, 0};
    EXPECT_EQ(lc_simulate(s.p, &grid, &bad_backend, lux.data(), 12, nullptr),
              LC_ERR_INVALID_ARGUMENT);
    const LcSimOptions bad_reserved{LC_BACKEND_SEQUENTIAL, 0, 1, 1};
    EXPECT_EQ(lc_simulate(s.p, &grid, &bad_reserved, lux.data(), 12, nullptr),
              LC_ERR_INVALID_ARGUMENT);

    grid.nx = 0;
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, lux.data(), 12, nullptr), LC_ERR_INVALID_ARGUMENT);
    grid.nx = 4;
    grid.v_edge = LcVec3{1, 1, 0};  // not perpendicular to u_edge
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, lux.data(), 12, nullptr), LC_ERR_INVALID_ARGUMENT);
    EXPECT_NE(last_error().find("perpendicular"), std::string::npos) << last_error();
}

TEST(CApi, LightOutsideRoomIsReportedBySimulate) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_set_room(s.p, 2, 2, 2), LC_OK);
    ASSERT_EQ(lc_scene_add_point_light(s.p, LcVec3{5, 5, 5}, 100.0), LC_OK);
    const LcGridDesc grid = one_cell_at_origin();
    double lux = 0.0;
    EXPECT_EQ(lc_simulate(s.p, &grid, nullptr, &lux, 1, nullptr), LC_ERR_INVALID_ARGUMENT);
    EXPECT_NE(last_error().find("outside the room"), std::string::npos) << last_error();
}

TEST(CApi, ErrorMessageSizeQueryAndTruncation) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_add_sphere(s.p, LcVec3{0, 0, 0}, -1.0), LC_ERR_INVALID_ARGUMENT);
    const std::size_t needed = lc_last_error_message(nullptr, 0);
    const std::string full = last_error();
    EXPECT_EQ(needed, full.size() + 1);

    char tiny[5] = {'x', 'x', 'x', 'x', 'x'};
    EXPECT_EQ(lc_last_error_message(tiny, sizeof tiny), needed);
    EXPECT_EQ(std::string(tiny), full.substr(0, 4));  // truncated and NUL-terminated
}

TEST(CApi, LastErrorIsPerThread) {
    ScenePtr s;
    ASSERT_EQ(lc_scene_add_sphere(s.p, LcVec3{0, 0, 0}, -1.0), LC_ERR_INVALID_ARGUMENT);
    const std::string main_message = last_error();

    std::string other_before, other_after;
    std::thread worker([&] {
        other_before = last_error();  // nothing failed on this thread yet
        (void)lc_scene_create(nullptr);
        other_after = last_error();
    });
    worker.join();

    EXPECT_EQ(other_before, "");
    EXPECT_EQ(other_after, "out_scene is NULL");
    EXPECT_EQ(last_error(), main_message);  // unaffected by the other thread
}
