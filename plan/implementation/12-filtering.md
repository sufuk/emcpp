# Implementation Guide — Filtering (`emc::filtering`) 🧲

Complete, copy-paste-quality C++23 code (header, `.cpp`, example, Catch2 v3 tests) for the Filtering
category's calculators, built on the canonical foundation surface from
[`00-foundation-code.md`](00-foundation-code.md).

This guide covers **one** calculator:

| Calculator | Namespace | Header | Impl | Test |
|---|---|---|---|---|
| Ferrite Toroid Impedance | `emc::filtering` | `include/emc/filtering/ferrite_toroid.hpp` | `src/filtering/ferrite_toroid.cpp` | `tests/filtering/ferrite_toroid_test.cpp` |

It assumes the foundation headers: `emc::Result<T>`, `emc::Error`, `emc::ErrorCode`, the
`emc::in_range`/`emc::require_positive`/`emc::require_nonzero` validators, `emc::constants::{pi, mu0}`,
the `emc::units::*` quantity aliases, and the `emc::test::{approx}` helpers. Those names are canonical and
are not redefined here.

---

## Ferrite Toroid Impedance — `ferrite_toroid`

### 1. Overview

A ferrite suppression toroid of `N` turns wound on a rectangular cross-section core (height `h`, inner
radius `a`, outer radius `b`) is modeled at frequency `f` using the **complex relative permeability**
`μ_r = μ_r′ − j·μ_r″`. The geometric inductance of the core is

```text
L  = (μ₀ N² h / 2π) · ln(b/a)
```

and the complex impedance of the wound core splits into a real (loss / resistance) part and an
imaginary (reactance) part:

```text
X   = 2π f · μ_r′ · L      (reactance,  Ω — the energy-storing part, from the real μ′)
R   = 2π f · μ_r″ · L      (resistance, Ω — the lossy part, from the imaginary μ″)
|Z| = √(R² + X²)           (magnitude of the complex impedance, Ω)
```

The whole point of a ferrite for EMI suppression is that at the frequency of interest `μ_r″` (and thus
`R`) dominates: the part dissipates the unwanted high-frequency energy as heat rather than reflecting it.

> [!IMPORTANT]
> Here `a` is the **inner radius** and `b` the **outer radius** of the toroid core — *not* a wire
> radius. `ln(b/a)` is real and finite only for `b > a > 0`; `a = 0` is a `ln`-domain blow-up that must
> be guarded explicitly (otherwise it silently yields `inf`/`nan`). The input field names reflect the
> inner/outer geometry.

### 2. Public header

```c++
// include/emc/filtering/ferrite_toroid.hpp
#pragma once

#include <expected>

#include <mp-units/systems/si.h>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::pi, emc::constants::mu0
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Frequency / Length / Inductance / Impedance

namespace emc::filtering {

using emc::units::Frequency;
using emc::units::Inductance;
using emc::units::Impedance;
using emc::units::Length;

// ---------------------------------------------------------------------------
//  Input — geometry arrives as typed mp-units Length/Frequency, so no unit
//  factor tables are needed at the boundary. The complex relative permeability
//  is carried as TWO dimensionless doubles (mu_r' real, mu_r'' imag), matching
//  the canonical "mu_r is a double" rule (doc 03 §3). Defaults give a sensible
//  representative toroid so a bare `FerriteToroidInput{}` computes a valid case.
// ---------------------------------------------------------------------------
struct FerriteToroidInput {
    double    turns          = 1000.0;                       ///< N      [-]   number of turns
    double    mu_r_real      = 100.0;                        ///< μ_r′   [-]   real part of complex μ_r
    double    mu_r_imag      = 50.0;                         ///< μ_r″   [-]   imag (loss) part of complex μ_r
    Length    height         = 0.05 * mp_units::si::metre;   ///< h      [m]   core height (> 0)
    Length    outer_radius   = 0.04 * mp_units::si::metre;   ///< b      [m]   outer radius (> 0; ln(b/a))
    Length    inner_radius   = 0.03 * mp_units::si::metre;   ///< a      [m]   inner radius (> 0; ln(b/a) denom)
    Frequency frequency      = 1.0 * mp_units::si::mega<mp_units::si::hertz>;  ///< f [Hz]
};

// ---------------------------------------------------------------------------
//  Result — L/X/R/Z, all carrying units in the type (H, Ω, Ω, Ω); a front end
//  picks a display unit with `.in(unit)`.
// ---------------------------------------------------------------------------
struct FerriteToroidResult {
    Inductance inductance;   ///< L    [H]   geometric inductance of the wound core
    Impedance  reactance;    ///< X    [Ω]   2π f μ_r′ L  (energy-storing, from μ′)
    Impedance  resistance;   ///< R    [Ω]   2π f μ_r″ L  (lossy, from μ″ — the suppression term)
    Impedance  impedance;    ///< |Z|  [Ω]   √(R² + X²)
};

/// Validate ranges/positivity. b > a > 0 (so ln(b/a) is finite & defined), h > 0,
/// f > 0, N > 0, and the permeability parts non-negative.
[[nodiscard]] std::expected<void, emc::Error> validate(const FerriteToroidInput& in);

/// Compute L, X, R, |Z| from the complex permeability and toroid geometry.
///   L  = (μ₀ N² h / 2π) · ln(b/a)
///   X  = 2π f μ_r′ L,  R = 2π f μ_r″ L,  |Z| = √(R²+X²)
[[nodiscard]] emc::Result<FerriteToroidResult> calculate(const FerriteToroidInput& in);

// ---------------------------------------------------------------------------
//  Calculator-concept binding: the (Input, Result, calculate) triple checked at
//  compile time, so a signature drift is a hard error AT THIS HEADER (doc 06 §1e).
// ---------------------------------------------------------------------------
struct FerriteToroid {
    using Input  = FerriteToroidInput;
    using Result = FerriteToroidResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::filtering::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::filtering::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<FerriteToroid>);

} // namespace emc::filtering
```

