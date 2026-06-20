# Implementation Guide — Component: PCB Trace Impedance (`emc::component`, bidirectional solvers) 🛠️

> Purpose: full, copy-paste-quality C++23 for the four **bidirectional** circuit-board trace-impedance
> calculators — Microstrip, Stripline, Dual Stripline, Embedded Microstrip. Each models a single
> algebraic relation `Z0 = f(H, T, W[, C], eps_r)` and exposes it as a **family of `solve_*` free
> functions** sharing one `detail::` physics core. This is the headline component guide: it shows the
> *solve-for-X* design and how **mp-units typed inputs make unit handling trivial** while
> **`std::expected` + `validate()` reports out-of-domain inputs as typed errors**.

Calculators covered:

| Calculator | Namespace | Header | Impl | Test |
|---|---|---|---|---|
| Microstrip Trace | `emc::component` | `include/emc/component/microstrip_trace.hpp` | `src/component/microstrip_trace.cpp` | `tests/component/microstrip_trace_test.cpp` |
| Stripline Trace | `emc::component` | `include/emc/component/stripline_trace.hpp` | `src/component/stripline_trace.cpp` | `tests/component/stripline_trace_test.cpp` |
| Dual Stripline Trace | `emc::component` | `include/emc/component/dual_stripline_trace.hpp` | `src/component/dual_stripline_trace.cpp` | `tests/component/dual_stripline_trace_test.cpp` |
| Embedded Microstrip | `emc::component` | `include/emc/component/embedded_microstrip_trace.hpp` | `src/component/embedded_microstrip_trace.cpp` | `tests/component/embedded_microstrip_trace_test.cpp` |

All four reuse the **canonical foundation** verbatim — `emc::units::*`, `emc::Result`,
`emc::in_range`/`require_positive`/`require_nonzero`, `emc::ErrorCode`, `emc::constants`, and the
`emc::test::*` helpers — defined in [`00-foundation-code.md`](00-foundation-code.md). Read that first;
names below are not re-spelled.

---

## The shared design (read once, applies to all four)

Each calculator captures the *same* impedance relation `Z0 = f(H, T, W[, C], eps_r)` and lets the caller
solve for any one variable given the rest:

- `solve_impedance` (the forward solve) — characteristic impedance `Z0` (plus `C0`, `Tpd`).
- `solve_height` — solve `H` given `Z0, T, W, eps_r`.
- `solve_thickness` — solve `T`.
- `solve_width` — solve `W`.
- (dual stripline only) `solve_gap` — solve the inter-trace gap `C`.

### The chosen shape: distinct `solve_*` free functions over one `detail::` core

The design is: **one aggregate `Input`, one `Result`, and one free function per solve target**, all
forwarding to a shared `detail::` core that owns the physics in a single coherent unit. We deliberately do
*not* model the unknown with a runtime `std::optional` dispatch — the solve-for-X surface has four (or
five) distinct targets, so each is its own named function sharing the core. Naming the target in the
function (`solve_impedance`, `solve_height`, `solve_thickness`, `solve_width`) keeps each signature
self-documenting, lets the return type differ (`Z0` is an `Impedance`, `H` is a `Length`), and makes the
round-trip tests (`solve_impedance` then `solve_width` recovers the original width) read directly.

Trace dimensions arrive as typed `emc::units::Length` quantities, so the choice of mm vs mils is handled
by the type system at the call site, not by branching inside the math. The core pins one working unit
(`mm`) with `q.numerical_value_in(mm)`, runs the established closed-form algebra on bare doubles, and
re-attaches units on the way out. The caller later reads any display unit with `.in(mil)` / `.in(mm)`.

We define a private `mil` unit once, shared by all four headers via `detail/board_units.hpp`:

```c++
// include/emc/component/detail/board_units.hpp
#pragma once
#include <mp-units/systems/si.h>

namespace emc::component::detail {
using namespace mp_units;
// 1 mil = 1/1000 inch = 0.0254 mm — EXACT. Defined once so imperial trace dimensions are first-class
// typed quantities; conversions to/from mm are derived by mp-units, not by a hand-coded factor.
inline constexpr struct mil_ final : named_unit<"mil", mag_ratio<254, 10'000'000> * si::metre> {} mil;
}  // namespace emc::component::detail
```

> [!NOTE]
> mp-units lets us add the `mil` to the SI graph as a scaled `metre`; conversions to/from `mm` are then
> *derived* and exact. `mag_ratio<254, 10'000'000>` is `0.0000254 m = 0.0254 mm`. One definition, zero
> unit branches anywhere. (If a project-wide imperial unit set is later added to `units.hpp`, move `mil`
> there; it lives in `detail` for now because only these four calculators use it.)

A physics observation the tests exploit: **Microstrip, Stripline, and Dual Stripline `Z0` are
scale-invariant** — H, T, W (and C) appear only inside the dimensionless ratios `…/(0.8W + T)`,
`(2H+T)`, `8H`, `8(H+C)`, so multiplying every length by the same factor leaves `Z0` unchanged. That is
exactly why the working unit can be `mm` regardless of the caller's display unit. **Embedded Microstrip
is the exception** (its `(h1 − H − T)/0.1` term carries an absolute length scale), so its unit handling
is load-bearing and pinned explicitly.

---

## Microstrip Trace

### 1. Overview

Characteristic impedance of a surface trace over one reference plane — the standard IPC/Wheeler microstrip
closed form. Bidirectional: `solve_impedance` (Z0), `solve_height`, `solve_thickness`, `solve_width`.

Forward formula (the established IPC microstrip relation):

```text
ln    = ln( 5.98·H / (0.8·W + T) )
Z0    = 87·ln / sqrt(eps_r + 1.41)                          [ohm]
ctemp = 0.67·(eps_r + 1.41) / ln
C0    = ctemp / 2.54                                        [pF/cm]
Tpd   = ctemp·Z0 / 2.54                                     [ps/cm]
```

Inverse closed forms (with `k = Z0·sqrt(1.41 + eps_r)/87`):

```text
H = exp(k)·(0.8·W + T) / 5.98
T = 5.98·H / exp(k) − 0.8·W
W = (5.98·H / exp(k) − T) / 0.8
```

The `Input` carries `h, t, w` as typed `Length` and `eps_r` as a plain dimensionless `double`; the
`Result` carries `Z0` [ohm], capacitance per length `C0`, and propagation delay `Tpd`. Because `Z0` is
scale-invariant, the working unit is `mm`.

### 2. Public header — `include/emc/component/microstrip_trace.hpp`

