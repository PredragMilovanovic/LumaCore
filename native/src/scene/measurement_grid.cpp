#include "lumacore/scene/measurement_grid.hpp"

#include <cmath>
#include <stdexcept>

namespace lc {
namespace {

// |cos| of the angle between the edges must be below this to count as perpendicular.
constexpr double kPerpendicularTolerance = 1e-9;

}  // namespace

MeasurementGrid::MeasurementGrid(const Vec3& origin, const Vec3& u_edge, const Vec3& v_edge,
                                 std::size_t nx, std::size_t ny)
    : origin_(origin), u_edge_(u_edge), v_edge_(v_edge), nx_(nx), ny_(ny) {
    if (!is_finite(origin) || !is_finite(u_edge) || !is_finite(v_edge)) {
        throw std::invalid_argument("MeasurementGrid: inputs must be finite");
    }
    const std::optional<Vec3> u_dir = normalized(u_edge);
    const std::optional<Vec3> v_dir = normalized(v_edge);
    if (!u_dir || !v_dir) {
        throw std::invalid_argument("MeasurementGrid: edges must be non-zero");
    }
    if (std::abs(dot(*u_dir, *v_dir)) > kPerpendicularTolerance) {
        throw std::invalid_argument("MeasurementGrid: edges must be perpendicular");
    }
    if (nx == 0 || ny == 0) {
        throw std::invalid_argument("MeasurementGrid: nx and ny must be >= 1");
    }
    if (nx > kMaxPoints / ny) {  // overflow-safe form of nx * ny > kMaxPoints
        throw std::invalid_argument("MeasurementGrid: too many points");
    }
    u_step_ = u_edge / static_cast<double>(nx);
    v_step_ = v_edge / static_cast<double>(ny);
    normal_ = cross(*u_dir, *v_dir);  // unit: the inputs are unit and perpendicular
}

MeasurementGrid MeasurementGrid::horizontal(double x0, double y0, double x1, double y1, double z,
                                            std::size_t nx, std::size_t ny) {
    if (!(x1 > x0) || !(y1 > y0)) {
        throw std::invalid_argument("MeasurementGrid::horizontal: need x1 > x0 and y1 > y0");
    }
    return MeasurementGrid(Vec3{x0, y0, z}, Vec3{x1 - x0, 0.0, 0.0}, Vec3{0.0, y1 - y0, 0.0}, nx,
                           ny);
}

double MeasurementGrid::cell_area() const noexcept {
    return length(cross(u_step_, v_step_));
}

}  // namespace lc
