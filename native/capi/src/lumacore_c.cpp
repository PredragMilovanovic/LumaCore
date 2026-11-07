// C API implementation: translates between the C ABI and the C++ engine. No engine logic
// lives here - only argument checks, type conversion and exception-to-status mapping.

#include "lumacore_c.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>

#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/parallel/threaded_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "lumacore/version.hpp"

// ---- ABI layout lock ---------------------------------------------------------
// The C# side mirrors these layouts (and tests Marshal.SizeOf against the same numbers).
static_assert(sizeof(LcVec3) == 24, "LcVec3 layout changed: bump LC_API_VERSION");
static_assert(sizeof(LcGridDesc) == 80, "LcGridDesc layout changed: bump LC_API_VERSION");
static_assert(offsetof(LcGridDesc, nx) == 72 && offsetof(LcGridDesc, ny) == 76,
              "LcGridDesc layout changed: bump LC_API_VERSION");
static_assert(sizeof(LcSimOptions) == 16, "LcSimOptions layout changed: bump LC_API_VERSION");
static_assert(sizeof(LcSimStats) == 48, "LcSimStats layout changed: bump LC_API_VERSION");
static_assert(offsetof(LcSimStats, valid_points) == 40,
              "LcSimStats layout changed: bump LC_API_VERSION");

/// The opaque handle. The magic value lets us reject some invalid handles (e.g. a pointer
/// to something else, or a destroyed scene whose memory was not reused); it is a
/// best-effort check, not a guarantee - using a dangling handle remains undefined.
struct LcScene {
    static constexpr std::uint32_t kAlive = 0x4C554D41u;  // "LUMA"
    std::uint32_t magic = kAlive;
    lc::Scene scene;
};

namespace {

// ---- Thread-local last error (fixed buffer: setting it never allocates or throws) ----

constexpr std::size_t kErrorCapacity = 512;
thread_local char t_last_error[kErrorCapacity] = "";

void set_error(const char* message) noexcept {
    if (message == nullptr) {
        message = "unknown error";
    }
    const std::size_t n = std::min(std::strlen(message), kErrorCapacity - 1);
    std::memcpy(t_last_error, message, n);
    t_last_error[n] = '\0';
}

LcStatus fail(LcStatus status, const char* message) noexcept {
    set_error(message);
    return status;
}

/// Runs `body` and maps every exception to a status code. Nothing escapes.
template <class Body>
LcStatus guarded(Body&& body) noexcept {
    try {
        return body();
    } catch (const std::invalid_argument& e) {
        return fail(LC_ERR_INVALID_ARGUMENT, e.what());
    } catch (const std::bad_alloc&) {
        return fail(LC_ERR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::system_error& e) {
        return fail(LC_ERR_INTERNAL, e.what());  // e.g. a worker thread failed to start
    } catch (const std::exception& e) {
        return fail(LC_ERR_INTERNAL, e.what());
    } catch (...) {
        return fail(LC_ERR_INTERNAL, "unknown C++ exception");
    }
}

lc::Vec3 to_vec(const LcVec3& v) noexcept { return lc::Vec3{v.x, v.y, v.z}; }

bool is_live(const LcScene* s) noexcept { return s->magic == LcScene::kAlive; }

#define LC_REQUIRE_SCENE(s)                                                            \
    do {                                                                               \
        if ((s) == nullptr) return fail(LC_ERR_NULL_ARGUMENT, "scene is NULL");        \
        if (!is_live(s)) return fail(LC_ERR_INVALID_ARGUMENT, "scene handle is invalid"); \
    } while (false)

std::uint32_t clamp_u32(std::size_t n) noexcept {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(n, std::numeric_limits<std::uint32_t>::max()));
}

}  // namespace

