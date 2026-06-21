// src/component/resistance.cpp
#include <charconv>   // std::from_chars: locale-free, allocation-free integer parse
#include <cmath>      // std::sqrt, std::pow (constexpr in C++23)

#include <mp-units/systems/si.h>

#include <emc/component/resistance.hpp>
#include <emc/core/constants.hpp>   // emc::constants::pi, ::mu0

// ===========================================================================
//  Shared detail:: core — one copy of the physics for all four calculators.
//  Each free calculate() resolves its geometry to SI metres, fills a GeometrySI,
//  and hands it to resistance_from_geometry(). The "distinct named free functions
//  sharing a detail:: core" shape keeps the four from ever drifting apart.
// ===========================================================================
namespace emc::component::detail {

using namespace mp_units;
using mp_units::si::unit_symbols::m;     // metre
using mp_units::si::unit_symbols::ohm;   // ohm
using mp_units::si::unit_symbols::S;     // siemens

// All geometry resolved to SI metres before this is called.
struct GeometrySI {
    double area_m2;        // A          [m^2]
    double perimeter_m;    // 2*(w+t) or 2*pi*r  [m]  (for A_eff = perimeter * delta)
    double dc_threshold_m; // delta >= this  =>  DC branch  (d/4  or  wt/(2(w+t)))
};

struct CoreOut {
    emc::units::Resistivity   resistivity;   // rho      [ohm*m]
    emc::units::Conductivity  conductivity;  // sigma    [S/m]
    emc::units::Length        skin_depth;    // delta    [m]
    double r_per_metre;                       // Ohm / m  (resistance per unit length, SI metre)
    double r_total;                           // Ohm      (over length l)
    bool   skin_limited;                      // true if the A_eff branch was taken
};

// rho in ohm*m, length l in metres, f in Hz, mu_r dimensionless, SI geometry.
// All the per-metre / threshold math is done on plain doubles so the established
// closed-form formula constants stay exact; the named-unit result quantities are
// rebuilt with direct-init {..} at the end (mp-units relabel rule).
[[nodiscard]] inline CoreOut resistance_from_geometry(double rho, double l_m, double f_hz,
                                                      double mu_r, const GeometrySI& g) {
    const double sigma = 1.0 / rho;
    // skin depth: delta = 1 / sqrt(pi * f * mu_r * mu0 * sigma)
    // mu0 pulled once from the foundation so every calculator agrees to full precision.
    const double mu0 = emc::constants::mu0.numerical_value_in(si::henry / si::metre);
    const double delta = 1.0 / std::sqrt(emc::constants::pi * f_hz * mu_r * mu0 * sigma);

    double r_per_m{}, r_total{};
    bool skin_limited{};
    if (delta >= g.dc_threshold_m) {                 // DC branch: skin depth bigger than the conductor
        r_per_m      = rho / g.area_m2;              // Ohm / m
        r_total      = rho * l_m / g.area_m2;        // Ohm
        skin_limited = false;
    } else {                                         // skin-effect branch: current crowds into a delta shell
        const double a_eff = g.perimeter_m * delta;  // delta-thick shell
        r_per_m      = rho / a_eff;
        r_total      = rho * l_m / a_eff;
        skin_limited = true;
    }

    // Rebuild the back-filled quantities with direct-init {..}: the products carry a
    // derived quantity kind that does NOT implicitly convert to the named alias.
    return CoreOut{
        .resistivity  = emc::units::Resistivity{  rho   * (ohm * m) },
        .conductivity = emc::units::Conductivity{ sigma * (S / m)   },
        .skin_depth   = emc::units::Length{       delta * m         },
        .r_per_metre  = r_per_m,
        .r_total      = r_total,
        .skin_limited = skin_limited,
    };
}

// Resolve effective resistivity + mu_r.  Custom => use overrides + given mu_r;
// real material => table rho, mu_r forced to 1 (Nickel's mu_r=600 is deliberately
// NOT applied here; see the guide note on magnetic-wire as a future enhancement).
struct MatResolved { double rho; double mu_r; };

[[nodiscard]] inline emc::Result<MatResolved>
resolve_material(emc::materials::Material mat, double mu_r,
                 double custom_rho, double custom_sigma) {
    using emc::materials::Material;
    if (mat == Material::Custom) {
        double rho = custom_rho;
        if (rho == 0.0 && custom_sigma != 0.0) rho = 1.0 / custom_sigma;  // derive rho from sigma
        if (!(rho > 0.0))   // also rejects NaN
            return std::unexpected(emc::invalid_input(
                "Custom material needs a positive resistivity or conductivity", "resistivity"));
        return MatResolved{ .rho = rho, .mu_r = mu_r };
    }
    // Real material: pull rho from the single source of truth; an unknown material is a
    // typed UnknownMaterial error. mu_r forced to 1.
    auto props = emc::materials::properties(mat);     // Result<MaterialProperties>
    if (!props) return std::unexpected(props.error());
    const double rho = props->resistivity.numerical_value_in(mp_units::si::ohm * mp_units::si::metre);
    return MatResolved{ .rho = rho, .mu_r = 1.0 };
}

// Pack a CoreOut into the public result. Per-length value is Ohm/m, but the field
// is the Impedance alias, so direct-init {..} relabels the ohm-per-metre quantity
// into the named type (copy-init = would not compile).
[[nodiscard]] inline ConductorResistanceResult to_result(const CoreOut& core) {
    using namespace mp_units;
    using mp_units::si::unit_symbols::ohm;
    using mp_units::si::unit_symbols::m;
    return ConductorResistanceResult{
        .resistance_per_length = emc::units::Impedance{ (core.r_per_metre / m) * (ohm * m) },
        .resistance_total      = emc::units::Impedance{  core.r_total * ohm },
        .skin_depth            = core.skin_depth,
        .resistivity           = core.resistivity,
        .conductivity          = core.conductivity,
        .skin_limited          = core.skin_limited,
    };
}

} // namespace emc::component::detail