### 3. Implementation

```c++
// src/filtering/ferrite_toroid.cpp
#include <emc/filtering/ferrite_toroid.hpp>

#include <cmath>   // std::log, std::sqrt (constexpr in C++23)

#include <mp-units/math.h>          // mp_units::sqrt for typed quantities (optional)
#include <mp-units/systems/si.h>

namespace emc::filtering {

namespace {
using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz, H, ohm, ...
}

// ---------------------------------------------------------------------------
//  validate() — every precondition is a typed, range-bearing Error. Without
//  these guards, a=0 or b<=0 would compute ln(b/a) as -inf/nan and flow
//  silently into X/R/Z (doc 05 §1).
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const FerriteToroidInput& in) {
    // Extract numeric values in the fields' SI base units for range reporting.
    const double N     = in.turns;
    const double h_m   = in.height.numerical_value_in(m);
    const double b_m   = in.outer_radius.numerical_value_in(m);
    const double a_m   = in.inner_radius.numerical_value_in(m);
    const double f_hz  = in.frequency.numerical_value_in(Hz);

    if (auto r = emc::require_positive(N, "turns"); !r) return r;
    if (auto r = emc::require_positive(h_m, "height"); !r) return r;
    if (auto r = emc::require_positive(a_m, "inner_radius"); !r) return r;  // a=0 → ln(b/0) blow-up
    if (auto r = emc::require_positive(b_m, "outer_radius"); !r) return r;
    if (auto r = emc::require_positive(f_hz, "frequency"); !r) return r;

    // μ_r′ and μ_r″ are physically >= 0 (a passive ferrite cannot have negative
    // permeability). They MAY be 0 individually (e.g. a hypothetical lossless or
    // reactance-free band), so use a range check [0, ∞) rather than require_positive.
    if (auto r = emc::in_range(in.mu_r_real, 0.0, 1.0e9, "mu_r_real"); !r) return r;
    if (auto r = emc::in_range(in.mu_r_imag, 0.0, 1.0e9, "mu_r_imag"); !r) return r;

    // ln(b/a) is only real & finite for b > a > 0. b == a gives L = 0 (ln 1),
    // which is physically fine (no core), so we ALLOW b == a but REJECT b < a
    // (negative inductance is nonsense). a > 0 already guaranteed above.
    if (b_m < a_m)
        return std::unexpected(emc::domain_error(
            "outer_radius must be >= inner_radius (b >= a) for ln(b/a)", "outer_radius"));

    return {};   // all preconditions hold
}

// ---------------------------------------------------------------------------
//  calculate() — evaluate the closed-form math in SI base units (metres,
//  hertz), then re-attach mp-units types on the way out.
// ---------------------------------------------------------------------------
emc::Result<FerriteToroidResult> calculate(const FerriteToroidInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Pull plain doubles in canonical units (post-validation: all > 0, b >= a).
    const double N    = in.turns;
    const double up   = in.mu_r_real;   // μ_r′  (real)
    const double upp  = in.mu_r_imag;   // μ_r″  (imag / loss)
    const double h    = in.height.numerical_value_in(m);
    const double b    = in.outer_radius.numerical_value_in(m);
    const double a    = in.inner_radius.numerical_value_in(m);
    const double f    = in.frequency.numerical_value_in(Hz);

    const double mu0  = emc::constants::mu0.numerical_value_in(H / m);   // 1.25663706212e-6
    const double pi   = emc::constants::pi;                              // std::numbers::pi

    // L = (μ₀ N² h / 2π) · ln(b/a)        [H]
    const double L = ((N * N * mu0 * h) / (2.0 * pi)) * std::log(b / a);

    // ω = 2π f ; X and R from the real/imag permeability split.
    const double two_pi_f = 2.0 * pi * f;
    const double X = two_pi_f * up  * L;          // reactance  [Ω]
    const double R = two_pi_f * upp * L;          // resistance [Ω]
    const double Z = std::sqrt(R * R + X * X);    // |Z|        [Ω]

    return FerriteToroidResult{
        .inductance = L * H,
        .reactance  = X * ohm,
        .resistance = R * ohm,
        .impedance  = Z * ohm,
    };
}

} // namespace emc::filtering
```

