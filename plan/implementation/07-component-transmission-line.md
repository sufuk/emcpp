# Implementation Guide — Component: Transmission Line ⚡ (`emc::component`, `transmission_line.hpp`)

Seven transmission-line parameter calculators — **Coaxial Line**, **Microstrip Line**,
**Stripline**, **Narrow Trace Over Plane**, **Wide Trace Over Plane**, **Wire Over Plane**, and
**Wire Pair** — implemented as GUI-free, mp-units-typed, `std::expected`-returning free functions.
The four `*OverPlane` / `WirePair` variants share one *skin-depth → L/C/Z₀/R-per-length* algorithm;
this guide factors that into a single `detail::per_length_line(...)` helper so the constant
(`emc::constants::mu0`) and the pattern each live once.

All seven calculators live in the **`emc::component`** namespace, declared in
`include/emc/component/transmission_line.hpp`, implemented in
`src/component/transmission_line.cpp`, and tested in
`tests/component/transmission_line_test.cpp`. They share one header/TU because they are the same
family — characteristic parameters of a uniform line — and the four PCB/wire variants share an
algorithm. The bidirectional board-impedance calculators (Microstrip Trace, Stripline Trace, …) live
in their own `circuit_board_trace.hpp` header and are **not** covered here.

The canonical foundation surface (constants, units, error, materials, test helpers) is defined in
[`00-foundation-code.md`](00-foundation-code.md); this guide uses those exact names
(`emc::constants::mu0`, `emc::constants::eps0`, `emc::constants::pi`, `emc::units::Impedance`,
`emc::units::Frequency`, `emc::Result`, `emc::in_range`, `emc::test::approx`, …).

| Calculator | Inputs | What it computes |
|---|---|---|
| Coaxial Line | `D, d, ε_r` | Z₀, f_cutoff, C/len, L/len |
| Microstrip Line | `ε_r, w, h` | ε_eff, Z₀ (W/H branch) |
| Stripline | `ε_r, w, h, t` | Z₀ |
| Narrow Trace Over Plane | `f, h, w, t, σ, ε_r` | L, C, R, Z₀ per length |
| Wide Trace Over Plane | `f, h, w, t, σ, ε_r` | L, C, R, Z₀ per length |
| Wire Over Plane | `f, h, a, σ, ε_r` | L, C, R, Z₀ per length |
| Wire Pair | `f, s, d, σ, ε_r` | L, C, R, Z₀ per length |

> [!NOTE]
> **Display units in the per-length outputs.** The per-length math is evaluated in **µH/m** for L,
> **pF/m** for C, **mΩ/m** for R, and **Ω** for Z₀ (the `sqrt(1e6·L/C)` factor is the unit
> bookkeeping that converts `µH/m ÷ pF/m` into Ω²). We compute in those display units, then attach
> the unit with mp-units so the *type* records what the bare number means.

---

## The shared per-length pattern (read once; four calculators inherit it)

**Narrow Trace**, **Wide Trace**, **Wire Over Plane**, and **Wire Pair** are four spellings of one
algorithm:

```text
1.  L_pul = k_L * geometry()                 // µH/m  ; k_L, geometry differ per variant
2.  C_pul = (2π ε0 εr / geometry()) * 1e12   // pF/m  ; wire-pair uses π, wide uses w/h, ...
3.  Z0    = sqrt(1e6 * L_pul / C_pul)         // Ω
4.  δ     = 1 / sqrt(π f μ0 σ)                // skin depth, m
5.  Aeff  = (δ small) ? perimeter*δ : full-area      // m²
6.  R_pul = (1000 or 2000) / (σ * Aeff)      // mΩ/m
```

Steps 3–6 are *identical* across the four; the only differences are `Aeff`'s perimeter/area formula
and the wire-pair's `2000` numerator (two conductors). We capture steps 3–6 in one
`detail::per_length_line()` helper that takes the already-computed `L_pul` (µH/m), `C_pul` (pF/m),
the conductor's `σ`, the skin-effect `Aeff`, and the R numerator. Each calculator computes its
*geometry-specific* `L_pul`/`C_pul`/`Aeff` and hands them to the shared core, using
`emc::constants::mu0` / `emc::constants::eps0` throughout.

```c++
// the skin-depth + Z0 + R core, shared by the four per-length calculators
namespace emc::component::detail {

struct PerLength {
    emc::units::Inductance  L;   // per metre (attached as H by caller using a per-metre alias)
    emc::units::Capacitance C;   // per metre
    emc::units::Impedance   Z0;
    emc::units::Impedance   R;   // per metre
};

// f [Hz], sigma [S/m] as raw doubles (already in SI); L_uH_per_m / C_pF_per_m are the bare
// display-unit numbers; aeff_m2 is the skin-effect effective area; r_numer is 1000 (single
// conductor) or 2000 (wire pair). Returns the four per-length results with units attached.
[[nodiscard]] emc::Result<PerLength>
per_length_line(double L_uH_per_m, double C_pF_per_m,
                double sigma, double aeff_m2, double r_numer);

} // namespace emc::component::detail
```

---

## Coaxial Line

### 1. Overview

Characteristic impedance, TE₁₁ cutoff frequency, and per-unit-length capacitance and inductance of a
coaxial line, from the standard **inch-based** empirical EMC formulas:

```text
Z0    = 138 · log10(D/d) / sqrt(ε_r)                       [Ω]
f_c   = 11.8 / ( sqrt(ε_r) · π · (D + d)/2 )               [GHz]   (D, d in inches)
C     = 7.354 · ε_r / log10(D/d)                           [pF/ft]
L     = 140.4 · log10(D/d)                                 [nH/ft]
```

`D` is the dielectric outer diameter, `d` the inner-conductor diameter, both **in inches** for the
constants `11.8` / `7.354` / `140.4` to be correct. Inputs may be supplied in any length unit; the
formula reads them in inches with `numerical_value_in(si::inch)`.

> [!NOTE]
> The `11.8` constant makes the cutoff value come out **in GHz**, so the cutoff-frequency output is
> in GHz. We compute in inches→GHz, then attach SI units.

### 2. Public header

```c++
// include/emc/component/transmission_line.hpp   (Coaxial section)
#pragma once

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

// ----------------------------- Coaxial Line --------------------------------
struct CoaxialLineInput {
    emc::units::Length outer_diameter   = 3.2 * mp_units::si::milli<mp_units::si::metre>;  // D
    emc::units::Length inner_diameter   = 0.9 * mp_units::si::milli<mp_units::si::metre>;  // d
    double             relative_permittivity = 2.3;                                        // ε_r
};

struct CoaxialLineResult {
    emc::units::Impedance            impedance;            // Z0
    emc::units::Frequency            cutoff_frequency;     // f_c (TE11)
    emc::units::CapacitancePerLength capacitance;          // C per length
    emc::units::InductancePerLength  inductance;           // L per length
};

/// Validate D > d > 0 and ε_r >= 1.
[[nodiscard]] std::expected<void, emc::Error> validate(const CoaxialLineInput&);

/// Forward solve: D, d, ε_r -> (Z0, f_c, C/len, L/len).
[[nodiscard]] emc::Result<CoaxialLineResult> calculate(const CoaxialLineInput&);

} // namespace emc::component
```

