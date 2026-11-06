#include "scenarios.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lc::apps {

Scenario office_scenario() {
    Scenario s{"office", 6.0, 4.0, 3.0, 0.75, {}, {}, {}};
    for (const double y : {1.0, 3.0}) {
        for (const double x : {1.0, 3.0, 5.0}) {
            s.lamps.push_back({{x, y, 2.8}, 800.0});
        }
    }
    s.boxes.push_back({{0.05, 2.6, 0.0}, {0.65, 3.95, 2.0}});  // tall cabinet by the wall
    s.boxes.push_back({{2.9, 0.2, 0.0}, {3.1, 1.6, 1.6}});     // desk partition screen
    s.balls.push_back({{4.5, 2.6, 1.9}, 0.25});                 // pendant globe (opaque)
    return s;
}

Scenario single_scenario() {
    Scenario s{"single", 4.0, 4.0, 3.0, 0.75, {}, {}, {}};
    s.lamps.push_back({{2.0, 2.0, 2.75}, 1000.0});
    return s;
}

std::optional<Scenario> find_scenario(const std::string& name) {
    if (name == "office") return office_scenario();
    if (name == "single") return single_scenario();
    return std::nullopt;
}

Scene build_scene(const Scenario& sc) {
    Scene scene;
    scene.set_room(sc.width_m, sc.depth_m, sc.height_m);
    for (const Lamp& l : sc.lamps) scene.add_light(PointLight(l.position, l.intensity_cd));
    for (const Box& b : sc.boxes) scene.add_box(b.min, b.max);
    for (const Ball& b : sc.balls) scene.add_sphere(Sphere(b.center, b.radius));
    return scene;
}

MeasurementGrid workplane_grid(const Scenario& sc, double cell_m, double border_m) {
    if (!(cell_m >= 0.001 && cell_m <= 1.0)) {
        throw std::invalid_argument("cell size must be between 0.001 and 1 m");
    }
    if (!(border_m >= 0.0) || 2.0 * border_m >= std::min(sc.width_m, sc.depth_m)) {
        throw std::invalid_argument("border must be >= 0 and leave a non-empty area");
    }
    const double area_w = sc.width_m - 2.0 * border_m;
    const double area_d = sc.depth_m - 2.0 * border_m;
    const auto nx = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(area_w / cell_m)));
    const auto ny = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(area_d / cell_m)));
    return MeasurementGrid::horizontal(border_m, border_m, sc.width_m - border_m,
                                       sc.depth_m - border_m, sc.workplane_m, nx, ny);
}

}  // namespace lc::apps
