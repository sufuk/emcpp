// include/emc/shielding/shielding_effectiveness.hpp
//
// Shielding-Effectiveness leaf calculators (emc::shielding):
//   * Near-Field SE  — wave impedance set by the dominant E (high-Z) or H (low-Z)
//                      field component near the source.
//   * Plane-Wave SE  — far-field wave impedance fixed at the 377 ohm of free space.
//
// Both share the SAME closed form for skin depth (delta) and the N_s term, and both
// route pi through emc::constants::pi, so they agree bit-for-bit on the physics they
// share (the test suite asserts this directly).
//
// The Input carries an explicit `conductivity` field, so a caller may pin sigma to any
// value (e.g. 5.80e7 S/m for a reference copper run), while the near_field_se /
// plane_wave_se convenience overloads read the canonical emc::materials table.
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/materials.hpp>    // emc::materials::Material, properties()
#include <emc/core/units.hpp>        // emc::units::{Conductivity, Length, Frequency, Decibel}

namespace emc::shielding {

// Which near-field component dominates the wave impedance.
// enum class: a typed, exhaustively-switchable choice the caller states explicitly,
// so an ambiguous/unset branch can never reach the math (unlike a runtime bool flag).
enum class FieldType {
    Electric,   // high-impedance: Zw = 1/(2*pi*f*eps0*r)
    Magnetic,   // low-impedance:  Zw = 2*pi*f*mu0*r
};

// ---------------------------------------------------------------------------
//  ShieldingResult — the three dB terms shared by both SE calculators.
//  Invariant: shielding == absorption_loss + reflection_loss.
//  Each output is emc::units::Decibel (a typed log wrapper), never a linear
//  mp-units unit, so a dB value can never be accidentally added to a power in W.
// ---------------------------------------------------------------------------
struct ShieldingResult {
    emc::units::Decibel absorption_loss{};   // AL  [dB]
    emc::units::Decibel reflection_loss{};   // RL  [dB]
    emc::units::Decibel shielding{};         // SE = AL + RL [dB]
};

// ===========================================================================
//  Near-Field SE
// ===========================================================================

// ---------------------------------------------------------------------------
//  NearFieldSeInput — material given by explicit conductivity + relative
//  permeability; plus thickness, source-to-shield distance, frequency, and the
//  E/H field branch. Lengths/frequency are real mp-units quantities, so a caller
//  may pass 40 * mil or 1 * MHz and the library converts exactly.
// ---------------------------------------------------------------------------
struct NearFieldSeInput {
    emc::units::Conductivity conductivity{};               // sigma  [S/m]
    double                   relative_permeability = 1.0;  // mu_r   [-]
    emc::units::Length       thickness{};                  // t      [m]
    emc::units::Length       distance{};                   // r      [m]  (source-to-shield)
    emc::units::Frequency    frequency{};                  // f      [Hz]
    FieldType                field = FieldType::Electric;
};

// Reject non-positive sigma / t / r / f / mu_r (also keeps log10 off a <= 0 argument).
[[nodiscard]] std::expected<void, emc::Error> validate(const NearFieldSeInput& in);

// Forward calculation: NearFieldSeInput -> {AL, RL, SE}.
[[nodiscard]] emc::Result<ShieldingResult> calculate(const NearFieldSeInput& in);

// Convenience: build the input from an emc::materials::Material (canonical table).
// Chains the material lookup into calculate() so an unknown material short-circuits
// to ErrorCode::UnknownMaterial without an if-ladder.
[[nodiscard]] emc::Result<ShieldingResult>
near_field_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Length distance,
              emc::units::Frequency frequency, FieldType field);

// ===========================================================================
//  Plane-Wave SE
// ===========================================================================

// ---------------------------------------------------------------------------
//  PlaneWaveSeInput — like NearField but with NO distance and NO field branch
//  (the wave impedance is the fixed 377 ohm of free space).
// ---------------------------------------------------------------------------
struct PlaneWaveSeInput {
    emc::units::Conductivity conductivity{};               // sigma  [S/m]
    double                   relative_permeability = 1.0;  // mu_r   [-]
    emc::units::Length       thickness{};                  // t      [m]
    emc::units::Frequency    frequency{};                  // f      [Hz]
};

// Reject non-positive sigma / t / f / mu_r.
[[nodiscard]] std::expected<void, emc::Error> validate(const PlaneWaveSeInput& in);

// Forward calculation: PlaneWaveSeInput -> {AL, RL, SE}.
[[nodiscard]] emc::Result<ShieldingResult> calculate(const PlaneWaveSeInput& in);

// Convenience overload from a material id (canonical table).
[[nodiscard]] emc::Result<ShieldingResult>
plane_wave_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Frequency frequency);

// ===========================================================================
//  Calculator-concept bindings (compile-time contract; see 00-foundation-code.md).
//  Each tag is a zero-data type that names (Input, Result) and forwards to the
//  free calculate()/validate(), so generic code can treat the triple as one thing.
// ===========================================================================

struct NearFieldSe {
    using Input  = NearFieldSeInput;
    using Result = ShieldingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<NearFieldSe>);

struct PlaneWaveSe {
    using Input  = PlaneWaveSeInput;
    using Result = ShieldingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<PlaneWaveSe>);

} // namespace emc::shielding
