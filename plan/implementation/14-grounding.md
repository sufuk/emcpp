# Implementation Guide — Grounding (`emc::grounding`) ⚡

Complete, copy-paste-quality C++23 code (header + `.cpp` + example + Catch2 v3 tests) for the Grounding
category of the `emc` library, built on the pinned Foundation API from
[`00-foundation-code.md`](00-foundation-code.md).

This category contains exactly one calculator:

| Calculator | Namespace | Header | Impl | Test |
|---|---|---|---|---|
| Microstrip Line Current Distribution | `emc::grounding` | `include/emc/grounding/microstrip_current.hpp` | `src/grounding/microstrip_current.cpp` | `tests/grounding/microstrip_current_test.cpp` |

The public free function is `emc::grounding::microstrip_current_distribution(const MicrostripCurrentInput&)`
returning `emc::Result<MicrostripCurrentResult>`. It reuses the Foundation `emc::constants::pi`,
`emc::units::{Current, Length, …}`, `emc::Result`, and the
`emc::in_range`/`emc::require_positive`/`emc::require_nonzero` validators verbatim.

---

## Microstrip Line Current Distribution (ground-plane current density J)

### 1. Overview

When a current `I₀` flows down a microstrip trace of width `w` suspended a height `h` above a ground
plane, the *return* current spreads out across the ground plane rather than flowing in a thin line
directly under the trace. The lateral surface-current density on the ground plane, as a function of the
lateral offset `x` from the point directly below the trace, follows the standard Lorentzian (Cauchy)
profile

```text
            I₀            1
  J(x) = ─────────  ·  ──────────────
          π · w         1 + (x/h)²
```

with `J` a **linear surface current density in A/m** (current per unit transverse width of ground
plane). The current is maximal directly under the trace (`x = 0`) and falls off as `1/(1+(x/h)²)`,
reaching half its peak at `x = h`. Integrating `J` over all `x` recovers `I₀` (the `π` in the
denominator is exactly the normalization constant of the Lorentzian).

> [!NOTE]
> The denominators `π·w` and `1 + (x/h)²` set the two physical preconditions: `w > 0` and `h ≠ 0`.
> `I₀` and `x` are unconstrained — `x` may be negative, giving the symmetric profile `J(+x) = J(−x)`.

### 2. Public header — `include/emc/grounding/microstrip_current.hpp`

```c++
// include/emc/grounding/microstrip_current.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Current, Length, ...

namespace emc::grounding {

// ---------------------------------------------------------------------------
//  Input — the four geometric / electrical quantities of the microstrip return-
//  current problem. All mp-units quantities (no bare doubles, no factor-based
//  unit conversion), with sensible defaults so a designated-initializer call
//  site reads cleanly and a default-constructed Input is valid.
//      i0 = 1 mA,  x = 2 mm,  h = 1.6 mm,  w = 2.8 mm
// ---------------------------------------------------------------------------
struct MicrostripCurrentInput {
    emc::units::Current source_current;  // I0 : current flowing down the trace
    emc::units::Length  trace_width;     // w  : microstrip conductor width
    emc::units::Length  height;          // h  : trace height above the ground plane
    emc::units::Length  position;        // x  : lateral offset on the ground plane
};

// ---------------------------------------------------------------------------
//  Result — the single output: ground-plane surface current density at x.
// ---------------------------------------------------------------------------
struct MicrostripCurrentResult {
    // J : linear surface current density [A/m] = current per unit transverse width.
    // We reuse MagneticField (A/m): a ground-plane surface current density and the
    // tangential H-field it implies share the same dimension and unit, so this alias
    // is dimensionally exact. (See "features" note.)
    emc::units::MagneticField current_density;
};

// ---------------------------------------------------------------------------
//  validate() — physical/numerical preconditions the closed form requires:
//    * w > 0  (denominator pi*w)            -> require_positive
//    * h != 0 (denominator 1+(x/h)^2)       -> require_nonzero
//    * I0, x  are free (any real; x may be negative -> symmetric profile)
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error>
validate(const MicrostripCurrentInput& in);

// ---------------------------------------------------------------------------
//  Forward-only calculator: J(x) = (I0 / (pi*w)) * 1/(1 + (x/h)^2).
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<MicrostripCurrentResult>
microstrip_current_distribution(const MicrostripCurrentInput& in);

// ---------------------------------------------------------------------------
//  Concept binding (Foundation §5): the (Input, Result, calculate) triple as a
//  tag type, checked at compile time so a signature drift is a build error here.
// ---------------------------------------------------------------------------
struct MicrostripCurrentDistribution {
    using Input  = MicrostripCurrentInput;
    using Result = MicrostripCurrentResult;
    static emc::Result<Result> calculate(const Input& in) {
        return microstrip_current_distribution(in);
    }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::grounding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<MicrostripCurrentDistribution>);

} // namespace emc::grounding
```

