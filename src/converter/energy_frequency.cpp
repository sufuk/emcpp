// src/converter/energy_frequency.cpp
#include <emc/converter/energy_frequency.hpp>

#include <emc/core/constants.hpp>   // emc::constants::h (exact Planck constant)

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;   // hertz symbol for numeric extraction / relabel
using mp_units::si::unit_symbols::J;    // joule symbol for numeric extraction / relabel

namespace detail {

// One shared core: E = h * f. BOTH solvers route through the same exact h, so
// the forward and inverse can never drift out of being exact inverses (a
// duplicated factor in each direction would be an easy place to typo).

// E = h * f. [J*s] * [1/s] = [J]. The product has a derived quantity "kind",
// so .in(J) relabels it and we direct-init {..} into the named Energy type
// (copy-init = would NOT run that explicit relabel — deliberate in mp-units).
[[nodiscard]] inline Energy energy_from_frequency(emc::units::Frequency f) {
    return Energy{ (emc::constants::h * f).in(J) };
}

// f = E / h. [J] / [J*s] = [1/s]. Same direct-init relabel into Frequency.
[[nodiscard]] inline emc::units::Frequency frequency_from_energy(Energy e) {
    return emc::units::Frequency{ (e / emc::constants::h).in(Hz) };
}

} // namespace detail

// ----- forward: frequency -> energy -----------------------------------------

std::expected<void, emc::Error> validate(const FrequencyToEnergyInput& in) {
    // Pull the numeric value in Hz and range-check as a plain double. f > 0 is
    // the physical domain (a photon has positive frequency); require_positive
    // also rejects NaN, so the math is unreachable on a poisoned input.
    return emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency");
}

emc::Result<EnergyResult> solve_energy(const FrequencyToEnergyInput& in) {
    // transform runs the math only if validate() succeeded and forwards the
    // Error otherwise, so the formula never sees a bad input (no if/return noise).
    return validate(in).transform([&] {
        return EnergyResult{ .energy = detail::energy_from_frequency(in.frequency) };
    });
}

// ----- inverse: energy -> frequency -----------------------------------------

std::expected<void, emc::Error> validate(const EnergyToFrequencyInput& in) {
    // E > 0 is the physical domain; guards against a non-positive / NaN energy.
    return emc::require_positive(in.energy.numerical_value_in(J), "energy");
}

emc::Result<FrequencyResult> solve_frequency(const EnergyToFrequencyInput& in) {
    return validate(in).transform([&] {
        return FrequencyResult{ .frequency = detail::frequency_from_energy(in.energy) };
    });
}

} // namespace emc::converter
