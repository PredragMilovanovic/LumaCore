#include "lumacore/geometry/primitives.hpp"

#include <stdexcept>

namespace lc {
namespace {

// Two unit normals closer than this (1 - cos) are considered the same direction.
constexpr double kPlanarityTolerance = 1e-9;

}  // namespace

std::array<Triangle, 2> quad_triangles(const Vec3& a, const Vec3& b, const Vec3& c,
                                       const Vec3& d) {
    std::array<Triangle, 2> tris{Triangle(a, b, c), Triangle(a, c, d)};
    if (dot(tris[0].normal(), tris[1].normal()) < 1.0 - kPlanarityTolerance) {
        throw std::invalid_argument("quad_triangles: quad is not planar and convex");
    }
    return tris;
}

std::vector<Triangle> box_triangles(const Vec3& min, const Vec3& max) {
    if (!is_finite(min) || !is_finite(max)) {
        throw std::invalid_argument("box_triangles: bounds must be finite");
    }
    if (!(min.x < max.x && min.y < max.y && min.z < max.z)) {
        throw std::invalid_argument("box_triangles: min must be < max on every axis");
    }

    const double x0 = min.x, y0 = min.y, z0 = min.z;
    const double x1 = max.x, y1 = max.y, z1 = max.z;

    // Each face is listed counter-clockwise as seen from outside the box.
    const Vec3 faces[6][4] = {
        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}},  // -X
        {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}},  // +X
        {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}},  // -Y
        {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}},  // +Y
        {{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}},  // -Z
        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}},  // +Z
    };

    std::vector<Triangle> result;
    result.reserve(12);
    for (const auto& f : faces) {
        for (const Triangle& t : quad_triangles(f[0], f[1], f[2], f[3])) {
            result.push_back(t);
        }
    }
    return result;
}

}  // namespace lc
