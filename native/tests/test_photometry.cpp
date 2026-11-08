#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumacore/lighting/illuminance.hpp"
#include "lumacore/lighting/photometry.hpp"
#include "lumacore/lighting/point_light.hpp"
#include "lumacore/math/constants.hpp"
#include "test_helpers.hpp"

using lc::Photometry;
using lc::PointLight;
using lc::Vec3;

namespace {

/// Writes a minimal but complete EULUMDAT file. `planes` holds the stored C-planes (as the
/// symmetry requires) in cd per 1000 lm.
std::string make_ldt(int isym, const std::vector<double>& c_angles,
                     const std::vector<double>& gammas,
                     const std::vector<std::vector<double>>& planes, double lamp_flux = 1000.0,
                     bool decimal_comma = false) {
    std::ostringstream o;
    o.precision(17);
    const auto num = [&](double v) {
        std::ostringstream s;
        s.precision(17);
        s << v;
        std::string t = s.str();
        if (decimal_comma) {
            for (char& ch : t) {
                if (ch == '.') ch = ',';
            }
        }
        o << t << "\n";
    };
    o << "Test Company\n" << (isym == 1 ? 1 : 3) << "\n" << isym << "\n" << c_angles.size() << "\n";
    num(c_angles.size() > 1 ? c_angles[1] - c_angles[0] : 0.0);
    o << gammas.size() << "\n";
    num(gammas[1] - gammas[0]);
    o << "report-1\nTest Luminaire\nTL-1\ntest.ldt\n2026-11-08\n";
    for (int k = 0; k < 9; ++k) num(k < 3 ? 100.0 : 50.0);  // dimensions [mm]
    num(100.0);   // DFF
    num(80.0);    // LORL
    num(1.0);     // conversion factor
    num(0.0);     // tilt
    o << "1\n1\nLED\n";
    num(lamp_flux);
    o << "4000K\n80\n";
    num(25.0);
    for (int k = 0; k < 10; ++k) num(0.5);  // direct ratios
    for (const double c : c_angles) num(c);
    for (const double g : gammas) num(g);
    for (const auto& plane : planes) {
        for (const double v : plane) num(v);
    }
    return o.str();
}

std::vector<double> steps(double from, double to, double step) {
    std::vector<double> v;
    for (double x = from; x <= to + 1e-9; x += step) v.push_back(x);
    return v;
}

const std::vector<double> kC24 = steps(0, 345, 15);  // Mc = 24, Dc = 15°

/// Direction with the given C (scene, no rotation) and γ (from straight down).
Vec3 dir_cg(double c_deg, double g_deg) {
    const double c = c_deg * lc::kPi / 180.0, g = g_deg * lc::kPi / 180.0;
    return {std::sin(g) * std::cos(c), std::sin(g) * std::sin(c), -std::cos(g)};
}

}  // namespace

TEST(Photometry, IsotropicFileIsConstantAndConservesFlux) {
    const double v = 1000.0 / (4.0 * lc::kPi);  // cd/klm of a 1000 lm isotropic source
    const auto p = lc::parse_eulumdat_string(
        make_ldt(1, {0.0}, steps(0, 180, 10), {std::vector<double>(19, v)}));
    lc::test::Random rng;
    for (int k = 0; k < 100; ++k) {
        EXPECT_NEAR(p.intensity_cd(rng.uniform(0, 360), rng.uniform(0, 180)), v, 1e-12);
    }
    EXPECT_NEAR(p.luminous_flux_lm(), 1000.0, 1e-3 * 1000.0);  // 4 pi I
    EXPECT_EQ(p.info().name, "Test Luminaire");
    EXPECT_EQ(p.info().symmetry, Photometry::Symmetry::Rotational);
}

