#pragma once

#include "lumacore/lighting/point_light.hpp"
#include "lumacore/math/vec3.hpp"

namespace lc {

/// Direct illuminance [lx] at point p on a surface with unit normal n from one
/// isotropic point source, ignoring occlusion:
///
///     E = I * cos(theta) / r^2
///
/// I [cd], r = |light - p| [m], cos(theta) = n . (light - p) / r.
/// Returns 0 when the source is behind or level with the surface (cos(theta) <= 0)
/// or closer than kMinLightDistance. Precondition: |n| == 1.
[[nodiscard]] double point_illuminance(const PointLight& light, const Vec3& p,
                                       const Vec3& n) noexcept;

}  // namespace lc
