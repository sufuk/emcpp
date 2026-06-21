// include/emc/prediction/friis.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator concept
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // Power, Frequency, Length, Decibel, Dbm

namespace emc::prediction {

// Unit symbols (W, Hz, m, ...) for the input defaults below. Pulled in at
// namespace scope so the header's designated-initializer defaults can name
// literal units like 1.0 * W.
using namespace mp_units::si::unit_symbols;   // W, Hz, m

// ===========================================================================
//  Friis link-budget (received power, in dBm):
//      P_rx = 30 + 10*log10( P_tx * 10^(G_tx/10) * 10^(G_rx/10)
//                            * (c / (4*pi*R*f))^2 )
//
//  P_tx is a LINEAR transmit power (watts); the 30 + 10*log10 prefix lifts the
//  whole bracket to dBm. Only G_tx / G_rx are dB terms. c is the exact speed of
//  light, R the range, f the frequency.
// ===========================================================================

// Inputs for the Friis link-budget.
// P_tx is a linear transmit power, so it is typed as emc::units::Power (watts) —
// this makes the "P_tx is NOT a dBm level" rule explicit at the type level; the
// two antenna gains are dBi, modeled with the canonical Decibel log wrapper so a
// gain can never be silently mixed with a linear watt. Defaults describe a small,
// valid link so a default-constructed Input is immediately usable.
struct FriisInput {
    emc::units::Power     tx_power{1.0 * W};        // P_tx  (linear watt, > 0)
    emc::units::Decibel   tx_gain{0.5};             // G_tx  [dBi]
    emc::units::Decibel   rx_gain{0.25};            // G_rx  [dBi]
    emc::units::Frequency frequency{1000.0 * Hz};   // f     (!= 0)
    emc::units::Length    range{10.0 * m};          // R     (!= 0)
};

// Friis output: the received power as a typed dBm LEVEL. Returning Dbm (not a
// bare double) stops a downstream consumer feeding this log level into a
// linear-watt formula by accident.
struct FriisResult {
    emc::units::Dbm received_power{};   // P_rx [dBm]
};

// [[nodiscard]]: a dropped validation result is a bug — P_tx <= 0 (log domain),
// and R == 0 / f == 0 (the 1/(R*f) divide) are the failure modes.
[[nodiscard]] std::expected<void, emc::Error> validate(const FriisInput& in);
[[nodiscard]] emc::Result<FriisResult> calculate(const FriisInput& in);

// Tag type: names the (Input, Result, calculate, validate) quad so generic code
// and the static_assert below can check the contract at compile time.
struct Friis {
    using Input  = FriisInput;
    using Result = FriisResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Friis>);

} // namespace emc::prediction