```c++
// include/emc/component/microstrip_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

// Inputs for the FORWARD solve (Z0). Geometry is typed Length, so the call site supplies
// `1.5 * mm` or `62.0 * mil` and the unit handling is decided once, in the type. eps_r is a
// plain dimensionless double per the canonical API.
struct MicrostripInput {
    emc::units::Length height       {};   // H : dielectric thickness, trace -> plane
    emc::units::Length thickness    {};   // T : copper thickness
    emc::units::Length width        {};   // W : trace width
    double             relative_permittivity = 4.7;   // eps_r  [-]
};

struct MicrostripResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display as pF/cm)
    emc::units::TimePerLength        tpd {};   // s/m (display as ps/cm)
};

// FORWARD: solve characteristic impedance (+ C0, Tpd).
[[nodiscard]] emc::Result<MicrostripResult> calculate(const MicrostripInput& in);

// Shared validation (eps_r in [1,15]; positivity; 0.1 <= W/H <= 3) reported as typed errors.
[[nodiscard]] std::expected<void, emc::Error> validate(const MicrostripInput& in);

// --- INVERSE solves. Each takes the three known geometry/material fields + a target Z0 and
//     returns the missing dimension as a typed Length. Distinct names per target. -----------
struct MicrostripSolveHeight {                 // known: T, W, eps_r, Z0  ->  H
    emc::units::Impedance z0 {};
    emc::units::Length    thickness {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct MicrostripSolveThickness {              // known: H, W, eps_r, Z0  ->  T
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    width {};
    double                relative_permittivity = 4.7;
};
struct MicrostripSolveWidth {                  // known: H, T, eps_r, Z0  ->  W
    emc::units::Impedance z0 {};
    emc::units::Length    height {};
    emc::units::Length    thickness {};
    double                relative_permittivity = 4.7;
};

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const MicrostripSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const MicrostripSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const MicrostripSolveWidth&);

// Tag binding the forward triple to the Calculator concept (foundation §5).
struct MicrostripTrace {
    using Input  = MicrostripInput;
    using Result = MicrostripResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<MicrostripTrace>);

}  // namespace emc::component
```

### 3. Implementation — `src/component/microstrip_trace.cpp`

```c++
// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <emc/component/detail/board_units.hpp>

#include <cmath>   // std::log, std::exp, std::sqrt (constexpr in C++23)

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {

// Working unit for the empirical algebra. Z0 is scale-invariant, so the choice does not affect Z0.
constexpr auto U = mm;

// The single source of the forward Z0 math, on bare doubles in mm.
struct Core { double z0, c0_pf_cm, tpd_ps_cm; };

Core forward_core(double H, double T, double W, double eps) {
    const double ln    = std::log(5.98 * H / (0.8 * W + T));
    const double z0    = 87.0 * ln / std::sqrt(eps + 1.41);
    const double ctemp = 0.67 * (eps + 1.41) / ln;
    return Core{ .z0 = z0, .c0_pf_cm = ctemp / 2.54, .tpd_ps_cm = ctemp * z0 / 2.54 };
}

}  // namespace

std::expected<void, emc::Error> validate(const MicrostripInput& in) {
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // 0.1 <= W/H <= 3 keeps the inputs inside the formula's validated geometry band.
    if (auto r = emc::in_range(W / H, 0.1, 3.0, "width_to_height_ratio"); !r) return r;
    return {};
}

emc::Result<MicrostripResult> calculate(const MicrostripInput& in) {
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, T, W, in.relative_permittivity);
        return MicrostripResult{
            .z0  = c.z0 * ohm,
            // Re-attach physical units. The display numbers are pF/cm and ps/cm; we store
            // SI and let the caller print pF/cm / ps/cm via .in(...). (1 pF/cm = 1e-10 F/m.)
            .c0  = c.c0_pf_cm * (si::pico<si::farad> / si::centi<si::metre>),
            .tpd = c.tpd_ps_cm * (si::pico<si::second> / si::centi<si::metre>),
        };
    });
}

// ---- inverse solvers: one expression each; no unit branching. -------------------------------
namespace {
double inv_k(double z0, double eps) { return z0 * std::sqrt(1.41 + eps) / 87.0; }
}  // namespace

emc::Result<emc::units::Length> solve_height(const MicrostripSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double H = std::exp(inv_k(Z, in.relative_permittivity)) * (0.8 * W + T) / 5.98;
    if (!(H > 0.0)) return std::unexpected(emc::domain_error("microstrip height non-positive", "height"));
    return H * U;
}

emc::Result<emc::units::Length> solve_thickness(const MicrostripSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double T = 5.98 * H / std::exp(inv_k(Z, in.relative_permittivity)) - 0.8 * W;
    if (!(T > 0.0)) return std::unexpected(emc::domain_error("microstrip thickness non-positive", "thickness"));
    return T * U;
}

emc::Result<emc::units::Length> solve_width(const MicrostripSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double Z = in.z0.numerical_value_in(ohm);
    const double W = (5.98 * H / std::exp(inv_k(Z, in.relative_permittivity)) - T) / 0.8;
    if (!(W > 0.0)) return std::unexpected(emc::domain_error("microstrip width non-positive", "width"));
    return W * U;
}

}  // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **mp-units typed `Length` inputs (`numerical_value_in(mm)`)** — trace dimensions arrive as typed
  quantities, so mm vs mils is handled by the type system, not by branching. The unit decision is made
  once, at the call site, in the type; the core only ever sees bare doubles in one coherent unit.
- **`std::expected<void, Error>` `validate()` + monadic `transform`** — an out-of-domain input is a
  recoverable typed error the caller must handle, not a silently-stale result; `calculate` runs the math
  *only if* `validate` succeeds, chained via `transform`.
- **Distinct `solve_height/solve_thickness/solve_width` free functions** — the solve-for-X surface has
  several targets, so each is its own named function sharing the `inv_k` core. The inverse formulas are
  individually visible and testable, and each returns the correctly typed `Length`.
- **`constexpr <cmath>` (C++23)** — `std::log/std::exp/std::sqrt` are usable in the `static_assert` smoke
  test below; the closed form is pure.
- **Designated initializers on `MicrostripInput`** — `{.height = 1.5*mm, .thickness = 0.035*mm, …}` makes
  each field explicit at the call site, with no positional-ordering ambiguity.
- **`[[nodiscard]]` everywhere** — ignoring the `Result` (and thus the error path) now warns.

### 5. Example usage

```c++
#include <emc/component/microstrip_trace.hpp>
#include <mp-units/systems/si.h>
#include <print>

using namespace mp_units::si::unit_symbols;       // mm, ohm
using namespace emc::component;

