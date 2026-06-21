// include/emc/core/units.hpp
//
// The curated set of typed quantities used everywhere in emc. Every Input/Result
// field and every calculator signature uses these names (e.g. Frequency, Length,
// Impedance) instead of the long raw mp-units spelling. There is one Rep knob for
// the whole library. Relative permeability/permittivity are plain doubles (see note
// near Dimensionless), and dB/dBm are typed log wrappers, never linear units.
#pragma once

#include <cmath>       // std::pow / std::log10 for the dB/dBm conversions (constexpr-friendly in C++23)

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

namespace emc::units {

namespace mpu = mp_units;
namespace isq = mp_units::isq;
namespace si  = mp_units::si;

// --- One number type for the whole library. -----------------------------------
// Change this single line to switch every quantity's storage (e.g. to float).
using Rep = double;

// --- Short helper so each alias below fits on one line. -----------------------
// mp-units quantity: the unit lives in the TYPE, so wrong-unit math will not compile.
template <auto Reference>
using Q = mpu::quantity<Reference, Rep>;

// ----------------------------------------------------------------------
//  Core EMC quantities — each pins BOTH the kind of quantity AND its SI unit,
//  so e.g. a frequency can never be passed where a length is expected.
// ----------------------------------------------------------------------
using Frequency   = Q<isq::frequency[si::hertz]>;                         // Hz
using Length      = Q<isq::length[si::metre]>;                           // m
using Area        = Q<isq::area[si::metre * si::metre]>;                 // m^2
using Time        = Q<isq::time[si::second]>;                            // s

using Voltage     = Q<isq::voltage[si::volt]>;                          // V
using Current     = Q<isq::electric_current[si::ampere]>;              // A
using Power       = Q<isq::power[si::watt]>;                           // W
using Impedance   = Q<isq::resistance[si::ohm]>;                        // ohm  (Z0, R, RL, ...)
using Capacitance = Q<isq::capacitance[si::farad]>;                    // F
using Inductance  = Q<isq::inductance[si::henry]>;                     // H

// Material / field quantities
using Conductivity =
    Q<isq::conductivity[si::siemens / si::metre]>;                       // S/m
using Resistivity =
    Q<isq::resistivity[si::ohm * si::metre]>;                            // ohm*m
using ElectricField =
    Q<isq::electric_field_strength[si::volt / si::metre]>;               // V/m
using MagneticField =
    Q<isq::magnetic_field_strength[si::ampere / si::metre]>;             // A/m
using PowerDensity =
    Q<(isq::power / isq::area)[si::watt / (si::metre * si::metre)]>;      // W/m^2

// Angle is its own quantity here (mp-units treats the radian explicitly).
using Angle = Q<isq::angular_measure[si::radian]>;                       // rad

// ----------------------------------------------------------------------
//  Dimensionless — quantities that are pure ratios (coverage, VSWR, gain).
//
//  NOTE: relative permeability (mu_r) and relative permittivity (eps_r) are NOT
//  modeled here. They are plain doubles in the canonical API, because they are
//  bare ratios that appear unqualified in the empirical formulas (Wheeler
//  microstrip, skin depth). This generic alias is for ratio OUTPUTS (VSWR, gain,
//  optical coverage) where keeping the "kind" still adds value.
// ----------------------------------------------------------------------
using Dimensionless = Q<mpu::one>;                                       // generic ratio quantity

// ----------------------------------------------------------------------
//  Per-length results (microstrip etc.). Shared here so several guides reuse them.
// ----------------------------------------------------------------------
using CapacitancePerLength =
    Q<(isq::capacitance / isq::length)[si::farad / si::metre]>;          // F/m
using TimePerLength =
    Q<(isq::time / isq::length)[si::second / si::metre]>;                // s/m (propagation delay)

// ----------------------------------------------------------------------
//  dB / dBm — LOGARITHMIC, so they are NOT mp-units linear units. A decibel is
//  10*log10(ratio); dBm is dB relative to 1 mW. We wrap them in their own tiny
//  structs so a dB value can never be mistaken for linear power (which would make
//  "dBm + dBm" silently wrong). The linear physics stays as real units (Power in
//  watts, fields in V/m); crossing between linear and log is always an explicit call.
// ----------------------------------------------------------------------
struct Decibel { double value; };   // a pure-ratio dB value, e.g. shielding effectiveness, gain
struct Dbm     { double value; };   // dB relative to 1 mW (an absolute power level)

// Linear <-> log conversions are explicit functions only.
// [[nodiscard]]: do not ignore the result; throwing away a converted value is a bug, so the compiler warns.
// noexcept: promises not to throw, which lets the compiler optimize and callers rely on it.
[[nodiscard]] inline Power to_power(Dbm x) noexcept {            // dBm -> W
    return std::pow(10.0, x.value / 10.0) * (si::milli<si::watt>);
}
[[nodiscard]] inline Dbm to_dbm(Power p) noexcept {             // W -> dBm
    return Dbm{ 10.0 * std::log10(p.numerical_value_in(si::milli<si::watt>)) };
}
// constexpr: can run at compile time, so a dB-to-ratio value can fold into a constant.
[[nodiscard]] constexpr double to_ratio(Decibel d) noexcept {   // dB -> linear power ratio
    return std::pow(10.0, d.value / 10.0);
}
[[nodiscard]] inline Decibel to_decibel(double power_ratio) noexcept {  // linear ratio -> dB
    return Decibel{ 10.0 * std::log10(power_ratio) };
}

} // namespace emc::units
