# Implementation Guide — Cabling (`emc::cabling`) 🔌

Complete, copy-paste-quality C++23 code (header + `.cpp` + example + Catch2 v3 tests) for the two
cable-shielding calculators, built on the pinned foundation in
[`00-foundation-code.md`](00-foundation-code.md).

This guide covers the two math-bearing leaf calculators in the cabling group:

| Calculator | Namespace | Header | Impl | Test |
|---|---|---|---|---|
| Cable Braid Optical Coverage (OC%) | `emc::cabling` | `include/emc/cabling/braid_coverage.hpp` | `src/cabling/braid_coverage.cpp` | `tests/cabling/braid_coverage_test.cpp` |
| Crosstalk NEXT/FEXT (V_NE / V_FE) | `emc::cabling` | `include/emc/cabling/crosstalk.hpp` | `src/cabling/crosstalk.cpp` | `tests/cabling/crosstalk_test.cpp` |

Both are **forward-only** calculators (no inverse solve). The braid calculator is a pure closed-form
geometric ratio; the crosstalk calculator is a two-output (near-end + far-end) lumped-element model with a
**documented clamp** (`V_FE → −200 dB` when the far-end argument is non-positive), modeled as an explicit,
named behavior rather than a silent magic number.

Each section uses the foundation surface verbatim: `emc::Result<T>`, `emc::in_range` /
`emc::require_positive` / `emc::require_nonzero`, `emc::constants::pi`, the `emc::units::*` quantity
aliases, the `emc::units::Decibel` log wrapper, and the `emc::test::approx` helper.

---

## Cable Braid Optical Coverage — `emc::cabling::braid_optical_coverage`

### 1. Overview

Optical coverage (**OC%**) is the fraction of a cable's circumference physically covered by the metallic
braid shield — the single most useful first-order predictor of a braided shield's effectiveness. It is a
**pure geometric closed-form**: given the strand (wire) diameter `d`, the braid outer diameter `D`, the
number of picks per unit length `P`, the number of carriers `C`, and the number of strands (ends) per
carrier `N`, the weave angle and single-band fill combine into a coverage percentage.

The standard EMC braid-coverage formula:

```text
theta = atan( 2 * pi * (D + 2*d) * P / C )      // weave (braid) angle
F     = (P * N * d) / sin(theta)                // single-end fill factor (fraction)
OC    = 100 * (2*F - F^2)                        // optical coverage [%]
```

`OC = 100·(2F − F²)` is the two-band overlap expression `1 − (1 − F)²` scaled to a percentage — two
crossing bands each cover fraction `F`, and `(1 − F)²` is the probability a point is missed by both.

### 2. Public header — `include/emc/cabling/braid_coverage.hpp`

```c++
// include/emc/cabling/braid_coverage.hpp
#pragma once

#include <emc/core/calculator.hpp>   // emc::Calculator / static_assert tag
#include <emc/core/error.hpp>        // emc::Result, emc::Error
#include <emc/core/units.hpp>        // emc::units::Length, Dimensionless, Angle

namespace emc::cabling {

// ---------------------------------------------------------------------------
//  Input — braid geometry. Lengths are mp-units quantities (so the call site is
//  unit-explicit: 0.114 * mm, not a bare 0.000114 that *means* metres). Pick
//  density P, carrier count C and ends-per-carrier N are dimensionless counts.
//  Defaults give a realistic worked example (a typical RG-style braid).
// ---------------------------------------------------------------------------
struct BraidCoverageInput {
    emc::units::Length d = 0.000114 * emc::units::si::metre;   // strand (wire) diameter
    emc::units::Length D = 0.002950 * emc::units::si::metre;   // braid outer diameter
    double             P = 220.0;   // picks per unit length [1/m]
    double             C = 16.0;    // number of carriers [-]
    double             N = 5.8;     // strands (ends) per carrier [-]
};

// ---------------------------------------------------------------------------
//  Result — the coverage plus the intermediate weave angle and fill factor,
//  which are useful diagnostics. optical_coverage is a generic Dimensionless
//  ratio carrying the fraction (0..1); percent() is a convenience for printing.
// ---------------------------------------------------------------------------
struct BraidCoverageResult {
    emc::units::Dimensionless optical_coverage;  // fraction in [0, 1] (NOT yet *100)
    emc::units::Angle         weave_angle;        // theta [rad]
    double                    fill_factor;        // F [-]

    [[nodiscard]] double percent() const {
        return 100.0 * optical_coverage.numerical_value_in(emc::units::si::one);
    }
};

/// Validate braid geometry: all dimensions/counts must be strictly positive and
/// the carrier count C must be non-zero (it is a denominator inside atan()).
[[nodiscard]] std::expected<void, emc::Error> validate(const BraidCoverageInput& in);

/// Compute optical coverage from braid geometry (forward only).
/// theta = atan(2*pi*(D+2d)*P/C); F = P*N*d/sin(theta); OC = 2F - F^2.
[[nodiscard]] emc::Result<BraidCoverageResult> calculate(const BraidCoverageInput& in);

// ---- Calculator concept binding (compile-time contract, foundation §5) -------
struct BraidOpticalCoverage {
    using Input  = BraidCoverageInput;
    using Result = BraidCoverageResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::cabling::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::cabling::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<BraidOpticalCoverage>);

} // namespace emc::cabling
```

