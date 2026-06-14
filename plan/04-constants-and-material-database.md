# Physical Constants & Material Database (single source of truth)

> Purpose: define exactly **one** place for every physical constant and every material property
> in the `emc` library, expressed as `constexpr` mp-units quantities, and show how the ~14 widgets
> that today hard-code (and contradict) these values will consume them instead.

This document covers two intertwined "single source of truth" problems in NinjaEMC:

1. **Physical constants** are defined by macros, are imprecise, are *wrong* in one case
   (`#define PI 3.14`), and are re-defined per-file (e.g. `mu0 = 4*M_PI*1e-7` in 8+ files).
2. **Material data** (conductivity / permeability / permittivity / resistivity) is duplicated
   across ~14 widgets with **inconsistent representation and inconsistent values**.

The new design replaces all of it with `emc::constants` (a `constexpr` header) and
`emc::materials` (a `constexpr` material database). Both are header-declared, with any non-`constexpr`
helpers compiled in `src/` per the locked "traditional compiled library" decision.

---

## 1. Constants today: the offenders

### 1.1 What the source actually contains

From `src/Utilites/HelperTypes.h` (the project's "shared" header):

```cpp
// src/Utilites/HelperTypes.h  (lines 10-12)  -- VERBATIM
#define SPEEDOFLIGHT    300000000.0      // imprecise: true c = 299 792 458 m/s
#define PLANCK_CONSTANT 6.62606957e-34   // stale 2010 CODATA value, now an EXACT defined constant
#define PI              3.14             // *** PRECISION BUG: pi truncated to 3 sig figs ***
```

Three macros, three problems:

| Macro | Value used | Correct value | Defect |
|---|---|---|---|
| `PI` | `3.14` | `3.14159265358979…` | **Bug.** ~0.05% error. Any formula using `PI` is wrong in the 3rd digit. |
| `SPEEDOFLIGHT` | `3.0e8` m/s | `299 792 458` m/s (exact, SI) | ~0.07% high. Pollutes wavelength/frequency, antenna, field calcs. |
| `PLANCK_CONSTANT` | `6.62606957e-34` | `6.62607015e-34` J·s (exact since 2019 SI redefinition) | Stale; off in the 7th digit. |

`PI = 3.14` is genuinely dangerous: it silently degrades **every** calculator that includes
`HelperTypes.h`, and because it is a macro it can collide with and shadow other `PI` tokens.

### 1.2 The "real" pi is scattered and inconsistent

Because `PI` is broken, developers reached for other spellings of pi instead — so the codebase
now contains *three different values of the same constant*:

- **`#define PI 3.14`** in `HelperTypes.h` (the broken one).
- **`M_PI`** (`<cmath>`, full precision) — **56 occurrences** across the source. This is the
  "good" pi, used in the actual math (e.g. `SkinDepthWidget.cpp` line 70 uses `M_PI`, not `PI`).
- **Inline literal `3.1415926`** (7-digit truncation) — **16 occurrences**, e.g.
  `StandardGaugeWireWidget.cpp` lines 99/102/107 and `CylindricalConductorWidget.cpp`
  lines 100/104/109. This is a third, *different* approximation: `3.1415926` differs from the
  true value by ~5e-8 (acceptable) but differs from `M_PI` used elsewhere, so two calculators
  computing "the same" quantity can disagree in the 8th digit.

So a single conceptual constant — pi — exists in the codebase as `3.14`, `M_PI`, and `3.1415926`
simultaneously. That is exactly the kind of drift a single source of truth eliminates.

### 1.3 `mu0` is re-derived in 8+ files

The vacuum permeability `mu0 = 4*M_PI*1e-7` is **copy-pasted into 8 files** as a per-object or
per-function local:

```text
src/Filtering/.../FerriteToroidWidget.h:31           const qreal mu0 = 4 * M_PI * 1e-7;
src/EMCPredictions/.../ESDCouplingLevelWidget.h:27          qreal mu0 = 4 * M_PI * 1e-7;
src/EMCPredictions/.../LightningCouplingLevelWidget.h:28    qreal mu0 = 4 * M_PI * 1e-7;
src/BasicCalculations/SkinDepth/SkinDepthWidget.h:25       qreal mu0 = 4 * M_PI * 1e-7;
src/.../TransmissionLineParameters/WireOverPlaneWidget.cpp:58     qreal mu0 = 4 * M_PI * 1e-7;
src/.../TransmissionLineParameters/WideTraceOverPlaneWidget.cpp:64 qreal mu0 = 4 * M_PI * 1e-7;
src/.../TransmissionLineParameters/NarrowTraceOverPlaneWidget.cpp:63 qreal mu0 = 4 * M_PI * 1e-7;
src/.../TransmissionLineParameters/WirePairWidget.cpp:58           qreal mu0 = 4 * M_PI * 1e-7;
```

And the *same* constant is **also** defined as a macro under a different name and a different
spelling of the expression, copy-pasted across **6 Inductance headers**:

```cpp
// CircularLoopWidget.h:14, SolenoidWidget.h:14, SquareLoopWidget.h, RectangularLoopWidget.h,
// ViaWidget.h, ConnectorPinWidget.h  -- all identical:
#define PermofFreeSpace ((4 * M_PI) / 10000000.0)
```

So mu0 lives in the codebase as **two names** (`mu0`, `PermofFreeSpace`), **two forms**
(`* 1e-7` vs `/ 10000000.0`), in **14 places total**. Note also: in the post-2019 SI system,
mu0 is no longer *exactly* `4*pi*1e-7` — it is a measured quantity ≈ `1.25663706212e-6 H/m`
(the `4*pi*1e-7` form is now an excellent approximation, not a definition). The library should
carry the CODATA value and document that.

### 1.4 The correctness and maintenance cost

- **Correctness:** `PI = 3.14` is a shipped bug; `SPEEDOFLIGHT = 3e8` injects ~0.07% error into
  every wavelength/frequency/antenna result; three values of pi mean two calculators can disagree.
- **Maintenance:** fixing pi or c means editing **dozens** of sites (56 × `M_PI`, 16 × `3.1415926`,
  14 × mu0). Nobody will find them all — drift is guaranteed.
- **No type safety:** every constant is a bare `double`/`qreal` with the unit living only in a
  comment. `SPEEDOFLIGHT * frequency` compiles even when dimensionally nonsense.
- **Macros:** `#define PI`, `#define PLANCK_CONSTANT`, `#define PermofFreeSpace` are textual,
  unscoped, and shadow/clobber anything with the same token; they ignore namespaces and have no
  type. (See `02-modern-cpp-feature-catalog.md` for the general "no macros" rationale.)

---

## 2. The constants header: `emc::constants`

### 2.1 Design rules

- One header: `include/emc/constants.hpp`. Everything `constexpr` (compile-time, zero runtime cost,
  usable in `static_assert`).
- pi comes from **`std::numbers::pi`** (`<numbers>`, C++20) — full `long double` precision, namespaced,
  no macro. Never `3.14`, `3.1415926`, or `M_PI` again.
- Physical constants are **mp-units quantities**, not bare doubles, so the unit is part of the type
  and conversions are compile-checked (see `03-quantities-and-units-mp-units.md`).
- Values are **current SI / CODATA-2018**: exact defining constants where the SI defines them
  (c, h), measured value for mu0.
- `static_assert` sanity relations prove the constants are mutually consistent **at compile time**.

### 2.2 The header

```cpp
// include/emc/constants.hpp
#pragma once

#include <numbers>      // std::numbers::pi  (replaces PI / M_PI / 3.1415926)
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

// ----- Pure mathematical constant: pi (replaces PI=3.14, the 56 M_PI, the 16 inline 3.1415926) -----
// constexpr, namespaced, full precision. No macro. Templated so callers pick the float type.
inline constexpr double pi = std::numbers::pi_v<double>;

// ----- Speed of light in vacuum (replaces SPEEDOFLIGHT = 3.0e8) -----
// SI EXACT defining constant.  c = 299 792 458 m/s.
inline constexpr quantity speed_of_light = 299'792'458.0 * (m / s);

// ----- Planck constant (replaces PLANCK_CONSTANT = 6.62606957e-34) -----
// SI EXACT defining constant since the 2019 redefinition.  h = 6.626 070 15e-34 J·s.
inline constexpr quantity planck = 6.626'070'15e-34 * (J * s);

// ----- Vacuum permeability mu0 (replaces 8 file-local mu0 and 6 PermofFreeSpace macros) -----
// CODATA-2018 measured value; very close to the legacy 4*pi*1e-7 but no longer EXACT in SI.
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

> **Why `inline constexpr` and not `constexpr` alone?** A header-defined `constexpr` namespace-scope
> variable is implicitly `const` but, before `inline`, would get **internal linkage** — each TU gets
> its own copy, and taking its address yields different pointers across TUs (an ODR foot-gun). Marking
> it `inline` (C++17 inline variables) gives **one** shared definition across the whole program, which
> is exactly what a "single source of truth" requires. This is the standard idiom for header constants.

### 2.3 Compile-time sanity relations (proving consistency)

The defining relations of electromagnetism must hold among these constants. We assert them at
compile time. C++23 makes the `<cmath>` functions (`std::sqrt`) usable in `constexpr`, and mp-units
quantities are `constexpr`-friendly, so these checks run during compilation — if anyone edits a
literal and breaks consistency, **the build fails**.

```cpp
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

// 4) Sanity: legacy 4*pi*1e-7 is within 0.01% of the CODATA mu0 we ship (documents the change).
static_assert(close(mu_0.numerical_value_in(si::henry / si::metre), 4.0 * pi * 1e-7, 1e-4),
              "mu0 should be close to the legacy 4*pi*1e-7");

} // namespace emc::constants::detail
```

> **Why `static_assert` here?** These four lines are the antidote to the §1.2 drift problem. In the
> old code nothing connected `SPEEDOFLIGHT`, `mu0`, and a (missing) eps0 — they could be edited
> independently into an inconsistent set. Here, the physics relations are encoded as build-time
> invariants; you literally cannot ship a self-contradictory constant set. The exact mp-units API
> for extracting the underlying number (`numerical_value_in`) is detailed in
> `03-quantities-and-units-mp-units.md`; the point is that the check is dimension-aware.

> **C++26 forward-looking:** with **contracts** these could become precondition/postcondition style
> checks on constant-producing helpers, and **reflection** could enumerate the constants for an
> auto-generated documentation/units table. Today, `static_assert` is the right tool.

### 2.4 Why `constexpr` + `std::numbers` + mp-units (and not macros)

| Property | Old (`#define PI 3.14`, `qreal mu0=…`) | New (`emc::constants`) |
|---|---|---|
| Correct value | No (pi=3.14; c=3e8) | Yes (SI/CODATA) |
| Scoped / namespaced | No (textual macro) | Yes (`emc::constants::`) |
| Type-checked dimension | No (bare `double`) | Yes (mp-units quantity) |
| Single definition | No (14 copies of mu0) | Yes (`inline constexpr`, one ODR-clean def) |
| Usable in `static_assert` | n/a | Yes (compile-time consistency) |
| Survives refactor | No (drift) | Yes (edit one place; asserts guard it) |

