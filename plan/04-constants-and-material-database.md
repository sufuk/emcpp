# 🧮 Physical Constants & Material Database (single source of truth)

> [!IMPORTANT]
> **Purpose:** define exactly **one** place for every physical constant and every material property
> in the `emc` library, expressed as `constexpr` mp-units quantities, so that every calculator that
> needs pi, the speed of light, vacuum permeability, or a material's conductivity draws from the
> *same* authoritative value.

EMC calculations are dense with physical constants (pi, _c_, μ₀, ε₀, _Z₀_) and material properties
(conductivity, permeability, permittivity, resistivity). A library that lets each calculator carry its
own copy of these values invites silent numerical drift between calculators that should agree to the
last digit. This document defines two compile-time databases that solve that once:

1. **`emc::constants`** — a `constexpr` header of exact CODATA/SI physical constants as mp-units
   quantities.
2. **`emc::materials`** — a `constexpr` material database: one row per material, holding standard
   textbook property values (σ, μ_r, ε_r, ρ).

Both are header-declared so the values are usable at compile time and in `static_assert`, with any
non-`constexpr` helpers compiled in `src/` per the library's "traditional compiled library" decision.

---

## 1. ⚙️ The constants header: `emc::constants`

### 1.1 Design rules

EMC formulas span Hz to GHz and require constants that are **exact** and **dimension-safe**, because a
wavelength, antenna, or field result is only as accurate as the _c_ and pi feeding it. The constants
header therefore follows a few strict rules:

- **One header:** `include/emc/constants.hpp`. Everything `constexpr` — compile-time, zero runtime
  cost, usable in `static_assert`.
- **pi from the standard library:** `std::numbers::pi` (`<numbers>`, C++20) — full precision,
  namespaced, no macro.
- **Physical constants are mp-units quantities**, not bare doubles, so the unit is part of the type
  and conversions are compile-checked (see `03-quantities-and-units-mp-units.md`).
- **Values are current SI / CODATA:** exact defining constants where the SI defines them (_c_, _h_,
  _e_), the measured CODATA value for μ₀.
- **`static_assert` sanity relations** prove the constants are mutually consistent **at compile time**.

> [!NOTE]
> **Provenance.** The values below are the SI/CODATA-2018 set. _c_, _h_, and the elementary charge _e_
> are **exact defining constants** of the SI (no uncertainty by definition since the 2019 redefinition).
> μ₀ and ε₀ are **measured** quantities in the post-2019 SI: μ₀ is no longer *exactly* 4π×10⁻⁷ H/m but
> a measured value ≈ 1.256 637 062 12×10⁻⁶ H/m. Each literal below is annotated with which category it
> belongs to, so a future maintainer knows what may legitimately be revised when CODATA publishes a new
> adjustment.

### 1.2 The header

```c++
// include/emc/constants.hpp
#pragma once

#include <numbers>      // std::numbers::pi  (full precision, namespaced)
#include <cmath>        // std::sqrt is constexpr in C++23 (used in static_assert below)

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

namespace emc::constants {

using namespace mp_units;
using mp_units::si::unit_symbols::A;   // ampere
using mp_units::si::unit_symbols::F;   // farad
using mp_units::si::unit_symbols::H;   // henry
using mp_units::si::unit_symbols::J;   // joule
using mp_units::si::unit_symbols::m;   // metre
using mp_units::si::unit_symbols::s;   // second
using mp_units::si::unit_symbols::ohm; // ohm

// ----- Pure mathematical constant: pi -----
// constexpr, namespaced, full precision via <numbers>.
inline constexpr double pi = std::numbers::pi_v<double>;

// ----- Speed of light in vacuum -----
// SI EXACT defining constant.  c = 299 792 458 m/s.
inline constexpr quantity speed_of_light = 299'792'458.0 * (m / s);

// ----- Planck constant -----
// SI EXACT defining constant since the 2019 redefinition.  h = 6.626 070 15e-34 J·s.
inline constexpr quantity planck = 6.626'070'15e-34 * (J * s);

// ----- Vacuum permeability mu0 -----
// CODATA-2018 measured value; very close to 4*pi*1e-7 but no longer EXACT in the post-2019 SI.
inline constexpr quantity mu_0 = 1.256'637'062'12e-6 * (H / m);

// ----- Vacuum permittivity eps0 -----
// Derived: eps0 = 1 / (mu0 * c^2).  Stored as a literal CODATA value; the static_assert below
// proves it agrees with the derivation, so both forms are guaranteed consistent.
inline constexpr quantity epsilon_0 = 8.854'187'8128e-12 * (F / m);

// ----- Impedance of free space Z0 = sqrt(mu0/eps0) ~ 376.730313668 ohm -----
inline constexpr quantity impedance_of_free_space = 376.730'313'668 * ohm;

// ----- Elementary charge (used by ESD/lightning coupling calculators) -----
inline constexpr quantity elementary_charge = 1.602'176'634e-19 * (A * s); // coulomb = A*s, EXACT

} // namespace emc::constants
```