> [!NOTE]
> The canonical free-function name in [`07`](../07-calculator-inventory.md) is `braid_optical_coverage`.
> We expose the category-overloaded `calculate(const BraidCoverageInput&)` (so the `Calculator` concept
> binds) **and** a thin alias `braid_optical_coverage`; both forward to the same body.

### 3. Implementation — `src/cabling/braid_coverage.cpp`

```c++
// src/cabling/braid_coverage.cpp
#include <emc/cabling/braid_coverage.hpp>

#include <cmath>   // std::atan, std::sin (constexpr in C++23, but trig stays runtime here)

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::cabling {

using namespace mp_units;
using emc::units::si::metre;
using mp_units::one;

std::expected<void, emc::Error> validate(const BraidCoverageInput& in) {
    // Pull each length out in metres so any reported range is in the user's unit.
    const double d = in.d.numerical_value_in(metre);
    const double D = in.D.numerical_value_in(metre);

    // Every geometric quantity must be strictly positive (a zero strand diameter,
    // zero braid OD, or zero pick density is not a cable). require_positive also
    // rejects NaN (NaN > 0 is false).
    if (auto r = emc::require_positive(d, "d");            !r) return r;
    if (auto r = emc::require_positive(D, "D");            !r) return r;
    if (auto r = emc::require_positive(in.P, "P");         !r) return r;
    if (auto r = emc::require_positive(in.N, "N");         !r) return r;

    // C is a denominator inside atan(2*pi*(D+2d)*P / C): it must be non-zero AND,
    // being a physical carrier count, positive.
    if (auto r = emc::require_positive(in.C, "C");         !r) return r;

    return {};   // ok
}

emc::Result<BraidCoverageResult> calculate(const BraidCoverageInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Evaluate the geometry in coherent SI (metres). The formula is a pure ratio,
    // so working in one length unit keeps the arithmetic exact.
    const double d = in.d.numerical_value_in(metre);
    const double D = in.D.numerical_value_in(metre);
    const double P = in.P;
    const double C = in.C;
    const double N = in.N;

    // theta = atan( 2*pi*(D + 2d)*P / C )
    const double theta = std::atan(2.0 * emc::constants::pi * (D + 2.0 * d) * P / C);

    // sin(theta) is a denominator for F. atan() returns (-pi/2, pi/2); with all
    // inputs positive the argument is > 0, so theta in (0, pi/2) and sin(theta) > 0.
    // Guard anyway so a pathological input becomes a typed DivisionByZero, never inf.
    const double s = std::sin(theta);
    if (auto r = emc::require_nonzero(s, "sin(theta)"); !r)
        return std::unexpected(r.error());

    // F = (P * N * d) / sin(theta)
    const double F = (P * N * d) / s;

    // OC = 2F - F^2  (kept as a fraction here; percent() applies the *100).
    const double oc_fraction = 2.0 * F - F * F;

    return BraidCoverageResult{
        .optical_coverage = oc_fraction * one,
        .weave_angle      = theta * emc::units::si::radian,
        .fill_factor      = F,
    };
}

// Descriptive alias requested by the inventory (doc 07); same body.
emc::Result<BraidCoverageResult> braid_optical_coverage(const BraidCoverageInput& in) {
    return calculate(in);
}

} // namespace emc::cabling
```

> [!NOTE]
> We keep `2F − F²` as a *fraction* in the `Result` and apply `×100` only in `percent()`, so a test can
> compare either on the fraction (`×100` in the test) or via `percent()`. The arithmetic is identical
> either way.

### 4. Modern C++ features used here — and why

- **mp-units `Length` inputs (`d`, `D`)** — EMC braid dimensions span metres down to mils, so putting the
  unit in the type lets a call site write `0.114 * mm` and have mp-units convert, eliminating the silent
  "is this metres or millimetres?" foot-gun a bare `double` invites.
