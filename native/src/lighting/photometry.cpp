#include "lumacore/lighting/photometry.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "lumacore/math/constants.hpp"

namespace lc {

// ---- Photometry ----------------------------------------------------------------------

Photometry::Photometry(Info info, std::vector<double> c_deg, std::vector<double> gamma_deg,
                       std::vector<std::vector<double>> intensity_cd)
    : info_(std::move(info)),
      c_deg_(std::move(c_deg)),
      gamma_deg_(std::move(gamma_deg)),
      table_(std::move(intensity_cd)) {
    if (c_deg_.empty()) {
        throw std::invalid_argument("Photometry: at least one C-plane is required");
    }
    for (std::size_t k = 0; k < c_deg_.size(); ++k) {
        if (!(c_deg_[k] >= 0.0 && c_deg_[k] < 360.0) || (k > 0 && !(c_deg_[k] > c_deg_[k - 1]))) {
            throw std::invalid_argument("Photometry: C angles must increase strictly within [0, 360)");
        }
    }
    if (gamma_deg_.size() < 2) {
        throw std::invalid_argument("Photometry: at least two gamma angles are required");
    }
    for (std::size_t g = 0; g < gamma_deg_.size(); ++g) {
        if (!(gamma_deg_[g] >= 0.0 && gamma_deg_[g] <= 180.0) ||
            (g > 0 && !(gamma_deg_[g] > gamma_deg_[g - 1]))) {
            throw std::invalid_argument(
                "Photometry: gamma angles must increase strictly within [0, 180]");
        }
    }
    if (table_.size() != c_deg_.size()) {
        throw std::invalid_argument("Photometry: one intensity row per C-plane is required");
    }
    for (const auto& row : table_) {
        if (row.size() != gamma_deg_.size()) {
            throw std::invalid_argument("Photometry: each C-plane needs one value per gamma angle");
        }
        for (const double v : row) {
            if (!std::isfinite(v) || v < 0.0) {
                throw std::invalid_argument("Photometry: intensities must be finite and >= 0");
            }
        }
    }
}

double Photometry::intensity_in_plane(std::size_t plane, double gamma_deg) const noexcept {
    const std::vector<double>& row = table_[plane];
    const auto it = std::upper_bound(gamma_deg_.begin(), gamma_deg_.end(), gamma_deg);
    if (it == gamma_deg_.end()) {
        return row.back();  // gamma == last angle (beyond it was handled by the caller)
    }
    const auto j1 = static_cast<std::size_t>(it - gamma_deg_.begin());
    if (j1 == 0) {
        return row.front();  // below the first angle: clamp
    }
    const std::size_t j0 = j1 - 1;
    const double t = (gamma_deg - gamma_deg_[j0]) / (gamma_deg_[j1] - gamma_deg_[j0]);
    return row[j0] + t * (row[j1] - row[j0]);
}

double Photometry::intensity_cd(double c_deg, double gamma_deg) const noexcept {
    if (std::isnan(c_deg) || std::isnan(gamma_deg) || gamma_deg > gamma_deg_.back()) {
        return 0.0;
    }
    if (c_deg_.size() == 1) {
        return intensity_in_plane(0, gamma_deg);
    }

    double c = std::fmod(c_deg, 360.0);
    if (c < 0.0) {
        c += 360.0;
    }
    // Neighbouring planes, wrapping around 360°.
    const std::size_t n = c_deg_.size();
    const auto k1_it = std::upper_bound(c_deg_.begin(), c_deg_.end(), c);
    std::size_t k0 = 0, k1 = 0;
    double c0 = 0.0, c1 = 0.0;
    if (k1_it == c_deg_.begin()) {  // before the first plane
        k0 = n - 1;
        c0 = c_deg_[k0] - 360.0;
        k1 = 0;
        c1 = c_deg_[0];
    } else if (k1_it == c_deg_.end()) {  // after the last plane
        k0 = n - 1;
        c0 = c_deg_[k0];
        k1 = 0;
        c1 = c_deg_[0] + 360.0;
    } else {
        k1 = static_cast<std::size_t>(k1_it - c_deg_.begin());
        k0 = k1 - 1;
        c0 = c_deg_[k0];
        c1 = c_deg_[k1];
    }
    const double t = (c - c0) / (c1 - c0);
    return (1.0 - t) * intensity_in_plane(k0, gamma_deg) + t * intensity_in_plane(k1, gamma_deg);
}

double Photometry::luminous_flux_lm() const noexcept {
    // Midpoint rule on a fine grid; intensities are interpolated, so the grid is independent
    // of the file's angles. 0.5° x 0.25° steps: well below 0.1 % error for smooth curves.
    constexpr int kC = 720;
    constexpr int kG = 720;
    const double dc = 2.0 * kPi / kC;
    const double dg = kPi / kG;
    double flux = 0.0;
    for (int i = 0; i < kC; ++i) {
        const double c_deg = (i + 0.5) * 360.0 / kC;
        for (int j = 0; j < kG; ++j) {
            const double g = (j + 0.5) * dg;
            flux += intensity_cd(c_deg, g * 180.0 / kPi) * std::sin(g);
        }
    }
    return flux * dc * dg;
}

double Photometry::max_intensity_cd() const noexcept {
    double m = 0.0;
    for (const auto& row : table_) {
        for (const double v : row) {
            m = std::max(m, v);
        }
    }
    return m;
}

// ---- EULUMDAT parser -----------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/// Sequential, line-oriented reader with error messages that name the line.
class LdtReader {
public:
    explicit LdtReader(std::istream& in) {
        std::string line;
        while (std::getline(in, line)) {
            lines_.push_back(trim(line));
        }
    }

