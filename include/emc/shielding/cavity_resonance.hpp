// include/emc/shielding/cavity_resonance.hpp
#pragma once

#include <array>          // std::array — the mode tables and the Result mode lists
#include <cstdint>        // std::uint8_t — fixes CylFamily's underlying type to one byte
#include <expected>       // std::expected<void, Error> — validate()'s return shape
#include <string_view>    // std::string_view — non-owning mode labels ("f110", "mf011", ...)

#include <emc/core/calculator.hpp>    // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>     // emc::constants::c, emc::constants::pi (single source of truth)
#include <emc/core/error.hpp>         // emc::Result, emc::Error, in_range/require_positive/domain_error
#include <emc/core/units.hpp>         // emc::units::Length, emc::units::Frequency

#include <mp-units/systems/si.h>      // si::metre and the SI machinery for the typed Length defaults
#include <mp-units/math.h>            // mp_units::sqrt — the quantity-aware sqrt used in the kernels

namespace emc::shielding {

namespace mpu = mp_units;
namespace si  = mp_units::si;

// ===========================================================================
//  Shared mode-frequency type + the two closed-form kernels every calculator
//  reuses. The whole design choice: treat each enclosure's mode set as DATA
//  (a constexpr (m,n,p)/Bessel-root table) and fill a std::array<ModeFrequency>
//  with one std::ranges::transform through these kernels.
// ===========================================================================

// One (label, frequency) pair. `label` is the mode id ("f110", "mf011", "ef111",
// "f10", ...) so result columns map 1:1 onto the table rows and a front end can
// title each cell. string_view: a non-owning view of a string literal that
// outlives the Result, so there is no copy or allocation.
struct ModeFrequency {
    std::string_view       label;       // "f110", "mf011", "ef111", "f10", ...
    emc::units::Frequency  frequency{}; // resonant frequency [Hz]
};

namespace detail {

// Box kernel: f = (c / (2·√ε_r)) · √( sum of (index/dimension)² ).
// Absent axes pass index 0. Everything flows through mp-units, so the result is a
// true Frequency by construction: (1/length) has dimension 1/L; c·(1/L) is 1/T. ✔
[[nodiscard]] inline emc::units::Frequency
box_mode(double eps_r,
         double m, emc::units::Length l,
         double n, emc::units::Length w,
         double p, emc::units::Length h) {
    using namespace mp_units;                          // brings in the quantity-aware sqrt below
    // Per-axis spatial frequencies (index / dimension), each a quantity of 1/length.
    // A 0 index simply zeroes its term; l/w/h are validated > 0 so the division is always safe.
    // (We keep all three the same type so they add cleanly — no per-axis special case.)
    const auto km = m / l;
    const auto kn = n / w;
    const auto kp = p / h;
    const auto k  = sqrt(km * km + kn * kn + kp * kp);  // mp_units::sqrt proves √(1/L²)=1/L
    // f = (c / (2*sqrt(eps_r))) * k has dimension 1/time. The raw product's quantity "kind" is
    // a derived expression, so we read the number out in hertz and rebuild it as a Frequency:
    // a (value * si::hertz) already carries the frequency kind, so the label is clean.
    const double f_hz =
        (emc::constants::c / (2.0 * std::sqrt(eps_r)) * k).numerical_value_in(si::hertz);
    return emc::units::Frequency{ f_hz * si::hertz };
}

// Cylinder kernel: f = (c / (2π·√ε_r)) · √( (χ/r)² + (pπ/l)² ).
// χ is a Bessel-function root (TM: J_m root; TE: J'_m root); the axial term is pπ/l with
// full-precision emc::constants::pi, so every axial index p is exact (no truncated π).
[[nodiscard]] inline emc::units::Frequency
cyl_mode(double eps_r, double chi, emc::units::Length r,
         double p_axial_index, emc::units::Length l) {
    using namespace mp_units;
    const auto kr = chi / r;                            // radial wavenumber χ/r  [1/length]
    const auto kz = (p_axial_index * emc::constants::pi) / l;   // axial term pπ/l [1/length] (0 zeroes it)
    const auto k = sqrt(kr * kr + kz * kz);
    const double f_hz =
        (emc::constants::c / (2.0 * emc::constants::pi * std::sqrt(eps_r)) * k)
            .numerical_value_in(si::hertz);
    return emc::units::Frequency{ f_hz * si::hertz };
}

} // namespace detail

// ===========================================================================
//  Rectangular enclosure — 12 dominant TE/TM modes.
//  f_mnp = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² + (p/h)² )
// ===========================================================================

// Inputs for the rectangular cavity. Geometry is mp-units typed (the unit lives in
// the type, so wrong-unit math will not compile); eps_r is a bare ratio, hence a
// plain double. Defaults (0.5 m × 0.4 m × 0.2 m, ε_r = 1) make designated-initializer
// call sites read well.
struct RectangularCavityInput {
    emc::units::Length length{0.5 * si::metre};   // l  (interior, > 0)
    emc::units::Length width {0.4 * si::metre};   // w  (> 0)
    emc::units::Length height{0.2 * si::metre};   // h  (> 0)
    double             eps_r {1.0};               // relative permittivity (dimensionless, ≥ 1)
};

inline constexpr int kRectangularModeCount = 12;

struct RectangularCavityResult {
    std::array<ModeFrequency, kRectangularModeCount> modes{};