- **Designated-initializer `BraidCoverageInput`** — `{.d=…, .D=…, .P=…, .C=…, .N=…}` names every field,
  so the caller cannot transpose `C` and `D` the way a flat positional list invites.
- **`emc::constants::pi`** — one namespaced, full-precision pi removes any drift from ad-hoc
  truncated-pi definitions and the platform `M_PI` macro dependency.
- **`std::expected<void, Error>` validation + `[[nodiscard]]`** — braid inputs have physical domains, so
  an out-of-domain input (`C = 0` → `atan(inf) → π/2`, a zero `sin`) becomes a typed `OutOfRange` /
  `DivisionByZero` the caller must handle, rather than an `inf`/`nan` slipping into the result.
- **`Dimensionless` (quantity of dimension one) for the coverage ratio** — keeps `optical_coverage` as a
  ratio in `[0,1]` and forces the `×100` to be an explicit `percent()` call, removing the "is this 0.79
  or 79?" ambiguity a bare `OC` double carries.
- **Returning the intermediate `weave_angle` and `fill_factor`** — a struct result is free to surface
  diagnostics the engineer actually wants.

### 5. Example usage

```c++
#include <emc/cabling/braid_coverage.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;   // mm, m, etc.

    // A typical braid, written with unit literals + designated inits.
    const emc::cabling::BraidCoverageInput in{
        .d = 0.114 * mm,     // strand diameter (114 um)
        .D = 2.950 * mm,     // braid outer diameter
        .P = 220.0,          // picks per metre
        .C = 16.0,           // carriers
        .N = 5.8,            // ends per carrier
    };

    const auto r = emc::cabling::calculate(in);
    if (!r) {                                   // error arm
        std::println("braid error [{}]: {}",
                     emc::to_string(r.error().code), r.error().message);
        return 1;
    }

    // success arm — pull OC out as a percentage and the weave angle in degrees.
    std::println("optical coverage = {:.2f} %", r->percent());
    std::println("weave angle      = {:.2f} deg",
                 r->weave_angle.numerical_value_in(si::degree));   // ~15.4 deg
    std::println("fill factor F    = {:.4f}", r->fill_factor);
    // expected: optical coverage = 79.70 %, F = 0.5494
}
```

### 6. Unit tests — `tests/cabling/braid_coverage_test.cpp`

Expected values come from hand computation of the closed form; the suite adds a property/monotonicity
check and validation edge cases.

```c++
// tests/cabling/braid_coverage_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <emc/cabling/braid_coverage.hpp>
#include <emc/core/constants.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

// (a) KNOWN-VALUE TEST -- the worked example, hand-computed from the closed form:
//     theta=0.2679566, F=0.5494152, OC=79.69732946 %.
TEST_CASE("braid coverage worked example is 79.697 %", "[cabling][braid][known]") {
    const emc::cabling::BraidCoverageInput in{
        .d = 0.000114 * m, .D = 0.002950 * m, .P = 220.0, .C = 16.0, .N = 5.8};
    const auto r = emc::cabling::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->percent() * one, 79.69732946 * one, 1e-7));
    REQUIRE(emc::test::approx(r->fill_factor * one, 0.5494151518 * one, 1e-7));
    REQUIRE(emc::test::approx(r->weave_angle, 0.2679566261 * rad, 1e-7));
}

// (b) ROUND-TRIP / ORACLE TEST -- recompute the closed form independently for a
//     second geometry and confirm the implementation matches to machine epsilon.
TEST_CASE("braid coverage matches an independent closed-form oracle",
          "[cabling][braid][oracle]") {
    const emc::cabling::BraidCoverageInput in{
        .d = 0.000090 * m, .D = 0.004000 * m, .P = 180.0, .C = 24.0, .N = 7.0};
    const auto out = emc::cabling::calculate(in);
    REQUIRE(out.has_value());

    const double d = in.d.numerical_value_in(m), D = in.D.numerical_value_in(m);
    const double theta = std::atan(2.0 * emc::constants::pi * (D + 2.0 * d) * in.P / in.C);
    const double F = (in.P * in.N * d) / std::sin(theta);
    const double expected_oc = 100.0 * (2.0 * F - F * F);

    REQUIRE(emc::test::approx(out->percent() * one, expected_oc * one, 1e-9));
}

// (c) PROPERTY TEST -- OC = 2F - F^2 = 1 - (1-F)^2, so OC is monotone increasing
//     in F on F in [0,1]. Increasing the strand diameter d (which raises F) must
//     not DECREASE coverage in that regime.
TEST_CASE("braid coverage rises with strand diameter (in the physical regime)",
          "[cabling][braid][property]") {
    auto oc_for = [](double d_m) {
        return emc::cabling::calculate(
                   {.d = d_m * m, .D = 0.00295 * m, .P = 220.0, .C = 16.0, .N = 5.8})
            ->percent();
    };
    // Keep F < 1 so we stay on the monotone-increasing branch.
    REQUIRE(oc_for(0.00009) < oc_for(0.000114));
    REQUIRE(oc_for(0.000114) < oc_for(0.00013));
}

// (d) VALIDATION / EDGE TESTS -- the right ErrorCode for bad geometry.
TEST_CASE("braid coverage rejects bad geometry", "[cabling][braid][validation]") {
    SECTION("zero carrier count C is OutOfRange (it is a denominator)") {
        const auto r = emc::cabling::calculate(
            {.d = 0.000114 * m, .D = 0.00295 * m, .P = 220.0, .C = 0.0, .N = 5.8});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive failed
        REQUIRE(r.error().field == "C");
    }
    SECTION("non-positive strand diameter is OutOfRange") {
        const auto r = emc::cabling::calculate(
            {.d = 0.0 * m, .D = 0.00295 * m, .P = 220.0, .C = 16.0, .N = 5.8});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "d");
    }
}
```

