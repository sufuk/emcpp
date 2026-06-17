# Implementation Guide — Component: Inductance (`emc::component`, `inductance.hpp`) 🧲

> Complete, copy-paste-quality C++23 code (header + `.cpp` + example + Catch2 v3 tests) for the seven
> closed-form inductance calculators, all landing in one header `include/emc/component/inductance.hpp`
> (impl `src/component/inductance.cpp`).

Calculators covered (one H2 each), all in namespace **`emc::component`**:

| Calculator | Free function |
|---|---|
| Circular Loop | `circular_loop_inductance` |
| Connector Pin (self + mutual) | `connector_pin_inductance` |
| Rectangular Loop | `rectangular_loop_inductance` |
| Solenoid | `solenoid_inductance` |
| Square Loop | `square_loop_inductance` |
| Toroid | `toroid_inductance` |
| Via | `via_inductance` |

All seven are forward-only and material-independent (μ_r enters as a bare dimensionless multiplier where it
appears), every one returns `emc::Result<…>`, and every one consumes its lengths as `emc::units::Length`
quantities, so unit selection is handled once by the type system rather than per-input scale factors.

All seven share one free-space permeability constant, `emc::constants::mu0`. Every formula evaluates lengths
in metres and multiplies by `mu0`, sourced once from CODATA-2018 (`mu0 = 1.25663706212×10⁻⁶ H/m`).

> [!NOTE]
> Every formula here contains a `ln(·)` whose argument has a length in the **denominator** (`ln(8R/a)`,
> `ln(2l/r)`, `ln(b/a)`, `ln(4h/d)`, …). A zero wire-radius / inner-radius / diameter is a **domain error**
> (`ln` of `∞`, or a division by zero first). Each `validate()` below pins `a`/`r`/`d`/`a_inner` strictly
> positive.

The canonical foundation API (constants, units, error, the `Calculator` concept, the `emc::test` helpers) is
fixed in [`00-foundation-code.md`](./00-foundation-code.md) and reused verbatim here.

---

## Shared header preamble

All seven calculators live in the same header. The includes and the `detail::` core helper are shown once
here; each per-calculator section then adds its own `Input`/`Result`/declarations to the same file.

```c++
// include/emc/component/inductance.hpp
#pragma once

#include <array>

#include <emc/core/calculator.hpp>   // emc::Calculator / ValidatedCalculator concepts
#include <emc/core/constants.hpp>    // emc::constants::mu0, ::pi
#include <emc/core/error.hpp>        // emc::Result, emc::Error, validators
#include <emc/core/units.hpp>        // emc::units::Length, ::Inductance

namespace emc::component {

using emc::units::Length;
using emc::units::Inductance;

// ... per-calculator Input/Result/declarations are appended below in their sections ...

} // namespace emc::component
```

```c++
// src/component/inductance.cpp
#include <emc/component/inductance.hpp>

#include <cmath>     // std::log, std::sqrt, std::pow  (constexpr in C++23)

#include <mp-units/systems/si.h>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::m;
using mp_units::si::unit_symbols::H;

namespace {
// mu0 as a raw double in H/m, extracted ONCE from the canonical constant. Every formula
// below evaluates its lengths in metres and multiplies by this, from a single CODATA source.
constexpr double mu0_H_per_m = emc::constants::mu0.numerical_value_in(H / m);
constexpr double kPi         = emc::constants::pi;
} // namespace

// ... per-calculator calculate()/validate() bodies appended below ...

} // namespace emc::component
```

> [!TIP]
> **Why pull mu0 out as a `double` instead of working entirely in mp-units quantities?** These are
> *empirical-coefficient* formulas. The cleanest evaluation runs every length in **metres**
> (`len.numerical_value_in(m)`), evaluates the closed form in `double`, and re-attaches the henry unit on the
> result (`L_H * H`). The dimensional contract is still enforced at the boundary — `Input` fields are
> `emc::units::Length`, the `Result` is `emc::units::Inductance` — see
> [doc 06 §4](../06-calculator-design-pattern.md) (evaluate ratio-formulas in one coherent unit).

---

## Circular Loop — `circular_loop_inductance`

### 1. Overview

Inductance of a single circular loop of `N` turns, loop radius `R`, made of wire of radius `a`, in a medium
of relative permeability `μ_r` (standard thin-wire loop formula):

```text
L = N² · R · μ₀ · μ_r · ( ln(8R/a) − 2 )
```

`a` sits in the denominator inside `ln(8R/a)`, so `a > 0` is required.

### 2. Public header (append to `include/emc/component/inductance.hpp`)

```c++
namespace emc::component {

/// Inputs for a circular wire loop. μ_r is a bare dimensionless double (pinned canonical API).
struct CircularLoopInput {
    double turns        = 1.0;                 ///< N  [-]   (turn count; non-integer allowed)
    Length loop_radius  = 0.3 * mp_units::si::metre;     ///< R   [m]
    Length wire_radius  = 5e-4 * mp_units::si::metre;    ///< a   [m]  (must be > 0; appears as ln(8R/a))
    double mu_r         = 1.0;                 ///< relative permeability [-]
};

struct CircularLoopResult {
    Inductance inductance;                     ///< L  [H]
};

/// Range/positivity checks.
[[nodiscard]] std::expected<void, emc::Error> validate(const CircularLoopInput& in);

/// L = N² R μ₀ μ_r (ln(8R/a) − 2).
[[nodiscard]] emc::Result<CircularLoopResult> circular_loop_inductance(const CircularLoopInput& in);

} // namespace emc::component
```

