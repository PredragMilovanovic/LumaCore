#pragma once

#include "lumacore/math/vec3.hpp"

namespace lc {

/// Half-line origin + t * direction (t >= 0). Invariant: direction has unit length,
/// so the ray parameter t is a distance in metres.
class Ray {
public:
    /// Normalizes `direction`. Throws std::invalid_argument if the origin is not finite
    /// or the direction cannot be normalized (zero, tiny or non-finite).
    Ray(const Vec3& origin, const Vec3& direction);

    /// Unchecked construction for hot paths. Precondition: origin finite and
    /// |unit_direction| == 1 (asserted in debug builds).
    [[nodiscard]] static Ray from_unit_direction(const Vec3& origin,
                                                 const Vec3& unit_direction) noexcept;

    [[nodiscard]] const Vec3& origin() const noexcept { return origin_; }
    [[nodiscard]] const Vec3& direction() const noexcept { return direction_; }

    /// Point at parameter t (distance t from the origin).
    [[nodiscard]] Vec3 at(double t) const noexcept { return origin_ + direction_ * t; }

private:
    struct Unchecked {};
    Ray(const Vec3& origin, const Vec3& unit_direction, Unchecked) noexcept
        : origin_(origin), direction_(unit_direction) {}

    Vec3 origin_;
    Vec3 direction_;
};

}  // namespace lc
