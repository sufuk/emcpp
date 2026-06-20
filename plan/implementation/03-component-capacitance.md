# Implementation Guide — Component: Capacitance ⚡ (`emc::component`, `capacitance.hpp`)

Two capacitance calculators — **Parallel Plate** and **isolated Sphere** — implemented as mp-units-typed,
`std::expected`-returning free functions in the modern `emc` library. Each computes in true SI farads with
`emc::constants::eps0` and lets the caller pick a display unit via mp-units `.in()`.

Both live in the **`emc::component`** namespace, declared in `include/emc/component/capacitance.hpp`,
implemented in `src/component/capacitance.cpp`, and tested in `tests/component/capacitance_test.cpp`. They
share one header/TU because they are the same physical quantity (capacitance) computed from a small
geometry; a future plane/coax capacitor can join them with no new file.

| Calculator | What it computes |
|---|---|
| Parallel Plate | `C = ε₀·εr·A/d`  [F] |
| Sphere | `C = 4π·ε₀·r`  [F] |

The canonical foundation (constants, units, error, materials, test helpers) is defined once in
[`00-foundation-code.md`](00-foundation-code.md); this guide uses those exact names
(`emc::constants::eps0`, `emc::constants::pi`, `emc::units::Capacitance`, `emc::Result`, `emc::in_range`,
`emc::test::approx`, …).

> [!NOTE]
> Both calculators compute in real SI farads and defer all unit conversion to mp-units `.in(unit)`, so a
> capacitance can be displayed in F, nF, µF, or pF with the conversion factor derived by the type system —
> it cannot be wrong.

---

## Parallel Plate

### 1. Overview

Capacitance of an ideal parallel-plate capacitor with plate area `A`, separation `d`, and relative
permittivity `εr`:

```text
C = ε0 · εr · A / d        [F]
```

A `relative_permittivity` field (default `1.0`, i.e. vacuum/air) lets a user model a real dielectric
(FR-4, ceramic, …).

### 2. Public header — `include/emc/component/capacitance.hpp`

```c++
// include/emc/component/capacitance.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::eps0, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Area, ::Length, ::Capacitance

namespace emc::component {

// ===========================================================================
//  Parallel-plate capacitor:  C = eps0 * eps_r * A / d
// ===========================================================================

/// Inputs for emc::component::calculate(const ParallelPlateInput&).
/// mp-units-typed geometry + a plain-double relative permittivity (canonical API:
/// eps_r is a dimensionless double). Defaults make designated-init call sites read
/// cleanly (air, 1 m^2, 1 m).
struct ParallelPlateInput {
    emc::units::Area   area{1.0 * mp_units::si::square(mp_units::si::metre)};  ///< A  (> 0)
    emc::units::Length distance{1.0 * mp_units::si::metre};                    ///< d  (> 0)
    double relative_permittivity{1.0};                                        ///< eps_r (>= 1)
};

/// Result of the parallel-plate calculation.
struct ParallelPlateResult {
    emc::units::Capacitance capacitance{};   ///< C  [F]  (display with .in(si::pico<si::farad>))
};

/// Validate geometry & dielectric: area > 0, distance > 0 (nonzero), eps_r >= 1.
[[nodiscard]] std::expected<void, emc::Error> validate(const ParallelPlateInput& in);

/// C = eps0 * eps_r * A / d, in true SI farads. Returns the validated result or an Error.
[[nodiscard]] emc::Result<ParallelPlateResult> calculate(const ParallelPlateInput& in);

// ---------------------------------------------------------------------------
//  Concept binding: name the (Input, Result, calculate, validate) triple as a type
//  so generic code (golden runner) and a static_assert can check the contract.
// ---------------------------------------------------------------------------
struct ParallelPlate {
    using Input  = ParallelPlateInput;
    using Result = ParallelPlateResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<ParallelPlate>);

} // namespace emc::component
```

### 3. Implementation — `src/component/capacitance.cpp`

