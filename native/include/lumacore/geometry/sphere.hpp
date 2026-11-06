#pragma once

#include "lumacore/math/vec3.hpp"

namespace lc {

class Sphere {
public:
    /// Throws std::invalid_argument unless the center is finite and radius is finite and > 0.
    Sphere(const Vec3& center, double radius);

    [[nodiscard]] const Vec3& center() const noexcept { return center_; }
    [[nodiscard]] double radius() const noexcept { return radius_; }

private:
    Vec3 center_;
    double radius_;
};

}  // namespace lc