int main() {
    // 1.5 mm dielectric, 35 um copper, 2.65 mm trace, FR-4 eps_r 4.7.
    const MicrostripInput in{ .height = 1.5 * mm, .thickness = 0.035 * mm,
                              .width = 2.65 * mm, .relative_permittivity = 4.7 };

    auto r = calculate(in);
    if (!r) {                                     // error arm
        std::println("microstrip error: {}", r.error().what());
        return 1;
    }
    // pull each output in a display unit
    std::println("Z0  = {:.2f} ohm", r->z0.numerical_value_in(ohm));
    std::println("C0  = {:.3f} pF/cm",
                 r->c0.numerical_value_in(mp_units::si::pico<mp_units::si::farad>
                                          / mp_units::si::centi<mp_units::si::metre>));

    // inverse: what trace width gives Z0 = 50 ohm at the same stack-up?
    auto w = solve_width({ .z0 = 50.0 * ohm, .height = 1.5 * mm,
                           .thickness = 0.035 * mm, .relative_permittivity = 4.7 });
    if (w) std::println("W for 50 ohm = {:.3f} mm", w->numerical_value_in(mm));
}
```

### 6. Unit tests — `tests/component/microstrip_trace_test.cpp`

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/microstrip_trace.hpp>
#include "support/approx.hpp"

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using namespace emc::component;
using emc::ErrorCode;

namespace {
constexpr auto pf_cm = si::pico<si::farad> / si::centi<si::metre>;
constexpr auto ps_cm = si::pico<si::second> / si::centi<si::metre>;
}

// (a) HAND-COMPUTED known value, from the closed form above.
//     H=18.65392418, T=11.15918402, W=15.00320319, eps_r=5.0:
//       ln    = ln(5.98*18.65392418/(0.8*15.00320319+11.15918402)) = ln(111.5505.../23.1620...)
//       Z0    = 87*ln/sqrt(6.41)               ~= 54.01767 ohm
//       ctemp = 0.67*6.41/ln,  C0 = ctemp/2.54 ~= 1.07561 pF/cm,  Tpd = ctemp*Z0/2.54 ~= 58.10177 ps/cm
TEST_CASE("microstrip known value", "[component][microstrip]") {
    auto r = calculate({ .height = 18.65392418 * mm, .thickness = 11.15918402 * mm,
                         .width = 15.00320319 * mm, .relative_permittivity = 5.0 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  54.01767 * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->c0,  1.07561 * pf_cm, 1e-4));
    REQUIRE(emc::test::approx(r->tpd, 58.10177 * ps_cm, 1e-4));
}

// (b) ROUND-TRIP — forward Z0, then solve_width must recover W (and unit-invariance: mm vs mil).
TEST_CASE("microstrip Z0<->W round-trips", "[component][microstrip][roundtrip]") {
    const MicrostripInput in{ .height = 1.5 * mm, .thickness = 0.035 * mm,
                              .width = 2.65 * mm, .relative_permittivity = 4.7 };
    auto fwd = calculate(in);
    REQUIRE(fwd.has_value());
    auto w = solve_width({ .z0 = fwd->z0, .height = in.height,
                           .thickness = in.thickness, .relative_permittivity = 4.7 });
    REQUIRE(w.has_value());
    REQUIRE(emc::test::approx(*w, in.width, 1e-9));     // recovers the original width

    // Z0 is scale-invariant: feeding the SAME geometry expressed in mils yields the same Z0.
    using emc::component::detail::mil;                  // the exact mil unit
    auto fwd_mil = calculate({ .height = in.height.in(mil), .thickness = in.thickness.in(mil),
                               .width = in.width.in(mil), .relative_permittivity = 4.7 });
    REQUIRE(fwd_mil.has_value());
    REQUIRE(emc::test::approx(fwd_mil->z0, fwd->z0, 1e-9));
}

// (c) VALIDATION / EDGE — the right ErrorCode for each out-of-domain path.
TEST_CASE("microstrip validation", "[component][microstrip][validate]") {
    SECTION("eps_r out of range") {
        auto r = calculate({ .height = 1.5*mm, .thickness = 0.035*mm, .width = 2.65*mm,
                             .relative_permittivity = 20.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "relative_permittivity");
    }
    SECTION("W/H ratio out of range") {
        auto r = calculate({ .height = 1.0*mm, .thickness = 0.035*mm, .width = 100.0*mm,
                             .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "width_to_height_ratio");
    }
    SECTION("non-positive width") {
        auto r = calculate({ .height = 1.0*mm, .thickness = 0.035*mm, .width = 0.0*mm,
                             .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive -> OutOfRange
    }
}

// (d) CONSTEXPR-FRIENDLY smoke test: the forward core is pure closed-form. (Compile-time guard.)
static_assert([] {
    const double H = 18.65392418, T = 11.15918402, W = 15.00320319, e = 5.0;
    const double ln = std::log(5.98 * H / (0.8 * W + T));
    const double z0 = 87.0 * ln / std::sqrt(e + 1.41);
    return z0 > 54.0 && z0 < 54.1;          // matches the known value above
}(), "microstrip forward core must be constexpr-evaluable and ~54.02 ohm");
```

Each test guards: **(a)** an absolute hand-computed `Z0/C0/Tpd`; **(b)** the inverse solver inverts the
forward one and proves unit handling is type-driven (same `Z0` whether geometry is given in mm or mils);
**(c)** each out-of-domain path maps to the right `ErrorCode`; **(d)** the math is `constexpr`.

### 7. Design notes

- **Exact `mil` unit.** The only unit boundary in the whole calculator is `numerical_value_in(mm)` on the
  way in and `*U` on the way out; everything between is dimensionless algebra.
- **`C0 = ctemp/2.54`, `Tpd = ctemp·Z0/2.54`.** The per-cm outputs are the per-inch quantities divided by
  2.54, matching the established display convention; the `Result` stores SI and the caller selects the
  display unit.
- **Validation lives in `validate()`.** The `eps_r ∈ [1,15]`, positivity, and `0.1 ≤ W/H ≤ 3` checks are
  one shared function returning `std::expected<void, Error>`.
- **`ln` domain.** If `5.98H/(0.8W+T) ≤ 0` (only reachable with non-positive geometry, already rejected by
  `validate`), `std::log` would yield NaN; the positivity guards prevent it.

---

## Stripline Trace

### 1. Overview

Characteristic impedance of a trace centered between two reference planes — the standard IPC stripline
closed form. Bidirectional: `solve_impedance` (Z0), `solve_height`, `solve_thickness`, `solve_width`.

Forward formula (the established IPC stripline relation; `pi` is `emc::constants::pi`):

```text
Z0   = 60·ln( 4·(2·H + T) / (0.67·pi·(0.8·W + T)) ) / sqrt(eps_r)     [ohm]
Tpd  = 84.75·sqrt(eps_r)                                              [ps/inch]
C0   = Tpd / Z0                                                       [pF/inch]
```

Inverse closed forms (with `a = exp(Z0·sqrt(eps_r)/60)`):

```text
H = ( a·0.67·pi·(0.8·W + T) / 4 − T ) / 2
T = ( 0.67·a·pi·0.8·W − 8·H ) / ( 4 − 0.67·a·pi )
W = ( (8·H + 4·T) / (a·0.67·pi) − T ) / 0.8
```

> [!NOTE]
> **`Z0` is scale-invariant**: H, T, W appear only inside the ratio `(2H+T)/(0.8W+T)`, and the validation
> ratios `T/H` and `W/(H−T)` are likewise scale-free. We therefore evaluate the core in **mm** regardless
> of the caller's display unit.

### 2. Public header — `include/emc/component/stripline_trace.hpp`

```c++
// include/emc/component/stripline_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

struct StriplineInput {
    emc::units::Length height    {};   // H : plane-to-plane / 2 spacing (dielectric thickness)
    emc::units::Length thickness {};   // T : copper thickness
    emc::units::Length width     {};   // W : trace width
    double             relative_permittivity = 4.7;
};

struct StriplineResult {
    emc::units::Impedance            z0  {};   // ohm
    emc::units::CapacitancePerLength c0  {};   // F/m (display pF/inch)
    emc::units::TimePerLength        tpd {};   // s/m (display ps/inch)
};

[[nodiscard]] emc::Result<StriplineResult>           calculate(const StriplineInput&);
[[nodiscard]] std::expected<void, emc::Error>        validate (const StriplineInput&);

struct StriplineSolveHeight    { emc::units::Impedance z0{}; emc::units::Length thickness{};
                                 emc::units::Length width{};  double relative_permittivity = 4.7; };
struct StriplineSolveThickness { emc::units::Impedance z0{}; emc::units::Length height{};
                                 emc::units::Length width{};  double relative_permittivity = 4.7; };
struct StriplineSolveWidth     { emc::units::Impedance z0{}; emc::units::Length height{};
                                 emc::units::Length thickness{}; double relative_permittivity = 4.7; };

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const StriplineSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const StriplineSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const StriplineSolveWidth&);

struct StriplineTrace {
    using Input  = StriplineInput;
    using Result = StriplineResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<StriplineTrace>);

}  // namespace emc::component
```

