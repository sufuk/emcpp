// src/component/embedded_microstrip_trace.cpp
#include <emc/component/embedded_microstrip_trace.hpp>
#include <emc/component/detail/board_units.hpp>   // shared exact `mil` unit (registers it on the SI graph)

#include <mp-units/systems/si.h>

#include <cmath>   // std::log, std::exp, std::sqrt (constexpr in C++23)

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::ohm;

namespace {

// Working unit. Because Z0 is NOT scale-invariant here, the working unit is load-bearing and
// pinned EXPLICITLY: the closed form expects the geometry expressed in the same coherent scale
// used to derive it. Reading the typed Length in metres fixes that absolute scale once; the type
// system guarantees the caller's display unit (mm or mil) is converted exactly, with no branching.
constexpr auto U = si::metre;

// The single source of the forward Z0 / Tpd / C0 math, on bare doubles in metres.
struct Core { double z0, c0_pf_cm, tpd_ps_cm; };

Core forward_core(double h1, double H, double T, double W, double eps) {
    const double ln  = std::log(5.98 * H / (0.8 * W + T));
    // (1 - (h1 - H - T)/0.1): the burial-depth factor; this is what makes Z0 scale-dependent.
    const double z0  = 87.0 * ln * (1.0 - (h1 - H - T) / 0.1) / std::sqrt(eps + 1.41);
    // exp(-1.55*h1/H): the embedded propagation-delay correction (pure transcendental core).
    const double tpd = 84.75 * std::sqrt(0.475 * eps * (1.0 + std::exp(-(1.55 * h1 / H))) + 0.67);
    return Core{ .z0 = z0, .c0_pf_cm = tpd / z0, .tpd_ps_cm = tpd };
}

}  // namespace

std::expected<void, emc::Error> validate(const EmbeddedMicrostripInput& in) {
    // Read in the SAME working scale (metres) used by the core, then range-check as plain doubles.
    const double h1 = in.cover_height.numerical_value_in(U);
    const double H  = in.height.numerical_value_in(U);
    const double T  = in.thickness.numerical_value_in(U);
    const double W  = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // 0.1 <= W/H <= 3 keeps the inputs inside the formula's validated geometry band.
    if (auto r = emc::in_range(W / H, 0.1, 3.0, "width_to_height_ratio"); !r) return r;

    // The cover must sit above the trace: h1 > H + T. There is no finite upper bound on the cover
    // height, so this is a DomainError (not an in_range check); it also keeps the
    // (1 - (h1 - H - T)/0.1) factor well-defined.
    if (!(h1 > H + T))
        return std::unexpected(emc::domain_error("cover_height must exceed height + thickness",
                                                 "cover_height"));
    return {};
}

emc::Result<EmbeddedMicrostripResult> calculate(const EmbeddedMicrostripInput& in) {
    // transform runs the math only if validate() succeeded, forwarding the Error otherwise —
    // so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        const double h1 = in.cover_height.numerical_value_in(U);
        const double H  = in.height.numerical_value_in(U);
        const double T  = in.thickness.numerical_value_in(U);
        const double W  = in.width.numerical_value_in(U);
        const Core c    = forward_core(h1, H, T, W, in.relative_permittivity);
        return EmbeddedMicrostripResult{
            // Re-attach physical units. Display labels are pF/cm and ps/cm; we store SI and let the
            // caller print pF/cm / ps/cm via .in(...). (1 pF/cm = 1e-10 F/m.)
            .z0  = c.z0 * ohm,
            .c0  = c.c0_pf_cm * (si::pico<si::farad> / si::centi<si::metre>),
            .tpd = c.tpd_ps_cm * (si::pico<si::second> / si::centi<si::metre>),
        };
    });
}

}  // namespace emc::component
