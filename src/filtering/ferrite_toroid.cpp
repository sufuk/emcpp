// src/filtering/ferrite_toroid.cpp
#include <emc/filtering/ferrite_toroid.hpp>

#include <cmath>   // std::log, std::sqrt (constexpr in C++23)

#include <mp-units/systems/si.h>

namespace emc::filtering {

namespace {
using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz, H, ohm, ...
}  // namespace

// ---------------------------------------------------------------------------
//  validate() — every precondition is a typed, range-bearing Error. Without
//  these guards, a=0 or b<=0 would compute ln(b/a) as -inf/nan and flow
//  silently into X/R/Z.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const FerriteToroidInput& in) {
    // Extract numeric values in the fields' SI base units for range reporting.
    const double N    = in.turns;
    const double h_m  = in.height.numerical_value_in(m);
    const double b_m  = in.outer_radius.numerical_value_in(m);
    const double a_m  = in.inner_radius.numerical_value_in(m);
    const double f_hz = in.frequency.numerical_value_in(Hz);

    // require_positive returns std::expected<void,Error>; bubble the first failure up.
    if (auto r = emc::require_positive(N, "turns"); !r) return r;
    if (auto r = emc::require_positive(h_m, "height"); !r) return r;
    if (auto r = emc::require_positive(a_m, "inner_radius"); !r) return r;  // a=0 -> ln(b/0) blow-up
    if (auto r = emc::require_positive(b_m, "outer_radius"); !r) return r;
    if (auto r = emc::require_positive(f_hz, "frequency"); !r) return r;

    // mu_r' and mu_r'' are physically >= 0 (a passive ferrite cannot have negative
    // permeability). They MAY be 0 individually (e.g. a hypothetical lossless or
    // reactance-free band), so use a range check [0, inf) rather than require_positive.
    if (auto r = emc::in_range(in.mu_r_real, 0.0, 1.0e9, "mu_r_real"); !r) return r;
    if (auto r = emc::in_range(in.mu_r_imag, 0.0, 1.0e9, "mu_r_imag"); !r) return r;

    // ln(b/a) is only real & finite for b > a > 0. b == a gives L = 0 (ln 1),
    // which is physically fine (no core), so we ALLOW b == a but REJECT b < a
    // (negative inductance is nonsense). a > 0 already guaranteed above.
    if (b_m < a_m)
        return std::unexpected(emc::domain_error(
            "outer_radius must be >= inner_radius (b >= a) for ln(b/a)", "outer_radius"));

    return {};   // all preconditions hold
}

// ---------------------------------------------------------------------------
//  calculate() — evaluate the closed-form math in SI base units (metres,
//  hertz), then re-attach mp-units types on the way out.
// ---------------------------------------------------------------------------
emc::Result<FerriteToroidResult> calculate(const FerriteToroidInput& in) {
    // Guard first: on bad input forward the typed Error, so the formula never sees inf/nan.
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Pull plain doubles in canonical units (post-validation: all > 0, b >= a).
    const double N   = in.turns;
    const double up  = in.mu_r_real;   // mu_r'  (real)
    const double upp = in.mu_r_imag;   // mu_r'' (imag / loss)
    const double h   = in.height.numerical_value_in(m);
    const double b   = in.outer_radius.numerical_value_in(m);
    const double a   = in.inner_radius.numerical_value_in(m);
    const double f   = in.frequency.numerical_value_in(Hz);

    const double mu0 = emc::constants::mu0.numerical_value_in(H / m);   // 1.25663706212e-6
    const double pi  = emc::constants::pi;                              // std::numbers::pi

    // L = (mu0 N^2 h / 2 pi) * ln(b/a)        [H]   (N*N is exact for the integer exponent 2)
    const double L = ((N * N * mu0 * h) / (2.0 * pi)) * std::log(b / a);

    // omega = 2 pi f ; X and R from the real/imag permeability split.
    const double two_pi_f = 2.0 * pi * f;
    const double X = two_pi_f * up  * L;          // reactance  [ohm]
    const double R = two_pi_f * upp * L;          // resistance [ohm]
    const double Z = std::sqrt(R * R + X * X);    // |Z|        [ohm]

    // Direct-init {..} relabels each derived-kind value as the named alias (explicit on purpose:
    // copy-init = would not, since a product/quotient has a derived quantity kind).
    return FerriteToroidResult{
        .inductance = Inductance{L * H},
        .reactance  = Impedance{X * ohm},
        .resistance = Impedance{R * ohm},
        .impedance  = Impedance{Z * ohm},
    };
}

} // namespace emc::filtering