### 3. Implementation — `src/grounding/microstrip_current.cpp`

```c++
// src/grounding/microstrip_current.cpp
#include <emc/grounding/microstrip_current.hpp>

#include <emc/core/constants.hpp>   // emc::constants::pi

#include <mp-units/systems/si.h>

namespace emc::grounding {

using namespace mp_units;
using mp_units::si::unit_symbols::A;     // ampere
using mp_units::si::unit_symbols::m;     // metre

// ---------------------------------------------------------------------------
//  validate() — preconditions the closed-form needs.
// ---------------------------------------------------------------------------
std::expected<void, emc::Error>
validate(const MicrostripCurrentInput& in) {
    // Evaluate ranges in the field's natural display unit so a reported [lo,hi]
    // is meaningful to the user (mm for lengths, mA for current).
    const double w_mm = in.trace_width.numerical_value_in(si::milli<m>);
    const double h_mm = in.height.numerical_value_in(si::milli<m>);

    // pi*w must be a nonzero denominator AND a width is physically positive.
    if (auto r = emc::require_positive(w_mm, "trace_width"); !r)
        return r;
    // 1 + (x/h)^2 has h in the denominator: h must be nonzero (sign irrelevant).
    if (auto r = emc::require_nonzero(h_mm, "height"); !r)
        return r;

    return {};   // I0 and x are unconstrained (x may be negative: symmetric profile)
}

// ---------------------------------------------------------------------------
//  microstrip_current_distribution() — the Lorentzian, with all unit handling
//  done by mp-units rather than manual factor multiplications.
//      j = (i0 / (pi*w)) * 1/(1 + (x/h)^2)
// ---------------------------------------------------------------------------
emc::Result<MicrostripCurrentResult>
microstrip_current_distribution(const MicrostripCurrentInput& in) {
    return validate(in).transform([&] {
        // x/h is a pure dimensionless ratio; mp-units yields quantity<one>, and
        // .numerical_value_in(one) extracts the bare number for the (x/h)^2 term.
        const double ratio = (in.position / in.height).numerical_value_in(one);
        const double shape = 1.0 / (1.0 + ratio * ratio);   // Lorentzian shape factor [-]

        // Peak density I0/(pi*w): Current / Length = Current per Length = A/m.
        // mp-units checks the dimension; the result is an A/m quantity directly.
        const auto j_peak = in.source_current / (emc::constants::pi * in.trace_width);

        // J = peak * shape, expressed as A/m (== MagneticField alias).
        const emc::units::MagneticField j = (j_peak * shape).in(A / m);

        return MicrostripCurrentResult{ .current_density = j };
    });
}

} // namespace emc::grounding
```

> [!TIP]
> `validate(...).transform(...)` runs the lambda only on the success arm and forwards the `Error`
> unchanged on failure — so the div-by-zero guard and the math compose into one expression with no
> `if (!v) return v.error();` boilerplate. The lambda's result (`MicrostripCurrentResult`) is wrapped
> back into `Result<...>` automatically.

### 4. Modern C++ features used here — and why

- **mp-units quantity inputs (`emc::units::Current` / `Length`)** — EMC geometry spans Hz..GHz and
  m..mils, so passing typed quantities (`64.9 * mm`, `5.0 * mA`) gives compile-time unit safety and lets
  mp-units derive every conversion exactly. `w` and `x` can never be silently combined in mismatched
  units because `x/h` only type-checks when both are lengths, and there is no hand-written
  `mm = 0.001` / `mils = 0.0000254` factor table to mistype.