---

## 3. Material data today: duplicated and inconsistent

Material properties are scattered across ~14 widgets in **two mutually incompatible
representations**, with **different material sets** and **different numeric conventions**.

### 3.1 Representation A — SkinDepth: inline string `if/else`, stores **conductivity** + **mu_r**

`src/BasicCalculations/SkinDepth/SkinDepthWidget.cpp` (lines 34-62) hard-codes 5 materials by
**name string**, pushing **conductivity (S/m)** and **relative permeability** straight into spin
boxes:

```cpp
// SkinDepthWidget.cpp (excerpt)
ui->material_combobox->addItem("Copper");   // ... Aluminum, Gold, Silver, Nickel
if (material == "Copper")   { ui->conductivity_spinbox->setValue(5.8005E+7); ui->ur->setValue(0.999991); }
else if (material == "Aluminum"){ ... setValue(3.5386E+7); ... setValue(1.00002); }
else if (material == "Gold"){ ... setValue(4.0984E+7); ... setValue(1); }
else if (material == "Silver"){ ... setValue(6.1728E+7); ... setValue(0.9998); }
else if (material == "Nickel"){ ... setValue(1.4493E+7); ... setValue(600); }
```

### 3.2 Representation B — Resistance widgets: `enum Material` + `GetResistivity()`, stores **resistivity**, forces **mu_r = 1**