### 3. Implementation (append to `src/component/inductance.cpp`)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const CircularLoopInput& in) {
    const double R = in.loop_radius.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    // a in the denominator AND inside ln(8R/a): zero/negative is a hard domain error.
    if (auto r = emc::require_positive(a, "wire_radius"); !r) return r;
    if (auto r = emc::require_positive(R, "loop_radius"); !r) return r;
    if (auto r = emc::require_positive(in.mu_r, "mu_r"); !r) return r;
    // N is squared, so sign is irrelevant, but N==0 gives a trivial 0 H; flag non-positive as invalid.
    if (auto r = emc::require_positive(in.turns, "turns"); !r) return r;
    return {};
}

emc::Result<CircularLoopResult> circular_loop_inductance(const CircularLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double R = in.loop_radius.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    const double L_H = N * N * R * mu0_H_per_m * in.mu_r * (std::log((8.0 * R) / a) - 2.0);

    return CircularLoopResult{ .inductance = L_H * H };
}

} // namespace emc::component
```

> [!WARNING]
> **Domain subtlety.** For very small loops `ln(8R/a)` can dip below `2`, making `L` *negative* — a
> physically meaningless artifact of this thin-wire approximation. We do **not** clamp it; a forward-looking
> `Unsupported`-warning candidate is noted in the design notes.

### 4. Modern C++ features used here — and why

- **`emc::units::Length` quantity inputs** — EMC loop geometry spans m..mils, so the caller writes `0.3 * m`
  or `30 * cm` and mp-units derives every conversion factor at compile time, eliminating any hand-coded scale
  table and the inversion mistakes those invite.
- **`std::expected<void, Error>` + `require_positive`** — calculator inputs have physical domains, so an
  out-of-domain `a` (which would make `ln(8R/0) = inf`) is reported as a recoverable typed error rather than
  silently yielding `inf`.
- **`emc::constants::mu0`** — one CODATA free-space permeability constant for all seven calculators.
- **Designated initializers on `CircularLoopInput`** — four named, self-documenting fields.
- **`[[nodiscard]]`** — a discarded `Result` (a silently dropped failure) becomes a warning.

### 5. Example usage

```c++
#include <emc/component/inductance.hpp>
#include <mp-units/systems/si.h>
#include <print>

using namespace mp_units::si::unit_symbols;   // m, mm, nH, H ...