```c++
// src/component/capacitance.cpp
#include <emc/component/capacitance.hpp>

#include <mp-units/systems/si.h>

namespace emc::component {

using namespace mp_units;
using mp_units::si::metre;
using mp_units::si::square;

// ---- Parallel plate --------------------------------------------------------

std::expected<void, emc::Error> validate(const ParallelPlateInput& in) {
    // Extract numeric values in the field's natural SI unit, then range-check as
    // raw doubles (the foundation validators report [lo, hi] in that unit).
    const double a = in.area.numerical_value_in(square(metre));
    const double d = in.distance.numerical_value_in(metre);

    // area > 0
    if (auto r = emc::require_positive(a, "area"); !r) return r;
    // distance > 0 AND nonzero — guards the division by d (d == 0 would yield inf).
    if (auto r = emc::require_positive(d, "distance"); !r) return r;
    if (auto r = emc::require_nonzero(d, "distance"); !r) return r;
    // eps_r must be >= 1 (vacuum is the floor). Upper bound is generous.
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1.0e6, "relative_permittivity"); !r)
        return r;

    return {};
}

emc::Result<ParallelPlateResult> calculate(const ParallelPlateInput& in) {
    return validate(in).transform([&] {
        // C = eps0 * eps_r * A / d.
        //  - eps0 is an mp-units quantity [F/m] from emc::constants (CODATA value).
        //  - eps_r is a bare dimensionless double (canonical API).
        //  - A/d carries unit metre, so eps0*(A/d) -> farad: dimension-checked at compile time.
        const emc::units::Capacitance c =
            (emc::constants::eps0 * in.relative_permittivity * (in.area / in.distance))
                .in(si::farad);
        return ParallelPlateResult{ .capacitance = c };
    });
}

} // namespace emc::component
```

> [!TIP]
> `validate()` returns `std::expected<void, Error>`; `transform` runs the success continuation only when
> validation passed and forwards the `Error` untouched otherwise — so the math is unreachable on bad input
> with zero `if (!ok) return …;` boilerplate.

### 4. Modern C++ features used here — and why

- **`emc::constants::eps0` (mp-units `quantity` [F/m])** — EMC capacitances are computed from a typed
  permittivity, not a unitful-but-untyped magic number. `eps0 * A/d` is dimension-checked to be a
  capacitance; you *cannot* forget the `/d` or feed area-in-mils without a compile or conversion error.
- **mp-units `Area` / `Length` inputs + `.in(si::farad)` output** — EMC inputs span m²..mils² and m..mils,
  so mp-units gives compile-time unit safety on input and a type-derived conversion on output. The caller
  picks any display unit via `result.capacitance.in(...)`; the conversion factor is derived by mp-units, so
  an output expressed in F vs nF vs pF is always correct.
- **`std::expected<void, Error>` + `transform`** — calculator inputs have physical domains, so `validate()`
  reports an out-of-domain input (e.g. `d == 0` division by zero, negative area) as a recoverable typed
  `DivisionByZero` / `OutOfRange`, and `transform` keeps the happy path branch-free.
- **Designated-initializer aggregate `ParallelPlateInput`** — `{.area = …, .distance = …}` makes swapping
  area and distance at a call site a named-field mistake you can see, not a silent transposition.
- **`relative_permittivity` field defaulted to `1.0`** — models a real dielectric (FR-4, ceramic, …) while
  defaulting to the vacuum/air case.
- **`[[nodiscard]]` on `calculate`/`validate`** — ignoring the `Result` (and thus the error) is a compile
  warning.

### 5. Example usage

