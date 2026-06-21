// include/emc/component/inductance.hpp
#pragma once  // include this header once per translation unit

#include <expected>   // std::expected<void, emc::Error> for validate()

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::mu0, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Inductance

namespace emc::component {

// Short, local aliases for the two quantity kinds these seven calculators speak.
// Every input length is an emc::units::Length, every result an emc::units::Inductance,
// so unit selection (m, mm, cm, mils) is handled once by the type system at the call
// site instead of per-input scale factors inside each formula.
using emc::units::Length;
using emc::units::Inductance;

// All seven calculators are forward-only and share one free-space permeability
// constant (emc::constants::mu0). They evaluate their lengths in metres, run the
// closed form in double, then re-attach the henry unit on the result — the
// dimensional contract stays enforced at the Input/Result boundary.

// ===========================================================================
//  Circular Loop:  L = N^2 * R * mu0 * mu_r * (ln(8R/a) - 2)
// ===========================================================================

/// Inputs for a circular wire loop. mu_r is a bare dimensionless double (pinned API).
struct CircularLoopInput {
    double turns       = 1.0;                            ///< N  [-]   (turn count; non-integer allowed)
    Length loop_radius = 0.3 * mp_units::si::metre;      ///< R   [m]
    Length wire_radius = 5e-4 * mp_units::si::metre;     ///< a   [m]  (must be > 0; appears as ln(8R/a))
    double mu_r        = 1.0;                            ///< relative permeability [-]
};

struct CircularLoopResult {
    Inductance inductance{};   ///< L  [H]
};

// [[nodiscard]]: a dropped validation/result is a silently dropped failure — a bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const CircularLoopInput& in);

/// L = N^2 R mu0 mu_r (ln(8R/a) - 2).
[[nodiscard]] emc::Result<CircularLoopResult> circular_loop_inductance(const CircularLoopInput& in);

// ===========================================================================
//  Connector Pin (the one multi-output calculator: partial self + mutual):
//      L   = (mu0 l / 2pi) * (ln(2l / r) - 3/4)
//      M_p = (mu0 l / 2pi) * (ln(2l / s) - 1)
// ===========================================================================

struct ConnectorPinInput {
    Length length  = 0.01 * mp_units::si::metre;       ///< l  [m]  (pin length)
    Length radius  = 3e-4 * mp_units::si::metre;        ///< r  [m]  (pin radius, > 0; ln(2l/r))
    Length spacing = 2.54e-3 * mp_units::si::metre;     ///< s  [m]  (return spacing, > 0; ln(2l/s))
};

// Two named result fields, so one call returns both partial inductances and the
// caller can name them with a structured binding (no second out-parameter).
struct ConnectorPinResult {
    Inductance self_inductance{};    ///< L    [H]
    Inductance mutual_inductance{};  ///< M_p  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ConnectorPinInput& in);

/// Computes BOTH the partial self-inductance and the partial mutual inductance.
[[nodiscard]] emc::Result<ConnectorPinResult> connector_pin_inductance(const ConnectorPinInput& in);

// ===========================================================================
//  Rectangular Loop:
//      L = N^2 * (mu0 mu_r / pi) * [ -2(w+h) + 2 sqrt(w^2+h^2)
//                                    - h ln((h+sqrt(w^2+h^2))/w)
//                                    - w ln((w+sqrt(w^2+h^2))/h)
//                                    + h ln(2h/a) + w ln(2w/a) ]
// ===========================================================================

struct RectangularLoopInput {
    double turns       = 10.0;                          ///< N  [-]
    Length width       = 2.0 * mp_units::si::metre;     ///< w  [m]
    Length height      = 1.0 * mp_units::si::metre;     ///< h  [m]
    Length wire_radius = 1e-3 * mp_units::si::metre;    ///< a  [m]  (> 0; ln(2w/a), ln(2h/a))
    double mu_r        = 1.0;                            ///< relative permeability [-]
};

struct RectangularLoopResult {
    Inductance inductance{};   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularLoopInput& in);

[[nodiscard]] emc::Result<RectangularLoopResult> rectangular_loop_inductance(const RectangularLoopInput& in);

// ===========================================================================
//  Solenoid (Wheeler long-coil):  L = mu0 N^2 pi r^2 / l
//  The only formula with no logarithm, so it is constexpr end-to-end and its
//  definition lives inline in this header (below) instead of in the .cpp.
// ===========================================================================

struct SolenoidInput {
    double turns  = 10.0;                          ///< N  [-]
    Length radius = 0.01 * mp_units::si::metre;     ///< r  [m]  (coil radius)
    Length length = 0.10 * mp_units::si::metre;     ///< l  [m]  (coil length, > 0 — denominator)
};

struct SolenoidResult {
    Inductance inductance{};   ///< L  [H]
};

[[nodiscard]] constexpr std::expected<void, emc::Error> validate(const SolenoidInput& in);

/// L = mu0 N^2 pi r^2 / l. constexpr: no transcendental, pure closed form.
[[nodiscard]] constexpr emc::Result<SolenoidResult> solenoid_inductance(const SolenoidInput& in);

// ---- Solenoid inline definitions (constexpr, fold at compile time) ----------

constexpr std::expected<void, emc::Error> validate(const SolenoidInput& in) {
    const double r = in.radius.numerical_value_in(mp_units::si::metre);
    const double l = in.length.numerical_value_in(mp_units::si::metre);
    // N is squared (sign-immune) but N==0 yields a trivial 0 H; flag non-positive as invalid.
    if (auto e = emc::require_positive(in.turns, "turns"); !e) return e;
    if (auto e = emc::require_positive(r, "radius"); !e) return e;
    if (auto e = emc::require_nonzero(l, "length"); !e) return e;   // l in the denominator
    return {};
}

constexpr emc::Result<SolenoidResult> solenoid_inductance(const SolenoidInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N   = in.turns;
    const double r   = in.radius.numerical_value_in(mp_units::si::metre);
    const double l   = in.length.numerical_value_in(mp_units::si::metre);
    // mu0 pulled here as a plain double (H/m); pi is already a double.
    const double mu0 = emc::constants::mu0.numerical_value_in(
                           mp_units::si::henry / mp_units::si::metre);

    const double L_H = (mu0 * N * N * emc::constants::pi * r * r) / l;

    // Direct-init {..} relabels the derived-kind henry value as the named Inductance.
    return SolenoidResult{ .inductance = Inductance{ L_H * mp_units::si::henry } };
}

// ===========================================================================
//  Square Loop:  L = N^2 * (2 mu0 mu_r w / pi) * (ln(w/a) - 0.774)
// ===========================================================================

struct SquareLoopInput {
    double turns       = 10.0;                          ///< N  [-]
    Length side        = 1.0 * mp_units::si::metre;     ///< w  [m]  (loop side)
    Length wire_radius = 1e-3 * mp_units::si::metre;    ///< a  [m]  (> 0; ln(w/a))
    double mu_r        = 1.0;                            ///< relative permeability [-]
};

struct SquareLoopResult {
    Inductance inductance{};   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const SquareLoopInput& in);

[[nodiscard]] emc::Result<SquareLoopResult> square_loop_inductance(const SquareLoopInput& in);

// ===========================================================================
//  Toroid (rectangular core):  L = (mu0 N^2 h / 2pi) * ln(b/a)
//  Here a = inner radius, b = outer radius — NOT a wire radius. The field names
//  reflect that so they cannot be swapped (which would flip ln(b/a) negative).
// ===========================================================================

struct ToroidInput {
    double turns        = 10.0;                          ///< N  [-]
    Length height       = 0.01 * mp_units::si::metre;    ///< h  [m]  (core height)
    Length outer_radius = 0.06 * mp_units::si::metre;    ///< b  [m]  (> 0; ln(b/a))
    Length inner_radius = 0.04 * mp_units::si::metre;    ///< a  [m]  (> 0; denominator of ln(b/a))
};

struct ToroidResult {
    Inductance inductance{};   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ToroidInput& in);

/// L = (mu0 N^2 h / 2pi) * ln(b/a).
[[nodiscard]] emc::Result<ToroidResult> toroid_inductance(const ToroidInput& in);

// ===========================================================================
//  Via (PCB):  L = (mu0 h / 2pi) * (ln(4h/d) - 1)
// ===========================================================================

struct ViaInput {
    Length height   = 1.6e-3 * mp_units::si::metre;    ///< h  [m]  (board thickness / via length)
    Length diameter = 7.5e-4 * mp_units::si::metre;    ///< d  [m]  (> 0; ln(4h/d))
};

struct ViaResult {
    Inductance inductance{};   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ViaInput& in);

/// L = (mu0 h / 2pi) * (ln(4h/d) - 1).
[[nodiscard]] emc::Result<ViaResult> via_inductance(const ViaInput& in);

// ===========================================================================
//  Bind all seven (Input, Result, free-function) triples to the Calculator
//  concept. The macro names a zero-data tag struct that forwards to the free
//  calculate()/validate(); validate() is resolved by Input overload, so the
//  macro's emc::component::validate(in) picks the right one per Input type.
// ===========================================================================

#define EMC_BIND_INDUCTANCE(TAG, INPUT, RESULT, FN)                                  \
    struct TAG {                                                                     \
        using Input  = INPUT;                                                        \
        using Result = RESULT;                                                       \
        static emc::Result<Result> calculate(const Input& in) { return FN(in); }    \
        static std::expected<void, emc::Error> validate(const Input& in) {          \
            return emc::component::validate(in);                                     \
        }                                                                            \
    };                                                                               \
    static_assert(emc::ValidatedCalculator<TAG>)

EMC_BIND_INDUCTANCE(CircularLoop,    CircularLoopInput,    CircularLoopResult,    circular_loop_inductance);
EMC_BIND_INDUCTANCE(ConnectorPin,    ConnectorPinInput,    ConnectorPinResult,    connector_pin_inductance);
EMC_BIND_INDUCTANCE(RectangularLoop, RectangularLoopInput, RectangularLoopResult, rectangular_loop_inductance);
EMC_BIND_INDUCTANCE(Solenoid,        SolenoidInput,        SolenoidResult,        solenoid_inductance);
EMC_BIND_INDUCTANCE(SquareLoop,      SquareLoopInput,      SquareLoopResult,      square_loop_inductance);
EMC_BIND_INDUCTANCE(Toroid,          ToroidInput,          ToroidResult,          toroid_inductance);
EMC_BIND_INDUCTANCE(Via,             ViaInput,             ViaResult,             via_inductance);

#undef EMC_BIND_INDUCTANCE

} // namespace emc::component