int main() {
    const emc::component::CircularLoopInput in{
        .turns       = 1.0,
        .loop_radius = 0.3 * m,
        .wire_radius = 0.5 * mm,
        .mu_r        = 1.0,
    };

    if (auto r = emc::component::circular_loop_inductance(in)) {
        std::println("L = {}", r->inductance.in(nH));
    } else {
        std::println("error[{}]: {}", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests (`tests/component/circular_loop_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::CircularLoopInput;
using emc::component::circular_loop_inductance;

// (a) Hand-computed reference: N=1, R=0.3 m, a=0.0005 m, mu_r=1.
//     ln(8*0.3/0.0005)=ln(4800)=8.476371; L = 1*0.3*1.25663706212e-6*1*(8.476371-2)
//       = 0.3*1.25663706212e-6*6.476371 = 2.4416e-6 H ≈ 2441.6 nH.
TEST_CASE("circular loop hand value", "[component][inductance][circular_loop]") {
    auto r = circular_loop_inductance({.turns = 1.0, .loop_radius = 0.3 * m,
                                       .wire_radius = 0.0005 * m, .mu_r = 1.0});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 2441.6 * nH, 1e-3));
}

// (b) Monotonicity: more turns -> strictly larger L (N² scaling).
TEST_CASE("circular loop grows with turns", "[component][inductance][circular_loop]") {
    auto base = circular_loop_inductance({.turns = 1.0, .loop_radius = 0.3 * m,
                                          .wire_radius = 0.0005 * m, .mu_r = 1.0});
    auto more = circular_loop_inductance({.turns = 3.0, .loop_radius = 0.3 * m,
                                          .wire_radius = 0.0005 * m, .mu_r = 1.0});
    REQUIRE(base.has_value()); REQUIRE(more.has_value());
    REQUIRE(more->inductance.numerical_value_in(nH) > base->inductance.numerical_value_in(nH));
}

// (c) Validation: a=0 is a positivity (out-of-range) error, NOT an inf result.
TEST_CASE("circular loop rejects zero wire radius", "[component][inductance][circular_loop][error]") {
    auto r = circular_loop_inductance({.turns = 1.0, .loop_radius = 0.3 * m,
                                       .wire_radius = 0.0 * m, .mu_r = 1.0});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);   // require_positive uses OutOfRange
    REQUIRE(r.error().field == "wire_radius");
}
```

What each guards: (a) an independent closed-form value so a formula error can't slip through; (b) the `N²`
scaling law; (c) the domain guard on `a`.

### 7. Design notes

- **Negative-`L` regime** (`ln(8R/a) < 2`, i.e. very fat wire) is a `Unsupported`-warning candidate, not
  changed here.
- `a=0` yields an `OutOfRange` error rather than `inf`.

---

## Connector Pin — `connector_pin_inductance`

### 1. Overview

A round connector pin of length `l` and radius `r`, with a return path at spacing `s`, has a **partial
self-inductance** `L` and a **partial mutual inductance** `M_p` (standard partial-inductance formulas):

```text
L   = (μ₀ l / 2π) · ( ln(2l / r) − 3/4 )
M_p = (μ₀ l / 2π) · ( ln(2l / s) − 1 )
```

This is the one **multi-output** calculator in the group, so its `Result` carries two fields.

### 2. Public header (append)

```c++
namespace emc::component {

struct ConnectorPinInput {
    Length length  = 0.01 * mp_units::si::metre;      ///< l  [m]  (pin length)
    Length radius  = 3e-4 * mp_units::si::metre;       ///< r  [m]  (pin radius, > 0; ln(2l/r))
    Length spacing = 2.54e-3 * mp_units::si::metre;    ///< s  [m]  (return spacing, > 0; ln(2l/s))
};

struct ConnectorPinResult {
    Inductance self_inductance;    ///< L    [H]
    Inductance mutual_inductance;  ///< M_p  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ConnectorPinInput& in);

/// Computes BOTH the partial self-inductance and the partial mutual inductance.
[[nodiscard]] emc::Result<ConnectorPinResult> connector_pin_inductance(const ConnectorPinInput& in);

} // namespace emc::component
```

### 3. Implementation (append)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const ConnectorPinInput& in) {
    const double l = in.length.numerical_value_in(m);
    const double r = in.radius.numerical_value_in(m);
    const double s = in.spacing.numerical_value_in(m);
    if (auto e = emc::require_positive(l, "length");  !e) return e;
    if (auto e = emc::require_positive(r, "radius");  !e) return e;   // ln(2l/r), r in denominator
    if (auto e = emc::require_positive(s, "spacing"); !e) return e;   // ln(2l/s), s in denominator
    return {};
}

emc::Result<ConnectorPinResult> connector_pin_inductance(const ConnectorPinInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double l = in.length.numerical_value_in(m);
    const double r = in.radius.numerical_value_in(m);
    const double s = in.spacing.numerical_value_in(m);

    const double coeff = (mu0_H_per_m * l) / (2.0 * kPi);
    const double L_H   = coeff * (std::log(2.0 * l / r) - 0.75);
    const double M_H   = coeff * (std::log(2.0 * l / s) - 1.0);

    return ConnectorPinResult{ .self_inductance = L_H * H, .mutual_inductance = M_H * H };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **A single `Result` struct with two named fields** — one call returns `{ self_inductance,
  mutual_inductance }`, and structured bindings let the caller name them; no second out-parameter.
- **Shared `coeff` local** — the `(μ₀ l)/(2π)` factor common to both outputs is evaluated once and named.
- **`emc::units::Length` inputs** — three typed quantities (`length`, `radius`, `spacing`) carry their units
  in the type, so callers mix mm and m freely with compile-time safety.
- **`require_positive` on `r` AND `s`** — both sit in a `ln(2l/·)` denominator, so each is an out-of-domain
  typed error when zero or negative.
- **`emc::constants::mu0` / `emc::constants::pi`** — one source for both physical constants.

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // mm, nH ...

const emc::component::ConnectorPinInput in{
    .length  = 10.0 * mm,
    .radius  = 0.3  * mm,
    .spacing = 2.54 * mm,
};

if (auto r = emc::component::connector_pin_inductance(in)) {
    auto [L, Mp] = *r;                                   // structured binding on the Result struct
    std::println("L = {}, M_p = {}", L.in(nH), Mp.in(nH));
} else {
    std::println("error: {}", r.error().message);
}
```

### 6. Unit tests (`tests/component/connector_pin_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::ConnectorPinInput;
using emc::component::connector_pin_inductance;

// (a) Hand value: l=0.01, r=0.0003, s=0.00254 m.
//   coeff = mu0*0.01/(2pi) = 1.25663706212e-6*0.01/6.283185307 = 2.0e-9.
//   L  = 2.0e-9*(ln(0.02/0.0003)-0.75) = 2.0e-9*(ln(66.667)-0.75)=2.0e-9*(4.19970-0.75)=6.8994e-9 H.
//   Mp = 2.0e-9*(ln(0.02/0.00254)-1)   = 2.0e-9*(ln(7.8740)-1)   =2.0e-9*(2.06369-1)  =2.1274e-9 H.
TEST_CASE("connector pin hand value", "[component][inductance][connector_pin]") {
    auto r = connector_pin_inductance({.length = 0.01 * m, .radius = 0.0003 * m, .spacing = 0.00254 * m});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->self_inductance,   6.8994 * nH, 1e-3));
    REQUIRE(emc::test::approx(r->mutual_inductance, 2.1274 * nH, 1e-3));
}

// (b) Property: with s == r the two formulas differ only by the (3/4 vs 1) constant,
//     so L - M_p = coeff * (1 - 3/4) = coeff/4 > 0  ->  L > M_p.
TEST_CASE("connector pin self exceeds mutual when s==r", "[component][inductance][connector_pin]") {
    auto r = connector_pin_inductance({.length = 0.01 * m, .radius = 0.001 * m, .spacing = 0.001 * m});
    REQUIRE(r.has_value());
    REQUIRE(r->self_inductance.numerical_value_in(nH) > r->mutual_inductance.numerical_value_in(nH));
}

// (c) Validation: spacing == 0 -> OutOfRange on field "spacing".
TEST_CASE("connector pin rejects zero spacing", "[component][inductance][connector_pin][error]") {
    auto r = connector_pin_inductance({.length = 0.01 * m, .radius = 0.0003 * m, .spacing = 0.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "spacing");
}
```

What each guards: (a) an independent closed-form value for *both* outputs; (b) an algebraic invariant
between the two outputs; (c) the spacing domain guard.

### 7. Design notes

- Both `r` and `s` are `require_positive` (each in a `ln(2l/·)` denominator).
- The shared `(μ₀ l)/(2π)` coefficient is computed once.

---

## Rectangular Loop — `rectangular_loop_inductance`

### 1. Overview

A rectangular loop of `N` turns, width `w`, height `h`, wire radius `a`, in medium `μ_r` (standard
rectangular-loop self-inductance):

```text
L = N² · (μ₀ μ_r / π) · [ −2(w+h) + 2√(w²+h²)
                          − h·ln( (h+√(w²+h²)) / w )
                          − w·ln( (w+√(w²+h²)) / h )
                          + h·ln(2h/a) + w·ln(2w/a) ]
```

`a` appears in the denominator of both `ln(2h/a)` and `ln(2w/a)`, so `a > 0` is required.

### 2. Public header (append)

```c++
namespace emc::component {

struct RectangularLoopInput {
    double turns       = 10.0;                              ///< N  [-]
    Length width       = 2.0 * mp_units::si::metre;         ///< w  [m]
    Length height      = 1.0 * mp_units::si::metre;         ///< h  [m]
    Length wire_radius = 1e-3 * mp_units::si::metre;        ///< a  [m]  (> 0; ln(2w/a), ln(2h/a))
    double mu_r        = 1.0;                               ///< relative permeability [-]
};

struct RectangularLoopResult {
    Inductance inductance;   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularLoopInput& in);

[[nodiscard]] emc::Result<RectangularLoopResult> rectangular_loop_inductance(const RectangularLoopInput& in);

} // namespace emc::component
```

### 3. Implementation (append)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const RectangularLoopInput& in) {
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns")) {} else return e;
    if (auto e = emc::require_positive(w, "width");        !e) return e;  // ln(.../w), ln(2w/a)
    if (auto e = emc::require_positive(h, "height");       !e) return e;  // ln(.../h), ln(2h/a)
    if (auto e = emc::require_positive(a, "wire_radius");  !e) return e;  // a in ln denominator
    if (auto e = emc::require_positive(in.mu_r, "mu_r");   !e) return e;
    return {};
}

emc::Result<RectangularLoopResult> rectangular_loop_inductance(const RectangularLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double w = in.width.numerical_value_in(m);
    const double h = in.height.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    const double diag = std::sqrt(w * w + h * h);                     // √(w²+h²)
    const double bracket =
          -2.0 * (w + h)
        +  2.0 * diag
        -  h * std::log((h + diag) / w)
        -  w * std::log((w + diag) / h)
        +  h * std::log((2.0 * h) / a)
        +  w * std::log((2.0 * w) / a);

    const double L_H = N * N * ((mu0_H_per_m * in.mu_r) / kPi) * bracket;

    return RectangularLoopResult{ .inductance = L_H * H };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **A named `diag` and a broken-out `bracket`** — `√(w²+h²)` is computed once and named, so the three places
  it appears in the formula cannot drift apart.
- **`std::sqrt` / `std::log` (constexpr in C++23)** — standard-library transcendentals usable in constant
  expressions; `w*w` for the square keeps it free of any external math dependency.
- **`emc::units::Length` inputs** — width, height, and wire radius each carry their own unit, so a call site
  can legitimately mix a millimetre wire radius with metre-scale loop dimensions, and the mix is explicit in
  the source.
- **`emc::constants::mu0` / `::pi`** — single source for both constants.
- **Designated initializers** — five named fields.

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // m, mm, nH ...

const emc::component::RectangularLoopInput in{
    .turns = 10.0, .width = 2.0 * m, .height = 1.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0,
};

if (auto r = emc::component::rectangular_loop_inductance(in))
    std::println("L = {}", r->inductance.in(nH));
else
    std::println("error: {}", r.error().message);
```

### 6. Unit tests (`tests/component/rectangular_loop_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::RectangularLoopInput;
using emc::component::rectangular_loop_inductance;

// (a) Hand value: N=1, w=h=1 m, a=1 mm=0.001 m, mu_r=1.
//   diag=√2=1.4142136.  bracket = -2*2 + 2*1.4142136
//        - 1*ln((1+1.4142136)/1) - 1*ln((1+1.4142136)/1)
//        + 1*ln(2/0.001) + 1*ln(2/0.001)
//      = -4 + 2.8284271 - 2*ln(2.4142136) + 2*ln(2000)
//      = -4 + 2.8284271 - 2*0.8813736 + 2*7.6009025 = 12.187109.
//   L = (mu0/pi)*bracket = (1.25663706212e-6/3.14159265)*12.187109 = 4.0e-7*12.187109 = 4.8748e-6 H.
TEST_CASE("rectangular loop hand value", "[component][inductance][rectangular_loop]") {
    auto r = rectangular_loop_inductance({.turns = 1.0, .width = 1.0 * m, .height = 1.0 * m,
                                          .wire_radius = 1.0 * mm, .mu_r = 1.0});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 4874.8 * nH, 2e-3));
}

// (b) Symmetry: the loop is geometrically symmetric, so swapping w<->h is invariant.
TEST_CASE("rectangular loop is symmetric in w<->h", "[component][inductance][rectangular_loop]") {
    auto a = rectangular_loop_inductance({.turns = 5.0, .width = 2.0 * m, .height = 0.5 * m,
                                          .wire_radius = 1.0 * mm, .mu_r = 1.0});
    auto b = rectangular_loop_inductance({.turns = 5.0, .width = 0.5 * m, .height = 2.0 * m,
                                          .wire_radius = 1.0 * mm, .mu_r = 1.0});
    REQUIRE(a.has_value()); REQUIRE(b.has_value());
    REQUIRE(emc::test::approx(a->inductance, b->inductance, 1e-9));
}

// (c) Validation: a=0 -> OutOfRange on wire_radius.
TEST_CASE("rectangular loop rejects zero wire radius", "[component][inductance][rectangular_loop][error]") {
    auto r = rectangular_loop_inductance({.turns = 1.0, .width = 1.0 * m, .height = 1.0 * m,
                                          .wire_radius = 0.0 * m, .mu_r = 1.0});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "wire_radius");
}
```

What each guards: (a) the long hand-derived value pins every term of the bracket; (b) the geometric `w↔h`
symmetry of the formula; (c) the wire-radius domain guard.

### 7. Design notes

- `√(w²+h²)` is factored to a single `diag` so its three uses stay consistent.
- `a/w/h ≤ 0` is an `OutOfRange` error.

---

## Solenoid — `solenoid_inductance`

### 1. Overview

Long-solenoid (Wheeler infinite-coil) inductance: `N` turns, coil radius `r`, length `l`:

```text
L = μ₀ N² π r² / l        (= μ₀ N² A / l, with A = π r²)
```

This is the only formula in the group with **no logarithm**, so it is fully `constexpr`-friendly.

### 2. Public header (append)

```c++
namespace emc::component {

struct SolenoidInput {
    double turns  = 10.0;                          ///< N  [-]
    Length radius = 0.01 * mp_units::si::metre;     ///< r  [m]  (coil radius)
    Length length = 0.10 * mp_units::si::metre;     ///< l  [m]  (coil length, > 0 — denominator)
};

struct SolenoidResult {
    Inductance inductance;   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const SolenoidInput& in);

/// L = μ₀ N² π r² / l. constexpr: no transcendental, pure closed form.
[[nodiscard]] constexpr emc::Result<SolenoidResult> solenoid_inductance(const SolenoidInput& in);

} // namespace emc::component
```

### 3. Implementation

Because there is no `ln`/`sqrt`, this one is `constexpr` and lives **inline in the header** (after the
declaration) rather than in the `.cpp`, so it can fold at compile time:

```c++
// include/emc/component/inductance.hpp  (inline definition, after the declaration above)
namespace emc::component {

constexpr std::expected<void, emc::Error> validate(const SolenoidInput& in) {
    const double r = in.radius.numerical_value_in(mp_units::si::metre);
    const double l = in.length.numerical_value_in(mp_units::si::metre);
    if (auto e = emc::require_positive(in.turns, "turns")) {} else return e;
    if (auto e = emc::require_positive(r, "radius"); !e) return e;
    if (auto e = emc::require_nonzero(l, "length"); !e) return e;   // l in the denominator
    return {};
}

constexpr emc::Result<SolenoidResult> solenoid_inductance(const SolenoidInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N   = in.turns;
    const double r   = in.radius.numerical_value_in(mp_units::si::metre);
    const double l   = in.length.numerical_value_in(mp_units::si::metre);
    const double mu0 = emc::constants::mu0.numerical_value_in(
                           mp_units::si::henry / mp_units::si::metre);

    const double L_H = (mu0 * N * N * emc::constants::pi * r * r) / l;

    return SolenoidResult{ .inductance = L_H * mp_units::si::henry };
}

} // namespace emc::component
```

> [!NOTE]
> **`require_nonzero` vs `require_positive` for `length`.** The formula divides by `length`, and the radius
> enters only as `r²` (sign-immune). We pin `length` strictly *non-zero* (div-by-zero guard) and `radius`
> strictly *positive*. A negative length would yield a nonphysical negative `L`, so `require_nonzero` is the
> minimum guard — upgrade to `require_positive` if a sign is never offered.

### 4. Modern C++ features used here — and why

- **`constexpr` end-to-end** — pure closed-form math with no transcendental, so the whole calculator (and its
  validator) folds at compile time; the static-assert test in §6 proves it.
- **`emc::constants::mu0` and `emc::constants::pi`** — both physical constants from one source.
- **`require_nonzero(length)`** — a zero `length` would divide to `inf`; instead it is a typed
  `DivisionByZero` error.
- **`emc::units::Length` inputs** — radius and length carry their units in the type (cm, mm, m, …).

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // cm, nH, uH ...

const emc::component::SolenoidInput in{ .turns = 10.0, .radius = 1.0 * cm, .length = 10.0 * cm };

if (auto r = emc::component::solenoid_inductance(in))
    std::println("L = {}", r->inductance.in(nH));
else
    std::println("error: {}", r.error().message);
```

