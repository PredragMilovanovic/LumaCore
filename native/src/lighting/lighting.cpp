#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

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

PointLight::PointLight(const Vec3& position, std::shared_ptr<const Photometry> photometry,
                       double rotation_deg, double dimming)
    : position_(position),
      photometry_(std::move(photometry)),
      rotation_deg_(rotation_deg),
      dimming_(dimming) {
    if (!is_finite(position)) {
        throw std::invalid_argument("PointLight: position must be finite");
    }
    if (!photometry_) {
        throw std::invalid_argument("PointLight: photometry must not be null");
    }
    if (!std::isfinite(rotation_deg)) {
        throw std::invalid_argument("PointLight: rotation_deg must be finite");
    }
    if (!std::isfinite(dimming) || dimming < 0.0) {
        throw std::invalid_argument("PointLight: dimming must be finite and >= 0");
    }
    const double a = rotation_deg * kPi / 180.0;
    cos_rot_ = std::cos(a);
    sin_rot_ = std::sin(a);
}

double PointLight::intensity_toward(const Vec3& dir) const noexcept {
    if (!photometry_) {
        return intensity_cd_;
    }
    // Scene direction -> luminaire frame: undo the rotation about the vertical axis.
    const double lx = cos_rot_ * dir.x + sin_rot_ * dir.y;
    const double ly = -sin_rot_ * dir.x + cos_rot_ * dir.y;
    double c_deg = std::atan2(ly, lx) * 180.0 / kPi;
    if (c_deg < 0.0) {
        c_deg += 360.0;
    }
    // γ is measured from straight down (-Z).
    const double gamma_deg = std::acos(std::clamp(-dir.z, -1.0, 1.0)) * 180.0 / kPi;
    return dimming_ * photometry_->intensity_cd(c_deg, gamma_deg);
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
    // Isotropic lights skip the direction lookup, so their results are unchanged bit for bit.
    const double intensity =
        light.is_isotropic() ? light.intensity_cd() : light.intensity_toward(-to_light / r);
    return intensity * n_dot_l / (r_squared * r);
}

}  // namespace lc
