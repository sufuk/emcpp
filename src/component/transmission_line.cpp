// src/component/transmission_line.cpp
//
// Implementation of the seven transmission-line calculators declared in
// transmission_line.hpp. The four per-length variants (Narrow/Wide Trace,
// Wire Over Plane, Wire Pair) share detail::per_length_line for the
// skin-depth -> Z0 -> R block so the constant and pattern each live once.
#include <emc/component/transmission_line.hpp>

#include <cmath>   // std::log10, std::log, std::sqrt, std::pow, std::acosh — constexpr-friendly in C++23

#include <mp-units/systems/si.h>

namespace emc::component {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // mm, m, Hz, ohm, F, H, S, ...

namespace {
// Imperial lengths registered on the SI graph as scaled metres, so .in(inch) /
// .in(foot) round-trip EXACTLY against mm without a hand-coded factor (mp-units
// derives the conversion). Defined locally because only the coaxial calculator's
// inch-based empirical constants (138 / 11.8 / 7.354 / 140.4) need them.
//   1 inch = 25.4 mm  -> mag_ratio<254, 10'000>  metres
//   1 foot = 12 inch  -> mag_ratio<3'048, 10'000> metres
inline constexpr struct inch_ final
    : named_unit<"in", mag_ratio<254, 10'000> * si::metre> {} inch;
inline constexpr struct foot_ final
    : named_unit<"ft", mag_ratio<3'048, 10'000> * si::metre> {} foot;
} // namespace

// ===========================================================================
//  Coaxial Line
// ===========================================================================

std::expected<void, emc::Error> validate(const CoaxialLineInput& in) {
    // Pull both diameters in one shared unit (mm) so the positivity / ordering
    // checks compare like with like; the ratio formula reads them in inches later.
    const double D = in.outer_diameter.numerical_value_in(mm);
    const double d = in.inner_diameter.numerical_value_in(mm);
    if (auto r = emc::require_positive(d, "inner_diameter"); !r) return r;
    if (auto r = emc::require_positive(D, "outer_diameter"); !r) return r;
    if (D <= d)   // log10(D/d) must be > 0 for a physical line; D == d -> Z0 = 0
        return std::unexpected(emc::invalid_input(
            "outer diameter must exceed inner diameter", "outer_diameter"));
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    return {};
}

emc::Result<CoaxialLineResult> calculate(const CoaxialLineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // The empirical constants assume INCHES. The local `inch` unit derives
    // 1 inch == 25.4 mm, so the imperial/SI conversion can never drift.
    const double D     = in.outer_diameter.numerical_value_in(inch);
    const double d     = in.inner_diameter.numerical_value_in(inch);
    const double eps_r = in.relative_permittivity;

    const double ratio_log = std::log10(D / d);   // log10(D/d)
    const double sqrt_eps  = std::sqrt(eps_r);

    const double z0_ohm  = (138.0 * ratio_log) / sqrt_eps;                              // ohm
    const double fc_GHz  = 11.8 / (sqrt_eps * emc::constants::pi * ((D + d) / 2.0));    // GHz
    const double c_pF_ft = (7.354 * eps_r) / ratio_log;                                // pF/ft
    const double l_nH_ft = 140.4 * ratio_log;                                          // nH/ft

    // Each product/quotient below is a derived-kind quantity, so we store with
    // direct-init {..}: that runs the explicit relabel into the named alias
    // (copy-init '=' would not compile — mp-units makes the relabel deliberate).
    return CoaxialLineResult{
        .impedance        = emc::units::Impedance{ z0_ohm * ohm },
        .cutoff_frequency = emc::units::Frequency{ (fc_GHz * si::giga<Hz>).in(Hz) },
        // pF/ft and nH/ft are capacitance/length and inductance/length kinds, so
        // they relabel into the F/m and H/m aliases (mp-units rescales the value).
        .capacitance = emc::units::CapacitancePerLength{
            (c_pF_ft * (si::pico<F> / foot)).in(F / m) },
        .inductance = emc::units::InductancePerLength{
            (l_nH_ft * (si::nano<H> / foot)).in(H / m) },
    };
}

// ===========================================================================
//  Microstrip Line
// ===========================================================================

namespace {
// Hammerstad eps_eff, two-branch (W/H < 1 and W/H > 1). A free helper keeps the
// branch math out of calculate() and reusable in the test recompute.
[[nodiscard]] double micro_eps_eff(double eps_r, double ratio /*W/H*/) {
    const double inv = 1.0 / ratio;   // H/W
    if (ratio < 1.0)
        return (eps_r + 1.0) / 2.0
             + (eps_r - 1.0) / 2.0
               * (1.0 / std::sqrt(1.0 + 12.0 * inv) + 0.04 * std::pow(1.0 - ratio, 2));
    // ratio > 1.0
    return (eps_r + 1.0) / 2.0
         + (eps_r - 1.0) / (2.0 * std::sqrt(1.0 + 12.0 * inv));
}
} // namespace

std::expected<void, emc::Error> validate(const MicrostripLineInput& in) {
    const double w = in.width.numerical_value_in(mm);
    const double h = in.height.numerical_value_in(mm);
    if (auto r = emc::require_positive(w, "width");  !r) return r;
    if (auto r = emc::require_positive(h, "height"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (w == h)   // the Hammerstad branches cover W/H<1 and W/H>1 but not exactly W==H
        return std::unexpected(emc::Error{
            .code = emc::ErrorCode::Unsupported,
            .message = "W == H is not covered by the Hammerstad branches",
            .field = "width" });
    return {};
}

emc::Result<MicrostripLineResult> calculate(const MicrostripLineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Ratio-based: read W and H in a SHARED unit so the ratio is unit-free and a
    // W in mils with an H in mm can never silently mismatch.
    const double w     = in.width.numerical_value_in(mm);
    const double h     = in.height.numerical_value_in(mm);
    const double eps_r = in.relative_permittivity;
    const double ratio = w / h;   // W/H
    const double inv   = h / w;   // H/W

    const double eps_eff = micro_eps_eff(eps_r, ratio);

    double z0;
    if (ratio < 1.0)
        z0 = (60.0 / std::sqrt(eps_eff)) * std::log(8.0 * inv + 0.25 * ratio);
    else // ratio > 1.0
        z0 = (120.0 * emc::constants::pi)
           / (std::sqrt(eps_eff) * (ratio + 1.393 + (2.0 / 3.0) * std::log(ratio + 1.444)));

    return MicrostripLineResult{
        .effective_permittivity = eps_eff,
        .impedance              = emc::units::Impedance{ z0 * ohm },
    };
}

// ===========================================================================
//  Stripline
// ===========================================================================

std::expected<void, emc::Error> validate(const StriplineInput& in) {
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(w, "width");     !r) return r;
    if (auto r = emc::require_positive(h, "height");    !r) return r;
    if (auto r = emc::require_positive(t, "thickness"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    // ln argument must be positive; (0.8w + t) > 0 given the positivity above.
    const double arg = (1.9 * (2.0 * h + t)) / (0.8 * w + t);
    if (!(arg > 0.0))
        return std::unexpected(emc::domain_error("stripline ln argument <= 0", "width"));
    return {};
}

emc::Result<StriplineResult> calculate(const StriplineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Ratio argument: read all three lengths in the SAME unit (metre) so the ln
    // argument is exactly dimensionless.
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);

    const double z0 = (60.0 / std::sqrt(in.relative_permittivity))
                    * std::log((1.9 * (2.0 * h + t)) / (0.8 * w + t));

    return StriplineResult{ .impedance = emc::units::Impedance{ z0 * ohm } };
}

// ===========================================================================
//  Shared per-length core (steps 3-6 of the pattern) + helpers
// ===========================================================================

namespace detail {

// Resolve the conductivity: a table material's sigma, or the custom sigma for
// Material::Custom. Returns a typed error (UnknownMaterial / OutOfRange) cleanly.
[[nodiscard]] emc::Result<double> resolve_sigma(emc::materials::Material mat,
                                                emc::units::Conductivity custom) {
    if (mat == emc::materials::Material::Custom) {
        const double s = custom.numerical_value_in(si::siemens / si::metre);
        if (auto r = emc::require_positive(s, "custom_sigma"); !r)
            return std::unexpected(r.error());
        return s;
    }
    auto p = emc::materials::properties(mat);
    if (!p) return std::unexpected(p.error());
    return p->conductivity.numerical_value_in(si::siemens / si::metre);
}

// Skin depth delta = 1 / sqrt(pi * f * mu0 * sigma), with the canonical mu0.
[[nodiscard]] double skin_depth(double f_hz, double sigma) {
    const double mu0 = emc::constants::mu0.numerical_value_in(si::henry / si::metre);
    return 1.0 / std::sqrt(emc::constants::pi * f_hz * mu0 * sigma);
}

// Steps 3-6: Z0 + R + unit attachment, shared by the four per-length variants.
// Inputs are bare display-unit numbers:
//   L_uH_per_m : inductance per length  [uH/m]
//   C_pF_per_m : capacitance per length [pF/m]
//   sigma      : conductivity           [S/m]
//   aeff_m2    : skin-effect effective conductor cross-section [m^2]
//   r_numer    : 1000 (single conductor) or 2000 (wire pair, two conductors)
[[nodiscard]] emc::Result<LinePerLengthResult>
per_length_line(double L_uH_per_m, double C_pF_per_m,
                double sigma, double aeff_m2, double r_numer) {
    if (C_pF_per_m == 0.0)
        return std::unexpected(emc::division_by_zero("capacitance"));
    if (aeff_m2 == 0.0 || sigma == 0.0)
        return std::unexpected(emc::division_by_zero("resistance"));

    // Z0 = sqrt(1e6 * L[uH/m] / C[pF/m]) — the 1e6 is the uH/pF unit bookkeeping
    // that converts (uH/m / pF/m) into ohm^2. Keep that exact arithmetic, then
    // attach the physically-correct unit so the value and its type agree.
    const double z0               = std::sqrt(1.0e6 * L_uH_per_m / C_pF_per_m);
    const double r_milliohm_per_m = r_numer / (sigma * aeff_m2);   // mOhm/m

    // Each unit-bearing product is a derived-kind quantity, so direct-init {..}
    // relabels it into the named per-length alias.
    return LinePerLengthResult{
        .inductance  = emc::units::InductancePerLength{
            (L_uH_per_m * (si::micro<H> / m)).in(H / m) },
        .capacitance = emc::units::CapacitancePerLength{
            (C_pF_per_m * (si::pico<F> / m)).in(F / m) },
        .resistance  = emc::units::ResistancePerLength{
            (r_milliohm_per_m * (si::milli<ohm> / m)).in(ohm / m) },
        .characteristic_impedance = emc::units::Impedance{ z0 * ohm },
    };
}

} // namespace detail

// ===========================================================================
//  Narrow Trace Over Plane  (h > w)
//      L = 0.2 * acosh(4h/w)                          [uH/m]
//      C = (2*pi*eps0*eps_r / acosh(4h/w)) * 1e12     [pF/m]
//      Aeff = (delta <= wt/(2(w+t))) ? 2(w+t)*delta : w*t
//      R = 1000 / (sigma*Aeff)                        [mOhm/m]
// ===========================================================================

std::expected<void, emc::Error> validate(const NarrowTraceInput& in) {
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(w, "trace_width");     !r) return r;
    if (auto r = emc::require_positive(t, "trace_thickness"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (h <= w)   // narrow-trace approximation needs the trace above its own width
        return std::unexpected(emc::invalid_input(
            "trace height must exceed trace width", "trace_height"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const NarrowTraceInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f    = in.frequency.numerical_value_in(Hz);
    const double h    = in.trace_height.numerical_value_in(m);
    const double w    = in.trace_width.numerical_value_in(m);
    const double t    = in.trace_thickness.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom       = std::acosh(4.0 * h / w);   // acosh(4h/w)
    const double L_uH_per_m = 0.2 * geom;                // uH/m
    const double C_pF_per_m = (2.0 * emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= (w * t) / (2.0 * (w + t)))
                       ? 2.0 * (w + t) * delta   // skin-limited: current rides the perimeter
                       : w * t;                  // fully penetrated: full cross-section

    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

// ===========================================================================
//  Wide Trace Over Plane  (w > 5h)
//      L = 0.4*pi*(h/w)                               [uH/m]   (= mu0*mu_r*h/w, mu_r=1)
//      C = (eps0*eps_r*(w/h)) * 1e12                  [pF/m]
//      Aeff = (delta <= wt/(w+t)) ? (w+t)*delta : w*t
//      R = 1000 / (sigma*Aeff)                        [mOhm/m]
// ===========================================================================

std::expected<void, emc::Error> validate(const WideTraceInput& in) {
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(h, "trace_height");    !r) return r;
    if (auto r = emc::require_positive(t, "trace_thickness"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (w <= 5.0 * h)   // the wide-trace approximation is only valid for w > 5h
        return std::unexpected(emc::invalid_input(
            "trace width must exceed 5x trace height", "trace_width"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WideTraceInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f    = in.frequency.numerical_value_in(Hz);
    const double h    = in.trace_height.numerical_value_in(m);
    const double w    = in.trace_width.numerical_value_in(m);
    const double t    = in.trace_thickness.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    // 0.4*pi is mu0*mu_r*h/w pre-scaled to uH/m for mu_r = 1; the hidden mu0 is
    // made explicit here via the canonical pi instead of a magic number.
    const double L_uH_per_m = 0.4 * emc::constants::pi * h / w;                        // uH/m
    const double C_pF_per_m = (eps0 * in.relative_permittivity * w / h) * 1e12;        // pF/m

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= (w * t) / (w + t)) ? (w + t) * delta : w * t;

    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

// ===========================================================================
//  Wire Over Plane  (h > a)
//      L = 0.2*acosh(h/a)                             [uH/m]
//      C = (2*pi*eps0*eps_r / acosh(h/a)) * 1e12      [pF/m]
//      Aeff = (delta <= a/2) ? 2*pi*a*delta : pi*a^2
//      R = 1000 / (sigma*Aeff)                        [mOhm/m]
// ===========================================================================

std::expected<void, emc::Error> validate(const WireOverPlaneInput& in) {
    const double h = in.wire_height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto r = emc::require_positive(a, "wire_radius"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (h <= a)   // physical only when the wire sits above the plane
        return std::unexpected(emc::invalid_input(
            "wire height must exceed wire radius", "wire_height"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WireOverPlaneInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f    = in.frequency.numerical_value_in(Hz);
    const double h    = in.wire_height.numerical_value_in(m);
    const double a    = in.wire_radius.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom       = std::acosh(h / a);   // acosh(h/a)
    const double L_uH_per_m = 0.2 * geom;
    const double C_pF_per_m = (2.0 * emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= a / 2.0)
                       ? 2.0 * emc::constants::pi * a * delta   // skin shell
                       : emc::constants::pi * a * a;            // full disc
    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

// ===========================================================================
//  Wire Pair  (s > d, radius a = d/2)
//      L = 0.4*acosh(s/d)                             [uH/m]
//      C = (pi*eps0*eps_r / acosh(s/d)) * 1e12        [pF/m]
//      Aeff = (delta <= a/2) ? 2*pi*a*delta : pi*a^2
//      R = 2000 / (sigma*Aeff)                        [mOhm/m]   <- two conductors
// ===========================================================================

std::expected<void, emc::Error> validate(const WirePairInput& in) {
    const double s = in.spacing.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto r = emc::require_positive(d, "diameter"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (s <= d)   // overlapping conductors are non-physical
        return std::unexpected(emc::invalid_input(
            "wire spacing must exceed wire diameter", "spacing"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WirePairInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f    = in.frequency.numerical_value_in(Hz);
    const double s    = in.spacing.numerical_value_in(m);
    const double d    = in.diameter.numerical_value_in(m);
    const double a    = 0.5 * d;   // radius
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom       = std::acosh(s / d);   // acosh(s/d), spacing-over-diameter
    const double L_uH_per_m = 0.4 * geom;
    const double C_pF_per_m = (emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= a / 2.0)
                       ? 2.0 * emc::constants::pi * a * delta
                       : emc::constants::pi * a * a;

    // r_numer = 2000: the loop has two conductors in series. THIS is why
    // per_length_line takes r_numer instead of hard-coding 1000.
    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/2000.0);
}

} // namespace emc::component
