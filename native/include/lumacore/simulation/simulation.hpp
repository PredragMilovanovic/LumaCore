#pragma once

#include <cstddef>
#include <vector>

#include "lumacore/scene/measurement_grid.hpp"
#include "lumacore/scene/scene.hpp"

namespace lc {

struct SimulationOptions {
    bool shadows = true;  ///< false: every light reaches every point (no occlusion test)
};

/// What to compute. Holds a non-owning reference: the Scene must outlive the job and
/// must not be modified while a simulation runs.
struct SimulationJob {
    const Scene& scene;
    MeasurementGrid grid;
    SimulationOptions options;
};

/// Summary metrics over the measured (non-NaN) grid points.
struct IlluminanceStats {
    double min_lux = 0.0;
    double max_lux = 0.0;
    double avg_lux = 0.0;
    double uniformity_u0 = 0.0;    ///< E_min / E_avg (EN 12464-1); 0 if E_avg == 0
    std::size_t valid_points = 0;  ///< points included; 0 means all were excluded
};

struct SimulationResult {
    std::size_t nx = 0;
    std::size_t ny = 0;
    /// Row-major, index = j * nx + i (see MeasurementGrid). NaN marks a point that is
    /// not measured because it lies inside a solid occluder.
    std::vector<double> lux;
    IlluminanceStats stats;
    double elapsed_ms = 0.0;  ///< wall time of the executor run only
};

/// Direct illuminance [lx] at p (unit normal n): sum over lights in scene order of
/// I cos(theta) / r^2, skipping lights blocked by an occluder when `shadows` is true.
/// The fixed summation order makes results bit-reproducible across executors.
[[nodiscard]] double illuminance_at(const Scene& scene, const Vec3& p, const Vec3& n,
                                    bool shadows) noexcept;

/// Fills out[index(i, j)] for all rows j in [row_begin, row_end). The single kernel
/// every executor calls. Points inside a solid occluder get NaN (not measured).
/// Precondition: row_end <= grid.ny(), out has point_count() slots.
void compute_rows(const SimulationJob& job, std::size_t row_begin, std::size_t row_end,
                  double* out) noexcept;

/// Strategy interface for how the rows are scheduled (sequential, threads, GPU, ...).
class IExecutor {
public:
    virtual ~IExecutor() = default;
    IExecutor() = default;
    IExecutor(const IExecutor&) = delete;
    IExecutor& operator=(const IExecutor&) = delete;

    /// Must write all job.grid.point_count() values of `out`.
    virtual void run(const SimulationJob& job, double* out) const = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

/// Computes min / max / avg / U0 over the non-NaN values (all zero if none).
/// Throws std::invalid_argument if `lux` is empty.
[[nodiscard]] IlluminanceStats compute_stats(const std::vector<double>& lux);

/// Validates the scene, runs the executor, times it and computes statistics.
/// Throws std::invalid_argument for an invalid scene.
[[nodiscard]] SimulationResult simulate(const SimulationJob& job, const IExecutor& executor);

}  // namespace lc
