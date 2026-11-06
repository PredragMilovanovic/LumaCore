#include "lumacore/math/ray.hpp"

#include <cassert>
#include <stdexcept>

namespace lc {
namespace {

Vec3 checked_unit_direction(const Vec3& origin, const Vec3& direction) {
    if (!is_finite(origin)) {
        throw std::invalid_argument("Ray: origin must be finite");
    }
    const std::optional<Vec3> unit = normalized(direction);
    if (!unit) {
        throw std::invalid_argument("Ray: direction must be finite and non-zero");
    }
    return *unit;
}

}  // namespace

Ray::Ray(const Vec3& origin, const Vec3& direction)
    : origin_(origin), direction_(checked_unit_direction(origin, direction)) {}

Ray Ray::from_unit_direction(const Vec3& origin, const Vec3& unit_direction) noexcept {
    assert(is_finite(origin));
    assert(std::abs(length(unit_direction) - 1.0) < 1e-9);
    return Ray(origin, unit_direction, Unchecked{});
}

}  // namespace lc
