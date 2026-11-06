#include <cmath>
#include <stdexcept>

#include "lumacore/lighting/illuminance.hpp"
#include "lumacore/lighting/point_light.hpp"
#include "lumacore/math/constants.hpp"

namespace lc {

PointLight::PointLight(const Vec3& position, double intensity_cd)
    : position_(position), intensity_cd_(intensity_cd) {
    if (!is_finite(position)) {
        throw std::invalid_argument("PointLight: position must be finite");
    }
    if (!std::isfinite(intensity_cd) || intensity_cd < 0.0) {
        throw std::invalid_argument("PointLight: intensity_cd must be finite and >= 0");
    }
}

double point_illuminance(const PointLight& light, const Vec3& p, const Vec3& n) noexcept {
    const Vec3 to_light = light.position() - p;
    const double r_squared = length_squared(to_light);
    if (r_squared < kMinLightDistance * kMinLightDistance) {
        return 0.0;
    }
    // cos(theta) / r^2 = (n . to_light / r) / r^2 = (n . to_light) / r^3
    const double n_dot_l = dot(n, to_light);
    if (n_dot_l <= 0.0) {
        return 0.0;
    }
    const double r = std::sqrt(r_squared);
    return light.intensity_cd() * n_dot_l / (r_squared * r);
}

}  // namespace lc
