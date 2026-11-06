#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "lumacore/geometry/sphere.hpp"
#include "lumacore/geometry/triangle.hpp"
#include "lumacore/lighting/point_light.hpp"
#include "lumacore/math/ray.hpp"

namespace lc {

/// Axis-aligned rectangular room [0, width] x [0, depth] x [0, height] in metres.
/// The room is convex, so its own surfaces can never block a straight path between two
/// interior points; they are therefore not occluders. (They would matter for
/// inter-reflections, which this direct-light model does not compute.)
struct Room {
    double width_m;
    double depth_m;
    double height_m;

    [[nodiscard]] bool contains(const Vec3& p) const noexcept {
        return p.x >= 0.0 && p.x <= width_m && p.y >= 0.0 && p.y <= depth_m && p.z >= 0.0 &&
               p.z <= height_m;
    }
};

/// Solid axis-aligned box, kept alongside its triangles for point-inside queries.
struct Aabb {
    Vec3 min;
    Vec3 max;

    /// Strict interior: points on the surface (e.g. a desk top) are outside.
    [[nodiscard]] bool contains_strictly(const Vec3& p) const noexcept {
        return p.x > min.x && p.x < max.x && p.y > min.y && p.y < max.y && p.z > min.z &&
               p.z < max.z;
    }
};

/// Lights plus opaque occluders. Owns everything by value. Not thread-safe to modify;
/// read-only use (all const members) from many threads is safe, which is how the
/// simulation uses it.
class Scene {
public:
    /// Throws std::invalid_argument unless all dimensions are finite and > 0.
    void set_room(double width_m, double depth_m, double height_m);
    [[nodiscard]] const std::optional<Room>& room() const noexcept { return room_; }

    void add_light(const PointLight& light);
    void add_triangle(const Triangle& triangle);
    void add_sphere(const Sphere& sphere);
    /// Axis-aligned box occluder (12 triangles). Throws like box_triangles().
    void add_box(const Vec3& min, const Vec3& max);

    [[nodiscard]] const std::vector<PointLight>& lights() const noexcept { return lights_; }
    [[nodiscard]] const std::vector<Triangle>& triangles() const noexcept { return triangles_; }
    [[nodiscard]] const std::vector<Sphere>& spheres() const noexcept { return spheres_; }
    [[nodiscard]] std::size_t occluder_count() const noexcept {
        return triangles_.size() + spheres_.size();
    }

    /// True if any occluder is hit with t in (0, max_distance). Any-hit query with early
    /// exit: cheaper than finding the nearest hit, which shadows do not need.
    [[nodiscard]] bool occluded(const Ray& ray, double max_distance) const noexcept;

    /// True if p lies strictly inside a solid occluder (box or sphere). Such points are
    /// not measurable surfaces. Loose triangles are open surfaces and never contain p.
    [[nodiscard]] bool inside_solid(const Vec3& p) const noexcept;

    /// Checks cross-object consistency (e.g. every light inside the room, if one is set).
    /// Throws std::invalid_argument with a descriptive message on failure.
    void validate() const;

private:
    std::optional<Room> room_;
    std::vector<PointLight> lights_;
    std::vector<Triangle> triangles_;
    std::vector<Sphere> spheres_;
    std::vector<Aabb> boxes_;
};

}  // namespace lc
