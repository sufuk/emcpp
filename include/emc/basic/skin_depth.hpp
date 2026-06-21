// include/emc/basic/skin_depth.hpp
#pragma once

#include <expected>   // std::expected<void, Error> is the validate() return type

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for a skin-depth calculation.
///
/// Two ways to specify the conductor:
///   * pick a `material` (Copper, Silver, ...) and leave `conductivity` /
///     `relative_permeability` at their defaults -- `calculate()` fills them from
///     `emc::materials::properties()`; or
///   * set `material = Material::Custom` and supply `conductivity` and
///     `relative_permeability` yourself.
struct SkinDepthInput {
    // mp-units quantity fields: the unit lives in the TYPE, so f in Hz vs MHz can
    // never be confused at a call site (compile-time unit safety).
    emc::units::Frequency    frequency{};                       ///< f  > 0
    emc::materials::Material  material = emc::materials::Material::Copper;
    emc::units::Conductivity  conductivity{};                   ///< sigma, used iff material == Custom
    double                    relative_permeability = 1.0;      ///< mu_r, used iff material == Custom
};

/// Result of a skin-depth calculation.
struct SkinDepthResult {
    emc::units::Length skin_depth{};   ///< delta  [m]
};

/// Range/positivity checks on the physical inputs.
[[nodiscard]] std::expected<void, emc::Error> validate(const SkinDepthInput& in);

/// delta = sqrt(1 / (pi*f*mu0*mu_r*sigma)). Resolves the material, validates, then computes.
[[nodiscard]] emc::Result<SkinDepthResult> calculate(const SkinDepthInput& in);

// --- bind to the Calculator concept (compile-time contract) ---
// A tag struct exposing static calculate()/validate() so the type models the
// ValidatedCalculator concept; the static_assert proves the contract at compile time.
struct SkinDepth {
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<SkinDepth>);

} // namespace emc::basic
