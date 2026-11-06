#pragma once

#include <optional>

#include "lumacore/geometry/plane.hpp"
#include "lumacore/geometry/sphere.hpp"
#include "lumacore/geometry/triangle.hpp"
#include "lumacore/math/ray.hpp"

namespace lc {

/// Result of a ray-primitive query.
struct Hit {
    double t;     ///< Ray parameter = distance from the ray origin in metres.
    Vec3 point;   ///< ray.at(t)
    Vec3 normal;  ///< Unit geometric normal of the primitive (NOT flipped toward the ray).
};

// All queries report the nearest hit with t in the OPEN interval (t_min, t_max) and
// are two-sided (no back-face culling), which is what shadow rays need. A NaN bound
// or t_min >= t_max yields no hit. Rays lying inside a plane/triangle report no hit.

/// Ray-plane: t = (offset - n.o) / (n.d); no hit if |n.d| < kParallelEpsilon.
[[nodiscard]] std::optional<Hit> intersect(const Ray& ray, const Plane& plane, double t_min,
                                           double t_max) noexcept;

/// Ray-sphere: roots of t^2 + 2(oc.d)t + (|oc|^2 - r^2) = 0 using the cancellation-free
/// form. From inside the sphere the exit point is returned (normal still points outward).
[[nodiscard]] std::optional<Hit> intersect(const Ray& ray, const Sphere& sphere, double t_min,
                                           double t_max) noexcept;

/// Ray-triangle: Moller-Trumbore. Edges and vertices count as inside (closed triangle),
/// so meshes sharing edges have no gaps.
[[nodiscard]] std::optional<Hit> intersect(const Ray& ray, const Triangle& triangle, double t_min,
                                           double t_max) noexcept;

}  // namespace lc