### 3. Implementation — `src/component/stripline_trace.cpp`

```c++
// src/component/stripline_trace.cpp
#include <emc/component/stripline_trace.hpp>
#include <emc/component/detail/board_units.hpp>

#include <emc/core/constants.hpp>   // emc::constants::pi

#include <cmath>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {
constexpr auto U = mm;
constexpr double PI = emc::constants::pi;     // full-precision pi from the foundation

struct Core { double z0, c0_pf_in, tpd_ps_in; };

Core forward_core(double H, double T, double W, double eps) {
    const double z0  = 60.0 * std::log(4.0 * (2.0 * H + T) / (0.67 * PI * (0.8 * W + T)))
                       / std::sqrt(eps);
    const double tpd = 84.75 * std::sqrt(eps);          // ps/inch
    return Core{ .z0 = z0, .c0_pf_in = tpd / z0, .tpd_ps_in = tpd };
}
}  // namespace

std::expected<void, emc::Error> validate(const StriplineInput& in) {
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r) return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // stripline geometry guards: T/H < 0.25 and W/(H-T) < 0.35.
    if (!(T / H < 0.25))
        return std::unexpected(emc::out_of_range(0.0, 0.25, "thickness_to_height_ratio"));
    if (!(H - T > 0.0))
        return std::unexpected(emc::domain_error("height must exceed thickness", "height"));
    if (!(W / (H - T) < 0.35))
        return std::unexpected(emc::out_of_range(0.0, 0.35, "width_to_gap_ratio"));
    return {};
}

emc::Result<StriplineResult> calculate(const StriplineInput& in) {
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, T, W, in.relative_permittivity);
        return StriplineResult{
            .z0  = c.z0 * ohm,
            .c0  = c.c0_pf_in * (si::pico<si::farad> / si::inch),     // pF/inch -> F/m
            .tpd = c.tpd_ps_in * (si::pico<si::second> / si::inch),   // ps/inch -> s/m
        };
    });
}

namespace { double aa(double z0, double eps) { return std::exp(z0 * std::sqrt(eps) / 60.0); } }

emc::Result<emc::units::Length> solve_height(const StriplineSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double H = (a * 0.67 * PI * (0.8 * W + T) / 4.0 - T) / 2.0;
    if (!(H > 0.0)) return std::unexpected(emc::domain_error("stripline height non-positive", "height"));
    return H * U;
}

emc::Result<emc::units::Length> solve_thickness(const StriplineSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double denom = 4.0 - 0.67 * a * PI;
    if (denom == 0.0) return std::unexpected(emc::division_by_zero("thickness_solver"));
    const double T = (0.67 * a * PI * 0.8 * W - 8.0 * H) / denom;
    if (!(T > 0.0)) return std::unexpected(emc::domain_error("stripline thickness non-positive", "thickness"));
    return T * U;
}

emc::Result<emc::units::Length> solve_width(const StriplineSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double W = ((8.0 * H + 4.0 * T) / (a * 0.67 * PI) - T) / 0.8;
    if (!(W > 0.0)) return std::unexpected(emc::domain_error("stripline width non-positive", "width"));
    return W * U;
}

}  // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`emc::constants::pi`** — the same full-precision `pi` appears in both the forward `Z0` and the
  inverse `a`, so the two formulas agree to the last digit and the round-trip test would catch any drift.
  One namespaced constant, never a per-formula literal.
- **mp-units `Length` inputs** — trace dimensions are typed, so mm vs mils is a call-site type choice;
  `Z0`'s scale-invariance means the working unit `mm` is always valid.
- **`std::expected` `validate()`** — the geometry-band checks become typed `OutOfRange`/`DomainError`
  values; the `H − T > 0` guard also pre-empts a divide-by-zero in the `W/(H−T)` ratio.
- **`emc::division_by_zero` in `solve_thickness`** — the inverse `T` formula has a `(4 − 0.67·a·π)`
  denominator; a zero denominator becomes a typed `DivisionByZero` error instead of `inf`.
- **Designated initializers + `[[nodiscard]]`** — as in Microstrip.

### 5. Example usage

```c++
#include <emc/component/stripline_trace.hpp>
#include <mp-units/systems/si.h>
#include <print>

using namespace mp_units::si::unit_symbols;       // mm, ohm
using namespace emc::component;

int main() {
    auto r = calculate({ .height = 0.2 * mm, .thickness = 0.014 * mm,
                         .width = 0.06 * mm, .relative_permittivity = 4.7 });
    if (!r) { std::println("stripline: {}", r.error().what()); return 1; }
    std::println("Z0 = {:.2f} ohm", r->z0.numerical_value_in(ohm));

    // inverse: trace width for a 50 ohm centered stripline
    auto w = solve_width({ .z0 = 50.0 * ohm, .height = 0.2 * mm,
                           .thickness = 0.014 * mm, .relative_permittivity = 4.7 });
    if (w) std::println("W for 50 ohm = {:.4f} mm", w->numerical_value_in(mm));
}
```

### 6. Unit tests — `tests/component/stripline_trace_test.cpp`

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/stripline_trace.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using namespace emc::component;
using emc::ErrorCode;

namespace {
constexpr double PI = emc::constants::pi;
constexpr auto pf_in = si::pico<si::farad>  / si::inch;
constexpr auto ps_in = si::pico<si::second> / si::inch;
}

// (a) HAND-COMPUTED known value, from the closed form above.
//     H=81, T=2.4, W=7.5, eps_r=12.85382422:
//       Z0  = 60*ln(4*(2*81+2.4)/(0.67*pi*(0.8*7.5+2.4)))/sqrt(eps) ~= 60.51698 ohm
//       Tpd = 84.75*sqrt(eps)                                       ~= 303.84760 ps/inch
TEST_CASE("stripline known value", "[component][stripline]") {
    auto r = calculate({ .height = 81.0*mm, .thickness = 2.4*mm, .width = 7.5*mm,
                         .relative_permittivity = 12.85382422 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  60.51698 * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->tpd, 303.84760 * ps_in, 1e-5));
    REQUIRE(emc::test::approx(r->c0,  (303.84760 / 60.51698) * pf_in, 1e-4));
}

// (b) ROUND-TRIP Z0<->W.
TEST_CASE("stripline Z0<->W round-trips", "[component][stripline][roundtrip]") {
    const StriplineInput in{ .height = 81.0*mm, .thickness = 2.4*mm, .width = 7.5*mm,
                             .relative_permittivity = 12.85382422 };
    auto fwd = calculate(in);
    REQUIRE(fwd.has_value());
    auto w = solve_width({ .z0 = fwd->z0, .height = in.height, .thickness = in.thickness,
                           .relative_permittivity = 12.85382422 });
    REQUIRE(w.has_value());
    REQUIRE(emc::test::approx(*w, in.width, 1e-7));
}

// (c) VALIDATION / EDGE.
TEST_CASE("stripline validation", "[component][stripline][validate]") {
    SECTION("eps_r out of range") {
        auto r = calculate({ .height = 81.0*mm, .thickness = 2.4*mm, .width = 7.5*mm,
                             .relative_permittivity = 0.5 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
    SECTION("T/H ratio too large") {
        auto r = calculate({ .height = 1.0*mm, .thickness = 0.9*mm, .width = 0.05*mm,
                             .relative_permittivity = 4.7 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "thickness_to_height_ratio");
    }
}

// (d) CONSTEXPR smoke.
static_assert([] {
    const double H = 81, T = 2.4, W = 7.5, e = 12.85382422;
    const double z0 = 60.0 * std::log(4.0*(2.0*H+T)/(0.67*emc::constants::pi*(0.8*W+T))) / std::sqrt(e);
    return z0 > 60.4 && z0 < 60.6;
}(), "stripline core constexpr ~60.52 ohm");
```