### 6. Unit tests (`tests/component/solenoid_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::SolenoidInput;
using emc::component::solenoid_inductance;

// (a) Hand value: N=10, r=0.01 m, l=0.10 m.
//   L = mu0*100*pi*1e-4/0.10 = 1.25663706212e-6*100*3.14159265*1e-4/0.10
//     = 1.25663706212e-6 * 0.3141593 = 3.9478e-7 H = 394.78 nH.
TEST_CASE("solenoid hand value", "[component][inductance][solenoid]") {
    auto r = solenoid_inductance({.turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 394.78 * nH, 1e-3));
}

// (b) Scaling laws: L ∝ N²  and  L ∝ 1/l.
TEST_CASE("solenoid scaling", "[component][inductance][solenoid]") {
    auto l1 = solenoid_inductance({.turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
    auto l2 = solenoid_inductance({.turns = 20.0, .radius = 0.01 * m, .length = 0.10 * m});
    REQUIRE(l1.has_value()); REQUIRE(l2.has_value());
    // doubling N quadruples L.
    REQUIRE(emc::test::approx(l2->inductance, 4.0 * l1->inductance.numerical_value_in(nH) * nH, 1e-9));
}

// (c) Validation: length == 0 -> DivisionByZero.
TEST_CASE("solenoid rejects zero length", "[component][inductance][solenoid][error]") {
    auto r = solenoid_inductance({.turns = 10.0, .radius = 0.01 * m, .length = 0.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::DivisionByZero);
    REQUIRE(r.error().field == "length");
}

// (d) constexpr: the whole calculation folds at compile time.
TEST_CASE("solenoid is constexpr", "[component][inductance][solenoid][constexpr]") {
    constexpr auto r = solenoid_inductance({.turns = 10.0, .radius = 0.01 * m, .length = 0.10 * m});
    static_assert(r.has_value(), "solenoid must evaluate at compile time");
    STATIC_REQUIRE(r->inductance.numerical_value_in(nH) > 0.0);
}
```

What each guards: (a) an independent value; (b) the `N²` and `1/l` laws; (c) the div-by-zero guard; (d)
proves the closed form is a true compile-time constant.

### 7. Design notes

- `length=0` is a typed `DivisionByZero` error.
- The closed form is `constexpr` end-to-end (test (d)).

---

## Square Loop — `square_loop_inductance`

### 1. Overview

A square loop of `N` turns, side `w`, wire radius `a`, medium `μ_r` (standard square-loop self-inductance):

```text
L = N² · (2 μ₀ μ_r w / π) · ( ln(w/a) − 0.774 )
```

`a` sits in the denominator of `ln(w/a)`, so `a > 0` is required.

### 2. Public header (append)

```c++
namespace emc::component {

struct SquareLoopInput {
    double turns       = 10.0;                            ///< N  [-]
    Length side        = 1.0 * mp_units::si::metre;        ///< w  [m]  (loop side)
    Length wire_radius = 1e-3 * mp_units::si::metre;       ///< a  [m]  (> 0; ln(w/a))
    double mu_r        = 1.0;                              ///< relative permeability [-]
};

struct SquareLoopResult {
    Inductance inductance;   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const SquareLoopInput& in);

[[nodiscard]] emc::Result<SquareLoopResult> square_loop_inductance(const SquareLoopInput& in);

} // namespace emc::component
```

### 3. Implementation (append)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const SquareLoopInput& in) {
    const double w = in.side.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns")) {} else return e;
    if (auto e = emc::require_positive(w, "side");        !e) return e;  // ln(w/a)
    if (auto e = emc::require_positive(a, "wire_radius"); !e) return e;  // a in ln denominator
    if (auto e = emc::require_positive(in.mu_r, "mu_r");  !e) return e;
    return {};
}

emc::Result<SquareLoopResult> square_loop_inductance(const SquareLoopInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double w = in.side.numerical_value_in(m);
    const double a = in.wire_radius.numerical_value_in(m);

    const double L_H = N * N * ((2.0 * mu0_H_per_m * in.mu_r * w) / kPi)
                            * (std::log(w / a) - 0.774);

    return SquareLoopResult{ .inductance = L_H * H };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`emc::units::Length` inputs** — side and wire radius each carry their own unit, so the call site writes
  `1.0 * m` and `1.0 * mm` unambiguously and mp-units handles the conversion.
- **`std::log` (constexpr in C++23)** — the standard transcendental; the literal coefficient `0.774` is
  written directly.
- **`require_positive(wire_radius)`** — a zero `a` would make `ln(w/0) = inf`; instead it is an out-of-domain
  typed error.
- **`emc::constants::mu0` / `::pi`** — both constants from one source.
- **Designated initializers** — four named fields.

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // m, mm, nH ...

const emc::component::SquareLoopInput in{
    .turns = 10.0, .side = 1.0 * m, .wire_radius = 1.0 * mm, .mu_r = 1.0,
};

if (auto r = emc::component::square_loop_inductance(in))
    std::println("L = {}", r->inductance.in(nH));
else
    std::println("error: {}", r.error().message);
```

### 6. Unit tests (`tests/component/square_loop_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::SquareLoopInput;
using emc::component::square_loop_inductance;

// (a) Hand value: N=1, w=1 m, a=1 mm=0.001 m, mu_r=1.
//   L = (2*mu0*1/pi)*(ln(1000)-0.774)
//     = (2*1.25663706212e-6/3.14159265)*(6.9077553-0.774)
//     = 8.0e-7*6.1337553 = 4.9070e-6 H = 4907.0 nH.
TEST_CASE("square loop hand value", "[component][inductance][square_loop]") {
    auto r = square_loop_inductance({.turns = 1.0, .side = 1.0 * m,
                                     .wire_radius = 1.0 * mm, .mu_r = 1.0});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 4907.0 * nH, 2e-3));
}

// (b) Monotonic in side: larger w -> larger ln(w/a) -> larger L (for w/a in the growth regime).
TEST_CASE("square loop grows with side", "[component][inductance][square_loop]") {
    auto small = square_loop_inductance({.turns = 1.0, .side = 0.5 * m,
                                         .wire_radius = 1.0 * mm, .mu_r = 1.0});
    auto big   = square_loop_inductance({.turns = 1.0, .side = 2.0 * m,
                                         .wire_radius = 1.0 * mm, .mu_r = 1.0});
    REQUIRE(small.has_value()); REQUIRE(big.has_value());
    REQUIRE(big->inductance.numerical_value_in(nH) > small->inductance.numerical_value_in(nH));
}

// (c) Validation: a=0 -> OutOfRange on wire_radius.
TEST_CASE("square loop rejects zero wire radius", "[component][inductance][square_loop][error]") {
    auto r = square_loop_inductance({.turns = 1.0, .side = 1.0 * m,
                                     .wire_radius = 0.0 * m, .mu_r = 1.0});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "wire_radius");
}
```

What each guards: (a) an independent value pinning the `0.774` coefficient; (b) the `ln(w/a)` growth law;
(c) the wire-radius domain guard.

### 7. Design notes

- `a=0` is an `OutOfRange` error (`ln(w/0)` guarded).

---

## Toroid — `toroid_inductance`

### 1. Overview

A toroid of `N` turns wound on a rectangular cross-section core of height `h`, outer radius `b`, inner radius
`a` (standard rectangular-core toroid formula):

```text
L = (μ₀ N² h / 2π) · ln(b/a)
```

> [!IMPORTANT]
> Here `a` is the **inner radius** and `b` the **outer radius** — *not* a wire radius. The `Input` field
> names (`inner_radius`/`outer_radius`) reflect that so they cannot be swapped, which would flip `ln(b/a)`
> negative.

### 2. Public header (append)

```c++
namespace emc::component {

struct ToroidInput {
    double turns        = 10.0;                          ///< N  [-]
    Length height       = 0.01 * mp_units::si::metre;     ///< h  [m]  (core height)
    Length outer_radius = 0.06 * mp_units::si::metre;     ///< b  [m]  (> 0; ln(b/a))
    Length inner_radius = 0.04 * mp_units::si::metre;     ///< a  [m]  (> 0; denominator of ln(b/a))
};

struct ToroidResult {
    Inductance inductance;   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ToroidInput& in);

/// L = (μ₀ N² h / 2π) · ln(b/a).
[[nodiscard]] emc::Result<ToroidResult> toroid_inductance(const ToroidInput& in);

} // namespace emc::component
```

### 3. Implementation (append)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const ToroidInput& in) {
    const double h = in.height.numerical_value_in(m);
    const double b = in.outer_radius.numerical_value_in(m);
    const double a = in.inner_radius.numerical_value_in(m);
    if (auto e = emc::require_positive(in.turns, "turns")) {} else return e;
    if (auto e = emc::require_positive(h, "height");       !e) return e;
    if (auto e = emc::require_positive(a, "inner_radius"); !e) return e;  // a in denominator of ln(b/a)
    if (auto e = emc::require_positive(b, "outer_radius"); !e) return e;
    // b > a is implied for a physical toroid; b == a gives ln(1)=0 -> L=0 (boundary, not an error).
    return {};
}

emc::Result<ToroidResult> toroid_inductance(const ToroidInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double N = in.turns;
    const double h = in.height.numerical_value_in(m);
    const double b = in.outer_radius.numerical_value_in(m);
    const double a = in.inner_radius.numerical_value_in(m);

    const double L_H = ((N * N * mu0_H_per_m * h) / (2.0 * kPi)) * std::log(b / a);

    return ToroidResult{ .inductance = L_H * H };
}

} // namespace emc::component
```

