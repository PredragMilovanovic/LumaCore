// WebAssembly bindings: exposes the unchanged LumaCore engine to JavaScript (Embind).
//
// Built by scripts/build_wasm.ps1 with Emscripten into web/lumacore.js. The browser build
// runs the SequentialExecutor: browser threads need SharedArrayBuffer, which requires
// cross-origin isolation headers that static hosts such as GitHub Pages cannot send.
//
// Geometry (room, boxes, spheres, triangle meshes) and its bounding volume hierarchy are
// cached and rebuilt only after a geometry change; lights are replaced on every run. Dragging
// a lamp therefore costs one simulation, not a BVH rebuild.
//
// JavaScript usage:
//   const lc = await createLumaCore();
//   const engine = new lc.Engine();
//   engine.setRoom(6, 4, 3);
//   engine.addTriangles(new Float64Array([ax, ay, az, bx, by, bz, cx, cy, cz, ...]));
//   engine.addLight(3, 2, 2.8, 800);
//   const r = engine.simulate(0.5, 0.5, 5.5, 3.5, 0.75, 100, 60, true);
//   // r.ok, r.error, r.minLux, r.maxLux, r.avgLux, r.uniformityU0, r.validPoints,
//   // r.elapsedMs, r.lux (Float64Array view, row-major j * nx + i; valid until the next call)

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
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
    double intensity_cd;  // isotropic lights
    int photometry_id;    // -1 = isotropic
    double rotation_deg;
    double dimming;
};
struct BoxSpec {
    lc::Vec3 min;
    lc::Vec3 max;
};
struct SphereSpec {
    lc::Vec3 center;
    double radius;
};

/// All C++ exceptions are converted to { ok: false, error } or a status value, so nothing
/// throws into JavaScript.
class Engine {
public:
    // ---- Geometry (cached; any change marks it dirty) ---------------------------------

    void setRoom(double width_m, double depth_m, double height_m) {
        room_ = lc::Room{width_m, depth_m, height_m};
        has_room_ = true;
        dirty_ = true;
    }
    void clearRoom() {
        has_room_ = false;
        dirty_ = true;
    }
    void clearObstacles() {
        boxes_.clear();
        spheres_.clear();
        mesh_.clear();
        dirty_ = true;
    }
    void addBox(double x0, double y0, double z0, double x1, double y1, double z1) {
        boxes_.push_back({{x0, y0, z0}, {x1, y1, z1}});
        dirty_ = true;
    }
    void addSphere(double cx, double cy, double cz, double radius_m) {
        spheres_.push_back({{cx, cy, cz}, radius_m});
        dirty_ = true;
    }

    /// Adds triangles from a flat array [ax, ay, az, bx, by, bz, cx, cy, cz, ...] (metres).
    /// Degenerate or non-finite triangles, common in downloaded models, are skipped.
    /// Returns the number added; skippedTriangles() reports the rest.
    int addTriangles(const val& coordinates) {
        const std::vector<double> v = emscripten::convertJSArrayToNumberVector<double>(coordinates);
        int added = 0;
        for (std::size_t k = 0; k + 9 <= v.size(); k += 9) {
            try {
                mesh_.emplace_back(lc::Vec3{v[k], v[k + 1], v[k + 2]},
                                   lc::Vec3{v[k + 3], v[k + 4], v[k + 5]},
                                   lc::Vec3{v[k + 6], v[k + 7], v[k + 8]});
                ++added;
            } catch (const std::invalid_argument&) {
                ++skipped_;
            }
        }
        dirty_ = true;
        return added;
    }
    int skippedTriangles() const { return skipped_; }

    // ---- Lights (replaced on every run) -----------------------------------------------

    void clearLights() { lights_.clear(); }
    void addLight(double x, double y, double z, double intensity_cd) {
        lights_.push_back({{x, y, z}, intensity_cd, -1, 0.0, 1.0});
    }
    /// A light using a distribution previously loaded with addPhotometry().
    void addPhotometricLight(double x, double y, double z, int photometry_id, double rotation_deg,
                             double dimming) {
        lights_.push_back({{x, y, z}, 0.0, photometry_id, rotation_deg, dimming});
    }

    // ---- Photometry -------------------------------------------------------------------

