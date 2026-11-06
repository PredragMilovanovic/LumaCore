#pragma once

#include <array>
#include <vector>

#include "lumacore/geometry/triangle.hpp"

namespace lc {

/// Planar convex quad a-b-c-d (in order around the boundary) split into triangles
/// (a,b,c) and (a,c,d). Normal follows the right-hand rule. Throws std::invalid_argument
/// if the quad is degenerate or not planar (the two triangle normals disagree).
[[nodiscard]] std::array<Triangle, 2> quad_triangles(const Vec3& a, const Vec3& b, const Vec3& c,
                                                     const Vec3& d);

/// Axis-aligned box as 12 triangles with outward-facing normals.
/// Throws std::invalid_argument unless min < max on every axis and both are finite.
[[nodiscard]] std::vector<Triangle> box_triangles(const Vec3& min, const Vec3& max);

}  // namespace lc
