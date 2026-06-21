// src/cabling/crosstalk.cpp
#include <emc/cabling/crosstalk.hpp>

#include <cmath>       // std::log10 (runtime: not constexpr in C++23, so no compile-time fold)
#include <utility>     // std::pair for the resistance check table

#include <mp-units/systems/si.h>

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::cabling {

using namespace mp_units;
using emc::units::si::hertz;
using emc::units::si::ohm;
using emc::units::si::henry;
using emc::units::si::farad;

std::expected<void, emc::Error> validate(const CrosstalkInput& in) {
    // Reduce each quantity to a plain SI magnitude for the numeric checks; any
    // reported range comes back in these units (the validators take raw doubles).
    const double f   = in.f.numerical_value_in(hertz);
    const double RL  = in.R_L.numerical_value_in(ohm);
    const double RS  = in.R_S.numerical_value_in(ohm);
    const double RNE = in.R_NE.numerical_value_in(ohm);
    const double RFE = in.R_FE.numerical_value_in(ohm);
    const double Lm  = in.L_m.numerical_value_in(henry);
    const double Cm  = in.C_m.numerical_value_in(farad);

    // Frequency and the coupling elements must be strictly positive: with f<=0 or
    // Lm<=0/Cm<=0 the log10 argument is non-positive and V_NE itself collapses.
    if (auto r = emc::require_positive(f,  "f");   !r) return r;
    if (auto r = emc::require_positive(Lm, "L_m"); !r) return r;
    if (auto r = emc::require_positive(Cm, "C_m"); !r) return r;

    // Resistances must each be in a sane non-negative range. Every element is an
    // explicit std::pair so the initializer-list has one homogeneous element type.
    for (const auto& [v, name] : {std::pair<double, std::string_view>{RL,  "R_L"},
                                  std::pair<double, std::string_view>{RS,  "R_S"},
                                  std::pair<double, std::string_view>{RNE, "R_NE"},
                                  std::pair<double, std::string_view>{RFE, "R_FE"}})
        if (auto r = emc::in_range(v, 0.0, 1e12, name); !r) return r;

    // ...and the two SUMS that act as denominators must be non-zero.
    if (auto r = emc::require_nonzero(RS + RL,   "R_S+R_L");   !r) return r;
    if (auto r = emc::require_nonzero(RNE + RFE, "R_NE+R_FE"); !r) return r;

    return {};   // ok
}

emc::Result<CrosstalkResult> calculate(const CrosstalkInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Work in coherent SI; the model is unit-consistent in Hz, ohm, henry, farad,
    // so plain SI magnitudes evaluate the formula directly once the typed
    // quantities have folded any input-side scale (MHz, uH, pF) into the value.
    const double f   = in.f.numerical_value_in(hertz);
    const double RL  = in.R_L.numerical_value_in(ohm);
    const double RS  = in.R_S.numerical_value_in(ohm);
    const double RNE = in.R_NE.numerical_value_in(ohm);
    const double RFE = in.R_FE.numerical_value_in(ohm);
    const double Lm  = in.L_m.numerical_value_in(henry);
    const double Cm  = in.C_m.numerical_value_in(farad);

    const double omega = 2.0 * emc::constants::pi * f;        // angular frequency w = 2*pi*f

    // Shared sub-expressions computed once, so A_NE and A_FE stay in lockstep.
    const double inductive  = Lm / (RS + RL);                 // L_m / (R_S + R_L)
    const double capacitive = (RNE * RFE / (RNE + RFE)) * (RL * Cm / (RS + RL));
    const double split_ne   = RNE / (RNE + RFE);
    const double split_fe   = RFE / (RNE + RFE);

    // Near-end: inductive and capacitive contributions ADD.
    const double A_NE = omega * (split_ne * inductive + capacitive);

    // Far-end: the inductive term is SUBTRACTED (sign flip), so A_FE can go <= 0.
    const double A_FE = omega * (-1.0 * split_fe * inductive + capacitive);

    CrosstalkResult out{};

    // V_NE: the near-end argument is positive for physical inputs (omega>0, all
    // terms >= 0). Guard a non-positive argument as a typed DomainError rather
    // than emitting -nan from log10. (!(A_NE > 0.0) also catches NaN.)
    if (!(A_NE > 0.0))
        return std::unexpected(emc::domain_error(
            "near-end argument is non-positive (log10 undefined)", "V_NE"));
    // 20*log10 -> this is a VOLTAGE dB (not 10*log10 power dB); see guide.
    out.V_NE = emc::units::Decibel{ 20.0 * std::log10(A_NE) };

    // V_FE: the documented clamp. When A_FE <= 0 (inductive cancellation dominates)
    // substitute the named -200 dB floor instead of computing log10 of <= 0.
    if (A_FE <= 0.0) {
        out.V_FE            = kFarEndFloor;     // -200 dB sentinel
        out.far_end_clamped = true;             // make the clamp observable
    } else {
        out.V_FE = emc::units::Decibel{ 20.0 * std::log10(A_FE) };
    }

    return out;
}

} // namespace emc::cabling