    /// Parses EULUMDAT text. Returns an id >= 0, or -1 with the reason in lastError().
    int addPhotometry(const std::string& ldt_text) {
        try {
            photometries_.push_back(
                std::make_shared<const lc::Photometry>(lc::parse_eulumdat_string(ldt_text)));
            last_error_.clear();
            return static_cast<int>(photometries_.size()) - 1;
        } catch (const std::exception& e) {
            last_error_ = e.what();
            return -1;
        }
    }
    std::string lastError() const { return last_error_; }

    /// { ok, name, manufacturer, symmetry, lampFluxLm, lorPercent, fluxLm, maxCd,
    ///   lengthMm, widthMm, heightMm }
    val photometryInfo(int id) const {
        val out = val::object();
        const lc::Photometry* p = find_photometry(id);
        out.set("ok", p != nullptr);
        if (p == nullptr) {
            return out;
        }
        out.set("name", p->info().name);
        out.set("manufacturer", p->info().manufacturer);
        out.set("symmetry", static_cast<int>(p->info().symmetry));
        out.set("lampFluxLm", p->info().lamp_flux_lm);
        out.set("lorPercent", p->info().lor_percent);
        out.set("fluxLm", p->luminous_flux_lm());
        out.set("maxCd", p->max_intensity_cd());
        out.set("lengthMm", p->info().length_mm);
        out.set("widthMm", p->info().width_mm);
        out.set("heightMm", p->info().height_mm);
        return out;
    }

    /// Intensities [cd] in the half-plane C = c_deg for γ = 0..180° in `steps` equal steps
    /// (steps + 1 values), for drawing polar diagrams. Empty if the id is unknown.
    val polarCurve(int id, double c_deg, int steps) {
        curve_.clear();
        const lc::Photometry* p = find_photometry(id);
        if (p != nullptr && steps > 0 && steps <= 3600) {
            for (int k = 0; k <= steps; ++k) {
                curve_.push_back(p->intensity_cd(c_deg, 180.0 * k / steps));
            }
        }
        return val(emscripten::typed_memory_view(curve_.size(), curve_.data()));
    }

    // ---- Simulation -------------------------------------------------------------------

    /// Horizontal, upward-facing grid over [x0, x1] x [y0, y1] at height z.
    val simulate(double x0, double y0, double x1, double y1, double z, int nx, int ny,
                 bool shadows) {
        val out = val::object();
        try {
            if (nx <= 0 || ny <= 0) {
                throw std::invalid_argument("nx and ny must be > 0");
            }
            const double build_ms = prepare_scene();
            const lc::MeasurementGrid grid = lc::MeasurementGrid::horizontal(
                x0, y0, x1, y1, z, static_cast<std::size_t>(nx), static_cast<std::size_t>(ny));

            lux_.assign(grid.point_count(), 0.0);
            lc::SequentialExecutor executor;
            const lc::SimulationJob job{scene_, grid, lc::SimulationOptions{shadows}};
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
            out.set("buildMs", build_ms);
            out.set("triangles", static_cast<double>(scene_.triangles().size()));
            // Zero-copy view into WebAssembly memory. Valid until the next simulate() call
            // (or until memory grows), so JS must consume it immediately.
            out.set("lux", val(emscripten::typed_memory_view(lux_.size(), lux_.data())));
        } catch (const std::exception& e) {
            out.set("ok", false);
            out.set("error", std::string(e.what()));
        }
        return out;
    }

    /// Direct illuminance [lx] at arbitrary surface points: `points` and `normals` are flat
    /// arrays [x, y, z, ...] (normals need not be unit length). Same physics as the work plane
    /// (illuminance_at: E = I cos θ / r², shadow rays through the BVH), for walls, floor and
    /// furniture. A zero normal gives NaN. Returns { ok, error, lux (Float64Array view, valid
    /// until the next call), elapsedMs }.
    val evaluatePoints(const val& points, const val& normals, bool shadows) {
        val out = val::object();
        try {
            const std::vector<double> p = emscripten::convertJSArrayToNumberVector<double>(points);
            const std::vector<double> n = emscripten::convertJSArrayToNumberVector<double>(normals);
            if (p.size() != n.size() || p.size() % 3 != 0) {
                throw std::invalid_argument("points and normals must be flat xyz arrays of equal length");
            }
            prepare_scene();
            const auto start = std::chrono::steady_clock::now();
            surface_.assign(p.size() / 3, 0.0);
            for (std::size_t k = 0; k < surface_.size(); ++k) {
                const std::optional<lc::Vec3> unit = lc::normalized({n[3 * k], n[3 * k + 1], n[3 * k + 2]});
                surface_[k] = unit ? lc::illuminance_at(scene_, {p[3 * k], p[3 * k + 1], p[3 * k + 2]}, *unit, shadows)
                                   : std::numeric_limits<double>::quiet_NaN();
            }
            out.set("ok", true);
            out.set("elapsedMs", std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start).count());
            out.set("lux", val(emscripten::typed_memory_view(surface_.size(), surface_.data())));
        } catch (const std::exception& e) {
            out.set("ok", false);
            out.set("error", std::string(e.what()));
        }
        return out;
    }