> [!TIP]
> **Why `inline constexpr` and not `constexpr` alone?** A header-defined `constexpr` namespace-scope
> variable is implicitly `const` but, without `inline`, would get **internal linkage** — each TU gets
> its own copy, and taking its address yields different pointers across TUs (an ODR foot-gun). Marking
> it `inline` (C++17 inline variables) gives **one** shared definition across the whole program, which
> is exactly what a single source of truth requires. This is the standard idiom for header constants.

### 1.3 Compile-time sanity relations (proving consistency)

The defining relations of electromagnetism must hold among these constants. We assert them at
compile time. C++23 makes the `<cmath>` functions (`std::sqrt`) usable in `constexpr`, and mp-units
quantities are `constexpr`-friendly, so these checks run during compilation — if anyone edits a
literal and breaks consistency, **the build fails**.

```c++
// include/emc/constants.hpp  (appended)
namespace emc::constants::detail {

using namespace mp_units;

// Relative comparison helper (avoids brittle exact == on floating point).
[[nodiscard]] constexpr bool close(double a, double b, double rel = 1e-6) {
    const double d = a - b;
    const double m = (a < 0 ? -a : a);
    return (d < 0 ? -d : d) <= rel * (m == 0 ? 1.0 : m);
}

// 1) c == 1 / sqrt(eps0 * mu0)
inline constexpr double c_check =
    1.0 / std::sqrt((epsilon_0 * mu_0).numerical_value_in(si::farad * si::henry / (si::metre * si::metre)));
static_assert(close(c_check, speed_of_light.numerical_value_in(si::metre / si::second)),
              "c must equal 1/sqrt(eps0*mu0)");

// 2) Z0 == sqrt(mu0 / eps0)
inline constexpr double z0_check =
    std::sqrt((mu_0 / epsilon_0).numerical_value_in(si::ohm * si::ohm));
static_assert(close(z0_check, impedance_of_free_space.numerical_value_in(si::ohm)),
              "Z0 must equal sqrt(mu0/eps0)");

// 3) Z0 == mu0 * c  (alternative identity)
static_assert(close((mu_0 * speed_of_light).numerical_value_in(si::ohm),
                    impedance_of_free_space.numerical_value_in(si::ohm)),
              "Z0 must equal mu0*c");

// 4) Sanity: 4*pi*1e-7 is within 0.01% of the CODATA mu0 we ship (documents the measured value).
static_assert(close(mu_0.numerical_value_in(si::henry / si::metre), 4.0 * pi * 1e-7, 1e-4),
              "mu0 should be close to 4*pi*1e-7");

} // namespace emc::constants::detail
```

> [!IMPORTANT]
> **Why `static_assert` here?** The physics relations among _c_, μ₀, ε₀, and _Z₀_ are encoded as
> build-time invariants. Because the constants are mp-units quantities, the checks are
> **dimension-aware** — you cannot accidentally compare an impedance against a velocity. You literally
> cannot ship a self-contradictory constant set: if a maintainer revises one literal after a future
> CODATA adjustment without revising the others, the build stops. The mp-units API for extracting the
> underlying number (`numerical_value_in`) is detailed in `03-quantities-and-units-mp-units.md`.

