// src/component/dual_stripline_trace.cpp
#include <emc/component/dual_stripline_trace.hpp>
#include <emc/component/detail/board_units.hpp>   // detail::mil (call sites), detail::inch (display)

#include <emc/core/constants.hpp>                 // emc::constants::pi

#include <mp-units/systems/si.h>

#include <cmath>   // std::log, std::exp, std::sqrt (constexpr in C++23)

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {

// Working unit for the empirical algebra. Z0 is scale-invariant (H, C, T, W all appear only
// inside the dimensionless log ratios), so the choice of mm does not affect Z0; the caller
// later reads any display unit with .in(mm) / .in(mil).
constexpr auto U = mm;

// Full-precision pi from the foundation — shared identically by the forward Z0 and every
// inverse, so the formulas agree to the last digit and the round-trip tests stay exact.
constexpr double PI = emc::constants::pi;

// pF/inch and ps/inch storage relabels. C0 / Tpd are computed in the established per-inch
// display convention; we attach the matching unit and store SI (F/m, s/m), letting the caller
// select pF/inch / ps/inch on the way out. (detail::inch == 25.4 mm EXACT.)
constexpr auto pf_in = si::pico<si::farad>  / detail::inch;
constexpr auto ps_in = si::pico<si::second> / detail::inch;

// The single source of the forward Z0 / C0 / Tpd math, on bare doubles in mm. Z0 is the average
// of the two stripline-like terms (one over H, one over H + C).
struct Core { double z0, c0_pf_in, tpd_ps_in; };

Core forward_core(double H, double C, double T, double W, double eps) {
    const double denom = 0.67 * PI * (0.8 * W + T);
    const double z0    = 0.5 * (60.0 * std::log(8.0 * H       / denom) / std::sqrt(eps)
                              + 60.0 * std::log(8.0 * (H + C) / denom) / std::sqrt(eps));
    const double tpd   = 84.75 * std::sqrt(eps);          // ps/inch
    return Core{ .z0 = z0, .c0_pf_in = tpd / z0, .tpd_ps_in = tpd };
}

}  // namespace

// ---- shared validation ------------------------------------------------------
std::expected<void, emc::Error> validate(const DualStriplineInput& in) {
    // Pull each length in the one coherent working unit, then range-check as plain doubles.
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(C, "gap");       !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // dual-stripline geometry guards. H > T first, so the W/(H-T) ratio cannot divide by zero
    // or go negative; then the two validated bands W/(H-T) < 0.35 and T/H < 0.25.
    if (!(H - T > 0.0))
        return std::unexpected(emc::domain_error("height must exceed thickness", "height"));
    if (!(W / (H - T) < 0.35))
        return std::unexpected(emc::out_of_range(0.0, 0.35, "width_to_gap_ratio"));
    if (!(T / H < 0.25))
        return std::unexpected(emc::out_of_range(0.0, 0.25, "thickness_to_height_ratio"));
    return {};
}

// ---- FORWARD ----------------------------------------------------------------
emc::Result<DualStriplineResult> calculate(const DualStriplineInput& in) {
    // transform runs the math only if validate() succeeded, forwarding the Error otherwise —
    // the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double C = in.gap.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, C, T, W, in.relative_permittivity);
        // Re-attach units. The product/quotient has a DERIVED quantity kind, so each field is
        // stored with direct-init {..} into its named alias — the explicit "this value IS that
        // quantity" relabel mp-units requires.
        return DualStriplineResult{
            .z0  = emc::units::Impedance{ (c.z0 * ohm).in(ohm) },
            .c0  = emc::units::CapacitancePerLength{ (c.c0_pf_in * pf_in).in(si::farad / si::metre) },
            .tpd = emc::units::TimePerLength{ (c.tpd_ps_in * ps_in).in(si::second / si::metre) },
        };
    });
}

// ---- inverse solvers --------------------------------------------------------
namespace {
// dual uses a = exp(Z0·sqrt(eps)/(0.5·60)) = exp(Z0·sqrt(eps)/30). The 0.5 is the ½ averaging
// factor folded into the exponent so each inverse is one closed-form expression.
double aa(double z0, double eps) { return std::exp(z0 * std::sqrt(eps) / 30.0); }
}  // namespace

// solve_height:  H = ( sqrt(64²·C² + 4·64·a·P²) − 64·C ) / 128,  P = 0.67·pi·(0.8·W + T)
emc::Result<emc::units::Length> solve_height(const DualSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double P = 0.67 * PI * (0.8 * W + T);
    const double H = (std::sqrt(64.0 * 64.0 * C * C + 4.0 * 64.0 * a * P * P) - 64.0 * C) / 128.0;
    if (!(H > 0.0))
        return std::unexpected(emc::domain_error("dual height non-positive", "height"));
    return H * U;
}

// solve_gap:  C = ( a·P² − 64·H² ) / (64·H).  require_nonzero(H) guards the /(64·H) denominator.
emc::Result<emc::units::Length> solve_gap(const DualSolveGap& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    if (auto r = emc::require_nonzero(H, "height"); !r) return std::unexpected(r.error());
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double P = 0.67 * PI * (0.8 * W + T);
    const double C = (a * P * P - 64.0 * H * H) / (64.0 * H);
    if (!(C > 0.0))
        return std::unexpected(emc::domain_error("dual gap non-positive", "gap"));
    return C * U;
}

// solve_thickness:  T = sqrt( (64·H² + 64·H·C) / a ) / (0.67·pi) − 0.8·W
emc::Result<emc::units::Length> solve_thickness(const DualSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double T = std::sqrt((64.0 * H * H + 64.0 * H * C) / a) / (0.67 * PI) - 0.8 * W;
    if (!(T > 0.0))
        return std::unexpected(emc::domain_error("dual thickness non-positive", "thickness"));
    return T * U;
}

// solve_width:  W = ( sqrt( (64·H² + 64·H·C) / a ) / (0.67·pi) − T ) / 0.8
emc::Result<emc::units::Length> solve_width(const DualSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double W = (std::sqrt((64.0 * H * H + 64.0 * H * C) / a) / (0.67 * PI) - T) / 0.8;
    if (!(W > 0.0))
        return std::unexpected(emc::domain_error("dual width non-positive", "width"));
    return W * U;
}

}  // namespace emc::component