> Test guards: **(a)** a hand-computed oracle pins absolute correctness of the closed form; **(b)** an
> independent recomputation on a second geometry confirms machine-epsilon agreement; **(c)** the physical
> monotonicity `OC↑` with `F↑` holds; **(d)** validation returns the correct typed code, including the
> `C = 0` denominator case.

### 7. Design notes

- **Validation is explicit.** `validate()` runs `require_positive` on `d, D, P, N, C` and a
  `require_nonzero` guard on `sin(theta)`, so a blank/zero `C` (which would make `atan(±inf) = ±π/2`) or a
  zero `sin(theta)` (which would produce `inf`/`nan`) is reported as a typed error instead of being
  written into the result.
- **Display `×100` lives outside the math.** `calculate()` stores the fraction `2F − F²`; `percent()`
  multiplies by 100 at the boundary, keeping the ratio unambiguous internally.

---

## Crosstalk NEXT/FEXT — `emc::cabling::crosstalk`

### 1. Overview

The crosstalk calculator estimates the **near-end (V_NE)** and **far-end (V_FE)** coupled voltages
between two parallel transmission lines using the classic weak-coupling lumped-element model: inductive
(mutual `L_m`) and capacitive (mutual `C_m`) coupling combine through the four termination resistances
(`R_L`, `R_S`, `R_NE`, `R_FE`) at angular frequency `ω = 2πf`, expressed in dB.

The standard NEXT/FEXT coupling formulas:

```text
A_NE = 2*pi*f * [ (R_NE/(R_NE+R_FE)) * L_m/(R_S+R_L)
                + (R_NE*R_FE/(R_NE+R_FE)) * R_L*C_m/(R_S+R_L) ]
V_NE = 20 * log10( A_NE )

A_FE = 2*pi*f * [ -(R_FE/(R_NE+R_FE)) * L_m/(R_S+R_L)          // note the sign flip
                + (R_NE*R_FE/(R_NE+R_FE)) * R_L*C_m/(R_S+R_L) ]
V_FE = (A_FE <= 0) ? -200 : 20 * log10( A_FE )                 // documented clamp
```

Near-end coupling **adds** the inductive and capacitive contributions; far-end **subtracts** them (the
inductive term flips sign), which is why `A_FE` can fall to (or below) zero and produce a `log10` of a
non-positive number. The model handles that case by **clamping `V_FE = −200 dB`** — a floor representing
"negligible far-end coupling / below the model's useful range." We surface this clamp as a named,
inspectable behavior.

### 2. Public header — `include/emc/cabling/crosstalk.hpp`

