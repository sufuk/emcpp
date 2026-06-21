// include/emc/core/materials.hpp
#pragma once  // include this header once per translation unit

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

#include <emc/core/error.hpp>   // emc::Error, emc::ErrorCode, emc::Result
#include <emc/core/units.hpp>   // emc::units::Conductivity, emc::units::Resistivity
#include <emc/export.hpp>       // EMC_API (export/visibility marker)

namespace emc::materials {

using namespace mp_units;

// ---------------------------------------------------------------------------
//  Material — the list of conductors we know about.
//  enum class: scoped, so it will NOT silently turn into an int. A control
//  index or status number cannot be mixed up with a material.
//  The order here is the canonical pinned order; the table below must match it.
// ---------------------------------------------------------------------------
enum class Material {
    Custom = 0,
    Copper,
    Silver,
    Gold,
    Aluminium,
    Nickel,
    Tungsten,
    Platinum,
    Lead,
    Graphite,
    Count        // one past the last real material; used for sizing and looping
};

// ---------------------------------------------------------------------------
//  MaterialProperties — one row of data for one material.
//  conductivity and resistivity carry their unit in the TYPE (mp-units), so
//  wrong-unit math will not compile. mu_r and eps_r stay plain doubles because
//  they are dimensionless and appear bare in formulas (this is the pinned API).
// ---------------------------------------------------------------------------
struct MaterialProperties {
    emc::units::Conductivity conductivity;            // sigma   [S/m]
    double                   relative_permeability;   // mu_r    [-]
    double                   relative_permittivity;   // eps_r   [-]
    emc::units::Resistivity  resistivity;             // rho = 1/sigma  [ohm*m]  (cached)
};

namespace detail {
using mp_units::si::unit_symbols::S;     // siemens
using mp_units::si::unit_symbols::m;     // metre
using mp_units::si::unit_symbols::ohm;   // ohm

// Small helper so the table below reads like a clean data sheet.
// [[nodiscard]]: do not throw away the built row.
// consteval: MUST run at compile time, so a bad value is a build error, never a
// runtime cost. It also lets each row become a true compile-time constant.
[[nodiscard]] consteval MaterialProperties make(double sigma_S_per_m, double mu_r, double eps_r) {
    return MaterialProperties{
        .conductivity          = sigma_S_per_m * (S / m),
        .relative_permeability = mu_r,
        .relative_permittivity = eps_r,
        .resistivity           = (1.0 / sigma_S_per_m) * (ohm * m),
    };
}
} // namespace detail

// ---------------------------------------------------------------------------
//  The single source of truth. Conductivity values are standard handbook
//  figures (IACS-consistent for the common conductors).
//  The order must mirror the enum (Custom is left out); checked below.
//    * Copper:    5.96e7 S/m (IACS-consistent).
//    * Silver:    6.30e7 S/m.
//    * Gold:      4.10e7 S/m.
//    * Aluminium: 3.77e7 S/m; one canonical spelling "Aluminium".
//    * Nickel:    1.43e7 S/m, magnetic (mu_r = 600).
//    * Tungsten / Platinum / Lead / Graphite: mu_r = 1 each.
//  inline constexpr: a compile-time constant with ONE shared definition for the
//  whole program (no duplicate copy per source file).
// ---------------------------------------------------------------------------
inline constexpr std::array<MaterialProperties,
                            static_cast<std::size_t>(Material::Count) - 1> table{{
    //            sigma [S/m]   mu_r        eps_r
    detail::make(5.96e7,   0.999991, 1.0),  // Copper
    detail::make(6.30e7,   0.99998,  1.0),  // Silver
    detail::make(4.10e7,   1.0,      1.0),  // Gold
    detail::make(3.77e7,   1.00002,  1.0),  // Aluminium
    detail::make(1.43e7, 600.0,      1.0),  // Nickel    (magnetic: high mu_r)
    detail::make(1.79e7,   1.0,      1.0),  // Tungsten
    detail::make(9.43e6,   1.0,      1.0),  // Platinum
    detail::make(4.55e6,   1.0,      1.0),  // Lead
    detail::make(1.00e5,   1.0,      1.0),  // Graphite
}};

// Maps a name to its enum and back. Covers Custom plus every real material.
// std::string_view: we only READ the name and do not own it, so a view avoids
// a copy/allocation.
struct NameEntry { Material id; std::string_view name; };
inline constexpr std::array<NameEntry, static_cast<std::size_t>(Material::Count)> names{{
    {Material::Custom,    "Custom"},
    {Material::Copper,    "Copper"},
    {Material::Silver,    "Silver"},
    {Material::Gold,      "Gold"},
    {Material::Aluminium, "Aluminium"},
    {Material::Nickel,    "Nickel"},
    {Material::Tungsten,  "Tungsten"},
    {Material::Platinum,  "Platinum"},
    {Material::Lead,      "Lead"},
    {Material::Graphite,  "Graphite"},
}};

// ---------------------------------------------------------------------------
//  properties() — look up one material by enum.
//  [[nodiscard]]: do not ignore the return; dropping an error is a bug.
//  constexpr: usable at compile time (see the static_asserts at the bottom).
//  noexcept: promises not to throw, so callers can rely on that.
//  Returns Result<MaterialProperties> (std::expected): either the value or a
//  typed Error for Custom / out-of-range, never a magic sentinel number.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr Result<MaterialProperties> properties(Material m) noexcept {
    if (m == Material::Custom)
        return std::unexpected(Error{ .code = ErrorCode::UnknownMaterial,
                                      .message = "Custom material: supply properties explicitly",
                                      .field = "material" });
    const auto idx = static_cast<std::size_t>(m);
    if (idx == 0 || idx >= static_cast<std::size_t>(Material::Count))
        return std::unexpected(Error{ .code = ErrorCode::UnknownMaterial,
                                      .message = "Material enum out of range",
                                      .field = "material" });
    return table[idx - 1];   // -1 because Custom sits in slot 0 and has no table row
}