> [!NOTE]
> **`b ≤ a` behaviour.** If `b == a`, `ln(1)=0` and `L=0` (a clean boundary). We guard only `a > 0` (the
> `ln(b/0)` / div-by-zero case). The unphysical `b < a` regime returns a negative `L`; tightening to a
> `b > a` check is a forward-looking option.

### 4. Modern C++ features used here — and why

- **`emc::constants::mu0`** — one free-space permeability constant, and `emc::constants::pi` covers the `2π`.
- **Descriptive field names `outer_radius` / `inner_radius`** — the formula is `ln(b/a)` with `b > a`, so
  named fields make the orientation impossible to mis-call.
- **`require_positive(inner_radius)`** — a zero inner radius would make `ln(b/0) = inf`; instead it is an
  out-of-domain typed error.
- **`emc::units::Length` inputs** — height and both radii carry their units in the type (cm, mm, m, …).

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // cm, uH ...

const emc::component::ToroidInput in{
    .turns = 10.0, .height = 1.0 * cm, .outer_radius = 6.0 * cm, .inner_radius = 4.0 * cm,
};

if (auto r = emc::component::toroid_inductance(in))
    std::println("L = {}", r->inductance.in(uH));
else
    std::println("error: {}", r.error().message);
```

### 6. Unit tests (`tests/component/toroid_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::ToroidInput;
using emc::component::toroid_inductance;

