#pragma once

#include "lumacore/math/vec3.hpp"

namespace lc {

/// Isotropic point source: the same luminous intensity in every direction.
class PointLight {
public:
    /// Throws std::invalid_argument unless the position is finite and the intensity is
    /// finite and >= 0 (a 0 cd source is allowed, e.g. a switched-off luminaire).
    PointLight(const Vec3& position, double intensity_cd);

    [[nodiscard]] const Vec3& position() const noexcept { return position_; }
    [[nodiscard]] double intensity_cd() const noexcept { return intensity_cd_; }

private:
    Vec3 position_;
    double intensity_cd_;
};

}  // namespace lc