```c++
// include/emc/cabling/crosstalk.hpp
#pragma once

#include <emc/core/calculator.hpp>   // emc::Calculator tag
#include <emc/core/error.hpp>        // emc::Result, emc::Error
#include <emc/core/units.hpp>        // Frequency, Impedance, Inductance, Capacitance, Decibel

namespace emc::cabling {

// ---------------------------------------------------------------------------
//  The far-end clamp floor. When the far-end argument A_FE <= 0, log10 is
//  undefined; the model substitutes -200 dB. We name it so the value is
//  documented in ONE place and callers can detect a clamped result.
// ---------------------------------------------------------------------------
inline constexpr emc::units::Decibel kFarEndFloor{ -200.0 };

// ---------------------------------------------------------------------------
//  Input — all mp-units-typed. Frequency carries Hz (the call site writes
//  1 * MHz). L_m / C_m are typed Inductance / Capacitance (so a pF vs uF
//  mix-up cannot compile). The four resistances are Impedance (ohm).
//  Defaults: f=1 MHz, RL=RS=RNE=RFE=50, Lm=50 uH, Cm=50000 pF.
// ---------------------------------------------------------------------------
struct CrosstalkInput {
    emc::units::Frequency   f    = 1.0e6 * emc::units::si::hertz;       // 1 MHz
    emc::units::Impedance   R_L  = 50.0  * emc::units::si::ohm;
    emc::units::Impedance   R_S  = 50.0  * emc::units::si::ohm;
    emc::units::Impedance   R_NE = 50.0  * emc::units::si::ohm;
    emc::units::Impedance   R_FE = 50.0  * emc::units::si::ohm;
    emc::units::Inductance  L_m  = 50.0e-6 * emc::units::si::henry;     // 50 uH
    emc::units::Capacitance C_m  = 50000.0e-12 * emc::units::si::farad; // 50000 pF
};

// ---------------------------------------------------------------------------
//  Result — both coupled voltages as typed Decibel values, plus a flag telling
//  the caller V_FE was clamped to the floor (so a front end can annotate it).
// ---------------------------------------------------------------------------
struct CrosstalkResult {
    emc::units::Decibel V_NE;          // near-end coupled voltage [dB]
    emc::units::Decibel V_FE;          // far-end  coupled voltage [dB]
    bool                far_end_clamped = false;   // true => V_FE == kFarEndFloor
};

/// Validate: frequency, L_m, C_m strictly positive; (R_S + R_L) and
/// (R_NE + R_FE) non-zero (both are denominators).
[[nodiscard]] std::expected<void, emc::Error> validate(const CrosstalkInput& in);

/// Compute near-end and far-end crosstalk (forward only, two outputs).
/// Applies the documented V_FE = -200 dB clamp when the far-end argument <= 0.
[[nodiscard]] emc::Result<CrosstalkResult> calculate(const CrosstalkInput& in);

// ---- Calculator concept binding -------------------------------------------
struct Crosstalk {
    using Input  = CrosstalkInput;
    using Result = CrosstalkResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::cabling::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::cabling::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Crosstalk>);

} // namespace emc::cabling
```

### 3. Implementation — `src/cabling/crosstalk.cpp`

