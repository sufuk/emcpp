// src/basic/skin_depth.cpp
#include <emc/basic/skin_depth.hpp>

#include <utility>   // std::pair for the resolved (sigma, mu_r) bundle

#include <mp-units/math.h>          // mp_units::sqrt for quantities (unit-correct sqrt)
#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, S, m, ...

namespace {

// Resolve sigma and mu_r either from the material table or from the Custom fields.
// Returns (conductivity, mu_r) on success; a table lookup that fails short-circuits
// as a typed Error via std::expected.
[[nodiscard]] emc::Result<std::pair<emc::units::Conductivity, double>>
resolve_conductor(const SkinDepthInput& in) {
    if (in.material == emc::materials::Material::Custom)
        return std::pair{in.conductivity, in.relative_permeability};

    // properties() returns std::expected; transform() runs only on success and maps
    // the row to the (sigma, mu_r) pair, forwarding any UnknownMaterial error.
    return emc::materials::properties(in.material)
        .transform([](const emc::materials::MaterialProperties& p) {
            return std::pair{p.conductivity, p.relative_permeability};
        });
}

} // namespace

std::expected<void, emc::Error> validate(const SkinDepthInput& in) {
    // Pull the numeric value out in each field's SI unit, then range-check as a plain
    // double (the foundation validators work on doubles).
    const double f = in.frequency.numerical_value_in(Hz);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;   // f appears as pi*f in denom

    if (in.material == emc::materials::Material::Custom) {
        const double sigma = in.conductivity.numerical_value_in(S / m);
        if (auto r = emc::require_positive(sigma, "conductivity"); !r) return r;
        if (auto r = emc::require_positive(in.relative_permeability, "relative_permeability"); !r)
            return r;
    }
    return {};
}

emc::Result<SkinDepthResult> calculate(const SkinDepthInput& in) {
    // 1) resolve material first (Custom-without-props becomes a typed UnknownMaterial error).
    //    and_then chains: only runs the math if the resolve succeeded, forwarding errors.
    return resolve_conductor(in).and_then(
        [&](std::pair<emc::units::Conductivity, double> conductor)
            -> emc::Result<SkinDepthResult> {
            const auto [sigma, mu_r] = conductor;   // structured binding names both pieces

            // 2) validate using the resolved values: rebuild the input as Custom so the
            //    sigma/mu_r positivity checks always run, catching a corrupt table row
            //    before it can produce a nonsensical delta.
            SkinDepthInput resolved = in;
            resolved.material              = emc::materials::Material::Custom;
            resolved.conductivity          = sigma;
            resolved.relative_permeability = mu_r;
            if (auto v = validate(resolved); !v)
                return std::unexpected(v.error());

            // 3) mu = mu0 * mu_r   (mu_r is a plain double; mu0 is an mp-units quantity [H/m]).
            const auto mu = emc::constants::mu0 * mu_r;

            // 4) denominator = pi*f*mu*sigma -> carries units of 1/m^2 (a "per area").
            const auto denom = emc::constants::pi * in.frequency * mu * sigma;

            // 5) delta = sqrt(1/denom). mp_units::sqrt proves sqrt(m^2) = m, so the result
            //    is unit-correct. Direct-init {..} into the Length alias: the derived-kind
            //    metre value is explicitly relabelled as a Length (copy-init would not compile).
            const emc::units::Length delta{ sqrt(1.0 / denom).in(si::metre) };

            return SkinDepthResult{ .skin_depth = delta };
        });
}

} // namespace emc::basic