TEST(Photometry, LambertianDownlightFluxIsPiTimesPeak) {
    // I(γ) = I0 cos γ below the horizon, 0 above: Φ = π I0.
    const double i0 = 1000.0 / lc::kPi;
    const auto gammas = steps(0, 180, 5);
    std::vector<double> plane;
    for (const double g : gammas) plane.push_back(g <= 90.0 ? i0 * std::cos(g * lc::kPi / 180) : 0.0);
    const auto p = lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, {plane}));
    EXPECT_NEAR(p.intensity_cd(0, 0), i0, 1e-9);
    EXPECT_NEAR(p.intensity_cd(123, 60), i0 * 0.5, 1e-9);  // a sample angle: exact
    EXPECT_EQ(p.intensity_cd(0, 120), 0.0);
    EXPECT_NEAR(p.luminous_flux_lm(), 1000.0, 5e-3 * 1000.0);  // linear interpolation of cos
}

TEST(Photometry, ScalesByLampFluxAndAcceptsDecimalComma) {
    const auto gammas = steps(0, 90, 30);
    const std::vector<std::vector<double>> plane{{100.5, 80.25, 40.0, 10.0}};
    const auto p1 = lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, plane, 1000.0));
    const auto p3 = lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, plane, 3000.0));
    const auto pc = lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, plane, 3000.0, true));
    EXPECT_DOUBLE_EQ(p1.intensity_cd(0, 30), 80.25);
    EXPECT_DOUBLE_EQ(p3.intensity_cd(0, 30), 3.0 * 80.25);  // cd/klm x 3 klm
    EXPECT_DOUBLE_EQ(pc.intensity_cd(0, 30), p3.intensity_cd(0, 30));
    EXPECT_EQ(p1.intensity_cd(0, 120), 0.0);  // beyond the measured 90°
    EXPECT_DOUBLE_EQ(p1.intensity_cd(0, 90), 10.0);
}

TEST(Photometry, SymmetryBothPlanesExpandsTheQuadrant) {
    // Stored planes C0..C90; value = 100 + C, constant over γ.
    std::vector<std::vector<double>> planes;
    for (double c = 0; c <= 90; c += 15) planes.push_back(std::vector<double>(10, 100.0 + c));
    const auto p = lc::parse_eulumdat_string(make_ldt(4, kC24, steps(0, 90, 10), planes));
    EXPECT_DOUBLE_EQ(p.intensity_cd(30, 45), 130.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(150, 45), 130.0);  // mirrored at C90
    EXPECT_DOUBLE_EQ(p.intensity_cd(210, 45), 130.0);  // mirrored at C180
    EXPECT_DOUBLE_EQ(p.intensity_cd(330, 45), 130.0);  // mirrored at C0
    EXPECT_DOUBLE_EQ(p.intensity_cd(270, 45), 190.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(180, 45), 100.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(37.5, 45), 137.5);  // between C30 and C45
    EXPECT_EQ(p.c_angles_deg().size(), 24u);           // full circle after expansion
}

TEST(Photometry, SymmetryPlaneC0C180) {
    std::vector<std::vector<double>> planes;
    for (double c = 0; c <= 180; c += 15) planes.push_back(std::vector<double>(4, 100.0 + c));
    const auto p = lc::parse_eulumdat_string(make_ldt(2, kC24, steps(0, 90, 30), planes));
    EXPECT_DOUBLE_EQ(p.intensity_cd(200, 30), p.intensity_cd(160, 30));
    EXPECT_DOUBLE_EQ(p.intensity_cd(160, 30), 260.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(345, 30), 115.0);  // = C15
}

TEST(Photometry, SymmetryPlaneC90C270) {
    // Stored planes run C270 .. C345, C0 .. C90; value = 100 + C, with C in [-90, 90].
    std::vector<std::vector<double>> planes;
    for (double c = 270; c < 360; c += 15) planes.push_back(std::vector<double>(4, 100.0 + c - 360));
    for (double c = 0; c <= 90; c += 15) planes.push_back(std::vector<double>(4, 100.0 + c));
    const auto p = lc::parse_eulumdat_string(make_ldt(3, kC24, steps(0, 90, 30), planes));
    EXPECT_DOUBLE_EQ(p.intensity_cd(60, 30), 160.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(120, 30), 160.0);  // I(C) = I(180 - C)
    EXPECT_DOUBLE_EQ(p.intensity_cd(340, 30), 80.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(200, 30), 80.0);
}

