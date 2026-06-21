// src/component/inductance.cpp
#include <emc/component/inductance.hpp>

#include <cmath>     // std::log, std::sqrt  (constexpr in C++23)

#include <mp-units/systems/si.h>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::m;   // metre symbol for numerical_value_in / rebuild
using mp_units::si::unit_symbols::H;   // henry symbol for the result relabel

namespace {
// mu0 as a raw double in H/m, extracted ONCE from the canonical constant. Every formula
// below evaluates its lengths in metres and multiplies by this, from a single CODATA source.
// (We work in double for these empirical-coefficient formulas, then re-attach the henry unit.)
constexpr double mu0_H_per_m = emc::constants::mu0.numerical_value_in(H / m);
constexpr double kPi         = emc::constants::pi;
} // namespace

// ---- Circular Loop ---------------------------------------------------------

std::expected<void, emc::Error> validate(const CircularLoopInput& in) {
    const double R = in.loop_radius.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    // a sits in the denominator inside ln(8R/a): zero/negative is a hard domain error.
    if (auto r = emc::require_positive(a, "wire_radius"); !r) return r;
    if (auto r = emc::require_positive(R, "loop_radius"); !r) return r;
    if (auto r = emc::require_positive(in.mu_r, "mu_r"); !r) return r;
    // N is squared, so sign is irrelevant, but N==0 gives a trivial 0 H; flag non-positive.
    if (auto r = emc::require_positive(in.turns, "turns"); !r) return r;
    return {};
}

emc::Result<CircularLoopResult> circular_loop_inductance(const CircularLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double R = in.loop_radius.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    const double L_H = N * N * R * mu0_H_per_m * in.mu_r * (std::log((8.0 * R) / a) - 2.0);

    // Direct-init {..}: relabel the derived-kind henry value as the named Inductance.
    return CircularLoopResult{ .inductance = Inductance{ L_H * H } };
}

// ---- Connector Pin (self + mutual) -----------------------------------------

std::expected<void, emc::Error> validate(const ConnectorPinInput& in) {
    const double l = in.length.numerical_value_in(m);
    const double r = in.radius.numerical_value_in(m);
    const double s = in.spacing.numerical_value_in(m);
    if (auto e = emc::require_positive(l, "length");  !e) return e;
    if (auto e = emc::require_positive(r, "radius");  !e) return e;   // ln(2l/r), r in denominator
    if (auto e = emc::require_positive(s, "spacing"); !e) return e;   // ln(2l/s), s in denominator
    return {};
}

emc::Result<ConnectorPinResult> connector_pin_inductance(const ConnectorPinInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double l = in.length.numerical_value_in(m);
    const double r = in.radius.numerical_value_in(m);
    const double s = in.spacing.numerical_value_in(m);

    // The (mu0 l)/(2pi) factor is common to both outputs: evaluate once and name it.
    const double coeff = (mu0_H_per_m * l) / (2.0 * kPi);
    const double L_H   = coeff * (std::log(2.0 * l / r) - 0.75);
    const double M_H   = coeff * (std::log(2.0 * l / s) - 1.0);

    return ConnectorPinResult{ .self_inductance   = Inductance{ L_H * H },
                               .mutual_inductance = Inductance{ M_H * H } };
}

// ---- Rectangular Loop ------------------------------------------------------

std::expected<void, emc::Error> validate(const RectangularLoopInput& in) {
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns");   !e) return e;
    if (auto e = emc::require_positive(w, "width");          !e) return e;  // ln(.../w), ln(2w/a)
    if (auto e = emc::require_positive(h, "height");         !e) return e;  // ln(.../h), ln(2h/a)
    if (auto e = emc::require_positive(a, "wire_radius");    !e) return e;  // a in ln denominator
    if (auto e = emc::require_positive(in.mu_r, "mu_r");     !e) return e;
    return {};
}

emc::Result<RectangularLoopResult> rectangular_loop_inductance(const RectangularLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    // sqrt(w^2+h^2) is computed once and named, so its three uses cannot drift apart.
    const double diag = std::sqrt(w * w + h * h);                     // sqrt(w^2+h^2)
    const double bracket =
          -2.0 * (w + h)
        +  2.0 * diag
        -  h * std::log((h + diag) / w)
        -  w * std::log((w + diag) / h)
        +  h * std::log((2.0 * h) / a)
        +  w * std::log((2.0 * w) / a);

    const double L_H = N * N * ((mu0_H_per_m * in.mu_r) / kPi) * bracket;

    return RectangularLoopResult{ .inductance = Inductance{ L_H * H } };
}

// ---- Square Loop -----------------------------------------------------------

std::expected<void, emc::Error> validate(const SquareLoopInput& in) {
    const double w = in.side.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns");   !e) return e;
    if (auto e = emc::require_positive(w, "side");           !e) return e;  // ln(w/a)
    if (auto e = emc::require_positive(a, "wire_radius");    !e) return e;  // a in ln denominator
    if (auto e = emc::require_positive(in.mu_r, "mu_r");     !e) return e;
    return {};
}

emc::Result<SquareLoopResult> square_loop_inductance(const SquareLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double w = in.side.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    const double L_H = N * N * ((2.0 * mu0_H_per_m * in.mu_r * w) / kPi)
                            * (std::log(w / a) - 0.774);

    return SquareLoopResult{ .inductance = Inductance{ L_H * H } };
}

// ---- Toroid ----------------------------------------------------------------

std::expected<void, emc::Error> validate(const ToroidInput& in) {
    const double h = in.height.numerical_value_in(m);
    const double b = in.outer_radius.numerical_value_in(m);
    const double a = in.inner_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns");      !e) return e;
    if (auto e = emc::require_positive(h, "height");            !e) return e;
    if (auto e = emc::require_positive(a, "inner_radius");      !e) return e;  // a in denom of ln(b/a)
    if (auto e = emc::require_positive(b, "outer_radius");      !e) return e;
    // b > a is implied physically; b == a gives ln(1)=0 -> L=0 (boundary, not an error).
    return {};
}

emc::Result<ToroidResult> toroid_inductance(const ToroidInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double h = in.height.numerical_value_in(m);
    const double b = in.outer_radius.numerical_value_in(m);
    const double a = in.inner_radius.numerical_value_in(m);

    const double L_H = ((N * N * mu0_H_per_m * h) / (2.0 * kPi)) * std::log(b / a);

    return ToroidResult{ .inductance = Inductance{ L_H * H } };
}

// ---- Via -------------------------------------------------------------------

std::expected<void, emc::Error> validate(const ViaInput& in) {
    const double h = in.height.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto e = emc::require_positive(h, "height");   !e) return e;
    if (auto e = emc::require_positive(d, "diameter"); !e) return e;  // d in denom of ln(4h/d)
    return {};
}

emc::Result<ViaResult> via_inductance(const ViaInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double h = in.height.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);

    const double L_H = ((mu0_H_per_m * h) / (2.0 * kPi)) * (std::log(4.0 * h / d) - 1.0);

    return ViaResult{ .inductance = Inductance{ L_H * H } };
}

} // namespace emc::component
