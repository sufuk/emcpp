// include/emc/component/harmonic_trap.hpp
#pragma once

#include <expected>

#include <mp-units/systems/si.h>      // si::volt, si::nano<si::second> for the field defaults

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, ErrorCode, validators
#include <emc/core/units.hpp>        // emc::units::Frequency / Time / Voltage

namespace emc::component {

// ===========================================================================
//  Harmonic Trap (trapezoidal-pulse-train spectrum)
//
//  Forward-only, multi-output calculator. From a trapezoidal pulse train it
//  emits FOUR quantities at once: the fundamental frequency f0 = 1/T, the
//  n-th harmonic frequency f = n*f0, the exact two-sinc harmonic line
//  amplitude A_h (RMS), and the piecewise 0 / -20 / -40 dB-per-decade
//  envelope amplitude A_e (RMS).
// ===========================================================================

// ---------------------------------------------------------------------------
//  HarmonicTrapInput — the trapezoidal-pulse-train description.
//
//  Every time-valued field is a typed mp-units quantity, so a caller-supplied
//  value in s / ms / us / ns resolves to one type with a compile-time-derived
//  factor (a Length passed as `period` would not compile). `harmonic` (n) and
//  `duty_cycle` are dimensionless doubles because they appear bare in the
//  spectrum formula (n is an integer index; DC is a percentage).
//
//  Member defaults describe a representative pulse train (n=3, A_m=10 V,
//  t_r=5 ns, T=50 ns, DC=50 %), so a default-constructed Input is valid and
//  designated-initializer call sites can override only what they care about.
// ---------------------------------------------------------------------------
struct HarmonicTrapInput {
    double               harmonic   = 3.0;                                              // n  [-] (harmonic index; integer-valued in practice)
    emc::units::Voltage  amplitude  = 10.0 * mp_units::si::volt;                        // A_m [V]
    emc::units::Time     transition = 5.0  * mp_units::si::nano<mp_units::si::second>;  // t_r [s]
    emc::units::Time     period     = 50.0 * mp_units::si::nano<mp_units::si::second>;  // T   [s]
    double               duty_cycle = 50.0;                                            // DC [%] (0 < DC <= 100)
};

// ---------------------------------------------------------------------------
//  HarmonicTrapResult — the four outputs, each a typed quantity (no display
//  unit baked in; the caller picks one with .in(...)). f0/f are real
//  frequencies; A_h/A_e are RMS voltages. The two voltages cannot be swapped
//  with the two frequencies because their TYPES differ.
// ---------------------------------------------------------------------------
struct HarmonicTrapResult {
    emc::units::Frequency fundamental_frequency{};   // f0  [Hz]
    emc::units::Frequency harmonic_frequency{};      // f   [Hz]
    emc::units::Voltage   harmonic_amplitude{};      // A_h [V_rms]
    emc::units::Voltage   envelope_amplitude{};      // A_e [V_rms]
};

// ---------------------------------------------------------------------------
//  validate() — rejects out-of-domain inputs (DC=0, T=0, t_r=0) that would
//  otherwise produce inf/nan, returning a descriptive emc::Error.
//  [[nodiscard]]: dropping a validation result is a bug, so the compiler warns.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error> validate(const HarmonicTrapInput& in);

// ---------------------------------------------------------------------------
//  calculate() — forward, multi-output. Returns all four spectral quantities
//  or the first validation Error.
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<HarmonicTrapResult> calculate(const HarmonicTrapInput& in);

// ---------------------------------------------------------------------------
//  Concept binding — names the (Input, Result, calculate, validate) quad as a
//  type, checked at compile time so a signature drift is a build error at the
//  point of the mistake.
// ---------------------------------------------------------------------------
struct HarmonicTrap {
    using Input  = HarmonicTrapInput;
    using Result = HarmonicTrapResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<HarmonicTrap>);

} // namespace emc::component