> [!NOTE]
> **C++26 forward-looking:** with **contracts** these could become precondition/postcondition style
> checks on constant-producing helpers, and **reflection** could enumerate the constants for an
> auto-generated documentation/units table. Today, `static_assert` is the right tool.

### 1.4 Why `constexpr` + `std::numbers` + mp-units

| Property | `emc::constants` |
|---|---|
| Correct value (SI/CODATA) | ✅ |
| Scoped / namespaced | ✅ `emc::constants::` |
| Type-checked dimension | ✅ mp-units quantity |
| Single definition (ODR-clean) | ✅ `inline constexpr` |
| Usable in `static_assert` | ✅ compile-time consistency |
| Survives refactor | ✅ edit one place; asserts guard it |

---

## 2. 🗂️ The material database: `emc::materials`

EMC conductor calculations (skin depth, resistance, transmission-line parameters) all draw from the
same small set of metals. Rather than let each calculator embed its own conductivity or resistivity
literals — which is how two calculators end up disagreeing in the third digit for "the same" copper —
the library publishes **one** canonical table.

### 2.1 The property values the library ships

The database standardizes on **conductivity σ (S/m) at 20 °C**, **relative permeability** μ_r,
**relative permittivity** ε_r (≈ 1 for these conductors), and a cached **resistivity ρ = 1/σ** for
the resistance calculators. The values are standard, widely-published reference figures for
engineering-grade (IACS-consistent) metals; magnetic metals such as Nickel carry their characteristic
high μ_r.

| Material   | σ (S/m)   | ρ = 1/σ (Ω·m) | μ_r       | ε_r |
|---|---|---|---|---|
| Copper     | 5.96e7    | 1.678e-8 | 0.999991 | 1 |
| Silver     | 6.30e7    | 1.587e-8 | 0.99998  | 1 |
| Gold       | 4.10e7    | 2.439e-8 | 1.0      | 1 |
| Aluminum   | 3.77e7    | 2.653e-8 | 1.00002  | 1 |
| Nickel     | 1.43e7    | 6.993e-8 | 600      | 1 |
| Tungsten   | 1.79e7    | 5.587e-8 | 1.0      | 1 |
| Platinum   | 9.43e6    | 1.060e-7 | 1.0      | 1 |
| Lead       | 4.55e6    | 2.198e-7 | 1.0      | 1 |
| Graphite   | 1.00e5    | 1.000e-5 | 1.0      | 1 |

> [!NOTE]
> **Provenance.** These are standard textbook/handbook values for the bulk metals at 20 °C; each row
> in code carries a source/reference comment. The database keeps exactly **one row per material**, so
> revising a value is a one-line, reviewable change in a single place — and the `static_assert`
> invariants in §4.2 guarantee the row stays internally consistent (ρ = 1/σ, all values positive).

### 2.2 Goals

- One `enum class Material` (scoped, no implicit int conversions).
- One `MaterialProperties` struct holding **mp-units quantities** (σ, μ_r, ε_r, ρ).
- One `constexpr std::array` table — the *only* place values live.
- `constexpr` lookup by enum, and a `constexpr` name↔enum mapping.
- A `Custom` material so users can supply their own properties.
- Everything `constexpr` and pure → thread-safe, usable at compile time, testable with `static_assert`.

### 2.3 The header