### 4. Modern C++ features used here — and why

- **mp-units typed `Length` / `Frequency` inputs** — EMC geometry spans m..mils and frequency spans
  Hz..GHz, so `emc::units::Length`/`Frequency` give compile-time unit safety: the caller writes
  `0.05 * m` or `1.0 * mega<hertz>` and mp-units derives every conversion factor exactly. No
  hand-maintained unit-factor table can drift or invert.
- **Designated-initializer `FerriteToroidInput`** — seven named fields make a call site like
  `{.mu_r_real = 100, .mu_r_imag = 50, ...}` self-documenting, so the real/imaginary permeability
  parts can never be swapped by position.
- **Two `double` fields for the complex permeability (`mu_r_real`, `mu_r_imag`)** — the canonical API
  models `μ_r` as a *dimensionless double* (doc 03 §3); a ferrite's permeability is the complex pair
  `μ′ − jμ″`, so two doubles carry it directly. The empirical formula uses the parts independently
  (`X` from `μ′`, `R` from `μ″`), so two named scalars read more clearly and keep the `Input` a trivial
  aggregate. (A `std::complex<double>` would also work but buys nothing for this closed form.)
- **`std::expected<FerriteToroidResult, Error>` + `validate()`** — calculator inputs have physical
  domains, so an out-of-domain input is reported as a recoverable typed error. `a = 0` is a
  `[[nodiscard]]` `OutOfRange` and `b < a` is a `DomainError` the caller must handle, rather than an
  unguarded `ln(b/a)` producing `inf`/`nan`.
- **`emc::constants::mu0` (one `inline constexpr`)** — a single CODATA free-space permeability shared
  across the library, instead of a per-translation-unit copy.
- **`emc::constants::pi`** — one namespaced, full-precision π for the formula, with no
  reduced-precision literal anywhere.
- **`std::log` / `std::sqrt` from `<cmath>` (constexpr in C++23)** — the standard math the closed form
  needs, usable in `constexpr` evaluation; `N*N` is used for the squaring (exact for the integer
  exponent 2).
- **`[[nodiscard]]` on `calculate`/`validate`** — ignoring the result-or-error is a compile-time
  warning, so a failure cannot be silently dropped.
- **`emc::ValidatedCalculator<FerriteToroid>` `static_assert`** — the (Input, Result, calculate,
  validate) contract is verified at compile time in the header itself.

### 5. Example usage

```c++
#include <print>

#include <mp-units/systems/si.h>

#include <emc/filtering/ferrite_toroid.hpp>

int main() {
    using namespace mp_units::si::unit_symbols;   // m, ohm, H, ...
    using mp_units::si::mega;
    using mp_units::si::hertz;

    // A 1000-turn suppression toroid: μ_r = 100 − j50 at 1 MHz, h = 5 cm,
    // b = 4 cm outer, a = 3 cm inner.
    const emc::filtering::FerriteToroidInput in{
        .turns        = 1000.0,
        .mu_r_real    = 100.0,
        .mu_r_imag    = 50.0,
        .height       = 5.0 * (mp_units::si::centi<mp_units::si::metre>),
        .outer_radius = 4.0 * (mp_units::si::centi<mp_units::si::metre>),
        .inner_radius = 3.0 * (mp_units::si::centi<mp_units::si::metre>),
        .frequency    = 1.0 * mega<hertz>,
    };

    const auto result = emc::filtering::calculate(in);
    if (!result) {
        // Error arm: structured, inspectable — printed through a generic front end.
        std::println("ferrite toroid error: {} (field: {})",
                     result.error().message, result.error().field);
        return 1;
    }

    const auto& r = *result;
    std::println("L   = {:.3f} uH", r.inductance.in(mp_units::si::micro<H>).numerical_value_in(
                                         mp_units::si::micro<H>));
    std::println("X   = {:.1f} ohm", r.reactance.numerical_value_in(ohm));
    std::println("R   = {:.1f} ohm", r.resistance.numerical_value_in(ohm));
    std::println("|Z| = {:.1f} ohm", r.impedance.numerical_value_in(ohm));
    // Suppression rule of thumb: R should dominate at the design frequency.
    if (r.resistance > r.reactance)
        std::println("  -> loss-dominated: good suppression at this frequency.");
    return 0;
}
```