- **`emc::constants::pi` (= `std::numbers::pi`)** — the normalization constant routes through the single
  Foundation `pi`, so every calculator shares one full-precision π value
  (Foundation §2; [doc 04 §1.2](../04-constants-and-material-database.md)).
- **`std::expected` + `ErrorCode` (`emc::Result`)** — calculator inputs have physical domains, so an
  out-of-domain input is reported as a recoverable typed error rather than producing `inf`. `w ≤ 0` is a
  typed `ErrorCode::OutOfRange` and `h == 0` a typed `ErrorCode::DivisionByZero`, each a `[[nodiscard]]`
  value the caller must handle ([doc 05](../05-error-handling-and-validation.md)).
- **Monadic `std::expected::transform`** — composes "validate, then compute" into a single expression
  that short-circuits on the first failure ([doc 06](../06-calculator-design-pattern.md)).
- **Designated initializers on `Input`/`Result`** — call sites read
  `{ .source_current = 5*mA, .trace_width = 64.9*mm, .height = 57.4*mm, .position = 25.6*mm }`,
  self-documenting each value rather than relying on positional argument order.
- **`quantity<one>` for `x/h`** — the ratio is *typed* dimensionless, so `(x/h)²` is a compile-checked
  pure number; you cannot accidentally feed a length into the shape factor.
- **`[[nodiscard]]` on `calculate`/`validate`** — ignoring the `Result` is a compiler warning.
- **Reusing the `MagneticField` (A/m) alias for `J`** — a ground-plane *surface current density* and a
  tangential magnetic field strength share dimension and unit (A/m); reusing the Foundation alias keeps
  the unit exact and lets `emc::test::approx` compare `J` unit-aware without a bespoke quantity type.

### 5. Example usage

A generic front end prints results with `std::print`; no GUI toolkit is involved.

```c++
#include <emc/grounding/microstrip_current.hpp>
#include <print>

#include <mp-units/systems/si.h>

int main() {
    using namespace mp_units;
    using mp_units::si::unit_symbols::A;
    using mp_units::si::unit_symbols::m;
    using mp_units::si::unit_symbols::mm;
    using mp_units::si::unit_symbols::mA;

    // Reference case: I0 = 5 mA, x = 25.6 mm, h = 57.4 mm, w = 64.9 mm.
    const emc::grounding::MicrostripCurrentInput in{
        .source_current = 5.0  * mA,
        .trace_width    = 64.9 * mm,
        .height         = 57.4 * mm,
        .position       = 25.6 * mm,
    };

    const auto r = emc::grounding::microstrip_current_distribution(in);
    if (!r) {
        // Failure arm: structured, inspectable, localizable.
        std::println("error [{}]: {} (field: {})",
                     emc::to_string(r.error().code), r.error().message, r.error().field);
        return 1;
    }

    // Success arm: pull J out in the display unit you want (A/m or A/mm).
    std::println("J = {} A/m", r->current_density.numerical_value_in(A / m));   // ~0.0204545
    std::println("J = {} A/mm", r->current_density.numerical_value_in(A / mm)); // ~2.04545e-5

    // Edge case: zero width -> typed error, not 'inf'.
    const auto bad = emc::grounding::microstrip_current_distribution(
        { .source_current = 5.0 * mA, .trace_width = 0.0 * mm,
          .height = 57.4 * mm, .position = 25.6 * mm });
    if (!bad)
        std::println("rejected: {}", emc::to_string(bad.error().code));   // out_of_range
}
```

### 6. Unit tests — `tests/grounding/microstrip_current_test.cpp`

Expected values come from hand computation against the textbook closed form, plus property, symmetry,
monotonicity, and edge checks.

