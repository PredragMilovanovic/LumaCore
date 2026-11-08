// WebAssembly bindings: exposes the unchanged LumaCore engine to JavaScript (Embind).
//
// Built by scripts/build_wasm.ps1 with Emscripten into web/lumacore.js. The browser build
// runs the SequentialExecutor: browser threads need SharedArrayBuffer, which requires
// cross-origin isolation headers that static hosts such as GitHub Pages cannot send.
//
// JavaScript usage:
//   const lc = await createLumaCore();
//   const engine = new lc.Engine();
//   engine.setRoom(6, 4, 3);
//   engine.addLight(3, 2, 2.8, 800);
//   const r = engine.simulate(0.5, 0.5, 5.5, 3.5, 0.75, 100, 60, true);
//   // r.ok, r.error, r.minLux, r.maxLux, r.avgLux, r.uniformityU0, r.validPoints,
//   // r.elapsedMs, r.lux (Float64Array view, row-major j * nx + i; valid until the next call)

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "lumacore/version.hpp"

namespace {

using emscripten::val;

struct LightSpec {
    lc::Vec3 position;
    double intensity_cd;
};
struct BoxSpec {
    lc::Vec3 min;
    lc::Vec3 max;
};
struct SphereSpec {
    lc::Vec3 center;
    double radius;
};

/// Holds the scene description; builds an lc::Scene for every simulate() call. Rebuilding is
/// cheap (tens of objects) and keeps add/remove simple while lights are dragged around.
/// All C++ exceptions are converted to { ok: false, error } so nothing throws into JS.
class Engine {
public:
    void setRoom(double width_m, double depth_m, double height_m) {
        room_ = lc::Room{width_m, depth_m, height_m};
        has_room_ = true;
    }
    void clearRoom() { has_room_ = false; }

    void clearLights() { lights_.clear(); }
    void addLight(double x, double y, double z, double intensity_cd) {
        lights_.push_back({{x, y, z}, intensity_cd});
    }

    void clearObstacles() {
        boxes_.clear();
        spheres_.clear();
    }
    void addBox(double x0, double y0, double z0, double x1, double y1, double z1) {
        boxes_.push_back({{x0, y0, z0}, {x1, y1, z1}});
    }
    void addSphere(double cx, double cy, double cz, double radius_m) {
        spheres_.push_back({{cx, cy, cz}, radius_m});
    }

    /// Horizontal, upward-facing grid over [x0, x1] x [y0, y1] at height z.
    val simulate(double x0, double y0, double x1, double y1, double z, int nx, int ny,
                 bool shadows) {
        val out = val::object();
        try {
            if (nx <= 0 || ny <= 0) {
                throw std::invalid_argument("nx and ny must be > 0");
            }
            const lc::Scene scene = build_scene();
            const lc::MeasurementGrid grid = lc::MeasurementGrid::horizontal(
                x0, y0, x1, y1, z, static_cast<std::size_t>(nx), static_cast<std::size_t>(ny));

            lux_.assign(grid.point_count(), 0.0);
            lc::SequentialExecutor executor;
            const lc::SimulationJob job{scene, grid, lc::SimulationOptions{shadows}};
            const lc::RunSummary s = lc::simulate_into(job, executor, lux_.data(), lux_.size());

            out.set("ok", true);
            out.set("nx", nx);
            out.set("ny", ny);
            out.set("minLux", s.stats.min_lux);
            out.set("maxLux", s.stats.max_lux);
            out.set("avgLux", s.stats.avg_lux);
            out.set("uniformityU0", s.stats.uniformity_u0);
            out.set("validPoints", static_cast<double>(s.stats.valid_points));
            out.set("elapsedMs", s.elapsed_ms);
            // Zero-copy view into WebAssembly memory. Valid until the next simulate() call
            // (or until memory grows), so JS must consume it immediately.
            out.set("lux", val(emscripten::typed_memory_view(lux_.size(), lux_.data())));
        } catch (const std::exception& e) {
            out.set("ok", false);
            out.set("error", std::string(e.what()));
        }
        return out;
    }

private:
    lc::Scene build_scene() const {
        lc::Scene scene;
        if (has_room_) {
            scene.set_room(room_.width_m, room_.depth_m, room_.height_m);
        }
        for (const LightSpec& l : lights_) {
            scene.add_light(lc::PointLight(l.position, l.intensity_cd));
        }
        for (const BoxSpec& b : boxes_) {
            scene.add_box(b.min, b.max);
        }
        for (const SphereSpec& s : spheres_) {
            scene.add_sphere(lc::Sphere(s.center, s.radius));
        }
        return scene;
    }

    lc::Room room_{0.0, 0.0, 0.0};
    bool has_room_ = false;
    std::vector<LightSpec> lights_;
    std::vector<BoxSpec> boxes_;
    std::vector<SphereSpec> spheres_;
    std::vector<double> lux_;
};

std::string version() { return lc::version_string(); }

}  // namespace

EMSCRIPTEN_BINDINGS(lumacore) {
    emscripten::class_<Engine>("Engine")
        .constructor<>()
        .function("setRoom", &Engine::setRoom)
        .function("clearRoom", &Engine::clearRoom)
        .function("clearLights", &Engine::clearLights)
        .function("addLight", &Engine::addLight)
        .function("clearObstacles", &Engine::clearObstacles)
        .function("addBox", &Engine::addBox)
        .function("addSphere", &Engine::addSphere)
        .function("simulate", &Engine::simulate);
    emscripten::function("version", &version);
}