TEST(Photometry, NoSymmetryWrapsAround360) {
    const std::vector<std::vector<double>> planes{
        {10, 10}, {20, 20}, {30, 30}, {40, 40}};  // C0, C90, C180, C270
    const auto p = lc::parse_eulumdat_string(make_ldt(0, {0, 90, 180, 270}, {0, 90}, planes));
    EXPECT_DOUBLE_EQ(p.intensity_cd(45, 0), 15.0);
    EXPECT_DOUBLE_EQ(p.intensity_cd(315, 0), 25.0);  // between C270 (40) and C360 = C0 (10)
    EXPECT_DOUBLE_EQ(p.intensity_cd(-45, 0), 25.0);  // negative C wraps too
}

TEST(Photometry, MalformedFilesAreRejectedWithTheLine) {
    const auto gammas = steps(0, 90, 30);
    const std::vector<std::vector<double>> plane{{1, 2, 3, 4}};
    std::string text = make_ldt(1, {0.0}, gammas, plane);

    std::string bad_symmetry = text;
    bad_symmetry.replace(bad_symmetry.find("\n1\n1\n") + 3, 1, "7");  // line 3: Isym = 7
    try {
        (void)lc::parse_eulumdat_string(bad_symmetry);
        FAIL() << "expected an exception";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("line 3"), std::string::npos) << e.what();
    }

    EXPECT_THROW((void)lc::parse_eulumdat_string(text.substr(0, text.size() / 2)),
                 std::invalid_argument);  // truncated
    EXPECT_THROW((void)lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, {{1, -2, 3, 4}})),
                 std::invalid_argument);  // negative intensity
    EXPECT_THROW((void)lc::parse_eulumdat_string(make_ldt(4, steps(0, 300, 60), gammas, {{1, 2, 3, 4}})),
                 std::invalid_argument);  // Mc = 6 is not divisible by 4
    try {  // line 5 (Dc) is not a number
        (void)lc::parse_eulumdat_string("Company\n1\n1\n1\nzero\n");
        FAIL() << "expected an exception";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("expected a number"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("line 5"), std::string::npos) << e.what();
    }
    EXPECT_THROW((void)lc::load_eulumdat("does/not/exist.ldt"), std::invalid_argument);
}

TEST(Photometry, SampleFilesConserveFlux) {
    // tools/make_sample_ldt.py normalizes each sample so that luminaire flux = LOR x lamp flux.
    // The C++ integral (independent of the Python one) must agree.
    struct Sample {
        const char* file;
        double lamp_flux;
        double lor;
    };
    for (const Sample s : {Sample{"downlight_wide.ldt", 2000, 90}, Sample{"spot_narrow.ldt", 1500, 85},
                           Sample{"linear_batwing.ldt", 4000, 80}}) {
        const auto p = lc::load_eulumdat(std::string(LUMACORE_SAMPLES_DIR) + "/ldt/" + s.file);
        EXPECT_DOUBLE_EQ(p.info().lamp_flux_lm, s.lamp_flux) << s.file;
        EXPECT_DOUBLE_EQ(p.info().lor_percent, s.lor) << s.file;
        const double expected = s.lamp_flux * s.lor / 100.0;
        EXPECT_NEAR(p.luminous_flux_lm(), expected, 1e-3 * expected) << s.file;
    }
}

TEST(Photometry, LinearSampleIsWiderAcrossThanAlong) {
    const auto p = lc::load_eulumdat(std::string(LUMACORE_SAMPLES_DIR) + "/ldt/linear_batwing.ldt");
    EXPECT_EQ(p.info().symmetry, Photometry::Symmetry::BothPlanes);
    // At 50° from vertical the batwing (C90, across) is much stronger than along the length (C0).
    EXPECT_GT(p.intensity_cd(90, 50), 1.5 * p.intensity_cd(0, 50));
    EXPECT_DOUBLE_EQ(p.intensity_cd(90, 50), p.intensity_cd(270, 50));
    EXPECT_EQ(p.intensity_cd(0, 135), 0.0);  // no upward light
}

