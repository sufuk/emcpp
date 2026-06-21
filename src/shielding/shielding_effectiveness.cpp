// src/shielding/shielding_effectiveness.cpp
//
// Implementation of the Near-Field and Plane-Wave shielding-effectiveness
// calculators. Both live in the SAME translation unit so they provably share the
// skin-depth / N_s closed form, routed through emc::constants::pi.
#include <emc/shielding/shielding_effectiveness.hpp>

#include <cmath>   // std::sqrt, std::log10, std::abs

#include <emc/core/constants.hpp>   // emc::constants::{pi, eps0, mu0}

#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::si::unit_symbols::Hz;   // hertz
using mp_units::si::unit_symbols::S;    // siemens
using mp_units::si::unit_symbols::m;    // metre
namespace si = mp_units::si;

// Extract raw SI scalars in the exact units the EMC formula assumes, so the rest of
// the math is plain doubles. numerical_value_in() performs the exact, compile-time
// unit-checked conversion at the boundary (e.g. mil -> m, MHz -> Hz).
struct Raw { double sigma, mu_r, t, r, f; };

[[nodiscard]] Raw as_raw(const NearFieldSeInput& in) {
    return Raw{
        .sigma = in.conductivity.numerical_value_in(S / m),
        .mu_r  = in.relative_permeability,
        .t     = in.thickness.numerical_value_in(m),
        .r     = in.distance.numerical_value_in(m),
        .f     = in.frequency.numerical_value_in(Hz),
    };
}

// Skin depth delta = 1/sqrt(|pi^2 * 4e-7 * mu_r * sigma * f|), which equals
// 1/sqrt(pi * f * mu0 * mu_r * sigma) since mu0 = 4*pi*1e-7. pi is the full-precision
// emc::constants::pi, so Near-Field and Plane-Wave delta agree bit-for-bit.
[[nodiscard]] double skin_depth(double mu_r, double sigma, double f) {
    const double pi  = emc::constants::pi;   // single source of truth for pi
    const double arg = pi * pi * 4.0e-7 * mu_r * sigma * f;
    return 1.0 / std::sqrt(std::abs(arg));
}

// N_s = sqrt(2 * pi^2 * 4e-7 * mu_r * f / sigma); shared by both reflection terms.
[[nodiscard]] double n_s(double mu_r, double sigma, double f) {
    const double pi = emc::constants::pi;
    return std::sqrt(2.0 * pi * pi * 4.0e-7 * mu_r * f / sigma);
}
} // namespace

// ---------------------------------------------------------------------------
//  Near-Field SE
// ---------------------------------------------------------------------------

std::expected<void, emc::Error> validate(const NearFieldSeInput& in) {
    const Raw q = as_raw(in);
    // Each guard keeps a non-positive value out of a denominator / a log / a sqrt.
    if (auto r = emc::require_positive(q.sigma, "conductivity"); !r) return r;
    if (auto r = emc::require_positive(q.t,     "thickness");    !r) return r;
    if (auto r = emc::require_positive(q.r,     "distance");     !r) return r;
    if (auto r = emc::require_positive(q.f,     "frequency");    !r) return r;
    if (auto r = emc::require_positive(q.mu_r,  "relative_permeability"); !r) return r;
    return {};
}

emc::Result<ShieldingResult> calculate(const NearFieldSeInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const Raw    q  = as_raw(in);
    const double pi = emc::constants::pi;
    const double d  = skin_depth(q.mu_r, q.sigma, q.f);

    // Absorption loss [dB]: thicker shield / thinner skin depth absorbs more.
    const double al = 8.7 * (q.t / d);

    // Wave-impedance branch (E vs H near-field). eps0/mu0 come from the CODATA
    // single-source-of-truth, dimension-checked at the boundary via numerical_value_in.
    const double eps0 = emc::constants::eps0.numerical_value_in(si::farad / m);   // ~8.854e-12
    const double mu0  = emc::constants::mu0.numerical_value_in(si::henry / m);    // ~1.2566e-6
    const double zw = (in.field == FieldType::Electric)
                        ? 1.0 / (2.0 * pi * q.f * eps0 * q.r)   // high-Z (E-field)
                        : 2.0 * pi * q.f * mu0 * q.r;           // low-Z  (H-field)

    const double ns = n_s(q.mu_r, q.sigma, q.f);
    const double rl = 20.0 * std::log10(zw / (4.0 * ns));       // reflection loss [dB]

    // Each dB term is relabeled into the typed Decibel wrapper; SE = AL + RL.
    return ShieldingResult{
        .absorption_loss = emc::units::Decibel{ al },
        .reflection_loss = emc::units::Decibel{ rl },
        .shielding       = emc::units::Decibel{ al + rl },
    };
}

emc::Result<ShieldingResult>
near_field_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Length distance,
              emc::units::Frequency frequency, FieldType field) {
    // and_then chains the table lookup into calculate(); an unknown/Custom material
    // short-circuits to ErrorCode::UnknownMaterial with no nested-if ladder.
    return materials::properties(material).and_then(
        [&](const materials::MaterialProperties& mp) {
            return calculate(NearFieldSeInput{
                .conductivity          = mp.conductivity,
                .relative_permeability = mu_r,
                .thickness             = thickness,
                .distance              = distance,
                .frequency             = frequency,
                .field                 = field,
            });
        });
}

// ---------------------------------------------------------------------------
//  Plane-Wave SE
// ---------------------------------------------------------------------------

namespace {
// Reuse the same raw-extraction shape as NearField (minus distance) for consistency.
struct RawPW { double sigma, mu_r, t, f; };

[[nodiscard]] RawPW as_raw(const PlaneWaveSeInput& in) {
    return RawPW{
        .sigma = in.conductivity.numerical_value_in(S / m),
        .mu_r  = in.relative_permeability,
        .t     = in.thickness.numerical_value_in(m),
        .f     = in.frequency.numerical_value_in(Hz),
    };
}
} // namespace

std::expected<void, emc::Error> validate(const PlaneWaveSeInput& in) {
    const RawPW q = as_raw(in);
    if (auto r = emc::require_positive(q.sigma, "conductivity"); !r) return r;
    if (auto r = emc::require_positive(q.t,     "thickness");    !r) return r;
    if (auto r = emc::require_positive(q.f,     "frequency");    !r) return r;
    if (auto r = emc::require_positive(q.mu_r,  "relative_permeability"); !r) return r;
    return {};
}

emc::Result<ShieldingResult> calculate(const PlaneWaveSeInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const RawPW q = as_raw(in);

    // Same skin-depth / N_s closed form as Near-Field, so absorption coincides
    // bit-for-bit at identical sigma, t, f, mu_r.
    const double d  = skin_depth(q.mu_r, q.sigma, q.f);
    const double al = 8.7 * (q.t / d);

    const double ns = n_s(q.mu_r, q.sigma, q.f);
    const double rl = 20.0 * std::log10(377.0 / (4.0 * ns));   // fixed free-space wave impedance

    return ShieldingResult{
        .absorption_loss = emc::units::Decibel{ al },
        .reflection_loss = emc::units::Decibel{ rl },
        .shielding       = emc::units::Decibel{ al + rl },
    };
}

emc::Result<ShieldingResult>
plane_wave_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Frequency frequency) {
    return materials::properties(material).and_then(
        [&](const materials::MaterialProperties& mp) {
            return calculate(PlaneWaveSeInput{
                .conductivity          = mp.conductivity,
                .relative_permeability = mu_r,
                .thickness             = thickness,
                .frequency             = frequency,
            });
        });
}

} // namespace emc::shielding