    // Convenience: lowest (dominant) resonance among the 12. One-liner because the
    // Result is iterable (std::array, not 12 named members).
    [[nodiscard]] emc::units::Frequency dominant() const;
};

// (m,n,p) table. constexpr so it lives in read-only data and is iterable at compile
// time; adding a 13th mode is one row, not a copy-pasted 4-line block.
inline constexpr std::array<std::array<int, 3>, kRectangularModeCount> kRectangularModes{{
    //  m  n  p     label
    {1,1,0},   // f110
    {1,0,1},   // f101
    {0,1,1},   // f011
    {1,1,1},   // f111
    {2,0,1},   // f201
    {1,2,0},   // f120
    {2,1,1},   // f211
    {2,1,0},   // f210
    {0,2,1},   // f021
    {2,2,0},   // f220
    {2,2,1},   // f221
    {1,2,1},   // f121
}};

// Labels aligned 1:1 with kRectangularModes; zipped with the index table in the transform.
inline constexpr std::array<std::string_view, kRectangularModeCount> kRectangularLabels{{
    "f110","f101","f011","f111","f201","f120","f211","f210","f021","f220","f221","f121"
}};

// [[nodiscard]]: a dropped validation result is a bug. Overloaded on the Input type so
// every calculator's validate()/calculate() pair shares the two names.
[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularCavityInput& in);
[[nodiscard]] emc::Result<RectangularCavityResult> rectangular_cavity_modes(const RectangularCavityInput& in);

// Tag type: names the (Input, Result, calculate, validate) quadruple so generic code and
// the static_assert below can check the ValidatedCalculator contract at compile time.
struct RectangularCavity {
    using Input  = RectangularCavityInput;
    using Result = RectangularCavityResult;
    static emc::Result<Result> calculate(const Input& in) {
        return emc::shielding::rectangular_cavity_modes(in);
    }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<RectangularCavity>);

// ===========================================================================
//  Cylindrical enclosure — 12 TM + 9 TE modes via Bessel roots.
//  f = (c / (2π·√ε_r)) · √( (χ/r)² + (pπ/l)² )
// ===========================================================================

// A cylinder mode: family (TE/TM), the Bessel root χ used radially, and the axial
// index p (multiplied by π/l). `label` is the mode id.
enum class CylFamily : std::uint8_t { TE, TM };   // typed tag, not a naming convention

struct CylModeSpec {
    std::string_view label;   // "ef111", "mf011", ...
    CylFamily        family;  // TE (ef*) or TM (mf*)
    double           chi;     // Bessel root: J'_m root (TE) or J_m root (TM)
    int              p;       // axial index (axial term = p·π / l)
};

inline constexpr int kCylindricalModeCount = 21;

struct CylindricalCavityInput {
    emc::units::Length length{1.0 * si::metre};    // l  (> 0)
    emc::units::Length radius{0.05 * si::metre};   // r  (> 0)
    double             eps_r {1.0};                // ε_r (≥ 1)
};

struct CylindricalCavityResult {
    std::array<ModeFrequency, kCylindricalModeCount> modes{};
    [[nodiscard]] emc::units::Frequency dominant() const;
};

// The Bessel-root spec table. χ values are J'_m roots (TE, ef*) / J_m roots (TM, mf*);
// keeping them as reviewable table data makes the root set easy to audit in one place.
inline constexpr std::array<CylModeSpec, kCylindricalModeCount> kCylindricalModes{{
    // --- TE (ef*) : roots of J'_m -----------------------------------------
    {"ef111", CylFamily::TE, 1.841, 1},   // J'_1 first root
    {"ef021", CylFamily::TE, 5.331, 1},   // J'_2 second root
    {"ef211", CylFamily::TE, 3.054, 1},   // J'_2 first root
    {"ef011", CylFamily::TE, 3.832, 1},   // J'_0 first root
    {"ef022", CylFamily::TE, 7.016, 2},
    {"ef212", CylFamily::TE, 3.054, 2},
    {"ef012", CylFamily::TE, 3.832, 2},
    {"ef121", CylFamily::TE, 5.331, 1},
    {"ef221", CylFamily::TE, 6.706, 1},
    // --- TM (mf*) : roots of J_m ------------------------------------------
    {"mf011", CylFamily::TM, 2.405, 1},   // J_0 first root
    {"mf021", CylFamily::TM, 5.520, 1},   // J_0 second root
    {"mf211", CylFamily::TM, 5.135, 1},   // J_2 first root
    {"mf110", CylFamily::TM, 3.832, 0},   // J_1 first root, p=0
    {"mf022", CylFamily::TM, 5.520, 2},
    {"mf212", CylFamily::TM, 5.135, 2},
    {"mf111", CylFamily::TM, 3.832, 1},   // J_1 first root
    {"mf120", CylFamily::TM, 7.016, 0},   // J_1 second root, p=0
    {"mf210", CylFamily::TM, 5.135, 0},
    {"mf012", CylFamily::TM, 2.405, 2},
    {"mf121", CylFamily::TM, 7.016, 1},
    {"mf220", CylFamily::TM, 8.417, 0},   // J_2 second root, p=0
}};

[[nodiscard]] std::expected<void, emc::Error> validate(const CylindricalCavityInput& in);
[[nodiscard]] emc::Result<CylindricalCavityResult> cylindrical_cavity_modes(const CylindricalCavityInput& in);

struct CylindricalCavity {
    using Input  = CylindricalCavityInput;
    using Result = CylindricalCavityResult;
    static emc::Result<Result> calculate(const Input& in) {
        return emc::shielding::cylindrical_cavity_modes(in);
    }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<CylindricalCavity>);

// ===========================================================================
//  Circuit-board planes — lateral (m,n) resonance grid of a plane pair.
//  f_mn = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² )   (thin-plane: s < l/10, s < w/10)
// ===========================================================================

inline constexpr int kBoardPlaneModeCount = 12;

struct BoardPlaneInput {
    emc::units::Length length    {1.0 * si::metre};   // l  (> 0)
    emc::units::Length width     {1.0 * si::metre};   // w  (> 0)
    emc::units::Length separation{1.0 * si::metre};   // s  (plane spacing, > 0; thin-plane guarded)
    double             eps_r     {1.0};               // ε_r (≥ 1)
};

struct BoardPlaneResult {
    std::array<ModeFrequency, kBoardPlaneModeCount> modes{};
    [[nodiscard]] emc::units::Frequency dominant() const;
};

// (m,n) lateral mode grid. constexpr data, same table-driven design as the box.
inline constexpr std::array<std::array<int, 2>, kBoardPlaneModeCount> kBoardPlaneModes{{
    {1,0}, {2,1}, {3,1}, {0,1}, {1,2}, {3,2},
    {1,1}, {2,2}, {2,3}, {2,0}, {3,0}, {3,3},
}};
inline constexpr std::array<std::string_view, kBoardPlaneModeCount> kBoardPlaneLabels{{
    "f10","f21","f31","f01","f12","f32","f11","f22","f23","f20","f30","f33"
}};

[[nodiscard]] std::expected<void, emc::Error> validate(const BoardPlaneInput& in);
[[nodiscard]] emc::Result<BoardPlaneResult> circuit_board_plane_modes(const BoardPlaneInput& in);

struct BoardPlanes {
    using Input  = BoardPlaneInput;
    using Result = BoardPlaneResult;
    static emc::Result<Result> calculate(const Input& in) {
        return emc::shielding::circuit_board_plane_modes(in);
    }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<BoardPlanes>);

} // namespace emc::shielding