```c++
#include <print>
#include <emc/component/capacitance.hpp>
#include <mp-units/systems/si.h>

int main() {
    using namespace mp_units::si::unit_symbols;          // m2 area, m, pF, ...
    using mp_units::si::square;
    using mp_units::si::metre;

    // A 100 cm^2 plate, 1 mm apart, air dielectric.
    const emc::component::ParallelPlateInput in{
        .area     = 100.0 * square(mp_units::si::centi<metre>),   // 0.01 m^2
        .distance = 1.0   * mp_units::si::milli<metre>,           // 1e-3 m
        // relative_permittivity defaults to 1.0
    };

    if (auto r = emc::component::calculate(in)) {
        // Pull the result out in whatever unit the front end wants.
        std::println("C = {}", r->capacitance.in(mp_units::si::pico<F>));   // ~88.5 pF
    } else {
        const emc::Error& e = r.error();
        std::println("error [{}]: {} (field: {})",
                     emc::to_string(e.code), e.message, e.field);
    }
}
```

### 6. Unit tests — `tests/component/capacitance_test.cpp` (Parallel-Plate cases)

```c++
// tests/component/capacitance_test.cpp   (Parallel-Plate section; Sphere section appended below)
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <emc/component/capacitance.hpp>
#include "support/approx.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using mp_units::si::square;
using mp_units::si::metre;
using emc::ErrorCode;

// ---- (a) Hand-computed known value: C(1 m^2, 1 m, eps_r=1) == eps0 -----------
// A = 1 m^2, d = 1 m, eps_r = 1  =>  C = eps0 = 8.8541878128e-12 F = 8.8541878 pF.
TEST_CASE("ParallelPlate equals eps0 for unit geometry", "[component][capacitance]") {
    const emc::component::ParallelPlateInput in{ .area = 1.0 * square(metre),
                                                 .distance = 1.0 * metre };
    auto r = emc::component::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->capacitance, 8.8541878128e-12 * F, 1e-6));
}

// ---- (b) Textbook reference value: 100 cm^2 plate, 1 mm gap, air -------------
// C = eps0 * A/d = 8.8541878e-12 * 0.01 / 1e-3 = 8.8541878e-11 F = 88.54 pF.
TEST_CASE("ParallelPlate matches textbook 100cm^2/1mm air gap", "[component][capacitance]") {
    const emc::component::ParallelPlateInput in{
        .area     = 100.0 * square(si::centi<metre>),
        .distance = 1.0   * si::milli<metre>,
    };
    auto r = emc::component::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->capacitance.in(si::pico<F>), 88.5419 * si::pico<F>, 1e-4));
}

// ---- (c) Property: C scales linearly with area, inversely with distance ------
TEST_CASE("ParallelPlate scaling laws", "[component][capacitance][property]") {
    const emc::component::ParallelPlateInput base{ .area = 2.0 * square(metre),
                                                   .distance = 3.0 * metre };
    auto c0 = emc::component::calculate(base);
    REQUIRE(c0.has_value());

    // Double the area -> double the capacitance.
    auto c_2a = emc::component::calculate({ .area = 4.0 * square(metre), .distance = 3.0 * metre });
    REQUIRE(c_2a.has_value());
    REQUIRE(emc::test::approx(c_2a->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));

    // Halve the distance -> double the capacitance.
    auto c_halfd = emc::component::calculate({ .area = 2.0 * square(metre), .distance = 1.5 * metre });
    REQUIRE(c_halfd.has_value());
    REQUIRE(emc::test::approx(c_halfd->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));

    // eps_r doubles capacitance.
    auto c_er = emc::component::calculate({ .area = 2.0 * square(metre), .distance = 3.0 * metre,
                                            .relative_permittivity = 2.0 });
    REQUIRE(c_er.has_value());
    REQUIRE(emc::test::approx(c_er->capacitance, (2.0 * c0->capacitance).in(F), 1e-9));
}

// ---- (d) Validation / edge cases --------------------------------------------
TEST_CASE("ParallelPlate rejects bad geometry", "[component][capacitance][validate]") {
    SECTION("distance == 0 -> division-by-zero guard, not inf") {
        auto r = emc::component::calculate({ .area = 1.0 * square(metre), .distance = 0.0 * metre });
        REQUIRE_FALSE(r.has_value());
        // require_positive fires first for d==0 (0 is not > 0) -> OutOfRange.
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "distance");
    }
    SECTION("negative area -> OutOfRange") {
        auto r = emc::component::calculate({ .area = -1.0 * square(metre), .distance = 1.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "area");
    }
    SECTION("eps_r < 1 -> OutOfRange") {
        auto r = emc::component::calculate({ .area = 1.0 * square(metre), .distance = 1.0 * metre,
                                             .relative_permittivity = 0.5 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
}
```