### 6. Unit tests (`tests/filtering/ferrite_toroid_test.cpp`)

Expected values come from hand computation against the closed form, plus property, scaling, edge, and
validation checks.

```c++
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include <emc/filtering/ferrite_toroid.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;          // m, Hz, H, ohm
using mp_units::si::mega;
using mp_units::si::micro;
using mp_units::si::hertz;

using emc::filtering::FerriteToroidInput;
using emc::filtering::calculate;

// (a) HAND VALUE — pin the numbers with a paper calculation against the closed form.
//     N=1000, μ'=100, μ''=50, h=0.05 m, b=0.04 m, a=0.03 m, f=1 MHz.
//       ln(b/a)   = ln(0.04/0.03) = ln(1.33333) = 0.2876821
//       L = (1e6 * 1.25663706212e-6 * 0.05 / (2π)) * 0.2876821
//         = (1.25663706212e-6 * 50000 / 6.2831853) * 0.2876821    [N²=1e6, *0.05]
//         = (0.062831853106 / 6.2831853) * 0.2876821
//         = 0.0099999999... * 0.2876821 = 2.876821e-3 H  (≈ 2876.8 µH)
//       ω = 2π·1e6 = 6.2831853e6
//       X = ω·100·L = 6.2831853e6 * 100 * 2.876821e-3 = 1.807388e6 Ω
//       R = ω·50 ·L = 9.036939e5 Ω
//       |Z| = √(R²+X²) = √(X²(1+0.25)) = X·√1.25 = 2.020758e6 Ω
TEST_CASE("ferrite toroid hand value (representative toroid)", "[filtering][ferrite]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * mega<hertz>});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 2.876821e-3 * H,   1e-5));
    REQUIRE(emc::test::approx(r->reactance,  1.807388e6  * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->resistance, 9.036939e5  * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->impedance,  2.020758e6  * ohm, 1e-5));
}

// (b) PROPERTY: |Z| = √(R²+X²) — the magnitude must always equal the hypotenuse
//     of the (R, X) right triangle, and must dominate (or equal) each leg.
TEST_CASE("ferrite toroid |Z| is the R-X hypotenuse", "[filtering][ferrite][property]") {
    auto r = calculate({.turns = 250.0, .mu_r_real = 80.0, .mu_r_imag = 120.0,
                        .height = 0.012 * m, .outer_radius = 0.018 * m,
                        .inner_radius = 0.010 * m, .frequency = 10.0 * mega<hertz>});
    REQUIRE(r.has_value());
    const double R = r->resistance.numerical_value_in(ohm);
    const double X = r->reactance.numerical_value_in(ohm);
    const double Z = r->impedance.numerical_value_in(ohm);
    REQUIRE(emc::test::approx(r->impedance, std::sqrt(R*R + X*X) * ohm, 1e-9));
    REQUIRE(Z >= R);                       // hypotenuse >= each leg
    REQUIRE(Z >= X);
    // R/X must track μ''/μ' exactly (same L, same ω): here 120/80 = 1.5.
    REQUIRE(emc::test::approx((R / X) * mp_units::one, 1.5 * mp_units::one, 1e-9));
}

// (c) PROPERTY: |Z| scales as N² (R, X, L all linear in L which ∝ N²).
//     Doubling turns quadruples |Z|.
TEST_CASE("ferrite toroid impedance scales as N^2", "[filtering][ferrite][property]") {
    const FerriteToroidInput base{.turns = 100.0, .mu_r_real = 60.0, .mu_r_imag = 40.0,
                                  .height = 0.01 * m, .outer_radius = 0.02 * m,
                                  .inner_radius = 0.01 * m, .frequency = 1.0 * mega<hertz>};
    auto r1 = calculate(base);
    FerriteToroidInput dbl = base; dbl.turns = 200.0;
    auto r2 = calculate(dbl);
    REQUIRE(r1.has_value()); REQUIRE(r2.has_value());
    REQUIRE(emc::test::approx(r2->impedance,
                              4.0 * r1->impedance.numerical_value_in(ohm) * ohm, 1e-9));
}

// (d) VALIDATION — a = 0 is the ln(b/0) blow-up; must be a typed error.
TEST_CASE("ferrite toroid rejects zero inner radius", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.0 * m, .frequency = 1.0 * mega<hertz>});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "inner_radius");
}

// (e) VALIDATION — b < a gives a (would-be) negative inductance → DomainError.
TEST_CASE("ferrite toroid rejects outer < inner radius", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.02 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * mega<hertz>});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::DomainError);
    REQUIRE(r.error().field == "outer_radius");
}

// (f) VALIDATION — non-positive frequency → OutOfRange (f<=0 is unphysical
//     for a reactance calc).
TEST_CASE("ferrite toroid rejects non-positive frequency", "[filtering][ferrite][error]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.04 * m,
                        .inner_radius = 0.03 * m, .frequency = 0.0 * Hz});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// (g) EDGE: b == a → ln(1) == 0 → L = X = R = Z = 0 (allowed, faithful boundary).
TEST_CASE("ferrite toroid b==a gives zero impedance", "[filtering][ferrite][property]") {
    auto r = calculate({.turns = 1000.0, .mu_r_real = 100.0, .mu_r_imag = 50.0,
                        .height = 0.05 * m, .outer_radius = 0.03 * m,
                        .inner_radius = 0.03 * m, .frequency = 1.0 * mega<hertz>});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 0.0 * H,   1e-12));
    REQUIRE(emc::test::approx(r->impedance,  0.0 * ohm, 1e-12));
}
```