```c++
// src/cabling/crosstalk.cpp
#include <emc/cabling/crosstalk.hpp>

#include <cmath>   // std::log10

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::cabling {

using namespace mp_units;
using emc::units::si::hertz;
using emc::units::si::ohm;
using emc::units::si::henry;
using emc::units::si::farad;

std::expected<void, emc::Error> validate(const CrosstalkInput& in) {
    // Reduce each quantity to a plain SI magnitude for the numeric checks; any
    // reported range comes back in these units.
    const double f   = in.f.numerical_value_in(hertz);
    const double RL  = in.R_L.numerical_value_in(ohm);
    const double RS  = in.R_S.numerical_value_in(ohm);
    const double RNE = in.R_NE.numerical_value_in(ohm);
    const double RFE = in.R_FE.numerical_value_in(ohm);
    const double Lm  = in.L_m.numerical_value_in(henry);
    const double Cm  = in.C_m.numerical_value_in(farad);

    // Frequency and the coupling elements must be strictly positive: with f<=0 or
    // Lm<=0/Cm<=0 the log10 argument is non-positive and V_NE itself collapses.
    if (auto r = emc::require_positive(f,  "f");   !r) return r;
    if (auto r = emc::require_positive(Lm, "L_m"); !r) return r;
    if (auto r = emc::require_positive(Cm, "C_m"); !r) return r;

    // Resistances must be >= 0 individually...
    for (auto [v, name] : {std::pair{RL, "R_L"}, {RS, "R_S"}, {RNE, "R_NE"}, {RFE, "R_FE"}})
        if (auto r = emc::in_range(v, 0.0, 1e12, name); !r) return r;

    // ...and the two SUMS that act as denominators must be non-zero.
    if (auto r = emc::require_nonzero(RS + RL,   "R_S+R_L");   !r) return r;
    if (auto r = emc::require_nonzero(RNE + RFE, "R_NE+R_FE"); !r) return r;

    return {};
}

emc::Result<CrosstalkResult> calculate(const CrosstalkInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Work in coherent SI; the model is unit-consistent in Hz, ohm, henry, farad,
    // so plain SI magnitudes evaluate the formula directly once the typed
    // quantities have folded any input-side scale (MHz, uH, pF) into the value.
    const double f   = in.f.numerical_value_in(hertz);
    const double RL  = in.R_L.numerical_value_in(ohm);
    const double RS  = in.R_S.numerical_value_in(ohm);
    const double RNE = in.R_NE.numerical_value_in(ohm);
    const double RFE = in.R_FE.numerical_value_in(ohm);
    const double Lm  = in.L_m.numerical_value_in(henry);
    const double Cm  = in.C_m.numerical_value_in(farad);

    const double omega = 2.0 * emc::constants::pi * f;        // angular frequency

    // Shared sub-expressions computed once.
    const double inductive  = Lm / (RS + RL);                 // L_m / (R_S + R_L)
    const double capacitive = (RNE * RFE / (RNE + RFE)) * (RL * Cm / (RS + RL));
    const double split_ne   = RNE / (RNE + RFE);
    const double split_fe   = RFE / (RNE + RFE);

    // Near-end: inductive and capacitive contributions ADD.
    const double A_NE = omega * (split_ne * inductive + capacitive);

    // Far-end: the inductive term is SUBTRACTED (sign flip).
    const double A_FE = omega * (-1.0 * split_fe * inductive + capacitive);

    CrosstalkResult out{};

    // V_NE: the near-end argument is positive for physical inputs (omega>0, all
    // terms >= 0). Guard against a non-positive argument as a DomainError rather
    // than emitting -nan.
    if (!(A_NE > 0.0))
        return std::unexpected(emc::domain_error(
            "near-end argument is non-positive (log10 undefined)", "V_NE"));
    out.V_NE = emc::units::Decibel{ 20.0 * std::log10(A_NE) };

    // V_FE: the documented clamp. When A_FE <= 0 (inductive cancellation dominates)
    // substitute -200 dB instead of computing log10 of <= 0.
    if (A_FE <= 0.0) {
        out.V_FE            = kFarEndFloor;     // -200 dB
        out.far_end_clamped = true;
    } else {
        out.V_FE = emc::units::Decibel{ 20.0 * std::log10(A_FE) };
    }

    return out;
}

} // namespace emc::cabling
```

> [!IMPORTANT]
> The clamp is a defined behavior, not an error. For `A_FE <= 0` the model yields a valid, displayed
> −200 dB; `far_end_clamped` additionally lets consumers distinguish a *computed* −200 dB from a
> *floored* one. We deliberately do not turn it into an `Error` — see design notes for the trade-off.

### 4. Modern C++ features used here — and why

- **mp-units typed `Frequency` / `Inductance` / `Capacitance` inputs** — crosstalk inputs span MHz, µH
  and pF, so encoding the unit in the type lets the call site write `1 * MHz`, `50 * uH`, `50000 * pF`
  and have mp-units derive the scale. Passing `L_m` (henry) where `C_m` (farad) is expected is a compile
  error, not a silent factor mistake.
- **Designated-initializer `CrosstalkInput`** — `{.f=…, .R_NE=…, .L_m=…}` names every field, so the
  easy-to-transpose `R_NE`/`R_FE` pair (which differ only by the near/far sign in the math) can't be
  swapped silently.
- **`emc::units::Decibel` typed wrapper for the outputs** — `V_NE`/`V_FE` are *logarithmic* dB, not a
  linear mp-units unit. The wrapper (foundation §3) keeps them out of linear quantity arithmetic, so
  nobody can accidentally `V_NE + V_FE` as if they were watts. `kFarEndFloor` is a `Decibel`, so the
  clamp value is type-correct.
- **Named `inline constexpr kFarEndFloor` + `far_end_clamped` flag** — naming the floor documents intent
  in one place and lets tests assert on it; the flag makes the clamp *observable* instead of
  indistinguishable from a real computation.
- **Hoisted shared sub-expressions** — computing `inductive`, `capacitive`, `split_ne`, `split_fe` once
  is clearer and removes any risk of duplicated expressions drifting apart during edits.
- **`std::expected` + `domain_error` for the near-end log** — a non-positive `A_NE` becomes a typed
  `DomainError` instead of `-nan`/`-inf`. (`A_FE` keeps its clamp; only the near-end path becomes a typed
  error.)
- **`[[nodiscard]]` on `calculate`/`validate`** — ignoring the result becomes a compiler warning.

### 5. Example usage