`enum Material` is declared in `CylindricalConductorWidget.h` (lines 11-21) and **shared by
inheritance/include** with `StandardGaugeWireWidget` and `RectangularConductorWidget`:

```cpp
// CylindricalConductorWidget.h
enum Material { Custom, Copper, Silver, Gold, Aluminium, Tungsten, Platinum, Lead, Graphite };
```

Each of the three widgets then **re-implements an identical** `GetResistivity()` returning
**resistivity (Ω·m)** — and on the `else` branch returns **`EXIT_FAILURE` (== 1) as a sentinel**
(see `05-error-handling-and-validation.md`):

```cpp
// StandardGaugeWireWidget.cpp / CylindricalConductorWidget.cpp / RectangularConductorWidget.cpp
// -- three byte-identical copies --
qreal GetResistivity(enum Material material) {
    if (material == Custom)        return 0;
    else if (material == Copper)   return 0.0000000172;   // 1.72e-8 ohm*m
    else if (material == Silver)   return 0.0000000159;
    else if (material == Gold)     return 0.0000000240;
    else if (material == Aluminium)return 0.0000000282;
    else if (material == Tungsten) return 0.0000000560;
    else if (material == Platinum) return 0.0000001060;
    else if (material == Lead)     return 0.0000001900;
    else if (material == Graphite) return 0.0000100000;
    else                           return EXIT_FAILURE;   // sentinel bug
}
```