    const std::string& text(const char* what) {
        if (next_ >= lines_.size()) {
            fail(what, "unexpected end of file");
        }
        return lines_[next_++];
    }

    double number(const char* what) {
        std::string s = text(what);
        std::replace(s.begin(), s.end(), ',', '.');  // some files use a decimal comma
        std::istringstream iss(s);
        iss.imbue(std::locale::classic());
        double v = 0.0;
        iss >> v;
        if (iss.fail() || !(iss >> std::ws).eof() || !std::isfinite(v)) {
            fail(what, "expected a number, found '" + s + "'");
        }
        return v;
    }

    int integer(const char* what) {
        const double v = number(what);
        if (v != std::floor(v) || std::abs(v) > 1e6) {
            fail(what, "expected an integer");
        }
        return static_cast<int>(v);
    }

    [[noreturn]] void fail(const char* what, const std::string& message) const {
        throw std::invalid_argument("EULUMDAT line " + std::to_string(next_) + " (" + what +
                                    "): " + message);
    }

private:
    std::vector<std::string> lines_;
    std::size_t next_ = 0;
};

/// Adds (angle, values) unless a plane with (almost) the same angle exists.
void add_plane(std::vector<std::pair<double, std::vector<double>>>& planes, double angle,
               const std::vector<double>& values) {
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    if (angle > 360.0 - 1e-9) {
        angle = 0.0;
    }
    for (const auto& p : planes) {
        if (std::abs(p.first - angle) < 1e-9) {
            return;
        }
    }
    planes.emplace_back(angle, values);
}

}  // namespace