```c++
#include <emc/cabling/crosstalk.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;   // MHz, uH, pF, ohm

    // Default preset, written with unit literals + designated inits.
    const emc::cabling::CrosstalkInput in{
        .f    = 1.0 * MHz,
        .R_L  = 50.0 * ohm,  .R_S  = 50.0 * ohm,
        .R_NE = 50.0 * ohm,  .R_FE = 50.0 * ohm,
        .L_m  = 50.0 * uH,
        .C_m  = 50'000.0 * pF,
    };

    const auto r = emc::cabling::calculate(in);
    if (!r) {
        std::println("crosstalk error [{}]: {} (field: {})",
                     emc::to_string(r.error().code), r.error().message, r.error().field);
        return 1;
    }

    std::println("V_NE = {:.5f} dB", r->V_NE.value);          // ~14.80376 dB
    std::println("V_FE = {:.5f} dB{}", r->V_FE.value,
                 r->far_end_clamped ? "  (clamped to floor)" : "");
}
```

### 6. Unit tests — `tests/cabling/crosstalk_test.cpp`

Expected values are hand-computed from the closed form; the suite also pins the documented clamp and the
validation edge cases.

```c++
// tests/cabling/crosstalk_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <emc/cabling/crosstalk.hpp>
#include <emc/core/constants.hpp>

#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

namespace {
// Independent closed-form oracle for one input (mirrors the formula incl. clamp).
struct Oracle { double vne; double vfe; bool clamped; };
Oracle oracle(const emc::cabling::CrosstalkInput& in) {
    const double f   = in.f.numerical_value_in(hertz);
    const double RL  = in.R_L.numerical_value_in(ohm);
    const double RS  = in.R_S.numerical_value_in(ohm);
    const double RNE = in.R_NE.numerical_value_in(ohm);
    const double RFE = in.R_FE.numerical_value_in(ohm);
    const double Lm  = in.L_m.numerical_value_in(henry);
    const double Cm  = in.C_m.numerical_value_in(farad);
    const double w   = 2.0 * emc::constants::pi * f;
    const double ind = Lm / (RS + RL);
    const double cap = (RNE * RFE / (RNE + RFE)) * (RL * Cm / (RS + RL));
    const double A_NE = w * ((RNE / (RNE + RFE)) * ind + cap);
    const double A_FE = w * (-1.0 * (RFE / (RNE + RFE)) * ind + cap);
    Oracle o{ 20.0 * std::log10(A_NE), 0.0, A_FE <= 0.0 };
    o.vfe = o.clamped ? -200.0 : 20.0 * std::log10(A_FE);
    return o;
}
} // namespace

// (a) ORACLE TEST -- an independent closed-form recomputation agrees with the
//     implementation on V_NE and V_FE to tight tolerance.
TEST_CASE("crosstalk matches an independent closed-form oracle",
          "[cabling][crosstalk][oracle]") {
    const emc::cabling::CrosstalkInput in{
        .f = 2.5 * MHz, .R_L = 75.0 * ohm, .R_S = 50.0 * ohm,
        .R_NE = 60.0 * ohm, .R_FE = 90.0 * ohm,
        .L_m = 1.2 * uH, .C_m = 33000.0 * pF};
    const auto out = emc::cabling::calculate(in);
    REQUIRE(out.has_value());

    const Oracle ref = oracle(in);
    REQUIRE(emc::test::approx(out->V_NE.value * one, ref.vne * one, 1e-5));
    REQUIRE(emc::test::approx(out->V_FE.value * one, ref.vfe * one, 1e-5));
    REQUIRE(out->far_end_clamped == ref.clamped);
}

// (b) KNOWN-VALUE TESTS -- hand-computed from the closed form.
TEST_CASE("crosstalk known values", "[cabling][crosstalk][known]") {
    SECTION("f=1MHz, all 50ohm, Lm=0.5uH, Cm=94683.6pF") {
        const emc::cabling::CrosstalkInput in{
            .f = 1.0 * MHz, .R_L = 50.0 * ohm, .R_S = 50.0 * ohm,
            .R_NE = 50.0 * ohm, .R_FE = 50.0 * ohm,
            .L_m = 0.5 * uH, .C_m = 94683.6 * pF};
        const auto r = emc::cabling::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->V_NE.value * one, 17.44562 * one, 1e-4));
        REQUIRE(emc::test::approx(r->V_FE.value * one, 17.40893 * one, 1e-4));
        REQUIRE_FALSE(r->far_end_clamped);
    }
    SECTION("default preset: V_NE ~= 14.80376 dB") {
        const auto r = emc::cabling::calculate({});   // all defaults
        REQUIRE(r.has_value());
        REQUIRE(emc::test::approx(r->V_NE.value * one, 14.80376 * one, 1e-4));
    }
}

// (c) CLAMP TEST -- the documented V_FE floor. This input has A_FE < 0, so V_FE
//     must clamp to exactly -200 dB and set the flag, while V_NE stays finite.
TEST_CASE("crosstalk clamps V_FE to the -200 dB floor", "[cabling][crosstalk][clamp]") {
    const emc::cabling::CrosstalkInput in{
        .f = 10.42 * MHz, .R_L = 0.399 * ohm, .R_S = 5.428 * ohm,
        .R_NE = 22.92 * ohm, .R_FE = 79.86 * ohm,
        .L_m = 0.8 * uH, .C_m = 39628.4 * pF};
    const auto r = emc::cabling::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->far_end_clamped);
    REQUIRE(r->V_FE.value == -200.0);                         // exact floor, no log10
    REQUIRE(r->V_FE.value == emc::cabling::kFarEndFloor.value);
    REQUIRE(emc::test::approx(r->V_NE.value * one, 14.26700 * one, 1e-4));
}

// (d) VALIDATION / EDGE TESTS -- the right ErrorCode for bad inputs.
TEST_CASE("crosstalk rejects bad inputs", "[cabling][crosstalk][validation]") {
    SECTION("zero frequency is OutOfRange (log10 collapses)") {
        const auto r = emc::cabling::calculate(
            {.f = 0.0 * MHz, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "f");
    }
    SECTION("R_NE + R_FE == 0 is DivisionByZero") {
        // Both split-ratio denominators are (R_NE + R_FE).
        const auto r = emc::cabling::calculate(
            {.R_NE = 0.0 * ohm, .R_FE = 0.0 * ohm, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "R_NE+R_FE");
    }
    SECTION("R_S + R_L == 0 is DivisionByZero") {
        const auto r = emc::cabling::calculate(
            {.R_L = 0.0 * ohm, .R_S = 0.0 * ohm, .L_m = 50.0 * uH, .C_m = 50000.0 * pF});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "R_S+R_L");
    }
}
```