Guards: **(a)** an absolute hand-computed value; **(b)** the inverse inverts the forward; **(c)** the
out-of-domain paths map to `OutOfRange`; **(d)** `constexpr` with the shared full-precision `pi`.

### 7. Design notes

- **`emc::constants::pi`.** Because the *same* `pi` appears in both the forward `Z0` and the inverse `a`,
  the round-trip test exercises that they share it.
- **Scale-invariance.** The `mm` working unit is valid for any caller display unit; the round-trip test
  confirms the inverse returns the original geometry.
- **Guarded denominators.** The `(4 − 0.67·a·π)` and `(H − T)` denominators produce
  `DivisionByZero` / `DomainError` instead of `inf`/`nan`.

---

## Dual Stripline Trace

### 1. Overview

Two offset striplines between planes; `Z0` is the average of two single-stripline-like terms separated by
the inter-trace gap `C` — the established dual-stripline relation. **Five** solve targets:
`solve_impedance` (Z0), `solve_height`, `solve_gap`, `solve_thickness`, `solve_width`.

Forward formula (`pi` is `emc::constants::pi`):

```text
Z0  = ½·[ 60·ln( 8·H        / (0.67·pi·(0.8·W + T)) ) / sqrt(eps_r)
        + 60·ln( 8·(H + C)  / (0.67·pi·(0.8·W + T)) ) / sqrt(eps_r) ]     [ohm]
Tpd = 84.75·sqrt(eps_r)                                                   [ps/inch]
C0  = Tpd / Z0                                                            [pF/inch]
```

Inverse closed forms (each uses `a = exp(Z0·sqrt(eps_r)/(0.5·60))` = `exp(Z0·sqrt(eps_r)/30)`; let
`P = 0.67·pi·(0.8·W + T)`):

```text
H = ( sqrt( 64²·C² + 4·64·a·P² ) − 64·C ) / 128                       (solve_height)
C = ( a·P² − 64·H² ) / (64·H)                                          (solve_gap)
T = sqrt( (64·H² + 64·H·C) / a ) / (0.67·pi) − 0.8·W                   (solve_thickness)
W = ( sqrt( (64·H² + 64·H·C) / a ) / (0.67·pi) − T ) / 0.8            (solve_width)
```

> [!NOTE]
> Again **`Z0` is scale-invariant** in the geometry (H, C, T, W all scale together inside the log ratios),
> so the working unit `mm` is valid for any caller display unit.

### 2. Public header — `include/emc/component/dual_stripline_trace.hpp`

```c++
// include/emc/component/dual_stripline_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

struct DualStriplineInput {
    emc::units::Length height    {};   // H : trace -> nearer plane spacing
    emc::units::Length gap       {};   // C : spacing between the two traces
    emc::units::Length thickness {};   // T
    emc::units::Length width     {};   // W
    double             relative_permittivity = 4.7;
};

struct DualStriplineResult {
    emc::units::Impedance            z0  {};
    emc::units::CapacitancePerLength c0  {};   // F/m (display pF/inch)
    emc::units::TimePerLength        tpd {};   // s/m (display ps/inch)
};

[[nodiscard]] emc::Result<DualStriplineResult> calculate(const DualStriplineInput&);
[[nodiscard]] std::expected<void, emc::Error>  validate (const DualStriplineInput&);

// Five inverse targets, each naming the unknown. The extra gap target is just another
// named solver sharing the core; no special-casing.
struct DualSolveHeight    { emc::units::Impedance z0{}; emc::units::Length gap{}, thickness{}, width{};
                            double relative_permittivity = 4.7; };
struct DualSolveGap       { emc::units::Impedance z0{}; emc::units::Length height{}, thickness{}, width{};
                            double relative_permittivity = 4.7; };
struct DualSolveThickness { emc::units::Impedance z0{}; emc::units::Length height{}, gap{}, width{};
                            double relative_permittivity = 4.7; };
struct DualSolveWidth     { emc::units::Impedance z0{}; emc::units::Length height{}, gap{}, thickness{};
                            double relative_permittivity = 4.7; };

[[nodiscard]] emc::Result<emc::units::Length> solve_height   (const DualSolveHeight&);
[[nodiscard]] emc::Result<emc::units::Length> solve_gap      (const DualSolveGap&);
[[nodiscard]] emc::Result<emc::units::Length> solve_thickness(const DualSolveThickness&);
[[nodiscard]] emc::Result<emc::units::Length> solve_width    (const DualSolveWidth&);

struct DualStriplineTrace {
    using Input  = DualStriplineInput;
    using Result = DualStriplineResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<DualStriplineTrace>);

}  // namespace emc::component
```

### 3. Implementation — `src/component/dual_stripline_trace.cpp`

