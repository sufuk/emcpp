// include/emc/converter/energy_frequency.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator concepts (kept for the family surface)
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Frequency, emc::units::Q alias

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace emc::converter {

// ===========================================================================
//  Energy <-> Frequency :  E = h * f      (Planck relation, h = emc::constants::h)
//
//  Bidirectional, so it is modeled as TWO distinctly named free functions
//  (solve_energy / solve_frequency) sharing one detail:: core. Naming each
//  direction beats a single "magic target" function: the call site reads which
//  way it is converting, and the shared core keeps forward/inverse exact
//  inverses (no chance to typo a stray factor into only one direction).
// ===========================================================================

// Energy in joules (mp-units). The unit lives in the TYPE, so wrong-unit math
// will not compile. eV is a display-only boundary handled by the helpers below;
// internally energy always stays in joules so the h*f physics is exact.
using Energy = emc::units::Q<mp_units::isq::energy[mp_units::si::joule]>;

// ----- J <-> eV display boundary --------------------------------------------
// eV is not an SI base unit here. We cross it with the exact 2019-SI elementary
// charge: 1 eV = 1.602176634e-19 J. These are constexpr so the eV boundary folds
// at compile time (C++23 constexpr math) and feeds static_asserts in tests.
inline constexpr double joules_per_ev = 1.602'176'634e-19;   // exact (2019 SI elementary charge)
[[nodiscard]] constexpr double ev_to_joule(double ev) noexcept { return ev * joules_per_ev; }
[[nodiscard]] constexpr double joule_to_ev(double j)  noexcept { return j  / joules_per_ev; }

// ----- forward: frequency -> energy -----------------------------------------
struct FrequencyToEnergyInput { emc::units::Frequency frequency{}; };  // f (> 0)
struct EnergyResult          { Energy energy{}; };                     // E [J]

// [[nodiscard]]: a dropped validation result / energy value is a bug, so the
// compiler warns if a caller ignores it.
[[nodiscard]] std::expected<void, emc::Error> validate(const FrequencyToEnergyInput& in);
[[nodiscard]] emc::Result<EnergyResult>       solve_energy(const FrequencyToEnergyInput& in);

// ----- inverse: energy -> frequency -----------------------------------------
struct EnergyToFrequencyInput { Energy energy{}; };                    // E (> 0)
struct FrequencyResult        { emc::units::Frequency frequency{}; };  // f [Hz]

[[nodiscard]] std::expected<void, emc::Error> validate(const EnergyToFrequencyInput& in);
[[nodiscard]] emc::Result<FrequencyResult>    solve_frequency(const EnergyToFrequencyInput& in);

} // namespace emc::converter
