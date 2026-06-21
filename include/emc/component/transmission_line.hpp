// include/emc/component/transmission_line.hpp
//
// Seven transmission-line parameter calculators for the emc::component family:
//   Coaxial Line, Microstrip Line, Stripline, Narrow Trace Over Plane,
//   Wide Trace Over Plane, Wire Over Plane, Wire Pair.
//
// They share one header/TU because they are the same family — characteristic
// parameters of a uniform line — and the four PCB/wire variants share one
// skin-depth -> L/C/Z0/R-per-length algorithm factored into detail::per_length_line.
// All are GUI-free, mp-units-typed, std::expected-returning free functions.
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::mu0, ::eps0, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/materials.hpp>    // emc::materials::Material, properties()
#include <emc/core/units.hpp>        // emc::units:: aliases (incl. per-length ones)

namespace emc::component {

// ===========================================================================
//  Coaxial Line:  Z0, f_cutoff (TE11), C/length, L/length
//      Z0  = 138 * log10(D/d) / sqrt(eps_r)                 [ohm]
//      f_c = 11.8 / ( sqrt(eps_r) * pi * (D + d)/2 )        [GHz]   (D, d in inches)
//      C   = 7.354 * eps_r / log10(D/d)                     [pF/ft]
//      L   = 140.4 * log10(D/d)                             [nH/ft]
// ===========================================================================

// Geometry is mp-units typed (the unit lives in the type), so a wrong-unit input
// will not compile; eps_r is a plain double because it is a bare ratio in the
// formula. Defaults give designated-initializer call sites clean reading.
struct CoaxialLineInput {
    emc::units::Length outer_diameter   = 3.2 * mp_units::si::milli<mp_units::si::metre>;  // D
    emc::units::Length inner_diameter   = 0.9 * mp_units::si::milli<mp_units::si::metre>;  // d
    double             relative_permittivity = 2.3;                                        // eps_r
};

struct CoaxialLineResult {
    emc::units::Impedance            impedance{};         // Z0
    emc::units::Frequency            cutoff_frequency{};  // f_c (TE11)
    emc::units::CapacitancePerLength capacitance{};       // C per length (read as pF/ft)
    emc::units::InductancePerLength  inductance{};        // L per length (read as nH/ft)
};

/// Validate D > d > 0 and eps_r in [1, 1000].
[[nodiscard]] std::expected<void, emc::Error> validate(const CoaxialLineInput&);
/// Forward solve: D, d, eps_r -> (Z0, f_c, C/len, L/len).
[[nodiscard]] emc::Result<CoaxialLineResult> calculate(const CoaxialLineInput&);

// ===========================================================================
//  Microstrip Line:  eps_eff, Z0  (Wheeler/Hammerstad W/H branch)
// ===========================================================================

struct MicrostripLineInput {
    double             relative_permittivity = 13.0;                                       // eps_r
    emc::units::Length width  = 122.0 * mp_units::si::milli<mp_units::si::metre>;          // W
    emc::units::Length height =   3.0 * mp_units::si::milli<mp_units::si::metre>;          // H
};

struct MicrostripLineResult {
    double                effective_permittivity{};   // eps_eff (a bare ratio -> double)
    emc::units::Impedance impedance{};                // Z0
};

[[nodiscard]] std::expected<void, emc::Error> validate(const MicrostripLineInput&);
[[nodiscard]] emc::Result<MicrostripLineResult> calculate(const MicrostripLineInput&);

// ===========================================================================
//  Stripline:  Z0 = (60/sqrt(eps_r)) * ln( 1.9*(2h + t) / (0.8*w + t) )   [ohm]
// ===========================================================================

struct StriplineInput {
    double             relative_permittivity = 10.0;                                       // eps_r
    emc::units::Length width     = 10.0 * mp_units::si::metre;                             // w
    emc::units::Length height    = 10.0 * mp_units::si::metre;                             // h
    emc::units::Length thickness =  5.0 * mp_units::si::metre;                             // t
};

struct StriplineResult {
    emc::units::Impedance impedance{};   // Z0
};

[[nodiscard]] std::expected<void, emc::Error> validate(const StriplineInput&);
[[nodiscard]] emc::Result<StriplineResult> calculate(const StriplineInput&);

// ===========================================================================
//  Per-length calculators (Narrow Trace, Wide Trace, Wire Over Plane, Wire
//  Pair) — all share one skin-depth -> Z0 -> R block. They report the same
//  four-field per-length result.
// ===========================================================================

struct LinePerLengthResult {
    emc::units::InductancePerLength  inductance{};               // L  (uH/m, attached as H/m)
    emc::units::CapacitancePerLength capacitance{};              // C  (pF/m, attached as F/m)
    emc::units::ResistancePerLength  resistance{};               // R  (mOhm/m, attached as ohm/m)
    emc::units::Impedance            characteristic_impedance{}; // Z0
};

// --- Narrow Trace Over Plane (h > w) ---------------------------------------
struct NarrowTraceInput {
    emc::units::Frequency frequency       = 1.0 * mp_units::si::mega<mp_units::si::hertz>;
    emc::units::Length    trace_height    = 10.0  * mp_units::si::milli<mp_units::si::metre>;  // h
    emc::units::Length    trace_width     =  1.0  * mp_units::si::milli<mp_units::si::metre>;  // w
    emc::units::Length    trace_thickness = 0.035 * mp_units::si::milli<mp_units::si::metre>;  // t
    emc::materials::Material conductor    = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};                  // used iff conductor == Custom
    double                relative_permittivity = 1.0;        // eps_r (Air)
};

