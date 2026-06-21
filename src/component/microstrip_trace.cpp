// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <emc/component/detail/board_units.hpp>   // shared exact `mil` unit (registers it on the SI graph)

#include <mp-units/systems/si.h>

#include <cmath>   // std::log, std::exp, std::sqrt (constexpr in C++23)

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {

// Working unit for the empirical algebra. Z0 is scale-invariant, so the choice does
// not affect Z0 — pulling every length in one coherent unit (mm) keeps the established
// formula constants (5.98, 0.8, 2.54, ...) exact.
constexpr auto U = mm;

// The single source of the forward Z0 math, on bare doubles in mm. Both calculate()
// reads from here; the inverse solvers share the inv_k() helper below.
struct Core { double z0, c0_pf_cm, tpd_ps_cm; };

Core forward_core(double H, double T, double W, double eps) {
    const double ln    = std::log(5.98 * H / (0.8 * W + T));
    const double z0    = 87.0 * ln / std::sqrt(eps + 1.41);
    const double ctemp = 0.67 * (eps + 1.41) / ln;
    return Core{ .z0 = z0, .c0_pf_cm = ctemp / 2.54, .tpd_ps_cm = ctemp * z0 / 2.54 };
}

}  // namespace

std::expected<void, emc::Error> validate(const MicrostripInput& in) {
    // Pull each typed Length out in the working unit, then range-check as plain
    // doubles (the foundation validators report [lo, hi] in that unit).
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return r;
    // Positivity also guards the ln argument 5.98*H/(0.8*W+T) > 0 and the /(0.8*W+T) divide.
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // 0.1 <= W/H <= 3 keeps the inputs inside the formula's validated geometry band.
    if (auto r = emc::in_range(W / H, 0.1, 3.0, "width_to_height_ratio"); !r) return r;
    return {};
}

emc::Result<MicrostripResult> calculate(const MicrostripInput& in) {
    // transform runs the math only if validate() succeeded, and forwards the Error
    // otherwise — so the formula is unreachable on bad input, no if/return noise.
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, T, W, in.relative_permittivity);
        return MicrostripResult{
            // Re-attach physical units. z0 is a plain ohm value; direct-init {..} into
            // the named alias would be needed for a *derived*-kind product, but `c.z0 * ohm`
            // is already an ohm-kind quantity that initializes the Impedance field directly.
            .z0  = c.z0 * ohm,
            // The display numbers are pF/cm and ps/cm; we store SI and let the caller print
            // via .in(pico<farad>/centi<metre>) etc. (1 pF/cm = 1e-10 F/m.)
            .c0  = c.c0_pf_cm * (si::pico<si::farad> / si::centi<si::metre>),
            .tpd = c.tpd_ps_cm * (si::pico<si::second> / si::centi<si::metre>),
        };
    });
}

// ---- inverse solvers: one closed-form expression each; no unit branching. --------------------
namespace {
// Shared inverse core: k = Z0*sqrt(1.41 + eps_r)/87. exp(k) appears in every inverse form,
// so all three solvers agree to the last digit and the round-trip tests catch any drift.
double inv_k(double z0, double eps) { return z0 * std::sqrt(1.41 + eps) / 87.0; }
}  // namespace

emc::Result<emc::units::Length> solve_height(const MicrostripSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double H = std::exp(inv_k(Z, in.relative_permittivity)) * (0.8 * W + T) / 5.98;
    if (!(H > 0.0)) return std::unexpected(emc::domain_error("microstrip height non-positive", "height"));
    return H * U;   // re-attach the working unit; caller reads any display unit with .in(...)
}

emc::Result<emc::units::Length> solve_thickness(const MicrostripSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double T = 5.98 * H / std::exp(inv_k(Z, in.relative_permittivity)) - 0.8 * W;
    if (!(T > 0.0)) return std::unexpected(emc::domain_error("microstrip thickness non-positive", "thickness"));
    return T * U;
}

emc::Result<emc::units::Length> solve_width(const MicrostripSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double W = (5.98 * H / std::exp(inv_k(Z, in.relative_permittivity)) - T) / 0.8;
    if (!(W > 0.0)) return std::unexpected(emc::domain_error("microstrip width non-positive", "width"));
    return W * U;
}

}  // namespace emc::component