Photometry parse_eulumdat(std::istream& in) {
    LdtReader r(in);
    Photometry::Info info;

    info.manufacturer = r.text("company identification");
    (void)r.integer("type indicator Ityp");
    const int isym = r.integer("symmetry indicator Isym");
    if (isym < 0 || isym > 4) {
        r.fail("symmetry indicator Isym", "must be 0..4");
    }
    info.symmetry = static_cast<Photometry::Symmetry>(isym);
    const int mc = r.integer("number of C-planes Mc");
    (void)r.number("distance between C-planes Dc");
    const int ng = r.integer("number of gamma angles Ng");
    (void)r.number("distance between gamma angles Dg");
    if (mc < 1 || ng < 2) {
        r.fail("number of gamma angles Ng", "need Mc >= 1 and Ng >= 2");
    }
    if ((isym == 2 && mc % 2 != 0) || ((isym == 3 || isym == 4) && mc % 4 != 0)) {
        r.fail("number of C-planes Mc", "not compatible with the symmetry indicator");
    }
    (void)r.text("measurement report number");
    info.name = r.text("luminaire name");
    (void)r.text("luminaire number");
    (void)r.text("file name");
    (void)r.text("date/user");
    info.length_mm = r.number("luminaire length");
    info.width_mm = r.number("luminaire width");
    info.height_mm = r.number("luminaire height");
    static const char* const kLuminousArea[] = {
        "luminous area length", "luminous area width", "luminous area height C0",
        "luminous area height C90", "luminous area height C180", "luminous area height C270"};
    for (const char* what : kLuminousArea) {
        (void)r.number(what);
    }
    (void)r.number("downward flux fraction");
    info.lor_percent = r.number("light output ratio");
    const double conversion = r.number("conversion factor");
    (void)r.number("tilt");

    const int sets = r.integer("number of lamp sets");
    if (sets < 1) {
        r.fail("number of lamp sets", "at least one lamp set is required");
    }
    for (int s = 0; s < sets; ++s) {
        (void)r.integer("number of lamps");
        (void)r.text("lamp type");
        const double flux = r.number("total lamp flux");
        (void)r.text("colour temperature");
        (void)r.text("colour rendering");
        (void)r.number("wattage");
        if (s == 0) {
            info.lamp_flux_lm = flux;
        }
    }
    if (!(info.lamp_flux_lm > 0.0)) {
        throw std::invalid_argument("EULUMDAT: lamp flux of set 1 must be > 0");
    }
    for (int k = 0; k < 10; ++k) {
        (void)r.number("direct ratio");
    }

    std::vector<double> c_all(static_cast<std::size_t>(mc));
    for (double& c : c_all) c = r.number("C angle");
    std::vector<double> gammas(static_cast<std::size_t>(ng));
    for (double& g : gammas) g = r.number("gamma angle");

    // Which of the listed C-planes the intensity block contains, by symmetry.
    std::vector<std::size_t> stored;
    switch (isym) {
        case 0: for (int k = 0; k < mc; ++k) stored.push_back(static_cast<std::size_t>(k)); break;
        case 1: stored.push_back(0); break;
        case 2: for (int k = 0; k <= mc / 2; ++k) stored.push_back(static_cast<std::size_t>(k)); break;
        case 3:  // C270 .. C360/C0 .. C90
            for (int k = 3 * mc / 4; k < mc; ++k) stored.push_back(static_cast<std::size_t>(k));
            for (int k = 0; k <= mc / 4; ++k) stored.push_back(static_cast<std::size_t>(k));
            break;
        default: for (int k = 0; k <= mc / 4; ++k) stored.push_back(static_cast<std::size_t>(k)); break;
    }

    const double scale = info.lamp_flux_lm / 1000.0 * conversion;  // cd/klm -> cd
    std::vector<std::pair<double, std::vector<double>>> planes;
    for (const std::size_t k : stored) {
        std::vector<double> values(static_cast<std::size_t>(ng));
        for (double& v : values) {
            v = r.number("luminous intensity") * scale;
            if (v < 0.0) {
                r.fail("luminous intensity", "must be >= 0");
            }
        }
        const double c = isym == 1 ? 0.0 : c_all[k];
        // Expand the symmetry so the table covers the full circle.
        add_plane(planes, c, values);
        if (isym == 2) add_plane(planes, 360.0 - c, values);
        if (isym == 3) add_plane(planes, 180.0 - c, values);
        if (isym == 4) {
            add_plane(planes, 180.0 - c, values);
            add_plane(planes, 180.0 + c, values);
            add_plane(planes, 360.0 - c, values);
        }
    }

    std::sort(planes.begin(), planes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<double> c_deg;
    std::vector<std::vector<double>> table;
    for (auto& p : planes) {
        c_deg.push_back(p.first);
        table.push_back(std::move(p.second));
    }
    return Photometry(std::move(info), std::move(c_deg), std::move(gammas), std::move(table));
}

Photometry parse_eulumdat_string(const std::string& text) {
    std::istringstream in(text);
    return parse_eulumdat(in);
}

Photometry load_eulumdat(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::invalid_argument("cannot open EULUMDAT file: " + path);
    }
    return parse_eulumdat(in);
}

}  // namespace lc
