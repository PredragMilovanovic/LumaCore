#include <gtest/gtest.h>

#include <stdexcept>

#include "lumacore/lighting/illuminance.hpp"
#include "lumacore/scene/scene.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "test_helpers.hpp"

using lc::PointLight;
using lc::Scene;
using lc::Vec3;

namespace {
const Vec3 kUp{0.0, 0.0, 1.0};
const Vec3 kOrigin{0.0, 0.0, 0.0};

// One 900 cd light 3 m above the origin: unoccluded E at the origin = 100 lx.
Scene light_above_origin() {
    Scene s;
    s.add_light(PointLight(Vec3{0, 0, 3}, 900.0));
    return s;
}
}  // namespace

TEST(Scene, RoomValidation) {
    Scene s;
    EXPECT_THROW(s.set_room(0.0, 4.0, 3.0), std::invalid_argument);
    EXPECT_THROW(s.set_room(6.0, -1.0, 3.0), std::invalid_argument);
    s.set_room(6.0, 4.0, 3.0);
    s.add_light(PointLight(Vec3{3, 2, 2.8}, 500.0));
    EXPECT_NO_THROW(s.validate());
    s.add_light(PointLight(Vec3{3, 2, 3.5}, 500.0));  // above the ceiling
    EXPECT_THROW(s.validate(), std::invalid_argument);
}

TEST(Scene, ValidWithoutRoom) {
    Scene s = light_above_origin();
    EXPECT_NO_THROW(s.validate());
}

TEST(Scene, InvalidBoxLeavesSceneUnchanged) {
    Scene s;
    s.add_box(Vec3{0, 0, 0}, Vec3{1, 1, 1});
    EXPECT_THROW(s.add_box(Vec3{1, 1, 1}, Vec3{0, 0, 0}), std::invalid_argument);
    EXPECT_EQ(s.triangles().size(), 12u);  // strong exception guarantee
}

TEST(Shadows, BoxBetweenLightAndPointBlocksLight) {
    Scene s = light_above_origin();
    s.add_box(Vec3{-0.5, -0.5, 1.0}, Vec3{0.5, 0.5, 2.0});
    EXPECT_EQ(lc::illuminance_at(s, kOrigin, kUp, true), 0.0);
    EXPECT_NEAR(lc::illuminance_at(s, kOrigin, kUp, false), 100.0, 1e-12);
}

TEST(Shadows, SphereBetweenLightAndPointBlocksLight) {
    Scene s = light_above_origin();
    s.add_sphere(lc::Sphere(Vec3{0, 0, 1.5}, 0.2));
    EXPECT_EQ(lc::illuminance_at(s, kOrigin, kUp, true), 0.0);
}

TEST(Shadows, OccludersBeyondLightOrBehindPointDoNotBlock) {
    Scene s = light_above_origin();
    s.add_box(Vec3{-1, -1, 4.0}, Vec3{1, 1, 5.0});    // above the light
    s.add_box(Vec3{-1, -1, -2.0}, Vec3{1, 1, -1.0});  // below the point
    s.add_sphere(lc::Sphere(Vec3{2, 0, 1.5}, 0.5));   // off to the side
    EXPECT_NEAR(lc::illuminance_at(s, kOrigin, kUp, true), 100.0, 1e-12);
}

TEST(Shadows, SurfaceDoesNotShadowItself) {
    // Measurement point exactly on the top face of a box: the shadow-ray bias must
    // prevent the face from occluding its own point ("shadow acne").
    Scene s;
    s.add_light(PointLight(Vec3{0.3, 0.2, 4.0}, 900.0));
    s.add_box(Vec3{-1, -1, 0}, Vec3{1, 1, 1});
    const Vec3 on_top{0.25, -0.4, 1.0};
    EXPECT_GT(lc::illuminance_at(s, on_top, kUp, true), 0.0);
    EXPECT_EQ(lc::illuminance_at(s, on_top, kUp, true),
              lc::illuminance_at(s, on_top, kUp, false));
}

TEST(Shadows, PartialOcclusionBlocksOnlyThatLight) {
    Scene s;
    s.add_light(PointLight(Vec3{0, 0, 3}, 900.0));  // 100 lx at origin, will be blocked
    s.add_light(PointLight(Vec3{0, 0, 2}, 400.0));  // 100 lx at origin, below the box
    s.add_box(Vec3{-0.5, -0.5, 2.4}, Vec3{0.5, 0.5, 2.6});
    EXPECT_NEAR(lc::illuminance_at(s, kOrigin, kUp, true), 100.0, 1e-12);
    EXPECT_NEAR(lc::illuminance_at(s, kOrigin, kUp, false), 200.0, 1e-12);
}

TEST(Superposition, TotalIsSumOfIndividualLights_Randomized) {
    lc::test::Random rng;
    for (int k = 0; k < 200; ++k) {
        Scene s;
        double expected = 0.0;
        const Vec3 p = rng.vec(-1.0, 1.0);
        const int count = 1 + k % 8;
        for (int l = 0; l < count; ++l) {
            const PointLight light(rng.vec(-3.0, 3.0) + Vec3{0, 0, 4.0}, rng.uniform(0, 2000));
            expected += lc::point_illuminance(light, p, kUp);
            s.add_light(light);
        }
        EXPECT_NEAR(lc::illuminance_at(s, p, kUp, true), expected, 1e-12 * (1.0 + expected));
    }
}
