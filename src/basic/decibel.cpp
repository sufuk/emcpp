// src/basic/decibel.cpp
#include <emc/basic/decibel.hpp>

#include <cmath>   // std::pow, std::log10, std::sqrt

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // V, W, ohm

namespace detail {

// Build the full gain triple from a dB value (the single source of truth).
[[nodiscard]] GainResult gain_core(double db) {
    // Designated initializers name every field, so the two gains can never be swapped.
    return GainResult{
        .decibels     = emc::units::Decibel{db},
        .voltage_gain = std::pow(10.0, db / 20.0),   // V_gain = 10^(dB/20)
        .power_gain   = std::pow(10.0, db / 10.0),   // P_gain = 10^(dB/10)
    };
}

// Build the full level quad from a power and a load (the single source of truth).
[[nodiscard]] emc::Result<LevelResult> level_core(emc::units::Power power, emc::units::Impedance load) {
    // Pull plain doubles in the field's SI unit for the validators and raw-double math.
    const double p_w = power.numerical_value_in(W);
    const double r   = load.numerical_value_in(ohm);

    // require_positive returns std::expected<void,Error>; rewrap its error into Result.
    if (auto v = emc::require_positive(p_w, "power"); !v) return std::unexpected(v.error());
    if (auto v = emc::require_positive(r,   "load");  !v) return std::unexpected(v.error());

    // dBm = 10*log10(P / 1mW): reading P in milliwatts already divides by 1 mW.
    const double dbm = 10.0 * std::log10(power.numerical_value_in(si::milli<W>));
    // V = sqrt(P*R)  (RMS into the load); the sqrt-of-(W*ohm) is volts, so direct-init
    // {..} relabels the derived value into the named Voltage alias on purpose.
    const emc::units::Voltage v_rms{ (std::sqrt(p_w * r) * V).in(V) };
    const emc::units::Voltage v_pk { (std::sqrt(2.0) * v_rms).in(V) };   // Vp = V*sqrt(2)

    return LevelResult{
        .dbm     = emc::units::Dbm{dbm},
        .power   = power,
        .voltage = v_rms,
        .peak    = v_pk,
    };
}

} // namespace detail

// ---- gain panel ----------------------------------------------------------
emc::Result<GainResult> gain_from_db(emc::units::Decibel db) {
    return detail::gain_core(db.value);
}

emc::Result<GainResult> gain_from_voltage_gain(double voltage_gain) {
    // log10(<=0) is a domain error, so guard before taking the log.
    if (auto v = emc::require_positive(voltage_gain, "voltage_gain"); !v)
        return std::unexpected(v.error());
    return detail::gain_core(20.0 * std::log10(voltage_gain));   // dB = 20*log10(V_gain)
}

emc::Result<GainResult> gain_from_power_gain(double power_gain) {
    if (auto v = emc::require_positive(power_gain, "power_gain"); !v)
        return std::unexpected(v.error());
    return detail::gain_core(10.0 * std::log10(power_gain));     // dB = 10*log10(P_gain)
}

// ---- power / level panel -------------------------------------------------
emc::Result<LevelResult> level_from_dbm(emc::units::Dbm dbm, emc::units::Impedance load) {
    // P[W] = 10^((dBm - 30)/10); the -30 is the mW->W shift. Direct-init relabels W.
    const emc::units::Power power{ (std::pow(10.0, (dbm.value - 30.0) / 10.0) * W).in(W) };
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_power(emc::units::Power power, emc::units::Impedance load) {
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_voltage(emc::units::Voltage v, emc::units::Impedance load) {
    const double r = load.numerical_value_in(ohm);
    // A zero load divides by zero in P = V^2/R, so reject it as DivisionByZero here.
    if (auto chk = emc::require_nonzero(r, "load"); !chk) return std::unexpected(chk.error());
    // P = V^2 / R: (V*V/ohm) is dimension-checked to a power; .in(W) then direct-init.
    const emc::units::Power power{ (v * v / load).in(W) };
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_peak(emc::units::Voltage vp, emc::units::Impedance load) {
    // V_rms = Vp / sqrt(2), then defer to the voltage solver (single conversion path).
    return level_from_voltage(emc::units::Voltage{ (vp / std::sqrt(2.0)).in(V) }, load);
}

} // namespace emc::basic
