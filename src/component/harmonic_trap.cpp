// src/component/harmonic_trap.cpp
#include <emc/component/harmonic_trap.hpp>

#include <cmath>      // std::sin, std::abs  (constexpr in C++23, but kept runtime here)
#include <numbers>    // std::numbers::sqrt2_v  (exact sqrt(2))

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::component {

namespace {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, s, V

// sqrt(2) RMS scale factor (unit-peak fundamental), exact at double precision.
inline constexpr double kSqrt2 = std::numbers::sqrt2_v<double>;

// sinc_pi(x) = sin(x)/x, with the removable singularity at x=0 handled.
// Not reached for valid inputs (n>=1 and DC>0), but kept defensive + clear.
// [[maybe_unused]]: documents the math intent even though calculate() inlines
// the ratio directly; suppresses the -Wunused-function warning under -Werror.
// noexcept: pure math, never throws.
[[maybe_unused]] [[nodiscard]] double sinc_pi(double x) noexcept {
    if (x == 0.0) return 1.0;   // removable singularity: sin(x)/x -> 1 as x -> 0
    return std::sin(x) / x;
}

} // namespace

// ---------------------------------------------------------------------------
//  validate() — reject out-of-domain inputs before any arithmetic runs.
//  Ranges mirror the physically meaningful domain:
//    n          : harmonic index, must be >= 1 (and finite).
//    amplitude  : peak voltage, must be > 0 (0 is degenerate: A_h=A_e=0).
//    transition : edge time t_r, must be > 0 (t_r=0 -> pitrinv = 1/(pi*0) = inf).
//    period     : T, must be > 0 (T=0 -> f0 = 1/0 = inf).
//    duty_cycle : 0 < DC <= 100 (DC=0 -> div/0 in every sinc/tau term; DC>100 nonphysical).
//
//  Each validator returns std::expected<void, Error>; on failure we return that
//  same expected straight through (the function's return type matches), so there
//  is no manual std::unexpected wrapping for the validator results.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const HarmonicTrapInput& in) {
    // n: must be a positive harmonic index.
    if (auto r = emc::require_positive(in.harmonic, "harmonic"); !r)
        return r;
    if (in.harmonic < 1.0)
        return std::unexpected(emc::out_of_range(1.0, 1.0e9, "harmonic"));

    // amplitude > 0  (compared in volts, the field's natural unit).
    if (auto r = emc::require_positive(in.amplitude.numerical_value_in(V), "amplitude"); !r)
        return r;

    // transition time t_r > 0 (in seconds) — guards pitrinv = 1/(pi*t_r).
    if (auto r = emc::require_positive(in.transition.numerical_value_in(s), "transition"); !r)
        return r;

    // period T > 0 (in seconds) — guards f0 = 1/T and pitauinv = 1/(pi*(DC/100)*T).
    if (auto r = emc::require_positive(in.period.numerical_value_in(s), "period"); !r)
        return r;

    // duty cycle in (0, 100] % — guards every (DC/100) denominator.
    if (auto r = emc::in_range(in.duty_cycle, 0.0, 100.0, "duty_cycle"); !r)
        return r;
    if (auto r = emc::require_nonzero(in.duty_cycle, "duty_cycle"); !r)
        return r;   // DC==0 -> DivisionByZero (not just OutOfRange) so the caller learns exactly why

    return {};   // all good
}

// ---------------------------------------------------------------------------
//  calculate() — pull the raw seconds / volts out of the typed quantities ONCE,
//  evaluate the closed form in those coherent units, then re-attach units to
//  the four results.
// ---------------------------------------------------------------------------
emc::Result<HarmonicTrapResult> calculate(const HarmonicTrapInput& in) {
    // Run the math only on valid input; forward the first Error otherwise.
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Extract numeric values in coherent SI units (seconds, volts). mp-units
    // has already guaranteed the inputs are the right KIND of quantity.
    const double n   = in.harmonic;                          // [-]
    const double Am  = in.amplitude.numerical_value_in(V);   // [V]
    const double trS = in.transition.numerical_value_in(s);  // [s]
    const double Ts  = in.period.numerical_value_in(s);      // [s]
    const double DC  = in.duty_cycle;                        // [%]
    const double pi  = emc::constants::pi;                   // full-precision pi

    const double dc  = DC / 100.0;          // duty fraction

    // ---- Fundamental & harmonic frequency -------------------------------
    const double Fo = 1.0 / Ts;             // [Hz]  (T already validated > 0)
    const double F  = n * Fo;               // [Hz]

    // ---- Harmonic amplitude A_h (the two-sinc trapezoid line, RMS) -------
    // First sinc argument:  x1 = n*pi*dc        (pulse-width term)
    // Second sinc argument: x2 = n*pi*(t_r/T)   (edge term)
    const double x1 = n * pi * dc;
    const double x2 = n * pi * (trS / Ts);

    // |sin(x1)/x1| * |sin(x2)| / x2. The abs wraps the WHOLE width sinc but only
    // the NUMERATOR of the edge sinc; since x2 > 0 for valid inputs the two forms
    // are identical here — this matches the standard trapezoidal-spectrum form.
    const double Ah =
        kSqrt2 * (Am * dc)
        * std::abs(std::sin(x1) / x1)
        * std::abs(std::sin(x2)) / x2;

    // ---- Envelope amplitude A_e (piecewise 0 / -20 / -40 dB/dec) ---------
    const double pitauinv = 1.0 / (pi * dc * Ts);   // 1/(pi * tau),  tau = dc*T
    const double pitrinv  = 1.0 / (pi * trS);       // 1/(pi * t_r)

    double Ae = kSqrt2 * (Am * dc);                 // flat top (below the first breakpoint)
    if (F > pitauinv)
        Ae = (kSqrt2 * Am) / (n * pi);              // -20 dB/dec branch
    if (F > pitrinv)
        Ae = Ae * pitrinv / F;                      // -40 dB/dec branch

    // ---- Re-attach units to the four outputs ----------------------------
    // Each product (value * Hz / value * V) has a derived quantity kind; storing
    // it with DIRECT init {..} runs the explicit "this IS a Frequency/Voltage"
    // relabel that copy-init (=) would refuse on purpose.
    const emc::units::Frequency f0{ Fo * Hz };
    const emc::units::Frequency f { F  * Hz };
    const emc::units::Voltage   ah{ Ah * V  };
    const emc::units::Voltage   ae{ Ae * V  };

    return HarmonicTrapResult{
        .fundamental_frequency = f0,
        .harmonic_frequency    = f,
        .harmonic_amplitude    = ah,
        .envelope_amplitude    = ae,
    };
}

} // namespace emc::component