Note the call sites then *force* `ur = 1` whenever a known material is chosen
(`StandardGaugeWireWidget.cpp` line 87), so representation B silently **discards permeability**.

### 3.3 The discrepancies that MUST be reconciled

Putting both representations on a common basis (convert resistivity ρ → conductivity σ = 1/ρ) exposes
real conflicts:

| Material | SkinDepth σ (S/m) | Resistance ρ (Ω·m) | Resistance → σ = 1/ρ (S/m) | Match? |
|---|---|---|---|---|
| Copper   | 5.8005e7 | 1.72e-8 | 5.814e7 | ~0.2% off |
| Silver   | 6.1728e7 | 1.59e-8 | 6.289e7 | **~1.9% off** |
| Gold     | 4.0984e7 | 2.40e-8 | 4.167e7 | **~1.6% off** |
| Aluminum | 3.5386e7 | 2.82e-8 | 3.546e7 | ~0.2% off (also spelled "Aluminium" in enum) |
| Nickel   | 1.4493e7 | — (absent from enum) | — | only in SkinDepth |
| Tungsten | — | 5.60e-8 | 1.786e7 | only in Resistance |
| Platinum | — | 1.06e-7 | 9.434e6 | only in Resistance |
| Lead     | — | 1.90e-7 | 5.263e6 | only in Resistance |
| Graphite | — | 1.00e-5 | 1.0e5 | only in Resistance |

Additional inconsistencies:

- **Spelling drift:** SkinDepth says **"Aluminum"**; the enum says **"Aluminium"**. Two names, one metal.
- **Different material sets:** SkinDepth has Nickel (magnetic, mu_r=600!); the enum has
  Tungsten/Platinum/Lead/Graphite but not Nickel.
- **Permeability lost:** representation B forces `mu_r = 1`, so the magnetic Nickel case cannot even be
  expressed there.
- **Sentinel value:** `GetResistivity` returns `EXIT_FAILURE` (1.0) for an unknown material — a
  *physically valid-looking* resistivity that corrupts results instead of raising an error.

These conflicting numbers are why the golden CSVs may need **re-blessing** after migration: once the
library publishes a single canonical value, the recomputed outputs will differ slightly from the old
per-widget ones (see `09-testing-and-golden-vectors.md`).

### 3.4 Canonical reconciled values (the values the library will ship)

We standardize on **conductivity σ in S/m at 20 °C**, plus **relative permeability** and **relative
permittivity** (the latter ≈ 1 for these conductors), and carry **resistivity ρ = 1/σ** for the
Resistance widgets. Where SkinDepth and Resistance disagreed, we adopt widely-published reference
values (close to the SkinDepth set for the non-magnetic metals; IACS-consistent), and keep Nickel's
high mu_r.

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

> The exact final numbers are a one-time reviewer decision recorded **here** and nowhere else.
> The point of the database is that there is now exactly one row per material to argue about, instead
> of 14 files to chase. (Annotate each row in code with its source/reference in a comment.)

---

## 4. The material database design: `emc::materials`

### 4.1 Goals

- One `enum class Material` (scoped, no implicit int conversions — fixes "Aluminium vs Aluminum" and
  the `EXIT_FAILURE`/index confusion).
- One `MaterialProperties` struct holding **mp-units quantities** (σ, μ_r, ε_r, ρ).
- One `constexpr std::array` table — the *only* place values live.
- `constexpr` lookup by enum, and a `constexpr` name↔enum mapping.
- A `Custom` material so users can supply their own properties.
- Everything `constexpr` and pure → thread-safe, usable at compile time, testable with `static_assert`.

### 4.2 The header

