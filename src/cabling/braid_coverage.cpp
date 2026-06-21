// src/cabling/braid_coverage.cpp
#include <emc/cabling/braid_coverage.hpp>

#include <cmath>   // std::atan, std::sin (trig stays runtime here)

#include <mp-units/systems/si.h>     // si units (metre, radian, one)

#include <emc/core/constants.hpp>    // emc::constants::pi

namespace emc::cabling {

using namespace mp_units;
using mp_units::si::metre;
using mp_units::one;

std::expected<void, emc::Error> validate(const BraidCoverageInput& in) {
    // Pull each length out in metres so any reported range is in the user's unit.
    const double d = in.d.numerical_value_in(metre);
    const double D = in.D.numerical_value_in(metre);

    // Every geometric quantity must be strictly positive (a zero strand diameter,
    // zero braid OD, or zero pick density is not a cable). require_positive also
    // rejects NaN (NaN > 0 is false), so a garbage input cannot slip through.
    if (auto r = emc::require_positive(d, "d");    !r) return r;
    if (auto r = emc::require_positive(D, "D");    !r) return r;
    if (auto r = emc::require_positive(in.P, "P"); !r) return r;
    if (auto r = emc::require_positive(in.N, "N"); !r) return r;

    // C is a denominator inside atan(2*pi*(D+2d)*P / C): it must be non-zero AND,
    // being a physical carrier count, positive.
    if (auto r = emc::require_positive(in.C, "C"); !r) return r;

    return {};   // ok
}

emc::Result<BraidCoverageResult> calculate(const BraidCoverageInput& in) {
    // Run validation first; forward the typed Error on bad input so the formula
    // below is unreachable for out-of-domain geometry.
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Evaluate the geometry in coherent SI (metres). The formula is a pure ratio,
    // so working in one length unit keeps the arithmetic exact.
    const double d = in.d.numerical_value_in(metre);
    const double D = in.D.numerical_value_in(metre);
    const double P = in.P;
    const double C = in.C;
    const double N = in.N;

    // theta = atan( 2*pi*(D + 2d)*P / C )  — the braid weave angle.
    const double theta = std::atan(2.0 * emc::constants::pi * (D + 2.0 * d) * P / C);

    // sin(theta) is a denominator for F. atan() returns (-pi/2, pi/2); with all
    // inputs positive the argument is > 0, so theta in (0, pi/2) and sin(theta) > 0.
    // Guard anyway so a pathological input becomes a typed DivisionByZero, never inf.
    const double s = std::sin(theta);
    if (auto r = emc::require_nonzero(s, "sin(theta)"); !r)
        return std::unexpected(r.error());

    // F = (P * N * d) / sin(theta)  — single-end fill fraction.
    const double F = (P * N * d) / s;

    // OC = 2F - F^2  (kept as a fraction here; percent() applies the *100).
    const double oc_fraction = 2.0 * F - F * F;

    // Direct-init {..} relabels each derived-kind value into its named alias:
    // oc_fraction*one is a dimension-one quantity -> Dimensionless; theta*radian
    // -> Angle. Copy-init (=) would not perform this explicit relabel — on purpose.
    return BraidCoverageResult{
        .optical_coverage = emc::units::Dimensionless{ oc_fraction * one },
        .weave_angle      = emc::units::Angle{ theta * mp_units::si::radian },
        .fill_factor      = F,
    };
}

// Descriptive alias requested by the inventory (doc 07); same body.
emc::Result<BraidCoverageResult> braid_optical_coverage(const BraidCoverageInput& in) {
    return calculate(in);
}

} // namespace emc::cabling
