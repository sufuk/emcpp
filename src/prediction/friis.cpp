// src/prediction/friis.cpp
#include <emc/prediction/friis.hpp>

#include <cmath>   // std::pow, std::log10

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>   // emc::constants::c, ::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // W, Hz, m, s

std::expected<void, emc::Error> validate(const FriisInput& in) {
    // P_tx > 0 : it sits inside log10(); P_tx <= 0 is a math-domain error.
    // require_positive returns std::expected<void,Error>; pass its error straight up.
    if (auto ok = emc::require_positive(in.tx_power.numerical_value_in(W), "tx_power"); !ok)
        return ok;
    // R and f appear in the denominator (c/(4*pi*R*f))^2, so both must be non-zero
    // (== 0 would divide by zero and yield inf).
    if (auto ok = emc::require_nonzero(in.range.numerical_value_in(m), "range"); !ok)
        return ok;
    if (auto ok = emc::require_nonzero(in.frequency.numerical_value_in(Hz), "frequency"); !ok)
        return ok;
    return {};
}

emc::Result<FriisResult> calculate(const FriisInput& in) {
    // transform runs the math only if validate() succeeded, forwarding the Error
    // otherwise — so the formula is unreachable on bad input, with no if/return noise.
    return validate(in).transform([&] {
        // Pull plain doubles in each field's SI unit for the raw-double link-budget math.
        const double Ptx = in.tx_power.numerical_value_in(W);
        const double Gtx = in.tx_gain.value;     // dBi (the Decibel wrapper's dB number)
        const double Grx = in.rx_gain.value;     // dBi
        const double R   = in.range.numerical_value_in(m);
        const double f   = in.frequency.numerical_value_in(Hz);

        // c is the one exact, unit-bearing speed-of-light constant; read it as m/s.
        // It enters squared inside log10, so full precision keeps the budget dB-accurate.
        const double c = emc::constants::c.numerical_value_in(m / s);

        // Free-space term (c / (4*pi*R*f))^2. Full-precision pi from the shared constant.
        const double fspl_term = std::pow(c / (4.0 * emc::constants::pi * R * f), 2.0);

        // P_rx = 30 + 10*log10( P_tx * 10^(G_tx/10) * 10^(G_rx/10) * fspl_term ).
        // The two 10^(dB/10) factors turn each dBi gain into a linear power ratio.
        const double Prx_dBm =
            30.0 + 10.0 * std::log10(Ptx
                                     * std::pow(10.0, Gtx / 10.0)
                                     * std::pow(10.0, Grx / 10.0)
                                     * fspl_term);

        // P_rx is genuinely a dBm level, so wrap it in the typed Dbm log wrapper.
        return FriisResult{ .received_power = emc::units::Dbm{Prx_dBm} };
    });
}

} // namespace emc::prediction
