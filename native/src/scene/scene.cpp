#include "lumacore/scene/scene.hpp"

#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include "lumacore/geometry/intersection.hpp"
#include "lumacore/geometry/primitives.hpp"

namespace lc {

void Scene::set_room(double width_m, double depth_m, double height_m) {
    for (const double d : {width_m, depth_m, height_m}) {
        if (!std::isfinite(d) || !(d > 0.0)) {
            throw std::invalid_argument("Scene::set_room: dimensions must be finite and > 0");
        }
    }
    room_ = Room{width_m, depth_m, height_m};
}

void Scene::add_light(const PointLight& light) { lights_.push_back(light); }

void Scene::add_triangle(const Triangle& triangle) { triangles_.push_back(triangle); }

void Scene::add_sphere(const Sphere& sphere) { spheres_.push_back(sphere); }

void Scene::add_box(const Vec3& min, const Vec3& max) {
    const std::vector<Triangle> box = box_triangles(min, max);  // validates before mutating
    boxes_.reserve(boxes_.size() + 1);  // may throw; nothing modified yet
    triangles_.insert(triangles_.end(), box.begin(), box.end());
    boxes_.push_back(Aabb{min, max});  // cannot throw after the reserve
}

bool Scene::inside_solid(const Vec3& p) const noexcept {
    for (const Aabb& b : boxes_) {
        if (b.contains_strictly(p)) {
            return true;
        }
    }
    for (const Sphere& s : spheres_) {
        if (length_squared(p - s.center()) < s.radius() * s.radius()) {
            return true;
        }
    }
    return false;
}

bool Scene::occluded(const Ray& ray, double max_distance) const noexcept {
    for (const Triangle& t : triangles_) {
        if (intersect(ray, t, 0.0, max_distance)) {
            return true;
        }
    }
    for (const Sphere& s : spheres_) {
        if (intersect(ray, s, 0.0, max_distance)) {
            return true;
        }
    }
    return false;
}

void Scene::validate() const {
    if (!room_) {
        return;
    }
    for (std::size_t k = 0; k < lights_.size(); ++k) {
        if (!room_->contains(lights_[k].position())) {
            const Vec3& p = lights_[k].position();
            throw std::invalid_argument("Scene: light " + std::to_string(k) + " at (" +
                                        std::to_string(p.x) + ", " + std::to_string(p.y) + ", " +
                                        std::to_string(p.z) + ") is outside the room");
        }
    }
}

}  // namespace lc