// --- Wide Trace Over Plane (w > 5h) ----------------------------------------
struct WideTraceInput {
    emc::units::Frequency frequency       = 1.0 * mp_units::si::mega<mp_units::si::hertz>;
    emc::units::Length    trace_height    =  1.0  * mp_units::si::milli<mp_units::si::metre>;  // h
    emc::units::Length    trace_width     = 10.0  * mp_units::si::milli<mp_units::si::metre>;  // w
    emc::units::Length    trace_thickness = 0.035 * mp_units::si::milli<mp_units::si::metre>;  // t
    emc::materials::Material conductor    = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};
    double                relative_permittivity = 1.0;
};

// --- Wire Over Plane (h > a) -----------------------------------------------
struct WireOverPlaneInput {
    emc::units::Frequency frequency   = 5.0 * mp_units::si::hertz;
    emc::units::Length    wire_height = 22.0 * mp_units::si::centi<mp_units::si::metre>;  // h
    emc::units::Length    wire_radius = 20.0 * mp_units::si::centi<mp_units::si::metre>;  // a
    emc::materials::Material conductor = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};
    double                relative_permittivity = 1.0;
};

// --- Wire Pair (s > d) -----------------------------------------------------
struct WirePairInput {
    emc::units::Frequency frequency = 1.0 * mp_units::si::mega<mp_units::si::hertz>;
    emc::units::Length    spacing   = 10.0 * mp_units::si::centi<mp_units::si::metre>;   // s
    emc::units::Length    diameter  =  0.1 * mp_units::si::centi<mp_units::si::metre>;   // d
    emc::materials::Material conductor = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};
    double                relative_permittivity = 1.0;
};

[[nodiscard]] std::expected<void, emc::Error> validate(const NarrowTraceInput&);
[[nodiscard]] std::expected<void, emc::Error> validate(const WideTraceInput&);
[[nodiscard]] std::expected<void, emc::Error> validate(const WireOverPlaneInput&);
[[nodiscard]] std::expected<void, emc::Error> validate(const WirePairInput&);

[[nodiscard]] emc::Result<LinePerLengthResult> calculate(const NarrowTraceInput&);
[[nodiscard]] emc::Result<LinePerLengthResult> calculate(const WideTraceInput&);
[[nodiscard]] emc::Result<LinePerLengthResult> calculate(const WireOverPlaneInput&);
[[nodiscard]] emc::Result<LinePerLengthResult> calculate(const WirePairInput&);

// ===========================================================================
//  Calculator-concept binding. Each (Input, Result, calculate, validate)
//  triple is wrapped in a zero-data tag struct and static_assert-ed against
//  the foundation concept, so a signature drift is a compile error here.
// ===========================================================================

struct CoaxialLine {
    using Input = CoaxialLineInput; using Result = CoaxialLineResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<CoaxialLine>);

struct MicrostripLine {
    using Input = MicrostripLineInput; using Result = MicrostripLineResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<MicrostripLine>);

struct Stripline {
    using Input = StriplineInput; using Result = StriplineResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<Stripline>);

struct NarrowTrace {
    using Input = NarrowTraceInput; using Result = LinePerLengthResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<NarrowTrace>);

struct WideTrace {
    using Input = WideTraceInput; using Result = LinePerLengthResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<WideTrace>);

struct WireOverPlane {
    using Input = WireOverPlaneInput; using Result = LinePerLengthResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<WireOverPlane>);

struct WirePair {
    using Input = WirePairInput; using Result = LinePerLengthResult;
    static emc::Result<Result> calculate(const Input& i) { return emc::component::calculate(i); }
    static std::expected<void, emc::Error> validate(const Input& i) { return emc::component::validate(i); }
};
static_assert(emc::ValidatedCalculator<WirePair>);

} // namespace emc::component