```c++
// tests/grounding/microstrip_current_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>     // std::pow, for the independent reference computation

#include <emc/grounding/microstrip_current.hpp>
#include <emc/core/constants.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using mp_units::si::unit_symbols::A;
using mp_units::si::unit_symbols::m;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::mA;

using emc::ErrorCode;
using emc::grounding::MicrostripCurrentInput;
using emc::grounding::microstrip_current_distribution;

// Independent SI reference: deliberately re-derives J from raw SI numbers,
// NOT by calling the library, so it is a true oracle for the table-driven test.
namespace {
[[nodiscard]] double j_reference_A_per_m(double i0_A, double x_m, double h_m, double w_m) {
    const double ratio = x_m / h_m;
    return (i0_A / (emc::constants::pi * w_m)) * (1.0 / (1.0 + ratio * ratio));
}

// Hand-picked reference input vectors (I0[mA], x[mm], h[mm], w[mm]), each
// cross-checked against the independent SI oracle below.
struct Row { double i0_mA, x_mm, h_mm, w_mm; };
constexpr Row kReferenceRows[] = {
    {5.0, 25.6, 57.4, 64.9},
    {1.0,  2.0,  1.6,  2.8},
    {10.0, 0.0,  1.6,  3.0},
};
} // namespace

// (a) Table-driven test over hand-chosen input vectors; the expected J for each
//     comes from the independent SI oracle, exercising the full unit-mapping +
//     formula path. from_range reports each row as its own Catch2 case.
TEST_CASE("microstrip current: reference input vectors", "[grounding]") {
    auto row = GENERATE_REF(from_range(std::begin(kReferenceRows), std::end(kReferenceRows)));

    const MicrostripCurrentInput in{
        .source_current = row.i0_mA * mA,
        .trace_width    = row.w_mm  * mm,
        .height         = row.h_mm  * mm,
        .position       = row.x_mm  * mm,
    };

    const auto out = microstrip_current_distribution(in);
    REQUIRE(out.has_value());

    const double expected = j_reference_A_per_m(
        row.i0_mA * 1e-3, row.x_mm * 1e-3, row.h_mm * 1e-3, row.w_mm * 1e-3);

    REQUIRE(emc::test::approx(out->current_density, expected * (A / m), 1e-9));
}

// (b) Independent hand-computed known value, computed OUTSIDE the test helper to
//     a literal so a wrong unit factor is caught.
//     I0=5mA, x=25.6mm, h=57.4mm, w=64.9mm:
//       ratio   = 25.6/57.4          = 0.4459930...
//       shape   = 1/(1+ratio^2)      = 0.8340...
//       peak    = 5e-3/(pi*0.0649)   = 0.024524... A/m
//       J       = peak*shape         = 0.0204545... A/m
TEST_CASE("microstrip current: hand-computed value", "[grounding]") {
    const auto out = microstrip_current_distribution({
        .source_current = 5.0  * mA,
        .trace_width    = 64.9 * mm,
        .height         = 57.4 * mm,
        .position       = 25.6 * mm,
    });
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->current_density, 0.0204545029 * (A / m), 1e-7));
}

// (c) Properties of the Lorentzian profile.
TEST_CASE("microstrip current: physical properties", "[grounding][property]") {
    const auto base = MicrostripCurrentInput{
        .source_current = 10.0 * mA, .trace_width = 3.0 * mm,
        .height = 1.6 * mm, .position = 0.0 * mm };

    SECTION("peak is at x = 0") {
        const auto at0 = microstrip_current_distribution(base);
        REQUIRE(at0.has_value());
        // J(0) = I0/(pi*w), independent of h.
        const double peak = (10e-3) / (emc::constants::pi * 3e-3);
        REQUIRE(emc::test::approx(at0->current_density, peak * (A / m), 1e-9));
    }

    SECTION("half-power at x = h") {
        auto at_h = base;
        at_h.position = at_h.height;   // x == h  -> shape = 1/(1+1) = 1/2
        const auto j0 = microstrip_current_distribution(base);
        const auto jh = microstrip_current_distribution(at_h);
        REQUIRE(j0.has_value());
        REQUIRE(jh.has_value());
        const double r =
            jh->current_density.numerical_value_in(A / m) /
            j0->current_density.numerical_value_in(A / m);
        REQUIRE(r == Catch::Approx(0.5).epsilon(1e-9));
    }

    SECTION("even symmetry: J(+x) == J(-x)") {
        auto plus = base;  plus.position  =  4.0 * mm;
        auto minus = base; minus.position = -4.0 * mm;
        const auto jp = microstrip_current_distribution(plus);
        const auto jm = microstrip_current_distribution(minus);
        REQUIRE(jp.has_value());
        REQUIRE(jm.has_value());
        REQUIRE(emc::test::approx(jp->current_density, jm->current_density, 1e-12));
    }

    SECTION("monotonic decay away from the trace") {
        auto near = base;  near.position = 1.0 * mm;
        auto far  = base;  far.position  = 8.0 * mm;
        const auto jn = microstrip_current_distribution(near);
        const auto jf = microstrip_current_distribution(far);
        REQUIRE(jn.has_value());
        REQUIRE(jf.has_value());
        REQUIRE(jn->current_density.numerical_value_in(A / m) >
                jf->current_density.numerical_value_in(A / m));
    }

    SECTION("J scales linearly with I0") {
        auto x1 = base; x1.source_current = 10.0 * mA; x1.position = 2.0 * mm;
        auto x2 = base; x2.source_current = 20.0 * mA; x2.position = 2.0 * mm;
        const auto a = microstrip_current_distribution(x1);
        const auto b = microstrip_current_distribution(x2);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(emc::test::approx(
            b->current_density, (2.0 * a->current_density.numerical_value_in(A / m)) * (A / m),
            1e-9));
    }
}

// (d) Validation / edge cases assert the RIGHT ErrorCode.
TEST_CASE("microstrip current: validation errors", "[grounding][error]") {
    const auto ok = MicrostripCurrentInput{
        .source_current = 5.0 * mA, .trace_width = 2.8 * mm,
        .height = 1.6 * mm, .position = 2.0 * mm };

    SECTION("zero width -> OutOfRange (require_positive), not inf") {
        auto in = ok; in.trace_width = 0.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "trace_width");
    }

    SECTION("negative width -> OutOfRange") {
        auto in = ok; in.trace_width = -1.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }

    SECTION("zero height -> DivisionByZero") {
        auto in = ok; in.height = 0.0 * mm;
        const auto r = microstrip_current_distribution(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "height");
    }
}

// (e) Concept conformance is a compile-time guarantee (static_assert in header),
//     re-checked here so the test TU also enforces the contract.
static_assert(emc::ValidatedCalculator<emc::grounding::MicrostripCurrentDistribution>,
              "MicrostripCurrentDistribution must satisfy ValidatedCalculator");
```

