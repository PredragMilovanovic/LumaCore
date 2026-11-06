#pragma once

namespace lc {

inline constexpr double kPi = 3.14159265358979323846;

// Numerical tolerances. Scene units are metres; the values below are chosen for
// geometry in the 1 mm .. 1 km range and are documented in docs/physics.md.

/// Vectors shorter than this are treated as zero and cannot be normalized.
inline constexpr double kMinNormalizableLength = 1e-12;

/// |n . d| below this means a ray is treated as parallel to a plane.
inline constexpr double kParallelEpsilon = 1e-12;

/// |det| below this in Moller-Trumbore means the ray is parallel to the triangle.
inline constexpr double kDeterminantEpsilon = 1e-12;

/// Triangles whose |e1 x e2| (twice the area, m^2) is below this are degenerate.
inline constexpr double kMinTriangleDoubleArea = 1e-12;

}  // namespace lc