`emc::units::InductancePerLength` is a thin per-metre inductance alias added to `units.hpp` next to
the existing `CapacitancePerLength`:

```c++
// include/emc/core/units.hpp  (one-line additions, alongside CapacitancePerLength)
using InductancePerLength =
    Q<(isq::inductance / isq::length)[si::henry / si::metre]>;            // H/m
using ResistancePerLength =
    Q<(isq::resistance / isq::length)[si::ohm / si::metre]>;              // Ω/m
```

### 3. Implementation

```c++
// src/component/transmission_line.cpp   (Coaxial section)
#include <emc/component/transmission_line.hpp>

#include <cmath>   // std::log10, std::sqrt — constexpr in C++23

namespace emc::component {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // mm, m, Hz, ohm, F, H, ...

std::expected<void, emc::Error> validate(const CoaxialLineInput& in) {
    const double D = in.outer_diameter.numerical_value_in(mm);
    const double d = in.inner_diameter.numerical_value_in(mm);
    if (auto r = emc::require_positive(d, "inner_diameter"); !r) return r;
    if (auto r = emc::require_positive(D, "outer_diameter"); !r) return r;
    if (D <= d)
        return std::unexpected(emc::invalid_input(
            "outer diameter must exceed inner diameter", "outer_diameter"));
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    return {};
}

emc::Result<CoaxialLineResult> calculate(const CoaxialLineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // The empirical constants assume INCHES. Convert once; 1 inch == 25.4 mm.
    const double D = in.outer_diameter.numerical_value_in(si::inch);
    const double d = in.inner_diameter.numerical_value_in(si::inch);
    const double eps_r = in.relative_permittivity;

    const double ratio_log = std::log10(D / d);              // log10(D/d)
    const double sqrt_eps  = std::sqrt(eps_r);

    const double z0_ohm   = (138.0 * ratio_log) / sqrt_eps;          // Ω
    const double fc_GHz    = 11.8 / (sqrt_eps * emc::constants::pi * ((D + d) / 2.0));  // GHz
    const double c_pF_ft   = (7.354 * eps_r) / ratio_log;           // pF/ft
    const double l_nH_ft   = 140.4 * ratio_log;                     // nH/ft

    return CoaxialLineResult{
        .impedance        = z0_ohm * ohm,
        .cutoff_frequency = fc_GHz * si::giga<Hz>,
        .capacitance      = c_pF_ft * (si::pico<F> / si::international::foot),
        .inductance       = l_nH_ft * (si::nano<H>  / si::international::foot),
    };
}

} // namespace emc::component
```

> [!TIP]
> `si::inch` and `si::international::foot` are the mp-units imperial-length definitions: `1 inch ==
> 25.4 mm` is derived by the library, not hard-coded, so the imperial/SI conversion can never drift.

### 4. Modern C++ features used here — and why

- **mp-units `Length` inputs + `.numerical_value_in(si::inch)`** — EMC geometry spans mils to metres,
  so the input is supplied in any length unit and the formula reads it in inches with one call,
  deriving the exact `25.4` factor instead of carrying a hand-wired conversion table.
- **`emc::constants::pi`** — a single canonical pi in the cutoff term, with no per-TU magic constant.
- **`std::expected` + `validate()`** — calculator inputs have physical domains, so an out-of-domain
  input (`D ≤ d`, which makes `log10(≤1) ≤ 0` non-physical; `ε_r = 0`, division by zero) is reported
  as a recoverable typed error rather than producing a meaningless number.
- **Designated-initializer `CoaxialLineInput`/`CoaxialLineResult`** — named fields self-document each
  geometric quantity and decouple call sites from field order.
- **Per-length unit aliases (`CapacitancePerLength`, `InductancePerLength`)** — the type records that
  C and L are *per foot*, which a bare `double` cannot.

### 5. Example usage

```c++
#include <emc/component/transmission_line.hpp>
#include <print>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

int main() {
    const auto r = emc::component::calculate(emc::component::CoaxialLineInput{
        .outer_diameter        = 3.2 * mm,
        .inner_diameter        = 0.9 * mm,
        .relative_permittivity = 2.3,
    });

    if (!r) {
        std::println("coaxial error: {}", r.error().message);
        return 1;
    }
    std::println("Z0  = {} Ω",     r->impedance.numerical_value_in(ohm));
    std::println("f_c = {} GHz",   r->cutoff_frequency.numerical_value_in(si::giga<Hz>));
    std::println("C   = {} pF/ft", r->capacitance.numerical_value_in(si::pico<F> / si::international::foot));
    std::println("L   = {} nH/ft", r->inductance.numerical_value_in(si::nano<H>  / si::international::foot));
}
```

### 6. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Coaxial section)
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <emc/component/transmission_line.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
namespace tl = emc::component;

// (a) Hand-computed known value: D=2 in, d=1 in, ε_r=1 -> Z0 = 138·log10(2) = 41.541 Ω.
TEST_CASE("coaxial known value", "[component][transmission_line][coaxial]") {
    auto r = tl::calculate({ .outer_diameter = 2.0 * si::inch,
                             .inner_diameter = 1.0 * si::inch,
                             .relative_permittivity = 1.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->impedance, 41.541 * ohm, 1e-3));
}

// (b) Textbook reference: D=3.2 in, d=0.9 in, ε_r=2.3 (independent closed-form recompute).
TEST_CASE("coaxial textbook reference", "[component][transmission_line][coaxial]") {
    const double D = 3.2, d = 0.9, e = 2.3;
    auto r = tl::calculate({ .outer_diameter = D * si::inch,
                             .inner_diameter = d * si::inch,
                             .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double lg = std::log10(D / d), se = std::sqrt(e);
    CHECK(emc::test::approx(r->impedance, (138.0 * lg / se) * ohm, 1e-4));
    CHECK(emc::test::approx(r->cutoff_frequency,
          (11.8 / (se * emc::constants::pi * (D + d) / 2.0)) * si::giga<Hz>, 1e-4));
}

// (c) Monotonicity: widening D (fixed d, ε_r) raises Z0.
TEST_CASE("coaxial Z0 grows with D/d", "[component][transmission_line][coaxial][property]") {
    auto a = tl::calculate({ .outer_diameter = 3.0 * mm, .inner_diameter = 1.0 * mm,
                             .relative_permittivity = 2.0 });
    auto b = tl::calculate({ .outer_diameter = 6.0 * mm, .inner_diameter = 1.0 * mm,
                             .relative_permittivity = 2.0 });
    REQUIRE(a.has_value()); REQUIRE(b.has_value());
    REQUIRE(b->impedance.numerical_value_in(ohm) > a->impedance.numerical_value_in(ohm));
}

// (d) Validation: D <= d -> InvalidInput; ε_r < 1 -> OutOfRange.
TEST_CASE("coaxial rejects bad geometry", "[component][transmission_line][coaxial][validation]") {
    auto r1 = tl::calculate({ .outer_diameter = 1.0 * mm, .inner_diameter = 2.0 * mm,
                              .relative_permittivity = 2.0 });
    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r1.error().code == emc::ErrorCode::InvalidInput);

    auto r2 = tl::calculate({ .outer_diameter = 3.0 * mm, .inner_diameter = 1.0 * mm,
                              .relative_permittivity = 0.5 });
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error().code == emc::ErrorCode::OutOfRange);
}
```

What each guards: (a) a closed-form anchor (`138·log10 2`); (b) a textbook reference point recomputed
independently; (c) the physical monotonicity of `Z₀ ∝ ln(D/d)`; (d) the domain validation.

---

## Microstrip Line

### 1. Overview

Effective dielectric constant and characteristic impedance of a microstrip line, with the classic
Wheeler/Hammerstad **W/H branch**:

```text
ratio = W/H
if W/H < 1:
   ε_eff = (ε_r+1)/2 + (ε_r-1)/2 · [ 1/sqrt(1+12·H/W) + 0.04·(1−W/H)² ]
   Z0    = (60/sqrt(ε_eff)) · ln( 8·H/W + 0.25·W/H )