What each guards: **(a)** pins the exact `C(1 m², 1 m) == ε₀` identity by hand; **(b)** locks a textbook
closed-form reference value; **(c)** proves the `A/d`/`εr` proportionalities so an accidental `*`/`/` swap
is caught; **(d)** proves the `inf`/`nan` and out-of-domain cases surface as typed errors with the right
`field`.

---

## Sphere

### 1. Overview

Self-capacitance of an isolated conducting sphere of radius `r` in free space:

```text
C = 4 · π · ε0 · r        [F]
```

### 2. Public header — `include/emc/component/capacitance.hpp` (appended)

```c++
// include/emc/component/capacitance.hpp   (same file, appended inside emc::component)
namespace emc::component {

// ===========================================================================
//  Isolated conducting sphere:  C = 4 * pi * eps0 * r
// ===========================================================================

/// Inputs for emc::component::calculate(const SphereInput&).
struct SphereInput {
    emc::units::Length radius{1.0 * mp_units::si::metre};   ///< r  (> 0)
};

/// Result of the sphere self-capacitance calculation.
struct SphereResult {
    emc::units::Capacitance capacitance{};   ///< C  [F]
};

/// Validate: radius > 0 (nonzero).
[[nodiscard]] std::expected<void, emc::Error> validate(const SphereInput& in);

/// C = 4*pi*eps0*r, in true SI farads.
[[nodiscard]] emc::Result<SphereResult> calculate(const SphereInput& in);

struct Sphere {
    using Input  = SphereInput;
    using Result = SphereResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Sphere>);

} // namespace emc::component
```

> [!NOTE]
> `emc::component::calculate` and `validate` are *overloaded* on the `Input` type (`ParallelPlateInput` vs
> `SphereInput`): the compiler selects by argument type with zero ambiguity, and the `Sphere` /
> `ParallelPlate` tag structs disambiguate for generic code.

### 3. Implementation — `src/component/capacitance.cpp` (appended)

```c++
// src/component/capacitance.cpp   (appended inside emc::component)

// ---- Sphere ----------------------------------------------------------------

std::expected<void, emc::Error> validate(const SphereInput& in) {
    const double r = in.radius.numerical_value_in(metre);
    if (auto v = emc::require_positive(r, "radius"); !v) return v;   // r > 0 (also excludes 0)
    return {};
}

emc::Result<SphereResult> calculate(const SphereInput& in) {
    return validate(in).transform([&] {
        // C = 4 * pi * eps0 * r.
        //  - pi is emc::constants::pi.
        //  - eps0 carries [F/m]; eps0 * r -> farad, dimension-checked.
        const emc::units::Capacitance c =
            (4.0 * emc::constants::pi * emc::constants::eps0 * in.radius).in(si::farad);
        return SphereResult{ .capacitance = c };
    });
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`emc::constants::pi` + `emc::constants::eps0`** — a single source of truth for both `π` and `ε₀`, so
  the Sphere and Parallel-Plate calculators are guaranteed to share the same fundamental constants. Writing
  `4 * pi * eps0` keeps the formula self-documenting.
- **mp-units `Length` input + `.in(si::farad)` / caller-side `.in(pF)`** — EMC radii span m..mils, so the
  typed input gives compile-time unit safety and the `.in(...)` output is converted by the type system.
- **`std::expected` + `transform` for the `r > 0` guard** — a radius has a physical domain, so a
  non-physical `r <= 0` is reported as a recoverable typed `OutOfRange` instead of producing a zero or
  negative capacitance.
- **Designated-initializer `SphereInput{ .radius = … }`** — keeps the call-site grammar identical across
  all `emc` calculators.
- **Overload resolution on `calculate`** — same name, type-selected.

### 5. Example usage

```c++
#include <print>
#include <emc/component/capacitance.hpp>
#include <mp-units/systems/si.h>

