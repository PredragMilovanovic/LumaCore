#pragma once

#include <memory>

#include "lumacore/lighting/photometry.hpp"
#include "lumacore/math/vec3.hpp"

namespace lc {

/// Point source: either isotropic (the same intensity in every direction) or with the
/// measured intensity distribution of a real luminaire (EULUMDAT photometry).
///
/// A photometric light hangs pointing down (γ = 0 is -Z) and may be rotated about the
/// vertical axis: rotation_deg is the scene angle of its C0 axis, counter-clockwise from +X.
class PointLight {
public:
    /// Isotropic source. Throws std::invalid_argument unless the position is finite and the
    /// intensity is finite and >= 0 (a 0 cd source is allowed, e.g. a switched-off luminaire).
    PointLight(const Vec3& position, double intensity_cd);

    /// Photometric source. The distribution is shared, not copied (many lights may use one
    /// file). `dimming` scales the output (1 = as measured). Throws std::invalid_argument for
    /// a null distribution, a non-finite position or rotation, or dimming < 0.
    PointLight(const Vec3& position, std::shared_ptr<const Photometry> photometry,
               double rotation_deg = 0.0, double dimming = 1.0);

    [[nodiscard]] const Vec3& position() const noexcept { return position_; }
    [[nodiscard]] bool is_isotropic() const noexcept { return photometry_ == nullptr; }

    /// Isotropic intensity [cd]; 0 for photometric lights (use intensity_toward()).
    [[nodiscard]] double intensity_cd() const noexcept { return intensity_cd_; }

    /// Intensity [cd] emitted toward the unit direction `dir` (from the light, scene axes).
    [[nodiscard]] double intensity_toward(const Vec3& dir) const noexcept;

    [[nodiscard]] const std::shared_ptr<const Photometry>& photometry() const noexcept {
        return photometry_;
    }
    [[nodiscard]] double rotation_deg() const noexcept { return rotation_deg_; }
    [[nodiscard]] double dimming() const noexcept { return dimming_; }

private:
    Vec3 position_;
    double intensity_cd_ = 0.0;
    std::shared_ptr<const Photometry> photometry_;
    double rotation_deg_ = 0.0;
    double dimming_ = 1.0;
    double cos_rot_ = 1.0;  // cached for intensity_toward()
    double sin_rot_ = 0.0;
};

}  // namespace lc
