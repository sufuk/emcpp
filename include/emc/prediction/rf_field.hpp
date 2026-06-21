// include/emc/prediction/rf_field.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // Length, ElectricField, MagneticField, PowerDensity, Dbm, Decibel

namespace emc::prediction {

// Unit symbols (m, km, ...) for the field defaults below. Pulled in at namespace
// scope so the header's designated-initializer defaults can name literal units.
using namespace mp_units::si::unit_symbols;   // m, km

// ===========================================================================
//  RF far-field from EIRP:  E = sqrt(30 * P_t_W * G_t_linear) / d
//                           H = E / (120*pi),   P_D = E * H
// ===========================================================================

// Inputs for the EIRP far-field E/H/power-density model.
// Power level and gain are LOG quantities, so they use the canonical typed
// wrappers (never bare doubles): a dBm power level can never be silently added
// to a linear watt, and a dBi gain stays distinct from an absolute level.
//   transmit_power : emc::units::Dbm     (dB relative to 1 mW)
//   gain           : emc::units::Decibel (a dBi gain — a pure power ratio in dB)
// Defaults describe a usable scenario (1 W EIRP base, 10 dBi, 10 km).
struct RfFieldInput {
    emc::units::Dbm     transmit_power{30.0};   // P_t [dBm]
    emc::units::Decibel gain{10.0};             // G_t [dBi]
    emc::units::Length  distance{10.0 * km};    // d   (> 0)
};

// All three far-field outputs, each carrying its own SI dimension.
struct RfFieldResult {
    emc::units::ElectricField electric_field{};   // E   [V/m]
    emc::units::MagneticField magnetic_field{};   // H   [A/m]
    emc::units::PowerDensity  power_density{};     // P_D [W/m^2]
};

// [[nodiscard]]: a dropped validation result is a bug — d <= 0 is the only divide.
[[nodiscard]] std::expected<void, emc::Error> validate(const RfFieldInput& in);
[[nodiscard]] emc::Result<RfFieldResult> calculate(const RfFieldInput& in);

// Tag type: names the (Input, Result, calculate, validate) quad so generic code
// and the static_assert below can check the contract at compile time.
struct RfField {
    using Input  = RfFieldInput;
    using Result = RfFieldResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<RfField>);

} // namespace emc::prediction