```c++
// src/component/dual_stripline_trace.cpp
#include <emc/component/dual_stripline_trace.hpp>
#include <emc/component/detail/board_units.hpp>

#include <emc/core/constants.hpp>

#include <cmath>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {
constexpr auto U = mm;
constexpr double PI = emc::constants::pi;

struct Core { double z0, c0_pf_in, tpd_ps_in; };

Core forward_core(double H, double C, double T, double W, double eps) {
    const double denom = 0.67 * PI * (0.8 * W + T);
    const double z0 = 0.5 * (60.0 * std::log(8.0 * H        / denom) / std::sqrt(eps)
                           + 60.0 * std::log(8.0 * (H + C)  / denom) / std::sqrt(eps));
    const double tpd = 84.75 * std::sqrt(eps);
    return Core{ .z0 = z0, .c0_pf_in = tpd / z0, .tpd_ps_in = tpd };
}
}  // namespace

std::expected<void, emc::Error> validate(const DualStriplineInput& in) {
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r) return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(C, "gap");       !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // dual geometry guards: W/(H-T) < 0.35 and T/H < 0.25.
    if (!(H - T > 0.0))
        return std::unexpected(emc::domain_error("height must exceed thickness", "height"));
    if (!(W / (H - T) < 0.35))
        return std::unexpected(emc::out_of_range(0.0, 0.35, "width_to_gap_ratio"));
    if (!(T / H < 0.25))
        return std::unexpected(emc::out_of_range(0.0, 0.25, "thickness_to_height_ratio"));
    return {};
}

emc::Result<DualStriplineResult> calculate(const DualStriplineInput& in) {
    return validate(in).transform([&] {
        const double H = in.height.numerical_value_in(U);
        const double C = in.gap.numerical_value_in(U);
        const double T = in.thickness.numerical_value_in(U);
        const double W = in.width.numerical_value_in(U);
        const Core c   = forward_core(H, C, T, W, in.relative_permittivity);
        return DualStriplineResult{
            .z0  = c.z0 * ohm,
            .c0  = c.c0_pf_in * (si::pico<si::farad> / si::inch),
            .tpd = c.tpd_ps_in * (si::pico<si::second> / si::inch),
        };
    });
}

namespace {
// dual uses a = exp(Z0*sqrt(eps)/(0.5*60)) = exp(Z0*sqrt(eps)/30).
double aa(double z0, double eps) { return std::exp(z0 * std::sqrt(eps) / 30.0); }
}  // namespace

emc::Result<emc::units::Length> solve_height(const DualSolveHeight& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double P = 0.67 * PI * (0.8 * W + T);
    const double H = (std::sqrt(64.0*64.0*C*C + 4.0*64.0*a*P*P) - 64.0*C) / 128.0;
    if (!(H > 0.0)) return std::unexpected(emc::domain_error("dual height non-positive", "height"));
    return H * U;
}

emc::Result<emc::units::Length> solve_gap(const DualSolveGap& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    if (auto r = emc::require_nonzero(H, "height"); !r) return std::unexpected(r.error());
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double P = 0.67 * PI * (0.8 * W + T);
    const double C = (a * P * P - 64.0 * H * H) / (64.0 * H);
    if (!(C > 0.0)) return std::unexpected(emc::domain_error("dual gap non-positive", "gap"));
    return C * U;
}

emc::Result<emc::units::Length> solve_thickness(const DualSolveThickness& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double W = in.width.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double T = std::sqrt((64.0*H*H + 64.0*H*C) / a) / (0.67 * PI) - 0.8 * W;
    if (!(T > 0.0)) return std::unexpected(emc::domain_error("dual thickness non-positive", "thickness"));
    return T * U;
}

emc::Result<emc::units::Length> solve_width(const DualSolveWidth& in) {
    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r)
        return std::unexpected(r.error());
    const double H = in.height.numerical_value_in(U);
    const double C = in.gap.numerical_value_in(U);
    const double T = in.thickness.numerical_value_in(U);
    const double a = aa(in.z0.numerical_value_in(ohm), in.relative_permittivity);
    const double W = (std::sqrt((64.0*H*H + 64.0*H*C) / a) / (0.67 * PI) - T) / 0.8;
    if (!(W > 0.0)) return std::unexpected(emc::domain_error("dual width non-positive", "width"));
    return W * U;
}

}  // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **mp-units `Length` inputs** — the dual calculator has four geometry fields (H/C/T/W); typing them means
  unit handling is a single call-site type decision per field, not branching on a unit selector inside the
  math. Every solver body is one expression.
- **A fifth `solve_gap` free function** — the solve-for-X surface has one extra target (`C`), so it is
  just another named solver sharing the `aa()` helper; no special-casing.
- **`emc::constants::pi`** — the shared full-precision `pi` is used identically in the forward `Z0` and
  every inverse, so the round-trip tests would expose any divergence.
- **`std::expected` `validate()` + `emc::require_nonzero`** — geometry-band checks become typed errors,
  and `require_nonzero(H)` in `solve_gap` guards the `/(64·H)` denominator as a typed `DivisionByZero`.
- **`std::sqrt`/`std::log`/`std::exp` (`constexpr` in C++23)** — keep the forward core compile-evaluable.

### 5. Example usage

```c++
#include <emc/component/dual_stripline_trace.hpp>
#include <mp-units/systems/si.h>
#include <print>

using namespace mp_units::si::unit_symbols;   // mm, ohm
using namespace emc::component;

int main() {
    auto r = calculate({ .height = 0.2 * mm, .gap = 0.05 * mm, .thickness = 0.014 * mm,
                         .width = 0.06 * mm, .relative_permittivity = 4.7 });
    if (!r) { std::println("dual: {}", r.error().what()); return 1; }
    std::println("Z0 = {:.2f} ohm", r->z0.numerical_value_in(ohm));

    auto g = solve_gap({ .z0 = r->z0, .height = 0.2*mm, .thickness = 0.014*mm,
                         .width = 0.06*mm, .relative_permittivity = 4.7 });
    if (g) std::println("recovered gap = {:.4f} mm", g->numerical_value_in(mm));
}
```

### 6. Unit tests — `tests/component/dual_stripline_trace_test.cpp`

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/dual_stripline_trace.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using namespace emc::component;
using emc::ErrorCode;

namespace {
constexpr double PI = emc::constants::pi;
constexpr auto pf_in = si::pico<si::farad>  / si::inch;
constexpr auto ps_in = si::pico<si::second> / si::inch;
}

// (a) HAND-COMPUTED known value, from the closed form above.
//     H=122, C=8.5, T=8.1, W=25, eps_r=9.866046252:
//       d  = 0.67*pi*(0.8*25+8.1)
//       Z0 = 0.5*( 60*ln(8*122/d)/sqrt(eps) + 60*ln(8*(122+8.5)/d)/sqrt(eps) ) ~= 54.19471 ohm
TEST_CASE("dual stripline known value", "[component][dual]") {
    auto r = calculate({ .height = 122.0*mm, .gap = 8.5*mm, .thickness = 8.1*mm, .width = 25.0*mm,
                         .relative_permittivity = 9.866046252 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0, 54.19471 * ohm, 1e-4));
}

// (b) ROUND-TRIP Z0<->gap (and Z0<->H).
TEST_CASE("dual stripline round-trips", "[component][dual][roundtrip]") {
    const DualStriplineInput in{ .height = 122.0*mm, .gap = 8.5*mm, .thickness = 8.1*mm,
                                 .width = 25.0*mm, .relative_permittivity = 9.866046252 };
    auto fwd = calculate(in);
    REQUIRE(fwd.has_value());
    auto g = solve_gap({ .z0 = fwd->z0, .height = in.height, .thickness = in.thickness,
                         .width = in.width, .relative_permittivity = 9.866046252 });
    REQUIRE(g.has_value());
    REQUIRE(emc::test::approx(*g, in.gap, 1e-6));
    auto h = solve_height({ .z0 = fwd->z0, .gap = in.gap, .thickness = in.thickness,
                            .width = in.width, .relative_permittivity = 9.866046252 });
    REQUIRE(h.has_value());
    REQUIRE(emc::test::approx(*h, in.height, 1e-6));
}

// (c) VALIDATION / EDGE.
TEST_CASE("dual stripline validation", "[component][dual][validate]") {
    SECTION("eps_r out of range") {
        auto r = calculate({ .height = 122.0*mm, .gap = 8.5*mm, .thickness = 8.1*mm, .width = 25.0*mm,
                             .relative_permittivity = 16.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
    SECTION("non-positive gap") {
        auto r = calculate({ .height = 122.0*mm, .gap = 0.0*mm, .thickness = 8.1*mm, .width = 25.0*mm,
                             .relative_permittivity = 9.8 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive(gap)
        REQUIRE(r.error().field == "gap");
    }
    SECTION("solve_gap guards H==0") {
        auto g = solve_gap({ .z0 = 50.0*ohm, .height = 0.0*mm, .thickness = 8.1*mm, .width = 25.0*mm,
                             .relative_permittivity = 9.8 });
        REQUIRE_FALSE(g.has_value());
        REQUIRE(g.error().code == ErrorCode::DivisionByZero);
    }
}

// (d) CONSTEXPR smoke.
static_assert([] {
    const double z0 = 0.5 * (60.0*std::log(8.0*122.0/(0.67*emc::constants::pi*(0.8*25.0+8.1)))/std::sqrt(9.866046252)
                            + 60.0*std::log(8.0*(122.0+8.5)/(0.67*emc::constants::pi*(0.8*25.0+8.1)))/std::sqrt(9.866046252));
    return z0 > 54.1 && z0 < 54.3;
}(), "dual core constexpr ~54.19 ohm");
```

