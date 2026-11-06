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

/// Shadow rays start this far (m) off the surface along its normal and stop this far
/// short of the light, so a surface never shadows itself ("shadow acne").
inline constexpr double kShadowBias = 1e-6;

/// Measurement points closer than this (m) to a point source receive no contribution:
/// the inverse-square law is singular at r -> 0 and physically meaningless well before.
inline constexpr double kMinLightDistance = 1e-3;

}  // namespace lc