// name -> enum.
// std::optional: models "maybe no match" as a type instead of a sentinel value.
[[nodiscard]] constexpr std::optional<Material> from_name(std::string_view n) noexcept {
    for (const auto& e : names)
        if (e.name == n) return e.id;
    return std::nullopt;
}

// enum -> name. Returns a borrowed view into the names table (no copy).
[[nodiscard]] constexpr std::string_view to_name(Material m) noexcept {
    for (const auto& e : names)
        if (e.id == m) return e.name;
    return "Unknown";
}

// ---------------------------------------------------------------------------
//  Named constants so code can write e.g. emc::materials::nickel.conductivity.
//  Each just aliases its row from the single table above.
//  inline constexpr: compile-time value, one shared definition program-wide.
// ---------------------------------------------------------------------------
inline constexpr MaterialProperties copper    = table[static_cast<std::size_t>(Material::Copper)    - 1];
inline constexpr MaterialProperties silver    = table[static_cast<std::size_t>(Material::Silver)    - 1];
inline constexpr MaterialProperties gold      = table[static_cast<std::size_t>(Material::Gold)      - 1];
inline constexpr MaterialProperties aluminium = table[static_cast<std::size_t>(Material::Aluminium) - 1];
inline constexpr MaterialProperties nickel    = table[static_cast<std::size_t>(Material::Nickel)    - 1];
inline constexpr MaterialProperties tungsten  = table[static_cast<std::size_t>(Material::Tungsten)  - 1];
inline constexpr MaterialProperties platinum  = table[static_cast<std::size_t>(Material::Platinum)  - 1];
inline constexpr MaterialProperties lead      = table[static_cast<std::size_t>(Material::Lead)      - 1];
inline constexpr MaterialProperties graphite  = table[static_cast<std::size_t>(Material::Graphite)  - 1];

// Out-of-line anchor: number of built-in (non-Custom) materials.
// [[nodiscard]]: do not drop the count. noexcept: never throws.
// Defined in src/core/materials.cpp so this header gives the library a real
// symbol to export.
[[nodiscard]] EMC_API std::size_t builtin_count() noexcept;

} // namespace emc::materials

// ---------------------------------------------------------------------------
//  Compile-time checks. These run during the build; if any fails the database
//  cannot compile, so it can never be merged in a broken state.
// ---------------------------------------------------------------------------
namespace emc::materials::detail {

using namespace mp_units;

// (a) Table length matches the enum (Custom excluded).
static_assert(table.size() == static_cast<std::size_t>(Material::Count) - 1,
              "materials::table size must equal number of non-Custom materials");

// (b) names[] covers Custom plus every real material exactly.
static_assert(names.size() == static_cast<std::size_t>(Material::Count),
              "names[] must list Custom plus every material");

// (c) Every value is strictly positive and self-consistent (rho == 1/sigma).
// consteval: this whole check is forced to run at compile time.
consteval bool all_values_sane() {
    for (const auto& p : table) {
        const double sigma = p.conductivity.numerical_value_in(si::siemens / si::metre);
        const double rho   = p.resistivity.numerical_value_in(si::ohm * si::metre);
        if (sigma <= 0.0)                      return false;
        if (p.relative_permeability <= 0.0)    return false;
        if (p.relative_permittivity <= 0.0)    return false;
        if (rho <= 0.0)                        return false;
        if (rho * sigma < 0.99 || rho * sigma > 1.01) return false;   // rho ~= 1/sigma
    }
    return true;
}
static_assert(all_values_sane(),
              "every material must have positive, self-consistent properties");

// (d) Lookup order and name mapping behave as expected.
static_assert(properties(Material::Gold).has_value());
static_assert(to_name(Material::Aluminium) == "Aluminium");
static_assert(from_name("Aluminium") == Material::Aluminium);
static_assert(!from_name("Aluminum").has_value());   // alternate spelling rejected on purpose

// (e) The named constants really alias their rows.
static_assert(nickel.relative_permeability == 600.0);

} // namespace emc::materials::detail