extern "C" {

// ---- Library ---------------------------------------------------------------------

LC_API std::uint32_t LC_CALL lc_api_version(void) { return LC_API_VERSION; }

LC_API const char* LC_CALL lc_version_string(void) { return lc::version_string(); }

LC_API const char* LC_CALL lc_status_name(LcStatus status) {
    switch (status) {
        case LC_OK: return "LC_OK";
        case LC_ERR_NULL_ARGUMENT: return "LC_ERR_NULL_ARGUMENT";
        case LC_ERR_INVALID_ARGUMENT: return "LC_ERR_INVALID_ARGUMENT";
        case LC_ERR_BUFFER_TOO_SMALL: return "LC_ERR_BUFFER_TOO_SMALL";
        case LC_ERR_OUT_OF_MEMORY: return "LC_ERR_OUT_OF_MEMORY";
        case LC_ERR_INTERNAL: return "LC_ERR_INTERNAL";
        default: return "LC_UNKNOWN_STATUS";
    }
}

LC_API size_t LC_CALL lc_last_error_message(char* buffer, size_t buffer_size) {
    const std::size_t needed = std::strlen(t_last_error) + 1;
    if (buffer != nullptr && buffer_size > 0) {
        const std::size_t n = std::min(needed - 1, buffer_size - 1);
        std::memcpy(buffer, t_last_error, n);
        buffer[n] = '\0';
    }
    return needed;
}

// ---- Scene -----------------------------------------------------------------------

LC_API LcStatus LC_CALL lc_scene_create(LcScene** out_scene) {
    if (out_scene == nullptr) {
        return fail(LC_ERR_NULL_ARGUMENT, "out_scene is NULL");
    }
    *out_scene = nullptr;
    return guarded([&] {
        *out_scene = new LcScene();
        return LC_OK;
    });
}

LC_API void LC_CALL lc_scene_destroy(LcScene* scene) {
    if (scene == nullptr || !is_live(scene)) {
        return;  // NULL or (detectably) already destroyed: nothing to do
    }
    scene->magic = 0;
    delete scene;
}

LC_API LcStatus LC_CALL lc_scene_set_room(LcScene* scene, double width_m, double depth_m,
                                          double height_m) {
    LC_REQUIRE_SCENE(scene);
    return guarded([&] {
        scene->scene.set_room(width_m, depth_m, height_m);
        return LC_OK;
    });
}

LC_API LcStatus LC_CALL lc_scene_add_point_light(LcScene* scene, LcVec3 position,
                                                 double intensity_cd) {
    LC_REQUIRE_SCENE(scene);
    return guarded([&] {
        scene->scene.add_light(lc::PointLight(to_vec(position), intensity_cd));
        return LC_OK;
    });
}

LC_API LcStatus LC_CALL lc_scene_add_box(LcScene* scene, LcVec3 min, LcVec3 max) {
    LC_REQUIRE_SCENE(scene);
    return guarded([&] {
        scene->scene.add_box(to_vec(min), to_vec(max));
        return LC_OK;
    });
}

LC_API LcStatus LC_CALL lc_scene_add_sphere(LcScene* scene, LcVec3 center, double radius_m) {
    LC_REQUIRE_SCENE(scene);
    return guarded([&] {
        scene->scene.add_sphere(lc::Sphere(to_vec(center), radius_m));
        return LC_OK;
    });
}

LC_API LcStatus LC_CALL lc_scene_add_triangle(LcScene* scene, LcVec3 a, LcVec3 b, LcVec3 c) {
    LC_REQUIRE_SCENE(scene);
    return guarded([&] {
        scene->scene.add_triangle(lc::Triangle(to_vec(a), to_vec(b), to_vec(c)));
        return LC_OK;
    });
}

LC_API LcStatus LC_CALL lc_scene_get_counts(const LcScene* scene, uint32_t* out_lights,
                                            uint32_t* out_occluders) {
    LC_REQUIRE_SCENE(scene);
    if (out_lights != nullptr) *out_lights = clamp_u32(scene->scene.lights().size());
    if (out_occluders != nullptr) *out_occluders = clamp_u32(scene->scene.occluder_count());
    return LC_OK;
}

// ---- Simulation ------------------------------------------------------------------

LC_API LcStatus LC_CALL lc_simulate(const LcScene* scene, const LcGridDesc* grid,
                                    const LcSimOptions* options, double* out_lux,
                                    size_t out_count, LcSimStats* out_stats) {
    LC_REQUIRE_SCENE(scene);
    if (grid == nullptr) return fail(LC_ERR_NULL_ARGUMENT, "grid is NULL");
    if (out_lux == nullptr) return fail(LC_ERR_NULL_ARGUMENT, "out_lux is NULL");

    const LcSimOptions defaults{LC_BACKEND_THREADED, 0u, 1, 0};
    const LcSimOptions& opt = options != nullptr ? *options : defaults;
    if (opt.backend != LC_BACKEND_SEQUENTIAL && opt.backend != LC_BACKEND_THREADED) {
        return fail(LC_ERR_INVALID_ARGUMENT, "options.backend must be LC_BACKEND_SEQUENTIAL or "
                                             "LC_BACKEND_THREADED");
    }
    if (opt.reserved != 0) {
        return fail(LC_ERR_INVALID_ARGUMENT, "options.reserved must be 0");
    }

    return guarded([&] {
        const lc::MeasurementGrid g(to_vec(grid->origin), to_vec(grid->u_edge),
                                    to_vec(grid->v_edge), grid->nx, grid->ny);
        if (out_count < g.point_count()) {
            return fail(LC_ERR_BUFFER_TOO_SMALL, "out_count is smaller than nx * ny");
        }
        const lc::SimulationJob job{scene->scene, g, lc::SimulationOptions{opt.shadows != 0}};

        lc::RunSummary summary;
        if (opt.backend == LC_BACKEND_SEQUENTIAL) {
            lc::SequentialExecutor executor;
            summary = lc::simulate_into(job, executor, out_lux, g.point_count());
        } else {
            const lc::ThreadedExecutor executor(opt.thread_count);
            summary = lc::simulate_into(job, executor, out_lux, g.point_count());
        }

        if (out_stats != nullptr) {
            out_stats->min_lux = summary.stats.min_lux;
            out_stats->max_lux = summary.stats.max_lux;
            out_stats->avg_lux = summary.stats.avg_lux;
            out_stats->uniformity_u0 = summary.stats.uniformity_u0;
            out_stats->elapsed_ms = summary.elapsed_ms;
            out_stats->valid_points = summary.stats.valid_points;
        }
        return LC_OK;
    });
}

}  // extern "C"