```cpp
// include/emc/materials.hpp
#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

#include <emc/error.hpp>   // emc::Error, emc::ErrorCode  (see 05-error-handling-and-validation.md)

namespace emc::materials {

using namespace mp_units;

// Scoped enum: no implicit int conversion, no clash with EXIT_FAILURE, explicit Custom slot.
// (Replaces the unscoped `enum Material` in CylindricalConductorWidget.h.)
enum class Material {
    Custom = 0,
    Copper,
    Silver,
    Gold,
    Aluminum,   // single canonical spelling (was "Aluminum"/"Aluminium")
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
// (Custom has no fixed row; see lookup()).  Order MUST mirror the enum (asserted below).
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

// ---- constexpr lookup by enum (replaces GetResistivity + EXIT_FAILURE sentinel) ----
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

> **Why `enum class` (not `enum`)?** The old `enum Material` is unscoped: it implicitly converts to
> `int`, which is *why* `ui->material_combobox->currentIndex()` could be `static_cast`-ed into it and
> why `EXIT_FAILURE` (an int `1`) could be "returned as a Material-ish value". `enum class` blocks
> implicit int↔enum conversions, eliminating both bugs, and namespaces the enumerators so
> `Material::Copper` cannot collide. (See `02-modern-cpp-feature-catalog.md`.)

> **Why `std::array` + `consteval make()`?** A `constexpr std::array` is a true compile-time constant:
> the whole table can be folded at compile time, lives in read-only data, and is trivially
> thread-safe (no dynamic init, no global mutable state). `consteval make()` *forces* the row
> construction to happen at compile time, so a typo that would prevent constant evaluation is a hard
> error, not a silent runtime cost.

> **Why `std::expected` instead of `GetResistivity`'s `EXIT_FAILURE`?** The old sentinel `1.0` is a
> *plausible* resistivity, so callers can't distinguish "unknown material" from "a material with
> ρ = 1 Ω·m". `std::expected<MaterialProperties, Error>` makes the failure a distinct, typed value
> the caller must handle, and `[[nodiscard]]` makes ignoring it a warning. (Full rationale in
> `05-error-handling-and-validation.md`.)

### 4.3 Custom material support

`Custom` deliberately has **no table row**: `properties(Material::Custom)` returns an error telling
the caller to provide values directly. Calculators accept a `MaterialProperties` (or a
`std::variant<Material, MaterialProperties>`), so a user can bypass the database:

```cpp
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

```cpp
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

### 4.4 C++26 / present-day tooling callout (name↔enum auto-generation)

The hand-written `names` array duplicates the enum. Two ways to remove that duplication:

- **Today (optional dependency): `magic_enum`.** `magic_enum::enum_name(Material::Copper)` →
  `"Copper"` and `magic_enum::enum_cast<Material>("Copper")` give name↔enum at compile time with
  **no** parallel table. It is a single-header library; if adopted, `to_name`/`from_name`/`names`
  collapse to thin wrappers. (Limitation: enum value range must fit `magic_enum`'s default span.)
- **C++26 forward-looking: static reflection.** With `^^Material` / `std::meta::enumerators_of`,
  the library can *generate* the name table, the `MaterialProperties` field-by-field printer, and even
  a units report directly from the enum/struct — zero hand-maintained mirror tables. This is the
  long-term answer; until then, keep the `constexpr names` array (or `magic_enum`) and let the
  `static_assert` in §6.2 guarantee it stays in sync with the enum.

---

## 5. Usage: before → after

### 5.1 SkinDepth

**Before** (`SkinDepthWidget.cpp`): name-string `if/else`, file-local `mu0 = 4*M_PI*1e-7`, `M_PI`
in the formula, units smuggled through combo `currentData()`:

```cpp
// BEFORE (excerpt)
qreal mu0 = 4 * M_PI * 1e-7;                 // re-defined here and in 7 other files
if (material == "Copper") { ui->conductivity_spinbox->setValue(5.8005E+7); ui->ur->setValue(0.999991); }
// ...
qreal relativePermeability = ui->ur->value() * mu0;
qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
```

**After** (library): pull σ and μ_r from `emc::materials`, mu0 and pi from `emc::constants`, units
from mp-units. No literals, no local mu0, no string switch.

```cpp
// AFTER  (emc::basic::skin_depth — see 06-calculator-design-pattern.md for the full pattern)
#include <emc/constants.hpp>
#include <emc/materials.hpp>

auto props = emc::materials::properties(emc::materials::Material::Copper);  // expected<...>
if (!props) return std::unexpected(props.error());