// ===========================================================================
//  The four calculators. validate()/the named free function per Input type.
// ===========================================================================
namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;

// ---- Circuit Board Trace ---------------------------------------------------

std::expected<void, emc::Error> validate(const TraceResistanceInput& in) {
    // Pull each field's numeric value in its SI unit, then range-check as a plain double.
    const double f   = in.frequency.numerical_value_in(Hz);
    const double l   = in.length.numerical_value_in(m);
    const double w   = in.width.numerical_value_in(m);
    const double t   = in.thickness.numerical_value_in(m);
    const double rho = in.resistivity.numerical_value_in(si::ohm * si::metre);

    if (auto r = emc::require_positive(f, "frequency");     !r) return r;
    if (auto r = emc::require_positive(l, "length");        !r) return r;
    if (auto r = emc::require_positive(w, "width");         !r) return r;
    if (auto r = emc::require_positive(t, "thickness");     !r) return r;
    if (auto r = emc::require_positive(rho, "resistivity"); !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult> trace_resistance(const TraceResistanceInput& in) {
    // transform runs the math only on the success path and threads any Error through.
    return validate(in).transform([&] {
        const double l   = in.length.numerical_value_in(m);
        const double f   = in.frequency.numerical_value_in(Hz);
        const double w   = in.width.numerical_value_in(m);
        const double t   = in.thickness.numerical_value_in(m);
        const double rho = in.resistivity.numerical_value_in(si::ohm * si::metre);

        // Rectangle: A = w*t, perimeter 2(w+t), DC threshold wt/(2(w+t)).
        const detail::GeometrySI g{
            .area_m2        = w * t,
            .perimeter_m    = 2.0 * (w + t),
            .dc_threshold_m = (w * t) / (2.0 * (w + t)),
        };
        // Copper => mu_r = 1.
        const auto core = detail::resistance_from_geometry(rho, l, f, /*mu_r=*/1.0, g);
        return detail::to_result(core);
    });
}

// ---- Cylindrical Conductor -------------------------------------------------

std::expected<void, emc::Error> validate(const CylindricalConductorInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto r = emc::require_positive(d, "diameter");  !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult>
cylindrical_conductor_resistance(const CylindricalConductorInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho   = in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma = in.custom_conductivity.numerical_value_in(si::siemens / si::metre);

    // resolve material, then compute: an UnknownMaterial / InvalidInput short-circuits the math.
    return detail::resolve_material(in.material, in.relative_permeability, custom_rho, custom_sigma)
        .transform([&](detail::MatResolved mr) {
            const double l = in.length.numerical_value_in(m);
            const double f = in.frequency.numerical_value_in(Hz);
            const double d = in.diameter.numerical_value_in(m);
            const double a = d / 2.0;

            // Circle: A = pi*a^2, perimeter 2*pi*a, DC threshold d/4.
            const detail::GeometrySI g{
                .area_m2        = emc::constants::pi * a * a,
                .perimeter_m    = 2.0 * emc::constants::pi * a,
                .dc_threshold_m = d / 4.0,
            };
            const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, g);
            return detail::to_result(core);
        });
}

// ---- Rectangular Conductor -------------------------------------------------

std::expected<void, emc::Error> validate(const RectangularConductorInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    const double w = in.width.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto r = emc::require_positive(w, "width");     !r) return r;
    if (auto r = emc::require_positive(t, "thickness"); !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult>
rectangular_conductor_resistance(const RectangularConductorInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho   = in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma = in.custom_conductivity.numerical_value_in(si::siemens / si::metre);

    return detail::resolve_material(in.material, in.relative_permeability, custom_rho, custom_sigma)
        .transform([&](detail::MatResolved mr) {
            const double l = in.length.numerical_value_in(m);
            const double f = in.frequency.numerical_value_in(Hz);
            const double w = in.width.numerical_value_in(m);
            const double t = in.thickness.numerical_value_in(m);

            // Same rectangle geometry as the trace, but with material-derived rho/mu_r.
            const detail::GeometrySI g{
                .area_m2        = w * t,
                .perimeter_m    = 2.0 * (w + t),
                .dc_threshold_m = (w * t) / (2.0 * (w + t)),
            };
            const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, g);
            return detail::to_result(core);
        });
}

// ---- Standard Gauge Wire (AWG) ---------------------------------------------

std::expected<int, emc::Error> parse_awg_gauge(std::string_view g) {
    // Aught sizes first (exact match, case-sensitive 'O').
    if (g == "OOOO") return -3;
    if (g == "OOO")  return -2;
    if (g == "OO")   return -1;
    if (g == "O")    return  0;
    // Otherwise: a signed integer ("0", "8", "39", "-3").
    // std::from_chars: locale-free, no allocation; the ptr==end check rejects trailing
    // garbage like "12x" outright (a toInt-style parse would silently accept the "12").
    int value{};
    const auto [ptr, ec] = std::from_chars(g.data(), g.data() + g.size(), value);
    if (ec != std::errc{} || ptr != g.data() + g.size())
        return std::unexpected(emc::invalid_input(
            "gauge must be OOOO/OOO/OO/O or an integer", "gauge"));
    return value;
}

emc::units::Length awg_diameter(int gauge) {
    // dm = 0.0254 * 0.005 * 92^((36 - g) / 39)   [metres]   (the AWG diameter law)
    const double dm = 0.0254 * 0.005 * std::pow(92.0, (36.0 - gauge) / 39.0);
    // Direct-init {..} into the Length alias: relabel the derived-kind metre value.
    return emc::units::Length{ dm * mp_units::si::metre };
}

std::expected<void, emc::Error> validate(const StandardGaugeWireInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto g = parse_awg_gauge(in.gauge); !g)            // malformed gauge => typed InvalidInput
        return std::unexpected(g.error());
    return {};
}

emc::Result<ConductorResistanceResult>
standard_gauge_wire_resistance(const StandardGaugeWireInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho   = in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma = in.custom_conductivity.numerical_value_in(si::siemens / si::metre);

    // Flat monadic pipeline: parse gauge -> resolve material -> compute. gauge was already
    // validated, so the parse here cannot fail, but and_then keeps the chain honest.
    return parse_awg_gauge(in.gauge)
        .and_then([&](int g) -> emc::Result<ConductorResistanceResult> {
            return detail::resolve_material(in.material, in.relative_permeability,
                                            custom_rho, custom_sigma)
                .transform([&](detail::MatResolved mr) {
                    const double l  = in.length.numerical_value_in(m);
                    const double f  = in.frequency.numerical_value_in(Hz);
                    const double dm = awg_diameter(g).numerical_value_in(m);
                    const double a  = dm / 2.0;

                    // Round wire sized by AWG: same circle geometry as Cylindrical.
                    const detail::GeometrySI geo{
                        .area_m2        = emc::constants::pi * a * a,
                        .perimeter_m    = 2.0 * emc::constants::pi * a,
                        .dc_threshold_m = dm / 4.0,
                    };
                    const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, geo);
                    return detail::to_result(core);
                });
        });
}

} // namespace emc::component
