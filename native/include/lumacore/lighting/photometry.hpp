#pragma once

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace lc {

/// Luminous intensity distribution of a real luminaire, as published in EULUMDAT (.ldt) files.
///
/// Angles use the C-γ system of the luminaire:
///   γ = 0° points straight down (nadir), 90° horizontal, 180° straight up;
///   C = 0° is the luminaire's reference axis (its local +X, usually the length of a linear
///   luminaire), C = 90° its local +Y; C increases counter-clockwise seen from above.
/// Intensities are absolute [cd], already scaled by the lamp flux.
class Photometry {
public:
    /// EULUMDAT symmetry indicator (line 3 of the file).
    enum class Symmetry {
        None = 0,          ///< all C-planes measured
        Rotational = 1,    ///< same in every C-plane
        PlaneC0C180 = 2,   ///< mirror-symmetric to the C0-C180 plane
        PlaneC90C270 = 3,  ///< mirror-symmetric to the C90-C270 plane
        BothPlanes = 4,    ///< symmetric to both planes (one quadrant measured)
    };

    struct Info {
        std::string name;          ///< luminaire name (line 9)
        std::string manufacturer;  ///< company identification (line 1)
        Symmetry symmetry = Symmetry::Rotational;
        double lamp_flux_lm = 0.0;  ///< total lamp flux of lamp set 1
        double lor_percent = 0.0;   ///< light output ratio of the luminaire
    };

    /// Builds a distribution from a full table: `c_deg` strictly increasing in [0, 360),
    /// `gamma_deg` strictly increasing in [0, 180], `intensity_cd[k][g]` for plane k, angle g.
    /// A single plane means rotational symmetry. Throws std::invalid_argument on bad input.
    Photometry(Info info, std::vector<double> c_deg, std::vector<double> gamma_deg,
               std::vector<std::vector<double>> intensity_cd);

    /// Intensity [cd] in direction (C, γ), bilinear in C (wrapping around 360°) and γ.
    /// Directions beyond the last measured γ get 0 (e.g. above a downlight measured to 90°).
    [[nodiscard]] double intensity_cd(double c_deg, double gamma_deg) const noexcept;

    /// Total luminous flux leaving the luminaire [lm]: numerical integral of I over the
    /// sphere (Φ = ∫∫ I sin γ dγ dC). For a consistent file ≈ lamp flux × LOR.
    [[nodiscard]] double luminous_flux_lm() const noexcept;

    [[nodiscard]] double max_intensity_cd() const noexcept;
    [[nodiscard]] const Info& info() const noexcept { return info_; }
    [[nodiscard]] const std::vector<double>& c_angles_deg() const noexcept { return c_deg_; }
    [[nodiscard]] const std::vector<double>& gamma_angles_deg() const noexcept { return gamma_deg_; }

private:
    [[nodiscard]] double intensity_in_plane(std::size_t plane, double gamma_deg) const noexcept;

    Info info_;
    std::vector<double> c_deg_;
    std::vector<double> gamma_deg_;
    std::vector<std::vector<double>> table_;
};

/// Parses an EULUMDAT file. Intensities in the file are cd per 1000 lm of lamp flux; they are
/// converted to cd with the flux of lamp set 1 and the conversion factor (line 24), and the
/// symmetry is expanded to the full 0-360° range. Accepts '.' or ',' as decimal separator.
/// Throws std::invalid_argument naming the offending line on malformed input.
[[nodiscard]] Photometry parse_eulumdat(std::istream& in);

/// Convenience overloads: from a string, or from a file path.
[[nodiscard]] Photometry parse_eulumdat_string(const std::string& text);
[[nodiscard]] Photometry load_eulumdat(const std::string& path);

}  // namespace lc