```c++
// include/emc/materials.hpp
#pragma once

#include <array>
#include <expected>
#include <optional>
#include <string_view>
#include <variant>

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

#include <emc/error.hpp>   // emc::Error, emc::ErrorCode  (see 05-error-handling-and-validation.md)

namespace emc::materials {

using namespace mp_units;

// Scoped enum: no implicit int conversion, explicit Custom slot.
enum class Material {
    Custom = 0,
    Copper,
    Silver,
    Gold,
    Aluminum,   // single canonical spelling
    Nickel,
    Tungsten,
    Platinum,
    Lead,
    Graphite,
    Count       // sentinel = number of real entries; handy for table sizing/iteration
};

// One properties record. Units live in the TYPE (mp-units), not in comments.
//   conductivity        sigma  [S/m]   == isq::electrical_conductivity
//   relative_permeability mu_r [-]     dimensionless (one)
//   relative_permittivity eps_r [-]    dimensionless (one)
//   resistivity         rho   [ohm*m]  == isq::resistivity ; redundant with 1/sigma but cached
struct MaterialProperties {
    quantity<isq::electrical_conductivity[si::siemens / si::metre]> conductivity;
    quantity<dimensionless[one]>                                    relative_permeability;
    quantity<dimensionless[one]>                                    relative_permittivity;
    quantity<isq::resistivity[si::ohm * si::metre]>                 resistivity;
};

namespace detail {
using mp_units::si::unit_symbols::S;     // siemens
using mp_units::si::unit_symbols::m;     // metre
using mp_units::si::unit_symbols::ohm;   // ohm

// Compact constructor so the table reads like a clean data sheet.
[[nodiscard]] consteval MaterialProperties make(double sigma_S_per_m, double mu_r, double eps_r) {
    return MaterialProperties{
        .conductivity          = sigma_S_per_m * (S / m),
        .relative_permeability = mu_r * one,
        .relative_permittivity = eps_r * one,
        .resistivity           = (1.0 / sigma_S_per_m) * (ohm * m),
    };
}
} // namespace detail

// THE single source of truth. Indexed by static_cast<std::size_t>(Material) - 1 for real entries
// (Custom has no fixed row; see properties()).  Order MUST mirror the enum (asserted below).
inline constexpr std::array<MaterialProperties, static_cast<std::size_t>(Material::Count) - 1> table{{
    //                     sigma [S/m]   mu_r        eps_r
    detail::make(5.96e7,   0.999991, 1.0),  // Copper
    detail::make(6.30e7,   0.99998,  1.0),  // Silver
    detail::make(4.10e7,   1.0,      1.0),  // Gold
    detail::make(3.77e7,   1.00002,  1.0),  // Aluminum
    detail::make(1.43e7, 600.0,      1.0),  // Nickel    (magnetic: high mu_r)
    detail::make(1.79e7,   1.0,      1.0),  // Tungsten
    detail::make(9.43e6,   1.0,      1.0),  // Platinum
    detail::make(4.55e6,   1.0,      1.0),  // Lead
    detail::make(1.00e5,   1.0,      1.0),  // Graphite
}};

// Name <-> enum mapping (constexpr table now; see C++26 callout for auto-generation).
struct NameEntry { Material id; std::string_view name; };
inline constexpr std::array<NameEntry, 10> names{{
    {Material::Custom,   "Custom"},
    {Material::Copper,   "Copper"},
    {Material::Silver,   "Silver"},
    {Material::Gold,     "Gold"},
    {Material::Aluminum, "Aluminum"},
    {Material::Nickel,   "Nickel"},
    {Material::Tungsten, "Tungsten"},
    {Material::Platinum, "Platinum"},
    {Material::Lead,     "Lead"},
    {Material::Graphite, "Graphite"},
}};

// ---- constexpr lookup by enum ----
// Returns std::expected: a clean, dimension-typed error for Custom/unknown, never a magic sentinel.
[[nodiscard]] constexpr std::expected<MaterialProperties, Error>
properties(Material m) noexcept {
    if (m == Material::Custom)
        return std::unexpected(Error{ErrorCode::CustomMaterialHasNoTableEntry, "Custom material: supply properties explicitly"});
    const auto idx = static_cast<std::size_t>(m);
    if (idx == 0 || idx >= static_cast<std::size_t>(Material::Count))
        return std::unexpected(Error{ErrorCode::UnknownMaterial, "Material enum out of range"});
    return table[idx - 1];           // -1 because Custom occupies slot 0
}

// ---- name -> enum ----
[[nodiscard]] constexpr std::optional<Material> from_name(std::string_view n) noexcept {
    for (const auto& e : names) if (e.name == n) return e.id;
    return std::nullopt;
}

// ---- enum -> name ----
[[nodiscard]] constexpr std::string_view to_name(Material m) noexcept {
    for (const auto& e : names) if (e.id == m) return e.name;
    return "Unknown";
}

} // namespace emc::materials
```