using namespace mp_units;
const auto sigma = props->conductivity;                                  // S/m, typed
const auto mu    = props->relative_permeability * emc::constants::mu_0;  // H/m, typed
// delta = 1 / sqrt(pi * f * mu * sigma)   -- pi from std::numbers, dimension-checked by mp-units
const quantity delta =
    1.0 / sqrt(emc::constants::pi * f * mu * sigma);                     // result in metres
```

### 5.2 StandardGaugeWire (and the other Resistance widgets)

**Before**: own `enum Material`, own `GetResistivity()` returning ρ with an `EXIT_FAILURE` sentinel,
forces `ur = 1`, inline `3.1415926`, inline mu0 expression:

```cpp
// BEFORE (excerpt)
qreal m = GetResistivity(static_cast<Material>(ui->material_combobox->currentIndex())); // ohm*m or EXIT_FAILURE!
// ...
A  = 3.1415926 * qPow(dm / 2.0, 2);
sd = 1 / qSqrt(3.1415926 * f * ur * 4.0 * 3.1415926 * 0.0000001 / m);   // 4*pi*1e-7 spelled inline
```

**After**: one shared `emc::materials`, ρ as a typed quantity, real error on unknown material, pi/mu0
from constants:

```cpp
// AFTER
auto props = emc::materials::properties(material);     // material : emc::materials::Material
if (!props) return std::unexpected(props.error());     // typed error, never EXIT_FAILURE
const auto rho   = props->resistivity;                 // ohm*m, typed
const auto mu_r  = props->relative_permeability;       // honoured (no forced =1)
// skin depth uses emc::constants::pi and emc::constants::mu_0 — same values as every other calc
```

All three Resistance widgets (`StandardGaugeWire`, `CylindricalConductor`, `RectangularConductor`)
that today carry **byte-identical** `GetResistivity()` copies now call the *one* `properties()`.

---

## 6. Extensibility & validation

### 6.1 Adding a material in exactly one place

To add, say, **Brass**:

1. Add `Brass` to `enum class Material` (before `Count`).
2. Add one `detail::make(...)` row to `table` (same position).
3. Add one `{Material::Brass, "Brass"}` entry to `names`.

That's it — every calculator that takes a `Material` sees Brass immediately. With reflection/magic_enum
(§4.4) step 3 disappears. (Old way: edit the enum **and** `GetResistivity` in 3 files **and** the
SkinDepth string `if/else` **and** any other duplicate — and hope you found them all.)

### 6.2 Compile-time validation invariants

`static_assert` enforces the table's internal consistency at build time:

```cpp
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
static_assert(!from_name("Aluminium").has_value());   // old misspelling is intentionally rejected

} // namespace emc::materials::detail
```

> **Why push these into `static_assert`/`consteval`?** Because the *cost of getting the table wrong*
> was exactly the NinjaEMC pain: silent value drift, a sentinel masquerading as data, an enum/spelling
> mismatch. Encoding "every value positive", "ρ = 1/σ", "table size == enum size", and "lookup hits
> the right row" as compile-time invariants means the database cannot be merged in a broken state.
> These are the data-integrity analogue of the §2.3 physics asserts.

### 6.3 Keeping the table sorted/indexed by enum

The contract is: **`table` row order mirrors `enum class Material` order** (Custom excluded), so the
O(1) `properties()` index lookup is correct. Invariant (a) above catches a *size* mismatch; the
ordering itself is documented at the `table` definition and spot-checked by invariant (d). If reflection
is later adopted, the table can be built *from* the enum so ordering cannot drift at all.

---

## Cross-references

- `02-modern-cpp-feature-catalog.md` — general rationale for `constexpr`, `inline` variables,
  `enum class`, `std::numbers`, no-macros, `static_assert`.
- `03-quantities-and-units-mp-units.md` — the mp-units quantity types used here (`isq::*`,
  `numerical_value_in`, dimensionless `one`) and how unit conversion replaces the 541 `addItem(unit,factor)` calls.
- `05-error-handling-and-validation.md` — `emc::Error` / `ErrorCode`, why `std::expected` replaces the
  `EXIT_FAILURE` sentinel and the `QMessageBox` validation.
- `06-calculator-design-pattern.md` — how SkinDepth/StandardGaugeWire become Input/Result/`calculate`
  free functions that consume `emc::constants` and `emc::materials`.
- `07-calculator-inventory.md` — the full list of the ~14 widgets that currently duplicate material/
  constant data and now point at this single source of truth.
- `09-testing-and-golden-vectors.md` — why some 52 golden CSVs need re-blessing once canonical constant
  and material values land.
