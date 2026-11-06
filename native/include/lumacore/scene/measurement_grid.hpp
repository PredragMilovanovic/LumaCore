#pragma once

#include <cstddef>

#include "lumacore/math/vec3.hpp"

namespace lc {

/// Planar rectangular grid of measurement points (calculation surface).
///
/// The rectangle spans origin .. origin + u_edge + v_edge and is divided into nx x ny
/// equal cells; the measurement point of each cell is its center (midpoint rule).
/// Points are stored row-major: index(i, j) = j * nx + i, i along u, j along v.
/// The surface normal is normalize(u_edge x v_edge): light arriving from that side counts.
class MeasurementGrid {
public:
    /// Upper bound on nx * ny, keeps allocations sane (8e8 bytes of doubles).
    static constexpr std::size_t kMaxPoints = 100'000'000;

    /// Throws std::invalid_argument if inputs are not finite, an edge is (near) zero,
    /// the edges are not perpendicular, nx or ny is 0, or nx * ny > kMaxPoints.
    MeasurementGrid(const Vec3& origin, const Vec3& u_edge, const Vec3& v_edge, std::size_t nx,
                    std::size_t ny);

    /// Horizontal work plane over [x0, x1] x [y0, y1] at height z, facing up (+Z).
    [[nodiscard]] static MeasurementGrid horizontal(double x0, double y0, double x1, double y1,
                                                    double z, std::size_t nx, std::size_t ny);

    [[nodiscard]] std::size_t nx() const noexcept { return nx_; }
    [[nodiscard]] std::size_t ny() const noexcept { return ny_; }
    [[nodiscard]] std::size_t point_count() const noexcept { return nx_ * ny_; }
    [[nodiscard]] std::size_t index(std::size_t i, std::size_t j) const noexcept {
        return j * nx_ + i;
    }

    /// Center of cell (i, j). Precondition: i < nx, j < ny.
    [[nodiscard]] Vec3 point(std::size_t i, std::size_t j) const noexcept {
        return origin_ + u_step_ * (static_cast<double>(i) + 0.5) +
               v_step_ * (static_cast<double>(j) + 0.5);
    }

    [[nodiscard]] const Vec3& normal() const noexcept { return normal_; }
    [[nodiscard]] const Vec3& origin() const noexcept { return origin_; }
    [[nodiscard]] const Vec3& u_edge() const noexcept { return u_edge_; }
    [[nodiscard]] const Vec3& v_edge() const noexcept { return v_edge_; }
    /// Area of one cell [m^2]; sum(E_i * cell_area) approximates the flux on the grid [lm].
    [[nodiscard]] double cell_area() const noexcept;

private:
    Vec3 origin_;
    Vec3 u_edge_;
    Vec3 v_edge_;
    std::size_t nx_;
    std::size_t ny_;
    Vec3 u_step_;
    Vec3 v_step_;
    Vec3 normal_;
};

}  // namespace lc