TEST(PhotometricLight, RotationTurnsTheC0Axis) {
    std::vector<std::vector<double>> planes;
    for (double c = 0; c <= 90; c += 15) planes.push_back(std::vector<double>(10, 100.0 + c));
    const auto dist = std::make_shared<const Photometry>(
        lc::parse_eulumdat_string(make_ldt(4, kC24, steps(0, 90, 10), planes)));

    const PointLight straight(Vec3{0, 0, 3}, dist);
    const PointLight turned(Vec3{0, 0, 3}, dist, 90.0);
    const PointLight dimmed(Vec3{0, 0, 3}, dist, 0.0, 0.5);
    const Vec3 along_x = dir_cg(0, 45), along_y = dir_cg(90, 45);
    EXPECT_NEAR(straight.intensity_toward(along_x), 100.0, 1e-9);  // C0
    EXPECT_NEAR(straight.intensity_toward(along_y), 190.0, 1e-9);  // C90
    EXPECT_NEAR(turned.intensity_toward(along_y), 100.0, 1e-9);    // C0 now points along +Y
    EXPECT_NEAR(turned.intensity_toward(along_x), 190.0, 1e-9);
    EXPECT_NEAR(dimmed.intensity_toward(along_x), 50.0, 1e-9);
    EXPECT_FALSE(straight.is_isotropic());

    EXPECT_THROW(PointLight(Vec3{}, std::shared_ptr<const Photometry>{}), std::invalid_argument);
    EXPECT_THROW(PointLight(Vec3{}, dist, 0.0, -1.0), std::invalid_argument);
}

TEST(PhotometricLight, IlluminanceUnderALambertianDownlight) {
    // I(γ) = I0 cos γ. On a horizontal plane at depth h and offset d: γ = θ, so
    // E = I0 cos γ · cos θ / r² = I0 h² / r⁴. Offsets chosen so γ hits sample angles.
    const double i0 = 1000.0 / lc::kPi, h = 2.0;
    const auto gammas = steps(0, 180, 5);
    std::vector<double> plane;
    for (const double g : gammas) plane.push_back(g <= 90.0 ? i0 * std::cos(g * lc::kPi / 180) : 0.0);
    const auto dist = std::make_shared<const Photometry>(
        lc::parse_eulumdat_string(make_ldt(1, {0.0}, gammas, {plane})));
    const PointLight light(Vec3{0, 0, h}, dist);
    const Vec3 up{0, 0, 1};

    EXPECT_NEAR(lc::point_illuminance(light, Vec3{0, 0, 0}, up), i0 / (h * h), 1e-9);
    for (const double g_deg : {30.0, 45.0, 60.0}) {
        const double d = h * std::tan(g_deg * lc::kPi / 180.0);
        const double r2 = h * h + d * d;
        const double expected = i0 * h * h / (r2 * r2);
        EXPECT_NEAR(lc::point_illuminance(light, Vec3{d, 0, 0}, up), expected, 1e-9 * expected)
            << g_deg << " degrees";
    }
}

TEST(PhotometricLight, IsotropicFileMatchesIsotropicLight) {
    const double v = 500.0;
    const auto dist = std::make_shared<const Photometry>(lc::parse_eulumdat_string(
        make_ldt(1, {0.0}, steps(0, 180, 15), {std::vector<double>(13, v)})));
    const PointLight from_file(Vec3{1, 2, 3}, dist);
    const PointLight ideal(Vec3{1, 2, 3}, v);
    lc::test::Random rng;
    for (int k = 0; k < 200; ++k) {
        const Vec3 p = rng.vec(-3, 2);
        const Vec3 n = rng.unit();
        const double a = lc::point_illuminance(from_file, p, n);
        EXPECT_NEAR(a, lc::point_illuminance(ideal, p, n), 1e-12 * (1.0 + a));
    }
}
