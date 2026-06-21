// include/emc/component/resistance.hpp
#pragma once

#include <expected>
#include <string_view>   // std::string_view: the AWG gauge arrives as borrowed text

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/materials.hpp>    // emc::materials::Material / properties()
#include <emc/core/units.hpp>        // emc::units::Frequency / Length / Impedance / ...

namespace emc::component {

// ---------------------------------------------------------------------------
//  Common AC/DC conductor result (every resistance calculator returns this).
//  resistance_per_length is stored as Ohm/m; resolve to Ohm/inch etc. at the
//  call site. resistance_total is the resistance over the full length l.
// ---------------------------------------------------------------------------
struct ConductorResistanceResult {
    emc::units::Impedance     resistance_per_length{};   // Ohm/m  (per unit length)
    emc::units::Impedance     resistance_total{};        // Ohm    (over the full length l)
    emc::units::Length        skin_depth{};              // delta  [m]
    emc::units::Resistivity   resistivity{};             // rho    [ohm*m]   (back-filled)
    emc::units::Conductivity  conductivity{};            // sigma  [S/m]     (back-filled)
    bool                      skin_limited{false};       // true => A_eff (HF) branch was taken
};

// ===========================================================================
//  Circuit Board Trace — fixed copper, rectangular cross-section.
//  No material selector: copper resistivity is baked in (overridable).
// ===========================================================================
struct TraceResistanceInput {
    emc::units::Frequency frequency{};                // f   [Hz]
    emc::units::Length    length{};                   // l   [m]
    emc::units::Length    width{};                    // w   [m]
    emc::units::Length    thickness{};                // t   [m]
    // Copper by default; exposed as an override so call sites stay terse but can deviate.
    // Direct-init in the default is fine: the literal already carries the ohm*m kind.
    emc::units::Resistivity resistivity =
        1.72e-8 * (mp_units::si::ohm * mp_units::si::metre);
};

// [[nodiscard]]: a dropped validation result or a discarded Result is a bug.
[[nodiscard]] std::expected<void, emc::Error> validate(const TraceResistanceInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult> trace_resistance(const TraceResistanceInput&);

// ===========================================================================
//  Cylindrical Conductor — round wire, material-derived rho/mu_r.
// ===========================================================================
struct CylindricalConductorInput {
    emc::units::Frequency      frequency{};                 // f   [Hz]
    emc::units::Length         length{};                    // l   [m]
    emc::units::Length         diameter{};                  // d   [m]
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0; // mu_r, honored only for Custom
    // Custom overrides (ignored unless material == Custom):
    emc::units::Resistivity    custom_resistivity{};        // rho [ohm*m]; 0 => derive from conductivity
    emc::units::Conductivity   custom_conductivity{};       // sigma [S/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const CylindricalConductorInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
cylindrical_conductor_resistance(const CylindricalConductorInput&);

// ===========================================================================
//  Rectangular Conductor — bar/trace, material-derived rho/mu_r.
// ===========================================================================
struct RectangularConductorInput {
    emc::units::Frequency      frequency{};                 // f   [Hz]
    emc::units::Length         length{};                    // l   [m]
    emc::units::Length         width{};                     // w   [m]
    emc::units::Length         thickness{};                 // t   [m]
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0; // mu_r, honored only for Custom
    emc::units::Resistivity    custom_resistivity{};        // rho [ohm*m]; 0 => derive from conductivity
    emc::units::Conductivity   custom_conductivity{};       // sigma [S/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularConductorInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
rectangular_conductor_resistance(const RectangularConductorInput&);

// ---------------------------------------------------------------------------
//  AWG gauge parser. Accepts "OOOO"/"OOO"/"OO"/"O" (aught sizes -> -3/-2/-1/0)
//  and signed integer strings ("0", "8", "39", "-3"). Anything else is an Error.
//  Returns the integer gauge used in the 92^((36-g)/39) diameter law.
//  std::expected<int, Error>: a malformed gauge is a typed error, never a silent
//  fallback to a plausible-but-wrong value.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<int, emc::Error> parse_awg_gauge(std::string_view gauge);

// Convert a parsed gauge to a wire diameter (the 92^((36-g)/39) law).
[[nodiscard]] emc::units::Length awg_diameter(int gauge);

// ===========================================================================
//  Standard Gauge Wire — round wire sized by AWG gauge.
// ===========================================================================
struct StandardGaugeWireInput {
    emc::units::Frequency      frequency{};                 // f   [Hz]
    emc::units::Length         length{};                    // l   [m]
    std::string_view           gauge{"0"};                  // "OOOO".."O" or an integer string
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0; // mu_r, honored only for Custom
    emc::units::Resistivity    custom_resistivity{};        // rho [ohm*m]; 0 => derive from conductivity
    emc::units::Conductivity   custom_conductivity{};       // sigma [S/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const StandardGaugeWireInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
standard_gauge_wire_resistance(const StandardGaugeWireInput&);

// ===========================================================================
//  Concept binding — one zero-data tag per calculator + a static_assert so the
//  emc::ValidatedCalculator contract is a compile-time tripwire in the header.
//  The validate(in) call resolves by Input type across the shared overload set.
// ===========================================================================
struct TraceResistance {
    using Input  = TraceResistanceInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return trace_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<TraceResistance>);

struct CylindricalConductor {
    using Input  = CylindricalConductorInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return cylindrical_conductor_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<CylindricalConductor>);

struct RectangularConductor {
    using Input  = RectangularConductorInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return rectangular_conductor_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<RectangularConductor>);

struct StandardGaugeWire {
    using Input  = StandardGaugeWireInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return standard_gauge_wire_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<StandardGaugeWire>);

} // namespace emc::component