**What each test guards:**

- **(a) Reference vectors** — runs hand-chosen input vectors through the full unit-mapping + formula
  path, comparing against an independent SI oracle; `from_range` reports each row as its own Catch2 case.
- **(b) Hand value** — pins one fully hand-computed literal (`0.0204545029 A/m`), so a wrong `mA`/`mm`
  unit factor or a dropped `π` is caught independently of the oracle.
- **(c) Properties** — peak-at-`x=0`, half-power-at-`x=h`, even symmetry, monotonic decay, and linearity
  in `I₀` are formula-independent physical invariants of the Lorentzian.
- **(d) Validation** — confirms `w ≤ 0` → `OutOfRange` and `h = 0` → `DivisionByZero` with the correct
  `.field`.
- **(e) Concept** — the `static_assert` keeps the (Input, Result, calculate, validate) contract from
  drifting.

> [!IMPORTANT]
> The `(x/h)²` shape factor makes `J` even in `x`; `validate()` deliberately does **not** constrain `x`
> (or `I₀`), guarding only the two real denominators (`w > 0`, `h ≠ 0`).

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — canonical `emc::constants::pi`,
  `emc::units::{Current, Length, MagneticField}`, `emc::Result`, the `require_positive`/`require_nonzero`
  validators, and the `emc::test::approx` helper this guide reuses verbatim.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — why
  `I₀`/`w`/`h`/`x` become mp-units quantities.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) — the
  `Error`/`ErrorCode`/`std::expected` model behind the div-by-zero guards.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the
  Input/Result/`calculate`/`validate` triple and the `Calculator`/`ValidatedCalculator` concepts the tag
  struct satisfies.
- [`../07-calculator-inventory.md`](../07-calculator-inventory.md) §8 — the spec row fixing the namespace,
  header, function name, and formula.