int main() {
    using namespace mp_units::si::unit_symbols;   // m, cm, pF
    using mp_units::si::metre;

    // A 10 cm radius sphere.
    const emc::component::SphereInput in{ .radius = 10.0 * mp_units::si::centi<metre> };  // 0.1 m

    if (auto r = emc::component::calculate(in)) {
        std::println("C = {}", r->capacitance.in(mp_units::si::pico<F>));   // ~11.1 pF
    } else {
        std::println("error [{}]: {}", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/component/capacitance_test.cpp` (Sphere cases, appended)

```c++
// tests/component/capacitance_test.cpp   (Sphere section — same file as Parallel-Plate section)

// ---- (a) Hand-computed known value: C(1 m) == 4*pi*eps0 ----------------------
// r = 1 m  =>  C = 4*pi*eps0 = 4*pi*8.8541878128e-12 = 1.112650e-10 F = 111.265 pF.
TEST_CASE("Sphere equals 4*pi*eps0 for unit radius", "[component][capacitance]") {
    auto r = emc::component::calculate({ .radius = 1.0 * metre });
    REQUIRE(r.has_value());
    const double expected_F = 4.0 * std::numbers::pi * 8.8541878128e-12;   // 1.112650e-10
    REQUIRE(emc::test::approx(r->capacitance, expected_F * F, 1e-9));
}

// ---- (b) Textbook reference value: 10 cm radius sphere ----------------------
// r = 0.1 m  =>  C = 4*pi*eps0*0.1 = 1.11265e-11 F = 11.1265 pF.
TEST_CASE("Sphere matches textbook 10cm radius", "[component][capacitance]") {
    auto r = emc::component::calculate({ .radius = 10.0 * si::centi<metre> });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->capacitance.in(si::pico<F>), 11.1265 * si::pico<F>, 1e-4));
}

// ---- (c) Property: linear in radius -----------------------------------------
TEST_CASE("Sphere capacitance is linear in radius", "[component][capacitance][property]") {
    auto c1 = emc::component::calculate({ .radius = 1.0 * metre });
    auto c3 = emc::component::calculate({ .radius = 3.0 * metre });
    REQUIRE(c1.has_value());
    REQUIRE(c3.has_value());
    REQUIRE(emc::test::approx(c3->capacitance, (3.0 * c1->capacitance).in(F), 1e-9));
}

// ---- (d) Validation / edge cases --------------------------------------------
TEST_CASE("Sphere rejects non-physical radius", "[component][capacitance][validate]") {
    SECTION("radius == 0 -> OutOfRange") {
        auto r = emc::component::calculate({ .radius = 0.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "radius");
    }
    SECTION("negative radius -> OutOfRange") {
        auto r = emc::component::calculate({ .radius = -2.0 * metre });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
```

What each guards: **(a)** pins the exact `C(1 m) = 4πε₀` value by hand; **(b)** locks a textbook closed-form
reference value; **(c)** proves linearity so a stray `r²`/`²r` typo is caught; **(d)** turns non-physical
`0`/negative radii into typed `OutOfRange` errors.

> [!IMPORTANT]
> Add `#include <numbers>` at the top of the test file for `std::numbers::pi` used in case (a).

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — `emc::constants::eps0`/`pi`,
  `emc::units::Area`/`Length`/`Capacitance`, `emc::Result`/`Error`/`ErrorCode`, the `in_range` /
  `require_positive` / `require_nonzero` validators, the `Calculator`/`ValidatedCalculator` concepts, and
  the `emc::test::approx` helper.
- [`../07-calculator-inventory.md`](../07-calculator-inventory.md) — inventory rows for Parallel Plate and
  Sphere (placement `emc::component`, header/impl/test paths).
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the testing harness and
  reference-value workflow.
