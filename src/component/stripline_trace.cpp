// src/component/stripline_trace.cpp
#include <emc/component/stripline_trace.hpp>
#include <emc/component/detail/board_units.hpp>   // detail::mil (call site) / detail::inch (display)

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>   // emc::constants::pi (shared full-precision pi)

#include <cmath>   // std::log, std::exp, std::sqrt (constexpr in C++23, runtime here)

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {

// Working unit for the empirical algebra. Z0 is scale-invariant, so the choice of mm
// does not affect Z0; we pin ONE coherent unit so the established formula constants
// (60, 4, 0.67, 0.8, 84.75) stay exact and the inputs need no unit branching.
constexpr auto U = mm;

// The SAME full-precision pi feeds both the forward Z0 and the inverse `a`, so the two
// agree to the last digit and any drift would surface in the round-trip tests.
constexpr double PI = emc::constants::pi;

// Single source of the forward stripline math, on bare doubles in mm / per-inch.
struct Core { double z0, c0_pf_in, tpd_ps_in; };

Core forward_core(double H, double T, double W, double eps) {
    const double z0  = 60.0 * std::log(4.0 * (2.0 * H + T) / (0.67 * PI * (0.8 * W + T)))
                       / std::sqrt(eps);
    const double tpd = 84.75 * std::sqrt(eps);          // ps/inch
    return Core{ .z0 = z0, .c0_pf_in = tpd / z0, .tpd_ps_in = tpd };
}

}  // namespace

std::expected<void, emc::Error> validate(const StriplineInput& in) {
    // Pull each typed Length into the one working unit as a bare double, then range-check.
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r) return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // Stripline geometry guards (scale-free ratios). T/H < 0.25; H must exceed T so the
    // W/(H-T) ratio is well-defined (this also pre-empts a divide-by-zero); W/(H-T) < 0.35.
    if (!(T / H < 0.25))
        return std::unexpected(emc::out_of_range(0.0, 0.25, "thickness_to_height_ratio"));
    if (!(H - T > 0.0))
        return std::unexpected(emc::domain_error("height must exceed thickness", "height"));
    if (!(W / (H - T) < 0.35))
        return std::unexpected(emc::out_of_range(0.0, 0.35, "width_to_gap_ratio"));
    return {};
}

emc::Result<StriplineResult> calculate(const StriplineInput& in) {
    // transform runs the math ONLY if validate() succeeded, forwarding the Error
    // otherwise — the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, T, W, in.relative_permittivity);

        // Re-attach physical units. Each product/quotient has a DERIVED quantity kind that
        // does not implicitly convert to the named alias, so we .in(<alias unit>) then
        // DIRECT-init {..} into the alias — the explicit "this value IS that quantity" relabel.
        return StriplineResult{
            // Z0 is a plain ohm number from the core.
            .z0  = emc::units::Impedance{ (c.z0 * ohm).in(ohm) },
            // C0 is pF/inch, Tpd is ps/inch; store SI (F/m, s/m) and let the caller print
            // pF/inch / ps/inch via .in(...). detail::inch is the EXACT 25.4 mm inch.
            .c0  = emc::units::CapacitancePerLength{
                       (c.c0_pf_in * (si::pico<si::farad> / detail::inch)).in(si::farad / si::metre) },
            .tpd = emc::units::TimePerLength{
                       (c.tpd_ps_in * (si::pico<si::second> / detail::inch)).in(si::second / si::metre) },
        };
    });
}

// ---- inverse solvers: one expression each; no unit branching. -------------------------------
namespace {
// `a = exp(Z0*sqrt(eps)/60)` — the inverse of the forward log; shares PI with forward_core.
double aa(double z0, double eps) { return std::exp(z0 * std::sqrt(eps) / 60.0); }
}  // namespace

emc::Result<emc::units::Length> solve_height(const StriplineSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double H = (a * 0.67 * PI * (0.8 * W + T) / 4.0 - T) / 2.0;
    // A non-positive H means the requested Z0 is unreachable for this geometry — typed domain error.
    if (!(H > 0.0)) return std::unexpected(emc::domain_error("stripline height non-positive", "height"));
    return emc::units::Length{ (H * U).in(U) };   // rebuild a typed Length from the bare mm value
}

emc::Result<emc::units::Length> solve_thickness(const StriplineSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    // The inverse T formula has a (4 - 0.67*a*pi) denominator; a zero denominator becomes a
    // typed DivisionByZero instead of inf.
    const double denom = 4.0 - 0.67 * a * PI;
    if (denom == 0.0) return std::unexpected(emc::division_by_zero("thickness_solver"));
    const double T = (0.67 * a * PI * 0.8 * W - 8.0 * H) / denom;
    if (!(T > 0.0)) return std::unexpected(emc::domain_error("stripline thickness non-positive", "thickness"));
    return emc::units::Length{ (T * U).in(U) };
}

emc::Result<emc::units::Length> solve_width(const StriplineSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double W = ((8.0 * H + 4.0 * T) / (a * 0.67 * PI) - T) / 0.8;
    if (!(W > 0.0)) return std::unexpected(emc::domain_error("stripline width non-positive", "width"));
    return emc::units::Length{ (W * U).in(U) };
}

}  // namespace emc::component
