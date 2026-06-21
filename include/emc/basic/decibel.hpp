// include/emc/basic/decibel.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

// ---------------------------------------------------------------------------
//  Gain panel: dB <-> voltage gain <-> power gain. All three are dimensionless.
//  emc::units::Decibel is the typed log wrapper from the foundation (NOT a linear
//  mp-units unit), so a dB value can never be added to a voltage by accident.
// ---------------------------------------------------------------------------
struct GainResult {
    emc::units::Decibel decibels{};      ///< dB
    double              voltage_gain{};  ///< V1/V2  = 10^(dB/20)
    double              power_gain{};    ///< P1/P2  = 10^(dB/10)
};

// [[nodiscard]]: a discarded conversion result is a bug, so the compiler warns.
// Each driver fills the whole triple from a different starting field (bidirectional).
[[nodiscard]] emc::Result<GainResult> gain_from_db(emc::units::Decibel db);
[[nodiscard]] emc::Result<GainResult> gain_from_voltage_gain(double voltage_gain);
[[nodiscard]] emc::Result<GainResult> gain_from_power_gain(double power_gain);

// ---------------------------------------------------------------------------
//  Power/level panel: dBm <-> power <-> voltage <-> Vp, all relative to a load R.
// ---------------------------------------------------------------------------
struct LevelResult {
    emc::units::Dbm     dbm{};        ///< dBm (= dB relative to 1 mW)
    emc::units::Power   power{};      ///< P   [W]
    emc::units::Voltage voltage{};    ///< RMS voltage into the load  [V]
    emc::units::Voltage peak{};       ///< Vp of the sinusoid = V*sqrt(2)  [V]
};

/// Every solver needs the load R to convert between power and voltage.
[[nodiscard]] emc::Result<LevelResult> level_from_dbm(emc::units::Dbm dbm, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_power(emc::units::Power power, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_voltage(emc::units::Voltage v, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_peak(emc::units::Voltage vp, emc::units::Impedance load);

} // namespace emc::basic