**What each test guards:** (a) an independent paper value pinning `ln(b/a)`, the `2πf` factor, and the
`√(R²+X²)` magnitude; (b) the `|Z|=√(R²+X²)` invariant and the `R/X = μ″/μ′` ratio identity; (c) the
`N²` scaling law; (d) the `a=0` `ln`-domain guard; (e) the `b<a` negative-inductance guard; (f) the
non-positive-frequency guard; (g) the `b==a` zero boundary.

> [!TIP]
> For a richer sweep, drive `calculate()` over a table of hand-computed `(N, μ′, μ″, h, b, a, f) →
> (L, X, R, Z)` reference rows and assert each output with a `1e-9` tolerance. The closed form supplies
> the oracle; no external fixture is needed.

### 7. Design notes

- **Single free-space permeability.** `emc::constants::mu0` uses the CODATA value
  `1.25663706212e-6` H/m. If a result is ever compared against a reference computed with the rounded
  `4π·10⁻⁷ ≈ 1.2566370614e-6`, the two agree to ~3e-9 relative — far below any display precision, which
  is why hand-value tolerances sit at `1e-5`..`1e-9` rather than `0`.
- **Validation up front.** `validate()` turns `a = 0`, `b = 0`, and `b < a` into typed `OutOfRange` /
  `DomainError` values (tests d, e) before any `ln`/`sqrt` runs, so no `inf`/`nan` can reach the result.
- **Complex permeability representation.** Two `double` fields (`mu_r_real`, `mu_r_imag`) keep the
  `Input` an aggregate and the designated-initializer call site simple. A future variant could accept
  `std::complex<double> mu_r` and derive `X`/`R` from `std::real`/`std::imag`, but that buys nothing for
  this closed form, so it is not done here.
- **`N*N` for the squaring.** Exact and cheaper than a general power for the integer exponent 2.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `error.hpp` / `constants.hpp` /
  `units.hpp` / `calculator.hpp` and `tests/support/` helpers reused here (`emc::constants::{pi, mu0}`,
  `emc::units::{Length, Frequency, Inductance, Impedance}`, `emc::test::approx`, `emc::in_range` /
  `require_positive`, `emc::domain_error`).
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — why `μ_r′`/`μ_r″`
  are dimensionless doubles and how typed `Length`/`Frequency` give compile-time unit safety.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) — the
  `Error`/`ErrorCode`/`std::expected` model this calculator's `validate()` uses.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the
  Input/Result/`calculate`/`validate` triple and the `ValidatedCalculator` concept this header binds to.
- [`04-component-inductance.md`](04-component-inductance.md) — the **geometric** toroid inductance
  (`emc::component::toroid_inductance`), which shares the `L = (μ₀N²h/2π)·ln(b/a)` core; this filtering
  calculator extends it with the complex-permeability `X`/`R`/`|Z|` impedance split.