if W/H > 1:
   ε_eff = (ε_r+1)/2 + (ε_r-1)/( 2·sqrt(1+12·H/W) )
   Z0    = 120π / ( sqrt(ε_eff)·[ W/H + 1.393 + (2/3)·ln(W/H + 1.444) ] )
```

The formulas are **ratio-based**, so W and H may be in any (shared) length unit — the ratio cancels
it.

> [!WARNING]
> The Hammerstad branches cover `W/H < 1` and `W/H > 1` but not exactly `W == H`; there both helpers
> are undefined. We make `W == H` an explicit `Unsupported` typed error rather than returning a
> meaningless value.

### 2. Public header

```c++
// include/emc/component/transmission_line.hpp   (Microstrip section)
namespace emc::component {

struct MicrostripLineInput {
    double             relative_permittivity = 13.0;                                        // ε_r
    emc::units::Length width  = 122.0 * mp_units::si::milli<mp_units::si::metre>;           // W
    emc::units::Length height =   3.0 * mp_units::si::milli<mp_units::si::metre>;           // H
};

struct MicrostripLineResult {
    double                effective_permittivity;   // ε_eff (dimensionless)
    emc::units::Impedance impedance;                // Z0
};

[[nodiscard]] std::expected<void, emc::Error> validate(const MicrostripLineInput&);
[[nodiscard]] emc::Result<MicrostripLineResult> calculate(const MicrostripLineInput&);

} // namespace emc::component
```

### 3. Implementation

```c++
// src/component/transmission_line.cpp   (Microstrip section)
namespace emc::component {

namespace {
// Hammerstad ε_eff, two-branch.
[[nodiscard]] double micro_eps_eff(double eps_r, double ratio /*W/H*/) {
    const double inv = 1.0 / ratio;   // H/W
    if (ratio < 1.0)
        return (eps_r + 1.0) / 2.0
             + (eps_r - 1.0) / 2.0
               * (1.0 / std::sqrt(1.0 + 12.0 * inv) + 0.04 * std::pow(1.0 - ratio, 2));
    // ratio > 1.0
    return (eps_r + 1.0) / 2.0
         + (eps_r - 1.0) / (2.0 * std::sqrt(1.0 + 12.0 * inv));
}
} // namespace

std::expected<void, emc::Error> validate(const MicrostripLineInput& in) {
    const double w = in.width.numerical_value_in(mm);
    const double h = in.height.numerical_value_in(mm);
    if (auto r = emc::require_positive(w, "width");  !r) return r;
    if (auto r = emc::require_positive(h, "height"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (w == h)   // boundary between the two branches: explicitly unsupported
        return std::unexpected(emc::Error{
            .code = emc::ErrorCode::Unsupported,
            .message = "W == H is not covered by the Hammerstad branches",
            .field = "width" });
    return {};
}

emc::Result<MicrostripLineResult> calculate(const MicrostripLineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Ratio-based: read W and H in a SHARED unit so the ratio is unit-free.
    const double w = in.width.numerical_value_in(mm);
    const double h = in.height.numerical_value_in(mm);
    const double eps_r = in.relative_permittivity;
    const double ratio = w / h;          // W/H
    const double inv   = h / w;          // H/W

    const double eps_eff = micro_eps_eff(eps_r, ratio);

    double z0;
    if (ratio < 1.0)
        z0 = (60.0 / std::sqrt(eps_eff)) * std::log(8.0 * inv + 0.25 * ratio);
    else // ratio > 1.0
        z0 = (120.0 * emc::constants::pi)
           / (std::sqrt(eps_eff) * (ratio + 1.393 + (2.0 / 3.0) * std::log(ratio + 1.444)));

    return MicrostripLineResult{
        .effective_permittivity = eps_eff,
        .impedance              = z0 * ohm,
    };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`std::expected` + an explicit `Unsupported` error for `W == H`** — the Hammerstad model has a
  genuine gap at the branch boundary, and a typed error is the honest way to report an input the
  formula does not cover.
- **mp-units `Length` inputs read in a shared unit** — the ratio `W/H` is unit-free by construction,
  so a `W` in mils and an `H` in mm can never silently mismatch.
- **`emc::constants::pi`** — the `120·π` numerator uses the canonical pi.
- **`double effective_permittivity`** — ε_eff is a *dimensionless ratio*, so per the canonical API it
  is a plain `double`, consistent with `mu_r`/`eps_r`.
- **Designated initializers** — `{ .relative_permittivity = …, .width = …, .height = … }`
  self-documents each value.

### 5. Example usage

```c++
auto r = emc::component::calculate(emc::component::MicrostripLineInput{
    .relative_permittivity = 4.4,        // FR-4
    .width  = 3.0 * mm,
    .height = 1.6 * mm,
});
if (r) {
    std::println("ε_eff = {}",  r->effective_permittivity);
    std::println("Z0    = {} Ω", r->impedance.numerical_value_in(ohm));
} else {
    std::println("error: {}", r.error().message);
}
```

### 6. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Microstrip section)

// (a) Hand-computed value: ε_r=10, W=9, H=1 (ratio=9>1) -> closed-form ε_eff, Z0.
TEST_CASE("microstrip known value (W/H>1)", "[component][transmission_line][microstrip]") {
    auto r = tl::calculate({ .relative_permittivity = 10.0, .width = 9.0 * mm, .height = 1.0 * mm });
    REQUIRE(r.has_value());
    const double eps_eff = (10.0+1)/2 + (10.0-1)/(2*std::sqrt(1+12*(1.0/9.0)));
    REQUIRE(r->effective_permittivity == Catch::Approx(eps_eff).epsilon(1e-9));
    const double z0 = (120*emc::constants::pi)
                    / (std::sqrt(eps_eff)*(9.0 + 1.393 + (2.0/3.0)*std::log(9.0+1.444)));
    REQUIRE(emc::test::approx(r->impedance, z0 * ohm, 1e-9));
}

// (b) Hand-computed value on the W/H<1 branch: ε_r=4, W=1, H=2.
TEST_CASE("microstrip known value (W/H<1)", "[component][transmission_line][microstrip]") {
    const double e = 4.0, w = 1.0, h = 2.0, ratio = w/h, inv = h/w;
    auto r = tl::calculate({ .relative_permittivity = e, .width = w * mm, .height = h * mm });
    REQUIRE(r.has_value());
    const double eps_eff = (e+1)/2 + (e-1)/2*(1/std::sqrt(1+12*inv) + 0.04*std::pow(1-ratio,2));
    CHECK(r->effective_permittivity == Catch::Approx(eps_eff).epsilon(1e-9));
}

// (c) Property: ε_eff lies between 1 and ε_r (physical bound).
TEST_CASE("microstrip eps_eff bounded by eps_r", "[component][transmission_line][microstrip][property]") {
    auto r = tl::calculate({ .relative_permittivity = 4.4, .width = 3.0 * mm, .height = 1.6 * mm });
    REQUIRE(r.has_value());
    REQUIRE(r->effective_permittivity > 1.0);
    REQUIRE(r->effective_permittivity < 4.4);
}

// (d) Validation: W==H -> Unsupported.
TEST_CASE("microstrip W==H is Unsupported", "[component][transmission_line][microstrip][validation]") {
    auto r = tl::calculate({ .relative_permittivity = 4.0, .width = 2.0 * mm, .height = 2.0 * mm });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::Unsupported);
}
```

What each guards: (a) an independent W/H>1 closed form; (b) the W/H<1 branch; (c) the physical
`1 < ε_eff < ε_r` invariant; (d) the `W==H` typed error.

---

## Stripline

### 1. Overview

Characteristic impedance of a symmetric stripline:

```text
Z0 = (60 / sqrt(ε_r)) · ln( 1.9·(2h + t) / (0.8·w + t) )      [Ω]
```

with `w` = trace width, `h` = dielectric height (substrate half-height), `t` = trace thickness, all
in a shared length unit (the `ln` argument is a length ratio, so units cancel).

### 2. Public header

```c++
// include/emc/component/transmission_line.hpp   (Stripline section)
namespace emc::component {

struct StriplineInput {
    double             relative_permittivity = 10.0;                                    // ε_r
    emc::units::Length width     = 10.0 * mp_units::si::metre;                          // w
    emc::units::Length height    = 10.0 * mp_units::si::metre;                          // h
    emc::units::Length thickness =  5.0 * mp_units::si::metre;                          // t
};

struct StriplineResult {
    emc::units::Impedance impedance;     // Z0
};

[[nodiscard]] std::expected<void, emc::Error> validate(const StriplineInput&);
[[nodiscard]] emc::Result<StriplineResult> calculate(const StriplineInput&);

} // namespace emc::component
```

### 3. Implementation

```c++
// src/component/transmission_line.cpp   (Stripline section)
namespace emc::component {

std::expected<void, emc::Error> validate(const StriplineInput& in) {
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(w, "width");     !r) return r;
    if (auto r = emc::require_positive(h, "height");    !r) return r;
    if (auto r = emc::require_positive(t, "thickness"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    // ln argument must be positive; denominator (0.8w + t) is > 0 given positivity above.
    const double arg = (1.9 * (2.0 * h + t)) / (0.8 * w + t);
    if (!(arg > 0.0))
        return std::unexpected(emc::domain_error("stripline ln argument <= 0", "width"));
    return {};
}

emc::Result<StriplineResult> calculate(const StriplineInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Ratio argument: read all three lengths in the SAME unit (metre).
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);

    const double z0 = (60.0 / std::sqrt(in.relative_permittivity))
                    * std::log((1.9 * (2.0 * h + t)) / (0.8 * w + t));

    return StriplineResult{ .impedance = z0 * ohm };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`std::expected` + `domain_error` guard on the `ln` argument** — for thin substrates / wide traces
  the argument can drop to ≤ 0, where `ln` is undefined; the domain is checked up front and reported
  as a typed `DomainError`.
- **mp-units `Length` inputs in a shared unit** — the `ln` argument is dimensionless, so reading all
  three in metres is exact and unit-mismatch-proof.
- **Designated initializers** matching the input order (`ε_r, w, h, t`).

### 5. Example usage

```c++
auto r = emc::component::calculate(emc::component::StriplineInput{
    .relative_permittivity = 4.2,
    .width     = 0.2 * mm,
    .height    = 0.4 * mm,
    .thickness = 0.035 * mm,
});
if (r) std::println("Z0 = {} Ω", r->impedance.numerical_value_in(ohm));
else   std::println("error: {}", r.error().message);
```

### 6. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Stripline section)
#include <mp-units/systems/si.h>

// (a) Hand-computed value: ε_r=4, h=t=w=1 -> Z0 = (60/2)·ln(1.9·3/1.8) = 30·ln(3.1666…).
TEST_CASE("stripline known value", "[component][transmission_line][stripline]") {
    auto r = tl::calculate({ .relative_permittivity = 4.0,
                             .width = 1.0 * m, .height = 1.0 * m, .thickness = 1.0 * m });
    REQUIRE(r.has_value());
    const double z0 = 30.0 * std::log(1.9 * 3.0 / 1.8);
    REQUIRE(emc::test::approx(r->impedance, z0 * ohm, 1e-9));
}

// (b) Textbook geometry: w=0.2, h=0.4, t=0.035 mm, ε_r=4.2 (independent recompute).
TEST_CASE("stripline textbook reference", "[component][transmission_line][stripline]") {
    const double w = 0.2e-3, h = 0.4e-3, t = 0.035e-3, e = 4.2;
    auto r = tl::calculate({ .relative_permittivity = e,
                             .width = 0.2*mm, .height = 0.4*mm, .thickness = 0.035*mm });
    REQUIRE(r.has_value());
    const double z0 = (60.0/std::sqrt(e)) * std::log((1.9*(2*h+t))/(0.8*w+t));
    CHECK(emc::test::approx(r->impedance, z0 * ohm, 1e-6));
}

// (c) Property: higher ε_r -> lower Z0 (1/sqrt(ε_r) scaling).
TEST_CASE("stripline Z0 falls with eps_r", "[component][transmission_line][stripline][property]") {
    auto lo = tl::calculate({ .relative_permittivity = 2.0,
                              .width = 0.2*mm, .height = 0.4*mm, .thickness = 0.035*mm });
    auto hi = tl::calculate({ .relative_permittivity = 8.0,
                              .width = 0.2*mm, .height = 0.4*mm, .thickness = 0.035*mm });
    REQUIRE(lo.has_value()); REQUIRE(hi.has_value());
    REQUIRE(hi->impedance.numerical_value_in(ohm) < lo->impedance.numerical_value_in(ohm));
}

// (d) Validation: w=0 -> require_positive (OutOfRange).
TEST_CASE("stripline rejects bad input", "[component][transmission_line][stripline][validation]") {
    auto r = tl::calculate({ .relative_permittivity = 4.0,
                             .width = 0.0 * m, .height = 1.0 * m, .thickness = 1.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}
```

What each guards: (a) an independent hand value; (b) a textbook geometry recomputed independently;
(c) the `1/√ε_r` scaling; (d) positivity validation.

---

## Per-length calculators — shared `detail::per_length_line`

The four remaining calculators (Narrow Trace, Wide Trace, Wire Over Plane, Wire Pair) all end with
the same skin-depth → Z₀ → R block. Here is the shared core, using `emc::constants::mu0` /
`emc::constants::eps0`.

### Shared header surface

```c++
// include/emc/component/transmission_line.hpp   (per-length common)
namespace emc::component {

// Common per-length result for the four PCB/wire variants.
struct LinePerLengthResult {
    emc::units::InductancePerLength  inductance;            // L  (µH/m attached as H/m)
    emc::units::CapacitancePerLength capacitance;           // C  (pF/m attached as F/m)
    emc::units::ResistancePerLength  resistance;            // R  (mΩ/m attached as Ω/m)
    emc::units::Impedance            characteristic_impedance;  // Z0
};

// --- Narrow Trace Over Plane ------------------------------------------------
struct NarrowTraceInput {
    emc::units::Frequency frequency      = 1.0 * mp_units::si::mega<mp_units::si::hertz>;
    emc::units::Length    trace_height   = 10.0  * mp_units::si::milli<mp_units::si::metre>;  // h
    emc::units::Length    trace_width    =  1.0  * mp_units::si::milli<mp_units::si::metre>;  // w
    emc::units::Length    trace_thickness= 0.035 * mp_units::si::milli<mp_units::si::metre>;  // t
    emc::materials::Material conductor   = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};                 // used iff conductor == Custom
    double                relative_permittivity = 1.0;       // ε_r (Air)
};

// --- Wide Trace Over Plane --------------------------------------------------
struct WideTraceInput {
    emc::units::Frequency frequency      = 1.0 * mp_units::si::mega<mp_units::si::hertz>;
    emc::units::Length    trace_height   =  1.0  * mp_units::si::milli<mp_units::si::metre>;  // h
    emc::units::Length    trace_width    = 10.0  * mp_units::si::milli<mp_units::si::metre>;  // w
    emc::units::Length    trace_thickness= 0.035 * mp_units::si::milli<mp_units::si::metre>;  // t
    emc::materials::Material conductor   = emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};
    double                relative_permittivity = 1.0;
};

// --- Wire Over Plane --------------------------------------------------------
struct WireOverPlaneInput {
    emc::units::Frequency frequency   = 5.0 * mp_units::si::hertz;
    emc::units::Length    wire_height = 22.0 * mp_units::si::centi<mp_units::si::metre>;  // h
    emc::units::Length    wire_radius = 20.0 * mp_units::si::centi<mp_units::si::metre>;  // a
    emc::materials::Material conductor= emc::materials::Material::Copper;
    emc::units::Conductivity custom_sigma{};
    double                relative_permittivity = 1.0;
};

// --- Wire Pair --------------------------------------------------------------
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

} // namespace emc::component
```

### Shared core implementation

```c++
// src/component/transmission_line.cpp   (per-length common core)
namespace emc::component::detail {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Resolve the conductivity: a table material's σ, or the custom σ for Material::Custom.
[[nodiscard]] emc::Result<double> resolve_sigma(emc::materials::Material mat,
                                                emc::units::Conductivity custom) {
    if (mat == emc::materials::Material::Custom) {
        const double s = custom.numerical_value_in(si::siemens / si::metre);
        if (!(s > 0.0))
            return std::unexpected(emc::require_positive(s, "custom_sigma").error());
        return s;
    }
    auto p = emc::materials::properties(mat);
    if (!p) return std::unexpected(p.error());
    return p->conductivity.numerical_value_in(si::siemens / si::metre);
}

// Steps 3-6 of the shared pattern. Inputs are bare display-unit numbers:
//   L_uH_per_m : inductance per length  [µH/m]
//   C_pF_per_m : capacitance per length [pF/m]
//   sigma      : conductivity           [S/m]
//   aeff_m2    : skin-effect effective conductor cross-section [m²]
//   r_numer    : 1000 (single conductor) or 2000 (wire pair, two conductors)
[[nodiscard]] emc::Result<LinePerLengthResult>
per_length_line(double L_uH_per_m, double C_pF_per_m,
                double sigma, double aeff_m2, double r_numer) {
    if (C_pF_per_m == 0.0)
        return std::unexpected(emc::division_by_zero("capacitance"));
    if (aeff_m2 == 0.0 || sigma == 0.0)
        return std::unexpected(emc::division_by_zero("resistance"));

    // Z0 = sqrt(1e6 * L[µH/m] / C[pF/m])  — the 1e6 is the µH/pF unit bookkeeping -> Ω².
    const double z0 = std::sqrt(1.0e6 * L_uH_per_m / C_pF_per_m);
    const double r_milliohm_per_m = r_numer / (sigma * aeff_m2);   // mΩ/m

    return LinePerLengthResult{
        .inductance               = L_uH_per_m * (si::micro<H> / m),
        .capacitance              = C_pF_per_m * (si::pico<F>  / m),
        .resistance               = r_milliohm_per_m * (si::milli<ohm> / m),
        .characteristic_impedance = z0 * ohm,
    };
}

// Skin depth δ = 1 / sqrt(π f μ0 σ), with the canonical μ0.
[[nodiscard]] double skin_depth(double f_hz, double sigma) {
    const double mu0 = emc::constants::mu0.numerical_value_in(si::henry / si::metre);
    return 1.0 / std::sqrt(emc::constants::pi * f_hz * mu0 * sigma);
}

} // namespace emc::component::detail
```

> [!IMPORTANT]
> `Z0 = sqrt(1e6*L/C)` deliberately mixes µH/m and pF/m; the `1e6` makes the result come out in Ω.
> We keep that exact arithmetic and *then* attach the physically-correct unit, so the value and its
> type agree.

---

## Narrow Trace Over Plane

### 1. Overview

Per-length L, C, R, Z₀ of a narrow PCB trace over a ground plane (`h > w`):

```text
L = 0.2 · acosh(4h/w)                         [µH/m]
C = (2π·ε0·ε_r / acosh(4h/w)) · 1e12          [pF/m]
δ = 1 / sqrt(π·f·μ0·σ)
Aeff = (δ ≤ wt/(2(w+t))) ? 2(w+t)·δ : w·t
R = 1000 / (σ·Aeff)                           [mΩ/m]
Z0 = sqrt(1e6·L/C)                            [Ω]
```

Inputs: frequency `f`, trace height `h`, width `w`, thickness `t`, conductor material / σ, and `ε_r`.
The geometry requires `h > w`.

### 2. Implementation

```c++
// src/component/transmission_line.cpp   (Narrow Trace section)
namespace emc::component {

std::expected<void, emc::Error> validate(const NarrowTraceInput& in) {
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(w, "trace_width");     !r) return r;
    if (auto r = emc::require_positive(t, "trace_thickness"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (h <= w)
        return std::unexpected(emc::invalid_input(
            "trace height must exceed trace width", "trace_height"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const NarrowTraceInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f = in.frequency.numerical_value_in(Hz);     // SI Hz
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom = std::acosh(4.0 * h / w);              // acosh(4h/w)
    const double L_uH_per_m = 0.2 * geom;                     // µH/m
    const double C_pF_per_m = (2.0 * emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= (w * t) / (2.0 * (w + t)))
                       ? 2.0 * (w + t) * delta
                       : w * t;

    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

} // namespace emc::component
```

### 3. Modern C++ features used here — and why

- **`emc::constants::mu0` / `eps0` / `pi`** — the EMC field constants come from a single canonical
  source, so every calculator that touches a skin-depth or capacitance term shares identical values
  to full precision.
- **`emc::materials::properties()` + `resolve_sigma`** — σ comes from the canonical material table
  (Copper = 5.96e7 S/m), or an explicit `custom_sigma` for `Material::Custom`, returning
  `UnknownMaterial` cleanly otherwise.
- **`std::expected` + `invalid_input`** — the `h ≤ w` precondition is a domain constraint, so
  violating it yields a typed `InvalidInput` the caller can recover from.
- **Shared `detail::per_length_line`** — steps 3–6 are written once; this calculator only contributes
  its geometry (`L`, `C`, `Aeff`).
- **mp-units `Frequency`/`Length`/`Conductivity` inputs** — `numerical_value_in(Hz)` /
  `numerical_value_in(m)` derive the SI value regardless of the unit the caller supplies.

### 4. Example usage

```c++
auto r = emc::component::calculate(emc::component::NarrowTraceInput{
    .frequency = 22.0 * MHz,
    .trace_height = 11.0 * mm, .trace_width = 5.0 * mm, .trace_thickness = 2.0 * mm,
    .conductor = emc::materials::Material::Copper,
    .relative_permittivity = 1.0,
});
if (r) {
    std::println("L  = {} µH/m", r->inductance.numerical_value_in(si::micro<H> / m));
    std::println("C  = {} pF/m", r->capacitance.numerical_value_in(si::pico<F> / m));
    std::println("R  = {} mΩ/m", r->resistance.numerical_value_in(si::milli<ohm> / m));
    std::println("Z0 = {} Ω",    r->characteristic_impedance.numerical_value_in(ohm));
}
```

### 5. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Narrow Trace section)

// (a) Hand-computed L: h=10mm, w=1mm -> L = 0.2·acosh(40) ≈ 0.2·4.3819 = 0.87638 µH/m.
TEST_CASE("narrow trace known L", "[component][transmission_line][narrow]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .trace_height = 10.0 * mm, .trace_width = 1.0 * mm,
                             .trace_thickness = 0.035 * mm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.2*std::acosh(40.0)) * (si::micro<H>/m), 1e-9));
}

// (b) Z0 from hand-computed L and C with explicit σ (Custom), independent recompute.
TEST_CASE("narrow trace Z0 reference", "[component][transmission_line][narrow]") {
    const double h = 11e-3, w = 5e-3, e = 1.0;
    auto r = tl::calculate({ .frequency = 22.0 * MHz,
                             .trace_height = 11.0 * mm, .trace_width = 5.0 * mm,
                             .trace_thickness = 2.0 * mm,
                             .conductor = emc::materials::Material::Custom,
                             .custom_sigma = 5.96e7 * (S / m),
                             .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double geom = std::acosh(4*h/w);
    const double L = 0.2 * geom;
    const double eps0 = emc::constants::eps0.numerical_value_in(F/m);
    const double Z0 = std::sqrt(1e6 * L / ((2*emc::constants::pi*eps0*e/geom)*1e12));
    CHECK(emc::test::approx(r->characteristic_impedance, Z0 * ohm, 1e-4));
}

// (c) Validation: h <= w -> InvalidInput.
TEST_CASE("narrow trace requires h>w", "[component][transmission_line][narrow][validation]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .trace_height = 1.0 * mm, .trace_width = 5.0 * mm,
                             .trace_thickness = 0.035 * mm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}
```

What each guards: (a) an independent `acosh` hand value for L; (b) Z₀ from hand-computed L/C with an
explicit σ; (c) the `h > w` precondition.

---

## Wide Trace Over Plane

### 1. Overview

Per-length L, C, R, Z₀ of a **wide** PCB trace (`w > 5h`) over a plane:

```text
L = 0.4·π·(h/w)                               [µH/m]   (i.e. μ0·μr·h/w with μr=1, in µH/m)
C = (ε0·ε_r·(w/h)) · 1e12                     [pF/m]
δ = 1 / sqrt(π·f·μ0·σ)
Aeff = (δ ≤ wt/(w+t)) ? (w+t)·δ : w·t
R = 1000 / (σ·Aeff)                           [mΩ/m]
Z0 = sqrt(1e6·L/C)                            [Ω]
```

The geometry requires `w > 5·h`. Inputs are the same six as Narrow Trace.

### 2. Implementation

```c++
// src/component/transmission_line.cpp   (Wide Trace section)
namespace emc::component {

std::expected<void, emc::Error> validate(const WideTraceInput& in) {
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(h, "trace_height");    !r) return r;
    if (auto r = emc::require_positive(t, "trace_thickness"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (w <= 5.0 * h)
        return std::unexpected(emc::invalid_input(
            "trace width must exceed 5x trace height", "trace_width"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WideTraceInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f = in.frequency.numerical_value_in(Hz);
    const double h = in.trace_height.numerical_value_in(m);
    const double w = in.trace_width.numerical_value_in(m);
    const double t = in.trace_thickness.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double L_uH_per_m = 0.4 * emc::constants::pi * h / w;            // µH/m
    const double C_pF_per_m = (eps0 * in.relative_permittivity * w / h) * 1e12;  // pF/m

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= (w * t) / (w + t)) ? (w + t) * delta : w * t;

    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

} // namespace emc::component
```

### 3. Modern C++ features used here — and why

- **`0.4·π` via `emc::constants::pi`** — the `0.4·π` factor is `μ0·μr·h/w` pre-scaled to µH/m for
  μr = 1; using the canonical pi makes the hidden `μ0` explicit in a comment instead of a magic
  number.
- **`emc::constants::eps0`** — the capacitance term shares the single canonical ε₀.
- **`std::expected` + `invalid_input` for `w ≤ 5h`** — the wide-trace approximation is only valid for
  `w > 5h`, so an out-of-domain geometry is a typed `InvalidInput`.
- **Shared `detail::per_length_line`** — identical Z₀/R bookkeeping, written once.

### 4. Example usage

```c++
auto r = emc::component::calculate(emc::component::WideTraceInput{
    .frequency = 100.0 * MHz,
    .trace_height = 0.2 * mm, .trace_width = 5.0 * mm, .trace_thickness = 0.035 * mm,
    .conductor = emc::materials::Material::Copper, .relative_permittivity = 4.4,
});
if (r) std::println("Z0 = {} Ω", r->characteristic_impedance.numerical_value_in(ohm));
else   std::println("error: {}", r.error().message);
```

### 5. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Wide Trace section)

// (a) Hand-computed L: h=1mm, w=10mm -> L = 0.4π·0.1 = 0.1256637 µH/m.
TEST_CASE("wide trace known L", "[component][transmission_line][wide]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .trace_height = 1.0 * mm, .trace_width = 10.0 * mm,
                             .trace_thickness = 0.035 * mm,
                             .conductor = emc::materials::Material::Copper,
                             .relative_permittivity = 1.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance,
            (0.4*emc::constants::pi*0.1) * (si::micro<H>/m), 1e-9));
}

// (b) C and Z0 from hand-computed values with explicit σ, independent recompute.
TEST_CASE("wide trace Z0 reference", "[component][transmission_line][wide]") {
    const double h = 0.2e-3, w = 5e-3, e = 4.4;
    auto r = tl::calculate({ .frequency = 100.0 * MHz,
                             .trace_height = 0.2 * mm, .trace_width = 5.0 * mm,
                             .trace_thickness = 0.035 * mm,
                             .conductor = emc::materials::Material::Custom,
                             .custom_sigma = 5.96e7 * (S / m),
                             .relative_permittivity = e });
    REQUIRE(r.has_value());
    const double eps0 = emc::constants::eps0.numerical_value_in(F/m);
    const double L = 0.4 * emc::constants::pi * h / w;
    const double C = (eps0 * e * w / h) * 1e12;
    CHECK(emc::test::approx(r->characteristic_impedance, std::sqrt(1e6*L/C) * ohm, 1e-4));
}

// (c) Validation: w <= 5h -> InvalidInput.
TEST_CASE("wide trace requires w>5h", "[component][transmission_line][wide][validation]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .trace_height = 1.0 * mm, .trace_width = 4.0 * mm,
                             .trace_thickness = 0.035 * mm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}
```

What each guards: (a) an independent `0.4π·h/w` hand value; (b) Z₀ from hand-computed L/C; (c) the
`w > 5h` precondition.

---

## Wire Over Plane

### 1. Overview

Per-length L, C, R, Z₀ of a round wire of radius `a` at height `h` over a ground plane (`h > a`):

```text
L = 0.2·acosh(h/a)                            [µH/m]
C = (2π·ε0·ε_r / acosh(h/a)) · 1e12           [pF/m]
δ = 1 / sqrt(π·f·μ0·σ)
Aeff = (δ ≤ a/2) ? 2π·a·δ : π·a²
R = 1000 / (σ·Aeff)                           [mΩ/m]
Z0 = sqrt(1e6·L/C)                            [Ω]
```

Inputs: frequency `f`, wire height `h`, radius `a`, conductor material / σ, and `ε_r`. The geometry
requires `h > a`.

### 2. Implementation

```c++
// src/component/transmission_line.cpp   (Wire Over Plane section)
namespace emc::component {

std::expected<void, emc::Error> validate(const WireOverPlaneInput& in) {
    const double h = in.wire_height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto r = emc::require_positive(a, "wire_radius"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (h <= a)
        return std::unexpected(emc::invalid_input(
            "wire height must exceed wire radius", "wire_height"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WireOverPlaneInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f = in.frequency.numerical_value_in(Hz);
    const double h = in.wire_height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom = std::acosh(h / a);                   // acosh(h/a)
    const double L_uH_per_m = 0.2 * geom;
    const double C_pF_per_m = (2.0 * emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= a / 2.0)
                       ? 2.0 * emc::constants::pi * a * delta
                       : emc::constants::pi * a * a;

    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/1000.0);
}

} // namespace emc::component
```

### 3. Modern C++ features used here — and why

- **`std::expected` + `invalid_input` for `h ≤ a`** — the configuration is only physical when the
  wire is above the plane, so `h ≤ a` is reported as a typed `InvalidInput` instead of a placeholder
  result.
- **`emc::constants::pi`** — used in both the `Aeff = 2π·a·δ` / `π·a²` area terms and the skin-depth
  sqrt, all the same canonical pi.
- **`emc::materials::properties()`** for σ.
- **mp-units inputs** — `f` in Hz, `h`/`a` in any length unit.

### 4. Example usage

```c++
auto r = emc::component::calculate(emc::component::WireOverPlaneInput{
    .frequency = 5.0 * Hz,
    .wire_height = 22.0 * cm, .wire_radius = 20.0 * cm,
    .conductor = emc::materials::Material::Copper, .relative_permittivity = 1.0,
});
if (r) {
    std::println("L  = {} µH/m", r->inductance.numerical_value_in(si::micro<H>/m));
    std::println("Z0 = {} Ω",    r->characteristic_impedance.numerical_value_in(ohm));
}
```

### 5. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Wire Over Plane section)

// (a) Hand-computed L: h=22cm, a=20cm -> L = 0.2·acosh(1.1).
TEST_CASE("wire over plane known L", "[component][transmission_line][wireplane]") {
    auto r = tl::calculate({ .frequency = 5.0 * Hz,
                             .wire_height = 22.0 * cm, .wire_radius = 20.0 * cm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.2*std::acosh(1.1)) * (si::micro<H>/m), 1e-9));
}

// (b) L at a wider separation (h=10cm, a=1cm) -> 0.2·acosh(10), independent recompute.
TEST_CASE("wire over plane L reference", "[component][transmission_line][wireplane]") {
    auto r = tl::calculate({ .frequency = 5.0 * Hz,
                             .wire_height = 10.0 * cm, .wire_radius = 1.0 * cm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    CHECK(emc::test::approx(r->inductance, (0.2*std::acosh(10.0)) * (si::micro<H>/m), 1e-9));
}

// (c) Validation: h <= a -> InvalidInput.
TEST_CASE("wire over plane requires h>a", "[component][transmission_line][wireplane][validation]") {
    auto r = tl::calculate({ .frequency = 5.0 * Hz,
                             .wire_height = 10.0 * cm, .wire_radius = 20.0 * cm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}
```

What each guards: (a) and (b) independent `acosh(h/a)` hand values; (c) the `h > a` precondition.

---

## Wire Pair

### 1. Overview

Per-length L, C, R, Z₀ of a balanced two-wire pair (spacing `s`, wire diameter `d`, radius `a = d/2`):

```text
L = 0.4·acosh(s/d)                            [µH/m]
C = (π·ε0·ε_r / acosh(s/d)) · 1e12            [pF/m]
δ = 1 / sqrt(π·f·μ0·σ)
Aeff = (δ ≤ a/2) ? 2π·a·δ : π·a²       (a = d/2)
R = 2000 / (σ·Aeff)                           [mΩ/m]   ← 2000, not 1000: TWO conductors
Z0 = sqrt(1e6·L/C)                            [Ω]
```

Inputs: frequency `f`, spacing `s`, wire diameter `d`, conductor material / σ, and `ε_r`. The
geometry requires `s > d`.

> [!NOTE]
> The `acosh(s/d)` uses the spacing-over-*diameter* ratio (a common engineering simplification), and
> the `R` numerator is **2000** (= 2 × 1000) because the loop has two conductors in series — this is
> the one place the shared core's `r_numer` parameter earns its keep.

### 2. Implementation

```c++
// src/component/transmission_line.cpp   (Wire Pair section)
namespace emc::component {

std::expected<void, emc::Error> validate(const WirePairInput& in) {
    const double s = in.spacing.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto r = emc::require_positive(d, "diameter"); !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 1000.0,
                               "relative_permittivity"); !r) return r;
    if (s <= d)
        return std::unexpected(emc::invalid_input(
            "wire spacing must exceed wire diameter", "spacing"));
    return {};
}

emc::Result<LinePerLengthResult> calculate(const WirePairInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    auto sigma = detail::resolve_sigma(in.conductor, in.custom_sigma);
    if (!sigma) return std::unexpected(sigma.error());

    const double f = in.frequency.numerical_value_in(Hz);
    const double s = in.spacing.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    const double a = 0.5 * d;                                 // radius
    const double eps0 = emc::constants::eps0.numerical_value_in(F / m);

    const double geom = std::acosh(s / d);                   // acosh(s/d)
    const double L_uH_per_m = 0.4 * geom;
    const double C_pF_per_m = (emc::constants::pi * eps0 * in.relative_permittivity / geom) * 1e12;

    const double delta = detail::skin_depth(f, *sigma);
    const double aeff  = (delta <= a / 2.0)
                       ? 2.0 * emc::constants::pi * a * delta
                       : emc::constants::pi * a * a;

    // r_numer = 2000: two conductors. THIS is why per_length_line takes r_numer.
    return detail::per_length_line(L_uH_per_m, C_pF_per_m, *sigma, aeff, /*r_numer=*/2000.0);
}

} // namespace emc::component
```

### 3. Modern C++ features used here — and why

- **Shared `detail::per_length_line` with an explicit `r_numer` parameter** — Wire Pair is the *only*
  variant with `R = 2000/(σ·Aeff)` (two conductors); passing `2000.0` reuses the identical Z₀/R
  bookkeeping with a one-token difference instead of a fourth copy of the block.
- **`emc::constants::pi`/`eps0`/`mu0`** — the field constants come from the single canonical source.
- **`std::expected` + `invalid_input` for `s ≤ d`** — overlapping conductors are non-physical, so
  `s ≤ d` is a typed `InvalidInput`.
- **mp-units `Length` inputs** — `s`/`d` may be supplied in any unit; the SI value is stored once and
  the display unit is chosen at read time (e.g.
  `numerical_value_in(si::micro<H> / si::international::foot)`), so there is no per-unit scaling state.
- **`emc::materials::properties()`** for σ.

### 4. Example usage

```c++
auto r = emc::component::calculate(emc::component::WirePairInput{
    .frequency = 1.0 * MHz,
    .spacing = 10.0 * cm, .diameter = 0.1 * cm,
    .conductor = emc::materials::Material::Copper, .relative_permittivity = 1.0,
});
if (r) {
    // pick the display unit at read time — m or per-foot:
    std::println("L  = {} µH/m",  r->inductance.numerical_value_in(si::micro<H>/m));
    std::println("Z0 = {} Ω",     r->characteristic_impedance.numerical_value_in(ohm));
}
```

### 5. Unit tests

```c++
// tests/component/transmission_line_test.cpp   (Wire Pair section)

// (a) Hand-computed L: s=10cm, d=1cm -> L = 0.4·acosh(10).
TEST_CASE("wire pair known L", "[component][transmission_line][wirepair]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .spacing = 10.0 * cm, .diameter = 1.0 * cm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, (0.4*std::acosh(10.0)) * (si::micro<H>/m), 1e-9));
}

// (b) "Two conductors" property: at identical geometry, wire-pair R is exactly 2x a
//     single-conductor wire-over-plane R (the 2000 vs 1000 numerator).
TEST_CASE("wire pair R is double the single-wire R", "[component][transmission_line][wirepair][property]") {
    const auto f = 1.0 * MHz; const auto a = 0.5 * cm;  // radius 0.5cm -> diameter 1cm
    auto pair = tl::calculate({ .frequency = f, .spacing = 10.0 * cm, .diameter = 1.0 * cm,
                                .conductor = emc::materials::Material::Copper });
    auto wire = tl::calculate({ .frequency = f, .wire_height = 10.0 * cm, .wire_radius = a,
                                .conductor = emc::materials::Material::Copper });
    REQUIRE(pair.has_value()); REQUIRE(wire.has_value());
    const double rp = pair->resistance.numerical_value_in(si::milli<ohm>/m);
    const double rw = wire->resistance.numerical_value_in(si::milli<ohm>/m);
    REQUIRE(rp == Catch::Approx(2.0 * rw).epsilon(1e-9));   // 2000 vs 1000, same Aeff
}

// (c) Validation: s <= d -> InvalidInput.
TEST_CASE("wire pair requires s>d", "[component][transmission_line][wirepair][validation]") {
    auto r = tl::calculate({ .frequency = 1.0 * MHz,
                             .spacing = 1.0 * cm, .diameter = 2.0 * cm,
                             .conductor = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::InvalidInput);
}
```

What each guards: (a) an independent `0.4·acosh(s/d)` value; (b) the `2000` vs `1000` numerator
factored through the shared core (same `Aeff`, so R doubles exactly); (c) the `s > d` precondition.

---

## Binding the calculators to the `Calculator` concept

At the bottom of `transmission_line.hpp`, each Input/Result/`calculate` triple is wrapped in a tag
struct and `static_assert`-ed against the foundation concept (per `00-foundation-code.md` §5), so a
signature drift is a compile error in this header:

```c++
// include/emc/component/transmission_line.hpp  (tail)
namespace emc::component {

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
```

> [!TIP]
> The four per-length tags share `LinePerLengthResult` but have distinct `Input` types, so the
> overloaded `calculate(const NarrowTraceInput&)` / `calculate(const WideTraceInput&)` / … are
> selected by argument type — each calculator is forward-only (one direction, multi-output).

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `emc::constants`, `emc::units`
  aliases (including the `InductancePerLength`/`ResistancePerLength` additions used here),
  `emc::materials::properties()`, the `Error`/`ErrorCode`/`Result` model, the `ValidatedCalculator`
  concept, and `emc::test::approx`.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — why `mu_r`/`ε_r`
  are plain doubles, the per-length quantity aliases, and `.numerical_value_in(unit)`.
- [`../04-constants-and-material-database.md`](../04-constants-and-material-database.md) — the Copper σ
  (5.96e7 S/m) and the single-source `mu0`/`eps0` used by every per-length calculator.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) — the
  `std::expected` model and `ErrorCode` taxonomy.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/
  `calculate`/`validate` triple and the `detail::` shared-core pattern that `per_length_line` follows.
