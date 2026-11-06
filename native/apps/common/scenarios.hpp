#pragma once

// Built-in lighting scenarios shared by lumacore_cli and lumacore_bench.

#include <optional>
#include <string>
#include <vector>

#include "lumacore/scene/measurement_grid.hpp"
#include "lumacore/scene/scene.hpp"

namespace lc::apps {

struct Box {
    Vec3 min;
    Vec3 max;
};
struct Ball {
    Vec3 center;
    double radius;
};
struct Lamp {
    Vec3 position;
    double intensity_cd;
};

/// Scenario description kept separately from lc::Scene so it can be written to the CSV
/// header (the Scene stores boxes only as triangles).
struct Scenario {
    std::string name;
    double width_m;
    double depth_m;
    double height_m;
    double workplane_m;
    std::vector<Lamp> lamps;
    std::vector<Box> boxes;
    std::vector<Ball> balls;
};

/// 6 x 4 x 3 m office: 2 x 3 luminaires (800 cd) at 2.8 m, a cabinet, a partition and a
/// pendant sphere; work plane at 0.75 m.
[[nodiscard]] Scenario office_scenario();

/// One 1000 cd source 2 m above the work plane: every point has a closed-form answer.
[[nodiscard]] Scenario single_scenario();

/// "office" or "single"; nullopt for an unknown name.
[[nodiscard]] std::optional<Scenario> find_scenario(const std::string& name);

[[nodiscard]] Scene build_scene(const Scenario& scenario);

/// Horizontal work-plane grid covering the room minus a border band of `border_m` along
/// every wall, with cells of about `cell_m`. Throws std::invalid_argument for a cell size
/// outside [0.001, 1] m or a border that leaves no area.
[[nodiscard]] MeasurementGrid workplane_grid(const Scenario& scenario, double cell_m,
                                             double border_m);

}  // namespace lc::apps