Guards: **(a)** an absolute hand-computed value; **(b)** *two* inverses (gap and height) invert the
forward; **(c)** `OutOfRange` for eps/gap and a `DivisionByZero` for the `/(64·H)` denominator;
**(d)** `constexpr`.

### 7. Design notes

- **Five named solvers over one core.** The extra unknown `C` adds `solve_gap` with no change to the
  shared `aa()`/`forward_core` machinery.
- **`emc::constants::pi`** is shared by forward and every inverse.
- **`solve_gap` denominator guard:** `require_nonzero(H)` makes the `(a·P² − 64H²)/(64H)` form a typed
  `DivisionByZero` rather than `inf`.

---

## Embedded Microstrip

### 1. Overview

A microstrip buried under a thin dielectric cover of total height `h1` (the trace's plane spacing is `H`,
copper `T`, cover top at `h1`) — the established embedded-microstrip relation. This calculator is
**forward-only**: it exposes `solve_impedance` (Z0) but no inverse `solve_*`, because the embedded
geometry has no closed-form inverse we can validate against an independent hand computation.

Forward formula — `h1` is the cover total height, `H` the dielectric height, `T` copper, `W` width:

```text
Z0   = 87·ln( 5.98·H / (0.8·W + T) ) · (1 − (h1 − H − T)/0.1) / sqrt(eps_r + 1.41)   [ohm]
Tpd  = 84.75·sqrt( 0.475·eps_r·(1 + exp(−1.55·h1/H)) + 0.67 )                          [ps/cm]
C0   = Tpd / Z0                                                                        [pF/cm]
```

> [!IMPORTANT]
> **Embedded is the unit-load-bearing case.** Unlike the other three, the `(h1 − H − T)/0.1` term carries
> an absolute length scale, so `Z0` is **not** scale-invariant — the working unit is a real choice, not a
> free one. We pin the working unit explicitly so the result depends on the geometry's absolute scale, and
> the type system makes that choice once. The closed form is dimensioned for trace geometry on the order
> of a few mils; the `Length` inputs and the pinned working unit fix the scale unambiguously.

The `Input` carries `cover_height (h1), height (H), thickness (T), width (W)` as typed `Length`, plus
`eps_r`; the `Result` carries `Z0`, `C0`, `Tpd`.

### 2. Public header — `include/emc/component/embedded_microstrip_trace.hpp`

```c++
// include/emc/component/embedded_microstrip_trace.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

struct EmbeddedMicrostripInput {
    emc::units::Length cover_height {};   // h1 : top of the cover dielectric
    emc::units::Length height       {};   // H  : trace -> plane dielectric height
    emc::units::Length thickness    {};   // T  : copper thickness
    emc::units::Length width        {};   // W  : trace width
    double             relative_permittivity = 4.7;
};

struct EmbeddedMicrostripResult {
    emc::units::Impedance            z0  {};
    emc::units::CapacitancePerLength c0  {};   // F/m
    emc::units::TimePerLength        tpd {};   // s/m
};

// FORWARD only — the embedded geometry has no validated closed-form inverse, so we expose no
// solve_* here. (Deriving one would be unverified physics.)
[[nodiscard]] emc::Result<EmbeddedMicrostripResult> calculate(const EmbeddedMicrostripInput&);
[[nodiscard]] std::expected<void, emc::Error>       validate (const EmbeddedMicrostripInput&);

struct EmbeddedMicrostripTrace {
    using Input  = EmbeddedMicrostripInput;
    using Result = EmbeddedMicrostripResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<EmbeddedMicrostripTrace>);

}  // namespace emc::component
```

### 3. Implementation — `src/component/embedded_microstrip_trace.cpp`

```c++
// src/component/embedded_microstrip_trace.cpp
#include <emc/component/embedded_microstrip_trace.hpp>
#include <emc/component/detail/board_units.hpp>

#include <cmath>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::mm;
using mp_units::si::unit_symbols::ohm;

namespace {
// Working unit. Because Z0 is NOT scale-invariant here, the working unit is load-bearing and pinned
// explicitly: the closed form expects the geometry expressed in the same coherent scale used to derive
// it. Reading the typed Length in metres fixes that absolute scale once; the type system guarantees the
// caller's display unit (mm or mil) is converted exactly, with no branching.
constexpr auto U = si::metre;

struct Core { double z0, c0_pf, tpd_ps; };

Core forward_core(double h1, double H, double T, double W, double eps) {
    const double ln  = std::log(5.98 * H / (0.8 * W + T));
    const double z0  = 87.0 * ln * (1.0 - (h1 - H - T) / 0.1) / std::sqrt(eps + 1.41);
    const double tpd = 84.75 * std::sqrt(0.475 * eps * (1.0 + std::exp(-(1.55 * h1 / H))) + 0.67);
    return Core{ .z0 = z0, .c0_pf = tpd / z0, .tpd_ps = tpd };
}
}  // namespace

std::expected<void, emc::Error> validate(const EmbeddedMicrostripInput& in) {
    // Read in the SAME working scale used by the core.
    const double h1 = in.cover_height.numerical_value_in(U);
    const double H  = in.height.numerical_value_in(U);
    const double T  = in.thickness.numerical_value_in(U);
    const double W  = in.width.numerical_value_in(U);

    if (auto r = emc::in_range(in.relative_permittivity, 1.0, 15.0, "relative_permittivity"); !r) return r;
    if (auto r = emc::require_positive(H, "height");    !r) return r;
    if (auto r = emc::require_positive(W, "width");     !r) return r;
    if (auto r = emc::require_positive(T, "thickness"); !r) return r;

    // 0.1 <= W/H <= 3.
    if (auto r = emc::in_range(W / H, 0.1, 3.0, "width_to_height_ratio"); !r) return r;
    // The cover must sit above the trace: h1 > H + T. There is no finite upper bound on cover height,
    // so this is a DomainError, not an in_range check.
    if (!(h1 > H + T))
        return std::unexpected(emc::domain_error("cover_height must exceed height + thickness",
                                                 "cover_height"));
    return {};
}

emc::Result<EmbeddedMicrostripResult> calculate(const EmbeddedMicrostripInput& in) {
    return validate(in).transform([&] {
        const double h1 = in.cover_height.numerical_value_in(U);
        const double H  = in.height.numerical_value_in(U);
        const double T  = in.thickness.numerical_value_in(U);
        const double W  = in.width.numerical_value_in(U);
        const Core c    = forward_core(h1, H, T, W, in.relative_permittivity);
        return EmbeddedMicrostripResult{
            .z0  = c.z0 * ohm,
            // Display labels are pF/cm and ps/cm; store SI and let the caller select the display unit.
            .c0  = c.c0_pf * (si::pico<si::farad> / si::centi<si::metre>),
            .tpd = c.tpd_ps * (si::pico<si::second> / si::centi<si::metre>),
        };
    });
}

}  // namespace emc::component
```