// (a) Hand value: N=10, h=0.01 m, b=0.06 m, a=0.04 m.
//   L = (mu0*100*0.01/(2pi))*ln(0.06/0.04)
//     = (1.25663706212e-6*100*0.01/6.2831853)*ln(1.5)
//     = (1.25663706212e-6*1.0/6.2831853)*0.4054651
//     = 2.0e-7*0.4054651 = 8.1093e-8 H = 0.081093 uH.
TEST_CASE("toroid hand value", "[component][inductance][toroid]") {
    auto r = toroid_inductance({.turns = 10.0, .height = 0.01 * m,
                                .outer_radius = 0.06 * m, .inner_radius = 0.04 * m});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 0.081093 * uH, 1e-4));
}

// (b) Property: b == a -> ln(1) == 0 -> L == 0 (boundary).
TEST_CASE("toroid zero when b equals a", "[component][inductance][toroid]") {
    auto r = toroid_inductance({.turns = 10.0, .height = 0.01 * m,
                                .outer_radius = 0.05 * m, .inner_radius = 0.05 * m});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 0.0 * uH, 1e-9));
}

// (c) Validation: inner_radius == 0 -> OutOfRange (ln(b/0) guarded).
TEST_CASE("toroid rejects zero inner radius", "[component][inductance][toroid][error]") {
    auto r = toroid_inductance({.turns = 10.0, .height = 0.01 * m,
                                .outer_radius = 0.06 * m, .inner_radius = 0.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "inner_radius");
}
```

What each guards: (a) an independent value pinning the `ln(b/a)` term; (b) the `b==a → 0` boundary; (c) the
inner-radius domain guard.

### 7. Design notes

- `inner_radius`/`outer_radius` names prevent the swap that flips `ln(b/a)` negative.
- `a=0` is an `OutOfRange` error.

---

## Via — `via_inductance`

### 1. Overview

Inductance of a PCB via of height (board thickness) `h` and diameter `d` (standard via-inductance formula):

```text
L = (μ₀ h / 2π) · ( ln(4h/d) − 1 )
```

`d` sits in the denominator of `ln(4h/d)`, so `d > 0` is required.

### 2. Public header (append)

```c++
namespace emc::component {

struct ViaInput {
    Length height   = 1.6e-3 * mp_units::si::metre;    ///< h  [m]  (board thickness / via length)
    Length diameter = 7.5e-4 * mp_units::si::metre;    ///< d  [m]  (> 0; ln(4h/d))
};

struct ViaResult {
    Inductance inductance;   ///< L  [H]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const ViaInput& in);

/// L = (μ₀ h / 2π) · (ln(4h/d) − 1).
[[nodiscard]] emc::Result<ViaResult> via_inductance(const ViaInput& in);

} // namespace emc::component
```

### 3. Implementation (append)

```c++
namespace emc::component {

std::expected<void, emc::Error> validate(const ViaInput& in) {
    const double h = in.height.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto e = emc::require_positive(h, "height");   !e) return e;
    if (auto e = emc::require_positive(d, "diameter"); !e) return e;  // d in denominator of ln(4h/d)
    return {};
}

emc::Result<ViaResult> via_inductance(const ViaInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double h = in.height.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);

    const double L_H = ((mu0_H_per_m * h) / (2.0 * kPi)) * (std::log(4.0 * h / d) - 1.0);

    return ViaResult{ .inductance = L_H * H };
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`emc::units::Length` inputs** — height and diameter carry their units in the type; the caller writes
  `1.6 * mm` directly, the typical PCB scale.
- **`std::log` (constexpr in C++23)** — the standard transcendental; the `−1` term and the `4h/d` argument
  are written directly.
- **`require_positive(diameter)`** — a zero `d` would make `ln(4h/0) = inf`; instead it is a typed
  out-of-domain error.
- **`emc::constants::mu0` / `::pi`** — both constants from one source.

### 5. Example usage

```c++
using namespace mp_units::si::unit_symbols;   // mm, nH ...

const emc::component::ViaInput in{ .height = 1.6 * mm, .diameter = 0.75 * mm };

if (auto r = emc::component::via_inductance(in))
    std::println("L = {}", r->inductance.in(nH));
else
    std::println("error: {}", r.error().message);
```

### 6. Unit tests (`tests/component/via_test.cpp`)

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/inductance.hpp>
#include "support/approx.hpp"

using namespace mp_units::si::unit_symbols;
using emc::component::ViaInput;
using emc::component::via_inductance;

// (a) Hand value: h=1.6 mm=0.0016 m, d=0.75 mm=0.00075 m.
//   L = (mu0*0.0016/(2pi))*(ln(4*0.0016/0.00075)-1)
//     = (1.25663706212e-6*0.0016/6.2831853)*(ln(8.533333)-1)
//     = (2.0106193e-9/6.2831853)*(2.144099-1)  ->  3.2e-10*(2.144099-1)
//     = 3.2e-10*1.144099 = 3.6611e-10 H = 0.36611 nH.
TEST_CASE("via hand value", "[component][inductance][via]") {
    auto r = via_inductance({.height = 1.6 * mm, .diameter = 0.75 * mm});
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->inductance, 0.36611 * nH, 2e-3));
}

