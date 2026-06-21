// include/emc/cabling/crosstalk.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator tag
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // Frequency, Impedance, Inductance, Capacitance, Decibel

namespace emc::cabling {

// ---------------------------------------------------------------------------
//  The far-end clamp floor. When the far-end argument A_FE <= 0, log10 is
//  undefined; the model substitutes -200 dB. We name it (inline constexpr, so
//  there is ONE definition shared across translation units) so the value is
//  documented in one place and callers can detect a clamped result by value.
// ---------------------------------------------------------------------------
inline constexpr emc::units::Decibel kFarEndFloor{ -200.0 };

// ---------------------------------------------------------------------------
//  Input — all mp-units-typed. Frequency carries Hz (the call site writes
//  1 * MHz). L_m / C_m are typed Inductance / Capacitance (so a pF vs uF
//  mix-up cannot compile). The four resistances are Impedance (ohm).
//  Defaults: f=1 MHz, RL=RS=RNE=RFE=50 ohm, Lm=50 uH, Cm=50000 pF.
//  Designated-initializer-friendly defaults mean a call site only overrides
//  the fields it cares about, and the easy-to-swap R_NE/R_FE pair is named.
// ---------------------------------------------------------------------------
struct CrosstalkInput {
    emc::units::Frequency   f    = 1.0e6 * emc::units::si::hertz;       // 1 MHz
    emc::units::Impedance   R_L  = 50.0  * emc::units::si::ohm;         // load termination
    emc::units::Impedance   R_S  = 50.0  * emc::units::si::ohm;         // source termination
    emc::units::Impedance   R_NE = 50.0  * emc::units::si::ohm;         // near-end termination
    emc::units::Impedance   R_FE = 50.0  * emc::units::si::ohm;         // far-end termination
    emc::units::Inductance  L_m  = 50.0e-6 * emc::units::si::henry;     // mutual inductance, 50 uH
    emc::units::Capacitance C_m  = 50000.0e-12 * emc::units::si::farad; // mutual capacitance, 50000 pF
};

// ---------------------------------------------------------------------------
//  Result — both coupled voltages as typed Decibel values (logarithmic, so the
//  wrapper keeps them out of linear quantity arithmetic), plus a flag telling
//  the caller V_FE was clamped to the floor (so a front end can annotate it).
// ---------------------------------------------------------------------------
struct CrosstalkResult {
    emc::units::Decibel V_NE{};         // near-end coupled voltage [dB]
    emc::units::Decibel V_FE{};         // far-end  coupled voltage [dB]
    bool                far_end_clamped = false;   // true => V_FE == kFarEndFloor
};

/// Validate: frequency, L_m, C_m strictly positive; each resistance in
/// [0, 1e12] ohm; (R_S + R_L) and (R_NE + R_FE) non-zero (both are denominators).
// [[nodiscard]]: ignoring a validation result is a bug, so the compiler warns.
[[nodiscard]] std::expected<void, emc::Error> validate(const CrosstalkInput& in);

/// Compute near-end and far-end crosstalk (forward only, two outputs).
/// Applies the documented V_FE = -200 dB clamp when the far-end argument <= 0.
[[nodiscard]] emc::Result<CrosstalkResult> calculate(const CrosstalkInput& in);

// ---- Calculator concept binding (compile-time contract, foundation) --------
// Tag type: names the (Input, Result, calculate, validate) quadruple so generic
// code and the static_assert below can check the contract at compile time.
struct Crosstalk {
    using Input  = CrosstalkInput;
    using Result = CrosstalkResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::cabling::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::cabling::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Crosstalk>);

} // namespace emc::cabling
