#include "lumacore/simulation/simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "lumacore/lighting/illuminance.hpp"
#include "lumacore/math/constants.hpp"

namespace lc {

double illuminance_at(const Scene& scene, const Vec3& p, const Vec3& n, bool shadows) noexcept {
    double total = 0.0;
    for (const PointLight& light : scene.lights()) {
        const double e = point_illuminance(light, p, n);
        if (e <= 0.0) {
            continue;  // behind the surface, too close, or a 0 cd source: no shadow ray
        }
        if (shadows) {
            const Vec3 to_light = light.position() - p;
            const double r = length(to_light);
            // r >= kMinLightDistance here (point_illuminance returned > 0), so r > 0.
            const Ray shadow_ray = Ray::from_unit_direction(p + n * kShadowBias, to_light / r);
            if (scene.occluded(shadow_ray, r - kShadowBias)) {
                continue;
            }
        }
        total += e;
    }
    return total;
}

void compute_rows(const SimulationJob& job, std::size_t row_begin, std::size_t row_end,
                  double* out) noexcept {
    const MeasurementGrid& grid = job.grid;
    const Vec3& n = grid.normal();
    constexpr double kNotMeasured = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t j = row_begin; j < row_end; ++j) {
        for (std::size_t i = 0; i < grid.nx(); ++i) {
            const Vec3 p = grid.point(i, j);
            out[grid.index(i, j)] = job.scene.inside_solid(p)
                                        ? kNotMeasured
                                        : illuminance_at(job.scene, p, n, job.options.shadows);
        }
    }
}

IlluminanceStats compute_stats(const std::vector<double>& lux) {
    if (lux.empty()) {
        throw std::invalid_argument("compute_stats: no values");
    }
    IlluminanceStats s;
    double sum = 0.0;
    for (const double e : lux) {
        if (std::isnan(e)) {
            continue;
        }
        if (s.valid_points == 0) {
            s.min_lux = s.max_lux = e;
        } else {
            s.min_lux = std::min(s.min_lux, e);
            s.max_lux = std::max(s.max_lux, e);
        }
        sum += e;
        ++s.valid_points;
    }
    if (s.valid_points > 0) {
        s.avg_lux = sum / static_cast<double>(s.valid_points);
        s.uniformity_u0 = s.avg_lux > 0.0 ? s.min_lux / s.avg_lux : 0.0;
    }
    return s;
}

SimulationResult simulate(const SimulationJob& job, const IExecutor& executor) {
    job.scene.validate();

    SimulationResult result;
    result.nx = job.grid.nx();
    result.ny = job.grid.ny();
    result.lux.assign(job.grid.point_count(), 0.0);

    const auto start = std::chrono::steady_clock::now();
    executor.run(job, result.lux.data());
    const auto stop = std::chrono::steady_clock::now();

    result.elapsed_ms = std::chrono::duration<double, std::milli>(stop - start).count();
    result.stats = compute_stats(result.lux);
    return result;
}

}  // namespace lc
