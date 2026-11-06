#pragma once

#include "lumacore/math/vec3.hpp"

namespace lc {

/// Infinite plane { x : dot(normal, x) == offset } with a unit normal.
class Plane {
public:
    /// Plane through `point` with the given normal (normalized internally).
    /// Throws std::invalid_argument if the point is not finite or the normal is degenerate.
    Plane(const Vec3& point, const Vec3& normal);

    [[nodiscard]] const Vec3& normal() const noexcept { return normal_; }
    [[nodiscard]] double offset() const noexcept { return offset_; }

    /// Positive on the side the normal points to; magnitude is the distance in metres.
    [[nodiscard]] double signed_distance(const Vec3& p) const noexcept {
        return dot(normal_, p) - offset_;
    }

private:
    Vec3 normal_;
    double offset_;
};

}  // namespace lc