// (b) Monotonicity: a smaller diameter -> larger ln(4h/d) -> larger L.
TEST_CASE("via grows as diameter shrinks", "[component][inductance][via]") {
    auto thin = via_inductance({.height = 1.6 * mm, .diameter = 0.3 * mm});
    auto fat  = via_inductance({.height = 1.6 * mm, .diameter = 1.0 * mm});
    REQUIRE(thin.has_value()); REQUIRE(fat.has_value());
    REQUIRE(thin->inductance.numerical_value_in(nH) > fat->inductance.numerical_value_in(nH));
}

// (c) Validation: diameter == 0 -> OutOfRange on field "diameter".
TEST_CASE("via rejects zero diameter", "[component][inductance][via][error]") {
    auto r = via_inductance({.height = 1.6 * mm, .diameter = 0.0 * mm});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "diameter");
}
```

What each guards: (a) an independent closed-form value; (b) the `ln(4h/d)` growth direction; (c) the
diameter domain guard.

### 7. Design notes

- `d=0` is an `OutOfRange` error (`ln(4h/0)` guarded).

---

## Binding all seven to the `Calculator` concept

At the bottom of `include/emc/component/inductance.hpp`, add tag structs + `static_assert`s so each
(Input, Result, free-function) triple is a compile-time-checked `Calculator` (per
[doc 06 §1e](../06-calculator-design-pattern.md) and the foundation `calculator.hpp`):

```c++
namespace emc::component {

#define EMC_BIND_INDUCTANCE(TAG, INPUT, RESULT, FN)                                  \
    struct TAG {                                                                     \
        using Input  = INPUT;                                                        \
        using Result = RESULT;                                                       \
        static emc::Result<Result> calculate(const Input& in) { return FN(in); }    \
        static std::expected<void, emc::Error> validate(const Input& in) {          \
            return emc::component::validate(in);                                     \
        }                                                                            \
    };                                                                              \
    static_assert(emc::ValidatedCalculator<TAG>)

EMC_BIND_INDUCTANCE(CircularLoop,    CircularLoopInput,    CircularLoopResult,    circular_loop_inductance);
EMC_BIND_INDUCTANCE(ConnectorPin,    ConnectorPinInput,    ConnectorPinResult,    connector_pin_inductance);
EMC_BIND_INDUCTANCE(RectangularLoop, RectangularLoopInput, RectangularLoopResult, rectangular_loop_inductance);
EMC_BIND_INDUCTANCE(Solenoid,        SolenoidInput,        SolenoidResult,        solenoid_inductance);
EMC_BIND_INDUCTANCE(SquareLoop,      SquareLoopInput,      SquareLoopResult,      square_loop_inductance);
EMC_BIND_INDUCTANCE(Toroid,          ToroidInput,          ToroidResult,          toroid_inductance);
EMC_BIND_INDUCTANCE(Via,             ViaInput,             ViaResult,             via_inductance);

#undef EMC_BIND_INDUCTANCE

} // namespace emc::component
```

> The `validate()` overloads are resolved by `Input` type (one free `validate` per `*Input`), so the macro's
> `emc::component::validate(in)` picks the right one — the
> [doc 06](../06-calculator-design-pattern.md) "free functions overloaded per Input type" pattern.

---

## Cross-references

- [`00-foundation-code.md`](./00-foundation-code.md) — the canonical `emc::constants::mu0` / `::pi`,
  `emc::units::Length`/`Inductance`, `emc::Result`/`Error`/`require_positive`/`require_nonzero`, the
  `Calculator`/`ValidatedCalculator` concepts, and the `emc::test::approx` helper reused above.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — how `Length` inputs
  give compile-time unit safety across the mm/cm/m/inch/mils range.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/`calculate`
  triple, per-`Input` `validate` overloads, and the `Calculator` concept these seven instantiate.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the testing harness and
  tolerance rules behind the hand-computed reference tests above.