> [!WARNING]
> **Cover-placement guard.** The `h1 > H + T` check is a `DomainError` (not an `in_range`) because the
> cover height has no finite upper bound — only the lower bound `H + T` is physically meaningful, and it
> also keeps the `(1 − (h1 − H − T)/0.1)` factor well-defined.

### 4. Modern C++ features used here — and why

- **mp-units `Length` inputs pin the load-bearing scale.** Embedded is the one board-impedance calculator
  whose `Z0` is *not* scale-invariant, so the absolute scale matters. Typing the inputs lets us pick one
  faithful working unit (`metre`) once; the type system converts the caller's mm or mil to that scale
  exactly, with no per-field unit branching.
- **`std::expected` `validate()`** — the geometry-band and cover-placement checks become typed errors,
  including the `h1 ≤ H + T` guard that becomes a `DomainError`.
- **`std::exp` in `tpd`** — the embedded propagation-delay formula's `exp(−1.55·h1/H)` term is part of the
  pure core; `constexpr <cmath>` keeps it compile-evaluable.
- **`[[nodiscard]]` + designated initializers** — as elsewhere.

### 5. Example usage

```c++
#include <emc/component/embedded_microstrip_trace.hpp>
#include <mp-units/systems/si.h>
#include <print>

using namespace mp_units::si::unit_symbols;   // mm, ohm
using namespace emc::component;

int main() {
    auto r = calculate({ .cover_height = 0.042 * mm, .height = 0.00455 * mm, .thickness = 0.0238 * mm,
                         .width = 0.001 * mm, .relative_permittivity = 11.41627113 });
    if (!r) { std::println("embedded: {}", r.error().what()); return 1; }
    std::println("Z0 = {:.5f} ohm", r->z0.numerical_value_in(ohm));   // -> 2.11445
}
```

### 6. Unit tests — `tests/component/embedded_microstrip_trace_test.cpp`

```c++
#include <catch2/catch_test_macros.hpp>

#include <emc/component/embedded_microstrip_trace.hpp>
#include "support/approx.hpp"

#include <cmath>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;        // mm, ohm
using namespace emc::component;
using emc::ErrorCode;

namespace {
constexpr auto pf_cm = si::pico<si::farad>  / si::centi<si::metre>;
constexpr auto ps_cm = si::pico<si::second> / si::centi<si::metre>;
}

// (a) HAND-COMPUTED known value, from the closed form above (geometry in metres).
//     h1=0.042, H=0.00455, T=0.0238, W=0.001, eps_r=11.41627113:
//       ln  = ln(5.98*H/(0.8*W+T))
//       Z0  = 87*ln*(1-(h1-H-T)/0.1)/sqrt(eps+1.41)                         ~= 2.11445 ohm
//       Tpd = 84.75*sqrt(0.475*eps*(1+exp(-1.55*h1/H))+0.67)                ~= 209.19232 ps/cm
//       C0  = Tpd/Z0                                                        ~= 98.93454 pF/cm
TEST_CASE("embedded microstrip known value", "[component][embedded]") {
    auto r = calculate({ .cover_height = 0.042*mm, .height = 0.00455*mm, .thickness = 0.0238*mm,
                         .width = 0.001*mm, .relative_permittivity = 11.41627113 });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->z0,  2.11445 * ohm, 1e-5));
    REQUIRE(emc::test::approx(r->c0,  98.93454 * pf_cm, 1e-4));
    REQUIRE(emc::test::approx(r->tpd, 209.19232 * ps_cm, 1e-4));
}

// (b) MONOTONICITY property — at fixed stack-up, deeper burial (larger h1) lowers Z0
//     (the (1 - (h1 - H - T)/0.1) factor decreases as h1 grows).
TEST_CASE("embedded microstrip Z0 decreases with cover height", "[component][embedded][property]") {
    auto base = calculate({ .cover_height = 0.030*mm, .height = 0.005*mm, .thickness = 0.001*mm,
                            .width = 0.006*mm, .relative_permittivity = 4.7 });
    auto deep = calculate({ .cover_height = 0.034*mm, .height = 0.005*mm, .thickness = 0.001*mm,
                            .width = 0.006*mm, .relative_permittivity = 4.7 });
    REQUIRE(base.has_value());
    REQUIRE(deep.has_value());
    REQUIRE(deep->z0.numerical_value_in(ohm) < base->z0.numerical_value_in(ohm));
}

// (c) VALIDATION / EDGE.
TEST_CASE("embedded microstrip validation", "[component][embedded][validate]") {
    SECTION("eps_r out of range") {
        auto r = calculate({ .cover_height = 0.042*mm, .height = 0.00455*mm, .thickness = 0.0238*mm,
                             .width = 0.001*mm, .relative_permittivity = 0.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
    SECTION("cover below trace -> DomainError") {
        auto r = calculate({ .cover_height = 0.001*mm, .height = 0.00455*mm, .thickness = 0.0238*mm,
                             .width = 0.001*mm, .relative_permittivity = 11.4 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DomainError);
        REQUIRE(r.error().field == "cover_height");
    }
}

// (d) CONSTEXPR smoke (geometry in metres).
static_assert([] {
    const double h1=0.042, H=0.00455, T=0.0238, W=0.001, e=11.41627113;
    const double z0 = 87.0*std::log(5.98*H/(0.8*W+T))*(1.0-(h1-H-T)/0.1)/std::sqrt(e+1.41);
    return z0 > 2.11 && z0 < 2.12;
}(), "embedded core constexpr ~2.11 ohm");
```

Guards: **(a)** an absolute hand-computed `2.11445 ohm` (plus `C0`/`Tpd`); **(b)** a physical monotonicity
property the closed form must obey; **(c)** `OutOfRange` for eps and a `DomainError` for the cover-placement
guard; **(d)** `constexpr`.

### 7. Design notes

- **Embedded is the only board-impedance calculator whose unit handling is load-bearing.** The
  `(h1 − H − T)/0.1` term means `Z0` is *not* scale-invariant; the working unit `metre` is pinned
  explicitly and the typed inputs guarantee an exact conversion from the caller's display unit.
- **Forward only.** The embedded geometry has no closed-form inverse we can validate, so the calculator
  ships `solve_impedance` and no `solve_*` solvers.
- **`h1 ≤ H + T` guard** is a `DomainError`, protecting the `(1 − (h1 − H − T)/0.1)` factor.
- **Display labels** are `pF/cm` / `ps/cm`; the `Result` stores SI and the caller selects the display unit.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `emc::units::*`, `emc::Result`,
  `emc::in_range`/`require_positive`/`require_nonzero`, `emc::ErrorCode`, `emc::domain_error`,
  `emc::Calculator`/`ValidatedCalculator`, and the `emc::test::approx` helper reused above.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the reference-vector
  harness and tolerance model that `emc::test::approx` feeds.