> Test guards: **(a)** an independent closed-form recomputation agrees on both outputs to tight
> tolerance; **(b)** hand-computed oracles pin V_NE/V_FE absolute values for a representative input and
> the default preset; **(c)** the documented `−200 dB` clamp fires on an input that drives `A_FE < 0` and
> is observable via `far_end_clamped`; **(d)** validation returns the correct typed code for the two
> denominator sums and zero frequency.

> [!NOTE]
> No `constexpr` test here. `std::log10` is not `constexpr` in C++23 (unlike `std::sqrt`/`std::abs`), so
> `calculate()` cannot fold at compile time. The braid calculator is in the same boat (`std::atan` /
> `std::sin` are runtime), so neither cabling calculator gets a constexpr test — both rely on the
> known-value and oracle coverage instead.

### 7. Design notes

- **The `V_FE = −200` clamp is a defined floor.** When `A_FE = 2πf·[−inductive + capacitive] ≤ 0`
  (inductive cancellation dominates), `log10` is undefined, so the model substitutes `−200 dB`. We expose
  `far_end_clamped` so a consumer can tell a floored value from a genuine computation. *Trade-off:* one
  could argue `A_FE ≤ 0` should be a `DomainError`; we chose the documented clamp because it is a
  well-defined, useful "below model range" sentinel, and a flagged value is more informative to a front
  end than a hard error.
- **Input-side units fold into the typed quantities.** `f` in **MHz**, `L_m` in **µH**, `C_m` in **pF**
  enter as `1 * MHz`, `50 * uH`, `50000 * pF`; mp-units derives the SI scale, so there is no
  hand-maintained factor table to get wrong.
- **20·log₁₀, not 10·log₁₀.** The model uses `20 * log10(...)` (a *voltage* dB). Do not "correct" it to
  `10·log10` (power dB) — these calculators report a voltage ratio.
- **Shared terms computed once.** `inductive`, `capacitive`, and the two split ratios are evaluated a
  single time and reused across `A_NE` and `A_FE`, a readability/maintenance choice that keeps the two
  arguments in lockstep.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `error.hpp` / `constants.hpp` /
  `units.hpp` / `calculator.hpp` and the `emc::test::approx` helper reused above (the `Decibel` wrapper,
  `pi`, the validators, the `Calculator` concept).
- [`../07-calculator-inventory.md`](../07-calculator-inventory.md) §7 (Cabling → `emc::cabling`) — the
  spec row for naming, placement, and the documented `V_FE = −200` clamp.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the testing harness and
  tolerance model.
```