> [!TIP]
> **Why `enum class` (not `enum`)?** A scoped enum blocks implicit `int`↔`enum` conversions, so a UI
> index or any stray integer cannot silently become a `Material`, and the enumerators are namespaced so
> `Material::Copper` cannot collide with anything. This makes material selection type-safe by
> construction. (See `02-modern-cpp-feature-catalog.md`.)

> [!TIP]
> **Why `std::array` + `consteval make()`?** A `constexpr std::array` is a true compile-time constant:
> the whole table is folded at compile time, lives in read-only data, and is trivially thread-safe (no
> dynamic init, no global mutable state). `consteval make()` *forces* row construction to happen at
> compile time, so a typo that would prevent constant evaluation is a hard error, not a silent runtime
> cost.

> [!IMPORTANT]
> **Why `std::expected` instead of a sentinel return?** A magic numeric sentinel (say `1.0`) is a
> *plausible* resistivity, so a caller cannot distinguish "unknown material" from "a material with
> ρ = 1 Ω·m". `std::expected<MaterialProperties, Error>` makes the failure a distinct, typed value the
> caller must handle, and `[[nodiscard]]` makes ignoring it a warning. (Full rationale in
> `05-error-handling-and-validation.md`.)

### 2.4 Custom material support

`Custom` deliberately has **no table row**: `properties(Material::Custom)` returns an error telling
the caller to provide values directly. Because a calculator can legitimately be called with an alloy or
composite that is not in the catalog, the API lets a user bypass the database with fully
type/unit-checked properties:

```c++
// Caller-supplied custom material — full type/unit checking, no database row needed.
using namespace mp_units;
using mp_units::si::unit_symbols::S; using mp_units::si::unit_symbols::m;
emc::materials::MaterialProperties my_alloy{
    .conductivity          = 2.5e7 * (S / m),
    .relative_permeability = 1.0 * one,
    .relative_permittivity = 1.0 * one,
    .resistivity           = (1.0 / 2.5e7) * (mp_units::si::ohm * m),
};
```

A small helper resolves "either a known material or explicit props" in one place:

```c++
// include/emc/materials.hpp  (appended)
namespace emc::materials {

// A calculator input can hold either a catalog id or fully-specified custom properties.
using MaterialSpec = std::variant<Material, MaterialProperties>;

[[nodiscard]] constexpr std::expected<MaterialProperties, Error>
resolve(const MaterialSpec& spec) noexcept {
    if (const auto* mp = std::get_if<MaterialProperties>(&spec))
        return *mp;                                   // user supplied props directly
    return properties(std::get<Material>(spec));      // look up catalog (errors on Custom)
}

} // namespace emc::materials
```

### 2.5 C++26 / present-day tooling callout (name↔enum auto-generation)

The hand-written `names` array duplicates the enum. Two ways to remove that duplication:

- **Today (optional dependency): `magic_enum`.** `magic_enum::enum_name(Material::Copper)` →
  `"Copper"` and `magic_enum::enum_cast<Material>("Copper")` give name↔enum at compile time with
  **no** parallel table. It is a single-header library; if adopted, `to_name`/`from_name`/`names`
  collapse to thin wrappers. (Limitation: enum value range must fit `magic_enum`'s default span.)
- **C++26 forward-looking: static reflection.** With `^^Material` / `std::meta::enumerators_of`,
  the library can *generate* the name table, the `MaterialProperties` field-by-field printer, and even
  a units report directly from the enum/struct — zero hand-maintained mirror tables. This is the
  long-term answer; until then, keep the `constexpr names` array (or `magic_enum`) and let the
  `static_assert` in §4.2 guarantee it stays in sync with the enum.

---

## 3. 🔌 Usage in calculators

### 3.1 Skin depth

The skin depth of a conductor is the standard EMC formula

```text
delta = 1 / sqrt(pi * f * mu * sigma)        mu = mu_r * mu_0
```

A calculator pulls σ and μ_r from `emc::materials`, μ₀ and pi from `emc::constants`, and lets mp-units
carry the units. There are no literals and no local copies of any constant:

```c++
// emc::basic::skin_depth — see 06-calculator-design-pattern.md for the full pattern
#include <emc/constants.hpp>
#include <emc/materials.hpp>

auto props = emc::materials::properties(emc::materials::Material::Copper);  // expected<...>
if (!props) return std::unexpected(props.error());

using namespace mp_units;
const auto sigma = props->conductivity;                                  // S/m, typed
const auto mu    = props->relative_permeability * emc::constants::mu_0;   // H/m, typed
// delta = 1 / sqrt(pi * f * mu * sigma)   -- pi from std::numbers, dimension-checked by mp-units
const quantity delta =
    1.0 / sqrt(emc::constants::pi * f * mu * sigma);                      // result in metres
```

### 3.2 Conductor resistance

A resistance calculator (round wire, cylindrical or rectangular conductor) uses ρ from the same
database as a typed quantity, honours the material's actual μ_r, and gets a real error on an unknown
material:

```c++
auto props = emc::materials::properties(material);     // material : emc::materials::Material
if (!props) return std::unexpected(props.error());     // typed error
const auto rho   = props->resistivity;                 // ohm*m, typed
const auto mu_r  = props->relative_permeability;       // honoured
// any skin-depth correction uses emc::constants::pi and emc::constants::mu_0 — the same values
// every other calculator sees
```

> [!CAUTION]
> Because every conductor calculator now calls the *one* `properties()`, "the same" copper resistivity
> is byte-identical everywhere. If you ever need a non-standard value (a specific alloy, a temperature
> other than 20 °C), use a `Custom` material via `resolve()` rather than editing the shared table — the
> table is what guarantees cross-calculator agreement.

---

## 4. 🛡️ Extensibility & validation

### 4.1 Adding a material in exactly one place

To add, say, **Brass**:

1. Add `Brass` to `enum class Material` (before `Count`).
2. Add one `detail::make(...)` row to `table` (same position).
3. Add one `{Material::Brass, "Brass"}` entry to `names`.

That's it — every calculator that takes a `Material` sees Brass immediately. With reflection or
`magic_enum` (§2.5), step 3 disappears.

### 4.2 Compile-time validation invariants

`static_assert` enforces the table's internal consistency at build time:

```c++
// include/emc/materials.hpp  (appended)
namespace emc::materials::detail {

using namespace mp_units;

// (a) Table length matches the enum (Custom excluded). Adding an enumerator without a row -> build error.
static_assert(table.size() == static_cast<std::size_t>(Material::Count) - 1,
              "materials::table size must equal number of non-Custom materials");

// (b) names[] covers Custom + every real material exactly.
static_assert(names.size() == static_cast<std::size_t>(Material::Count),
              "names[] must list Custom plus every material");

// (c) Every physical value is strictly positive and self-consistent (rho == 1/sigma).
consteval bool all_values_sane() {
    for (const auto& p : table) {
        if (p.conductivity.numerical_value_in(si::siemens / si::metre)        <= 0) return false;
        if (p.relative_permeability.numerical_value_in(one)                   <= 0) return false;
        if (p.relative_permittivity.numerical_value_in(one)                   <= 0) return false;
        if (p.resistivity.numerical_value_in(si::ohm * si::metre)             <= 0) return false;
        const double sigma = p.conductivity.numerical_value_in(si::siemens / si::metre);
        const double rho   = p.resistivity.numerical_value_in(si::ohm * si::metre);
        if (rho * sigma < 0.99 || rho * sigma > 1.01) return false;   // rho ≈ 1/sigma
    }
    return true;
}
static_assert(all_values_sane(), "every material must have positive, self-consistent properties");

// (d) Lookup ordering: properties(Material::Gold) really returns the Gold row.
static_assert(properties(Material::Gold).has_value());
static_assert(to_name(Material::Aluminum) == "Aluminum");
static_assert(from_name("Aluminum") == Material::Aluminum);
static_assert(!from_name("Aluminium").has_value());   // only the canonical spelling is accepted

} // namespace emc::materials::detail
```

> [!IMPORTANT]
> **Why push these into `static_assert`/`consteval`?** Encoding "every value positive", "ρ = 1/σ",
> "table size == enum size", and "lookup hits the right row" as compile-time invariants means the
> database cannot be merged in a broken state. These are the data-integrity analogue of the §1.3
> physics asserts — a self-contradictory table simply does not compile.

### 4.3 Keeping the table indexed by enum

The contract is: **`table` row order mirrors `enum class Material` order** (Custom excluded), so the
O(1) `properties()` index lookup is correct. Invariant (a) above catches a *size* mismatch; the
ordering itself is documented at the `table` definition and spot-checked by invariant (d). If
reflection is later adopted, the table can be built *from* the enum so ordering cannot drift at all.

---

## 5. 🧪 Testing the constants and database

> [!NOTE]
> Tests here use known-value arithmetic, physical identities, and `static_assert`, all derived from
> first principles. The full testing approach is in `09-testing-and-golden-vectors.md`.

- **Compile-time invariants (free):** the `static_assert` blocks in §1.3 and §4.2 are themselves tests
  — if they pass, the build is green; if a constant or row is inconsistent, the build fails. No runtime
  test is needed to cover them.
- **Known-value constants:** assert `speed_of_light == 299'792'458 m/s` and
  `elementary_charge == 1.602'176'634e-19 C` exactly (these are *defined* SI values, so exact equality
  is correct). Assert `pi` rounds to `3.14159265` to 8 digits.
- **Physical identities as round-trips:** check _Z₀_ = μ₀·_c_ and _c_ = 1/√(μ₀·ε₀) at runtime in Catch2
  as well, with a relative tolerance ~1e-6 — the same relations the compile-time asserts enforce, now
  exercised through the public quantity API.
- **Database known values:** a Catch2 `GENERATE` table of `{Material -> expected σ}` pairs, with the
  expected numbers being the §2.1 ship values written out by hand, e.g. `{Copper, 5.96e7}`,
  `{Gold, 4.10e7}`. For each, assert `properties(m)->conductivity` matches and that
  `resistivity * conductivity ≈ 1` (the inverse property).
- **Edge / validation tests:** `properties(Material::Custom)` returns
  `ErrorCode::CustomMaterialHasNoTableEntry`; an out-of-range cast returns `ErrorCode::UnknownMaterial`;
  `from_name("Aluminium")` (non-canonical spelling) returns `std::nullopt`.

---

## 🔗 Cross-references

- `02-modern-cpp-feature-catalog.md` — general rationale for `constexpr`, `inline` variables,
  `enum class`, `std::numbers`, and `static_assert`.
- `03-quantities-and-units-mp-units.md` — the mp-units quantity types used here (`isq::*`,
  `numerical_value_in`, dimensionless `one`) and how unit conversion works.
- `05-error-handling-and-validation.md` — `emc::Error` / `ErrorCode` and why `std::expected` is the
  return shape for fallible lookups and validation.
- `06-calculator-design-pattern.md` — how skin-depth and resistance calculators become
  Input/Result/`calculate` free functions that consume `emc::constants` and `emc::materials`.
- `07-calculator-inventory.md` — the **Calculator Catalog**: which calculators draw on this constants
  and material single source of truth.
- `09-testing-and-golden-vectors.md` — the **Testing Strategy**: known-value, property, and
  compile-time tests for the constants and material database.