private:
    /// Rebuilds geometry and its BVH if it changed, then replaces the lights. Returns the
    /// geometry build time in ms (0 if nothing changed).
    double prepare_scene() {
        double build_ms = 0.0;
        if (dirty_) {
            const auto start = std::chrono::steady_clock::now();
            rebuild_geometry();
            build_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count();
        }
        scene_.clear_lights();
        for (const LightSpec& l : lights_) {
            if (l.photometry_id < 0) {
                scene_.add_light(lc::PointLight(l.position, l.intensity_cd));
            } else {
                if (find_photometry(l.photometry_id) == nullptr) {
                    throw std::invalid_argument("unknown photometry id");
                }
                scene_.add_light(lc::PointLight(
                    l.position, photometries_[static_cast<std::size_t>(l.photometry_id)],
                    l.rotation_deg, l.dimming));
            }
        }
        return build_ms;
    }

    void rebuild_geometry() {
        lc::Scene scene;
        if (has_room_) {
            scene.set_room(room_.width_m, room_.depth_m, room_.height_m);
        }
        for (const BoxSpec& b : boxes_) {
            scene.add_box(b.min, b.max);
        }
        for (const SphereSpec& s : spheres_) {
            scene.add_sphere(lc::Sphere(s.center, s.radius));
        }
        scene.add_triangles(mesh_);
        scene.build_acceleration();
        scene_ = std::move(scene);  // only replaced once everything above succeeded
        dirty_ = false;
    }

    const lc::Photometry* find_photometry(int id) const {
        if (id < 0 || static_cast<std::size_t>(id) >= photometries_.size()) {
            return nullptr;
        }
        return photometries_[static_cast<std::size_t>(id)].get();
    }

    std::vector<std::shared_ptr<const lc::Photometry>> photometries_;
    std::string last_error_;
    std::vector<double> curve_;
    lc::Room room_{0.0, 0.0, 0.0};
    bool has_room_ = false;
    std::vector<LightSpec> lights_;
    std::vector<BoxSpec> boxes_;
    std::vector<SphereSpec> spheres_;
    std::vector<lc::Triangle> mesh_;
    int skipped_ = 0;
    lc::Scene scene_;
    bool dirty_ = true;
    std::vector<double> lux_;
    std::vector<double> surface_;
};

std::string version() { return lc::version_string(); }

}  // namespace

EMSCRIPTEN_BINDINGS(lumacore) {
    emscripten::class_<Engine>("Engine")
        .constructor<>()
        .function("setRoom", &Engine::setRoom)
        .function("clearRoom", &Engine::clearRoom)
        .function("clearObstacles", &Engine::clearObstacles)
        .function("addBox", &Engine::addBox)
        .function("addSphere", &Engine::addSphere)
        .function("addTriangles", &Engine::addTriangles)
        .function("skippedTriangles", &Engine::skippedTriangles)
        .function("clearLights", &Engine::clearLights)
        .function("addLight", &Engine::addLight)
        .function("addPhotometricLight", &Engine::addPhotometricLight)
        .function("addPhotometry", &Engine::addPhotometry)
        .function("lastError", &Engine::lastError)
        .function("photometryInfo", &Engine::photometryInfo)
        .function("polarCurve", &Engine::polarCurve)
        .function("simulate", &Engine::simulate)
        .function("evaluatePoints", &Engine::evaluatePoints);
    emscripten::function("version", &version);
}
