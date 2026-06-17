# Implementation Guide — Component: Harmonic Trap Waveform (`emc::component`, `harmonic_trap.hpp`) 〰️

A `mp-units`-typed, `std::expected`-returning free function that emits **four** outputs at once
(fundamental frequency, harmonic frequency, harmonic amplitude, envelope amplitude) for a trapezoidal
pulse train, with explicit guards against the degenerate `DC = 0`, `t_r = 0`, and `T = 0` cases that
would otherwise produce `inf`/`nan`.

The calculator lives in the **`emc::component`** namespace, declared in
`include/emc/component/harmonic_trap.hpp`, implemented in `src/component/harmonic_trap.cpp`, and tested
in `tests/component/harmonic_trap_test.cpp`. It is a **forward-only, multi-output** calculator with a
piecewise (branching) envelope.

The canonical foundation surface (constants, units, error, test helpers) is defined once in
[`00-foundation-code.md`](00-foundation-code.md); this guide uses those exact names
(`emc::constants::pi`, `emc::units::Frequency`, `emc::units::Time`, `emc::units::Voltage`,
`emc::Result`, `emc::require_positive`, `emc::in_range`, `emc::test::approx`, …).

| Calculator | What it computes |
|---|---|
| Harmonic Trap (waveform) | f₀, harmonic f, harmonic amplitude A_h (V_rms), envelope amplitude A_e (V_rms) of a trapezoidal pulse train |

---

## Harmonic Trap (waveform)

### 1. Overview

The Harmonic Trap waveform calculator models the spectral envelope of a **trapezoidal pulse train** —
the classic "two-sinc" digital-signal spectrum used to size a harmonic-suppression trap. Given the
harmonic index `n`, the pulse amplitude `A_m`, the edge transition time `t_r`, the period `T`, and the
duty cycle `DC` (in percent), it returns four quantities:

- **Fundamental frequency** `f₀ = 1 / T`
- **Harmonic frequency** `f = n · f₀`
- **Harmonic amplitude** (RMS) — the exact line amplitude at the n-th harmonic, the product of the two
  sinc terms of the trapezoid:

  ```text
  A_h = √2 · A_m · (DC/100)
        · |sinc_pi(n · DC/100)|            ← pulse-width sinc:  sin(x)/x with x = nπ·DC/100
        · |sin(nπ · t_r / T)| / (nπ · t_r / T)   ← edge sinc
  ```

- **Envelope amplitude** (RMS) — the piecewise 0 / −20 / −40 dB-per-decade asymptote, evaluated by two
  breakpoints:

  ```text
  Ae = √2 · A_m · (DC/100)                          (flat top, below the first breakpoint)
  if f > 1/(π·τ):       Ae = √2 · A_m / (nπ)        (−20 dB/dec region;  τ = (DC/100)·T)
  if f > 1/(π·t_r):     Ae = Ae · (1/(π·t_r)) / f   (−40 dB/dec region)
  ```

The RMS scale factor is `√2` (for a unit-peak fundamental), carried as `std::numbers::sqrt2` so the
constant is exact.

> [!NOTE]
> In `A_h` the absolute value wraps the *whole* width sinc (`|sin(x₁)/x₁|`) but only the **numerator**
> of the edge sinc (`|sin(x₂)| / x₂`). Since `x₂ = nπ·t_r/T > 0` for all valid inputs, `|sin(x₂)|/x₂`
> and `|sin(x₂)/x₂|` are identical here. The parenthesization below documents this established
> trapezoidal-spectrum form.

---

### 2. Public header — `include/emc/component/harmonic_trap.hpp`

```c++
// include/emc/component/harmonic_trap.hpp
#pragma once

#include <expected>

#include <emc/core/error.hpp>        // emc::Result, emc::Error, emc::ErrorCode, validators
#include <emc/core/units.hpp>        // emc::units::Frequency / Time / Voltage / Dimensionless
#include <emc/core/calculator.hpp>   // emc::Calculator concept + static_assert binding

namespace emc::component {

// ---------------------------------------------------------------------------
//  HarmonicTrapInput — the trapezoidal-pulse-train description.
//
//  Every time-valued field is a typed mp-units quantity, so a caller-supplied
//  value in s / ms / us / ns resolves to one type with a compile-time-derived
//  factor. `harmonic` (n) and `duty_cycle` are dimensionless doubles because
//  they appear bare in the spectrum formula (n is an integer index; DC is a %).
//
//  Member defaults describe a representative pulse train (n=3, A_m=10 V,
//  t_r=5 ns, T=50 ns, DC=50 %), so a default-constructed Input is valid and
//  designated-initializer call sites can override only what they care about.
// ---------------------------------------------------------------------------
struct HarmonicTrapInput {
    double               harmonic     = 3.0;     // n  [-]  (harmonic index; integer-valued in practice)
    emc::units::Voltage  amplitude    = 10.0 * mp_units::si::volt;          // A_m [V]
    emc::units::Time     transition   = 5.0  * mp_units::si::nano<mp_units::si::second>;   // t_r [s]
    emc::units::Time     period       = 50.0 * mp_units::si::nano<mp_units::si::second>;   // T   [s]
    double               duty_cycle   = 50.0;    // DC [%]  (0 < DC <= 100)
};

// ---------------------------------------------------------------------------
//  HarmonicTrapResult — the four outputs, each a typed quantity (no display
//  unit baked in; the caller picks one with .in(...)). f0/f are real frequencies;
//  A_h/A_e are RMS voltages.
// ---------------------------------------------------------------------------
struct HarmonicTrapResult {
    emc::units::Frequency fundamental_frequency;   // f0 [Hz]
    emc::units::Frequency harmonic_frequency;       // f  [Hz]
    emc::units::Voltage   harmonic_amplitude;       // A_h [V_rms]
    emc::units::Voltage   envelope_amplitude;       // A_e [V_rms]
};

// ---------------------------------------------------------------------------
//  validate() — rejects out-of-domain inputs (DC=0, T=0, t_r=0) that would
//  otherwise produce inf/nan, returning a descriptive emc::Error.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<void, emc::Error> validate(const HarmonicTrapInput& in);

// ---------------------------------------------------------------------------
//  calculate() — forward, multi-output. Returns all four spectral quantities or
//  the first validation Error.
// ---------------------------------------------------------------------------
[[nodiscard]] emc::Result<HarmonicTrapResult> calculate(const HarmonicTrapInput& in);

// ---------------------------------------------------------------------------
//  Concept binding — the calculator "as a type", checked at compile time so a
//  signature drift is a build error at the point of the mistake.
// ---------------------------------------------------------------------------
struct HarmonicTrap {
    using Input  = HarmonicTrapInput;
    using Result = HarmonicTrapResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::component::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::component::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<HarmonicTrap>);

} // namespace emc::component
```

---

### 3. Implementation — `src/component/harmonic_trap.cpp`

```c++
// src/component/harmonic_trap.cpp
#include <emc/component/harmonic_trap.hpp>

#include <cmath>      // std::sin, std::abs  (constexpr in C++23, but kept runtime here)
#include <numbers>    // std::numbers::sqrt2  (exact √2)

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::component {

namespace {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, s, V

// √2 RMS scale factor, exact.
inline constexpr double kSqrt2 = std::numbers::sqrt2_v<double>;

// sinc_pi(x) = sin(x)/x, with the removable singularity at x=0 handled.
// (Not reached for valid inputs because n>=1 and DC>0, but defensive + clear.)
[[nodiscard]] double sinc_pi(double x) noexcept {
    if (x == 0.0) return 1.0;
    return std::sin(x) / x;
}

} // namespace

// ---------------------------------------------------------------------------
//  validate() — reject out-of-domain inputs before any arithmetic runs.
//  Ranges mirror the physically meaningful domain:
//    n          : harmonic index, must be >= 1 (and finite).
//    amplitude  : peak voltage, must be > 0 (0 is degenerate: A_h=A_e=0).
//    transition : edge time t_r, must be > 0 (t_r=0 → pitrinv = 1/(pi·0) = inf).
//    period     : T, must be > 0 (T=0 → f0 = 1/0 = inf).
//    duty_cycle : 0 < DC <= 100  (DC=0 → div/0 in every sinc/tau term; DC>100 nonphysical).
// ---------------------------------------------------------------------------
std::expected<void, emc::Error> validate(const HarmonicTrapInput& in) {
    // n: must be a positive harmonic index. Reported in raw "-" units.
    if (auto r = emc::require_positive(in.harmonic, "harmonic"); !r)
        return r;
    if (in.harmonic < 1.0)
        return std::unexpected(emc::out_of_range(1.0, 1.0e9, "harmonic"));

    // amplitude > 0  (compared in volts, the field's natural unit).
    if (auto r = emc::require_positive(in.amplitude.numerical_value_in(V), "amplitude"); !r)
        return r;

    // transition time t_r > 0  (in seconds) — guards pitrinv = 1/(pi*t_r).
    if (auto r = emc::require_positive(in.transition.numerical_value_in(s), "transition"); !r)
        return r;

    // period T > 0 (in seconds) — guards f0 = 1/T and pitauinv = 1/(pi*(DC/100)*T).
    if (auto r = emc::require_positive(in.period.numerical_value_in(s), "period"); !r)
        return r;

    // duty cycle in (0, 100] % — guards every (DC/100) denominator.
    if (auto r = emc::in_range(in.duty_cycle, 0.0, 100.0, "duty_cycle"); !r)
        return r;
    if (auto r = emc::require_nonzero(in.duty_cycle, "duty_cycle"); !r)
        return r;   // DC==0 → DivisionByZero (not just OutOfRange) so the caller learns exactly why

    return {};   // all good
}

// ---------------------------------------------------------------------------
//  calculate() — pull the raw seconds / volts out of the typed quantities ONCE,
//  evaluate the closed form in those coherent units, then re-attach units to
//  the four results.
// ---------------------------------------------------------------------------
emc::Result<HarmonicTrapResult> calculate(const HarmonicTrapInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Extract numeric values in coherent SI units (seconds, volts). mp-units
    // does the unit math.
    const double n   = in.harmonic;                          // [-]
    const double Am  = in.amplitude.numerical_value_in(V);    // [V]
    const double trS = in.transition.numerical_value_in(s);   // [s]
    const double Ts  = in.period.numerical_value_in(s);       // [s]
    const double DC  = in.duty_cycle;                         // [%]
    const double pi  = emc::constants::pi;                    // full-precision pi

    const double dc  = DC / 100.0;          // duty fraction

    // ---- Fundamental & harmonic frequency -------------------------------
    const double Fo = 1.0 / Ts;             // [Hz]  (T already validated > 0)
    const double F  = n * Fo;               // [Hz]

    // ---- Harmonic amplitude A_h (the two-sinc trapezoid line, RMS) -------
    // First sinc argument:  x1 = n*pi*dc        (pulse-width term)
    // Second sinc argument: x2 = n*pi*(t_r/T)   (edge term)
    const double x1 = n * pi * dc;
    const double x2 = n * pi * (trS / Ts);

    //   |sin(x1)/x1|  *  |sin(x2)| / x2
    const double Ah =
        kSqrt2 * (Am * dc)
        * std::abs(std::sin(x1) / x1)
        * std::abs(std::sin(x2)) / x2;

    // ---- Envelope amplitude A_e (piecewise 0 / -20 / -40 dB/dec) ---------
    const double pitauinv = 1.0 / (pi * dc * Ts);   // 1/(pi * tau),  tau = dc*T
    const double pitrinv  = 1.0 / (pi * trS);        // 1/(pi * t_r)

    double Ae = kSqrt2 * (Am * dc);                  // flat top
    if (F > pitauinv)
        Ae = (kSqrt2 * Am) / (n * pi);               // -20 dB/dec branch
    if (F > pitrinv)
        Ae = Ae * pitrinv / F;                       // -40 dB/dec branch

    // ---- Re-attach units to the four outputs ----------------------------
    return HarmonicTrapResult{
        .fundamental_frequency = Fo * Hz,
        .harmonic_frequency    = F  * Hz,
        .harmonic_amplitude    = Ah * V,
        .envelope_amplitude    = Ae * V,
    };
}

} // namespace emc::component
```

> [!NOTE]
> The closed form is an empirical expression in a fixed coherent unit system (seconds, volts, Hz).
> Pulling the numeric values out with `numerical_value_in(s)` / `numerical_value_in(V)`, evaluating,
> then multiplying by `Hz`/`V` on the way out gives the caller a fully typed, dimension-checked result
> while keeping the arithmetic in one consistent unit world. The mp-units layer guarantees the inputs
> were the right *kind* of quantity (a `Length` passed as `period` would not compile).

---

### 4. Modern C++ features used here — and why

- **mp-units typed `Time` inputs (`transition`, `period`)** — EMC inputs span ns..µs edges and
  ns..ms periods, so a typed `Time` gives compile-time unit safety: the caller writes `5.0 * ns` or
  `50.0 * us` and mp-units derives the conversion factor, eliminating any chance of mixing a `ns` value
  with a `us` scale factor.
- **`std::expected<…, Error>` + `validate()`** — calculator inputs have physical domains (`DC > 0`,
  `T > 0`, `t_r > 0`), so an out-of-domain value is reported as a recoverable typed error
  (`DivisionByZero` / `OutOfRange`) the caller *must* handle, rather than propagating `inf`/`nan`.
- **Designated initializers on `HarmonicTrapInput`** — a call site `{.harmonic = 5, .duty_cycle = 50.0}`
  is self-documenting and order-independent, so the five physical inputs cannot be transposed.
- **`std::numbers::sqrt2` for the RMS factor** — the exact `std::numbers::sqrt2_v<double>` replaces any
  truncated literal, so the `√2` scaling carries full double precision.
- **`emc::constants::pi`** — one full-precision, namespaced pi for every π site in the formula,
  consistent with every other calculator (doc 04 §1.2).
- **Typed multi-output `HarmonicTrapResult` aggregate** — the four outputs (two frequencies, two RMS
  voltages) are returned as one strongly-typed, structured-binding-friendly value. The two voltages
  cannot be accidentally swapped with the two frequencies because their *types* differ.
- **`[[nodiscard]]` on `calculate`/`validate`** — ignoring the result becomes a compiler warning.
- **`constexpr`-friendly helpers + named constants** (`kSqrt2`, `sinc_pi`) — pure math with no UI
  dependency, so the core is trivially testable and (with C++23 `constexpr <cmath>`) foldable; the
  `sinc_pi(0)=1` removable-singularity handling documents the math intent.

---

### 5. Example usage — `examples/harmonic_trap_demo.cpp`

```c++
#include <print>          // std::println (C++23)

#include <emc/component/harmonic_trap.hpp>

#include <mp-units/systems/si.h>
#include <mp-units/format.h>     // formatting of quantities

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;   // ns, V, Hz, MHz, mV
    using namespace emc::component;

    // A 20 MHz clock-like trapezoid: 50 ns period, 5 ns edges, 50% duty,
    // 10 V peak, inspecting the 3rd harmonic. Designated initializers + unit
    // literals make the call site read like the spec sheet.
    const HarmonicTrapInput in{
        .harmonic   = 3.0,
        .amplitude  = 10.0 * V,
        .transition = 5.0  * ns,
        .period     = 50.0 * ns,
        .duty_cycle = 50.0,        // percent
    };

    const auto result = calculate(in);
    if (!result) {                  // handle BOTH arms of std::expected
        const emc::Error& e = result.error();
        std::println("Harmonic-trap error [{}]: {}", emc::to_string(e.code), e.message);
        return 1;
    }

    const HarmonicTrapResult& r = *result;
    // Pull each output out in a convenient DISPLAY unit with .in(...).
    std::println("f0  = {}",  r.fundamental_frequency.in(MHz));
    std::println("f   = {}",  r.harmonic_frequency.in(MHz));
    std::println("A_h = {}",  r.harmonic_amplitude.in(mV));
    std::println("A_e = {}",  r.envelope_amplitude.in(mV));

    // f0 = 20 MHz, f = 60 MHz for these inputs.
    return 0;
}
```

> [!TIP]
> The front end is framework-agnostic: any generic front end consumes the typed
> `HarmonicTrapResult` and chooses display units with `.in(...)`. The library never depends on a UI
> toolkit.

---

### 6. Unit tests — `tests/component/harmonic_trap_test.cpp` (Catch2 v3)

Expected values come from hand computation and the textbook trapezoidal-spectrum closed form, plus
property, parity, and edge checks — no external fixtures.

```c++
// tests/component/harmonic_trap_test.cpp
#include <cmath>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <emc/component/harmonic_trap.hpp>

#include "support/approx.hpp"   // emc::test::approx

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // s, V, Hz
using emc::ErrorCode;
using namespace emc::component;

// Textbook reference of the trapezoidal-spectrum closed form, in coherent SI
// units (seconds, volts, Hz). Used to prove calculate() reproduces the
// established formula exactly across both envelope breakpoints.
namespace {
struct Ref { double f0, f, ah, ae; };
Ref reference(double n, double Am, double trS, double Ts, double DC) {
    const double pi = std::numbers::pi_v<double>;
    const double s2 = std::numbers::sqrt2_v<double>;
    const double dc = DC / 100.0;
    const double Fo = 1.0 / Ts;
    const double F  = n * Fo;
    const double x1 = n * pi * dc;
    const double x2 = n * pi * (trS / Ts);
    const double Ah = s2 * (Am * dc) * std::abs(std::sin(x1) / x1)
                          * std::abs(std::sin(x2)) / x2;
    const double pitauinv = 1.0 / (pi * dc * Ts);
    const double pitrinv  = 1.0 / (pi * trS);
    double Ae = s2 * (Am * dc);
    if (F > pitauinv) Ae = (s2 * Am) / (n * pi);
    if (F > pitrinv)  Ae = Ae * pitrinv / F;
    return {Fo, F, Ah, Ae};
}
} // namespace

// (a) KNOWN-VALUE — a clean clock: 50 ns period, 5 ns edges, 50 % duty,
// 10 V peak, 3rd harmonic. Hand-computed:
//   f0 = 1/50ns = 20 MHz ;  f = 3*20 = 60 MHz.
//   dc = 0.5 ;  x1 = 3*pi*0.5 = 1.5pi  -> sin(1.5pi) = -1, |sin/x1| = 1/(1.5pi).
//   x2 = 3*pi*(5/50) = 0.3pi ;  sin(0.3pi)=0.80901699 ; |sin|/x2 = 0.80901699/0.94247780.
//   A_h = sqrt2 * (10*0.5) * (1/(1.5pi)) * (0.80901699/0.94247780).
TEST_CASE("HarmonicTrap known clock value", "[component][harmonic_trap][known]") {
    const HarmonicTrapInput in{
        .harmonic = 3.0, .amplitude = 10.0 * V,
        .transition = 5.0 * ns, .period = 50.0 * ns, .duty_cycle = 50.0,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());

    REQUIRE(emc::test::approx(r->fundamental_frequency, 20.0 * MHz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_frequency,    60.0 * MHz, 1e-9));

    const double pi = std::numbers::pi_v<double>;
    const double s2 = std::numbers::sqrt2_v<double>;
    const double ah = s2 * 5.0 * (1.0 / (1.5 * pi))
                         * (std::sin(0.3 * pi) / (0.3 * pi));
    REQUIRE(emc::test::approx(r->harmonic_amplitude, ah * V, 1e-9));
}

// (b) PARITY against the textbook closed form for a spread of cases — proves the
// ported branch logic (both envelope breakpoints) matches the established form.
TEST_CASE("HarmonicTrap reproduces the closed form", "[component][harmonic_trap][parity]") {
    struct C { double n, Am, tr, T, DC; };
    const auto c = GENERATE(values<C>({
        {3,   10, 5e-9,  50e-9, 50},      // both breakpoints below f (flat top)
        {71,  908.7, 873.86, 64.79, 96},  // large n, edges > period
        {256, 394.4, 156.98, 962.26, 12}, // low duty, -20/-40 regions
        {2,   1.0,  1e-3,  1.0,   1},      // tiny duty, slow edges
    }));
    const Ref ref = reference(c.n, c.Am, c.tr, c.T, c.DC);
    const HarmonicTrapInput in{
        .harmonic = c.n, .amplitude = c.Am * V,
        .transition = c.tr * s, .period = c.T * s, .duty_cycle = c.DC,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->fundamental_frequency, ref.f0 * Hz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_frequency,    ref.f  * Hz, 1e-9));
    REQUIRE(emc::test::approx(r->harmonic_amplitude,    ref.ah * V,  1e-9));
    REQUIRE(emc::test::approx(r->envelope_amplitude,    ref.ae * V,  1e-9));
}

// (c) PROPERTY — harmonic frequency is exactly n * fundamental, for every n.
TEST_CASE("HarmonicTrap f = n*f0 identity", "[component][harmonic_trap][property]") {
    const double n = GENERATE(1.0, 2.0, 3.0, 7.0, 99.0, 1000.0);
    const HarmonicTrapInput in{
        .harmonic = n, .amplitude = 5.0 * V,
        .transition = 2.0 * ns, .period = 100.0 * ns, .duty_cycle = 40.0,
    };
    const auto r = calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->harmonic_frequency,
                              n * r->fundamental_frequency.numerical_value_in(Hz) * Hz, 1e-12));
}

// (d) VALIDATION / EDGE — degenerate inputs return the RIGHT ErrorCode.
TEST_CASE("HarmonicTrap rejects degenerate inputs", "[component][harmonic_trap][validation]") {
    const HarmonicTrapInput base{
        .harmonic = 3.0, .amplitude = 10.0 * V,
        .transition = 5.0 * ns, .period = 50.0 * ns, .duty_cycle = 50.0,
    };

    SECTION("DC = 0 -> DivisionByZero") {
        auto in = base; in.duty_cycle = 0.0;
        auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
        REQUIRE(r.error().field == "duty_cycle");
    }
    SECTION("DC > 100 -> OutOfRange") {
        auto in = base; in.duty_cycle = 150.0;
        auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
    SECTION("period = 0 -> OutOfRange (would be f0 = 1/0)") {
        auto in = base; in.period = 0.0 * ns;
        auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "period");
    }
    SECTION("t_r = 0 -> OutOfRange (would be pitrinv = inf)") {
        auto in = base; in.transition = 0.0 * ns;
        auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "transition");
    }
    SECTION("harmonic = 0 -> OutOfRange") {
        auto in = base; in.harmonic = 0.0;
        auto r = calculate(in);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
```

What each test guards:

- **(a) known-value** — an independent hand-computation of a 20 MHz clock pins f₀, f, and A_h to a
  number derived *outside* the implementation, so a copy-paste bug in `calculate` cannot pass by
  agreeing with itself.
- **(b) parity** — exercises both envelope breakpoints (flat / −20 / −40 dB-per-decade) against a
  textbook re-derivation of the closed form, guarding the branch logic specifically.
- **(c) property** — the exact `f = n·f₀` invariant for any `n`, independent of all the other math.
- **(d) validation** — every div-by-zero / out-of-range case yields the correct `ErrorCode` and `field`.

> [!NOTE]
> `calculate()` calls `std::sin`/`std::abs`. In C++23 `<cmath>` is `constexpr`, so a `consteval` smoke
> test *is* possible on a conforming toolchain; while libstdc++/libc++ `constexpr <cmath>` support is
> still landing we keep it out of the portable test set and rely on the runtime parity test (b). A
> forward-looking `static_assert(calculate({...}))` can be added once the standard library catches up.

---

### 7. Design notes

- **Domain guard.** `DC = 0` makes every `(DC/100)` denominator zero → `A_h`, `pitauinv` blow up;
  `T = 0` makes `Fo = 1/0`; `t_r = 0` makes `pitrinv = 1/(π·0)` infinite. `validate()` rejects each with
  a descriptive `DivisionByZero` / `OutOfRange` `emc::Error` *before* any arithmetic runs.
- **Edge-sinc parenthesization.** The absolute value wraps the *numerator only* of the edge term
  (`|sin(x₂)| / x₂`) versus the whole ratio for the width term (`|sin(x₁)/x₁|`). Since `x₂ > 0` for all
  valid inputs the two are numerically identical; the form here matches the standard trapezoidal-spectrum
  expression and is documented so a reviewer does not "tidy" it into a behavioral change.
- **Typed time inputs.** `transition` and `period` are `emc::units::Time`; the caller supplies
  `* ns` / `* us` literals and mp-units derives the factor, so no per-unit branching is needed.
- **Multi-output, no solve-for.** Unlike the microstrip/stripline impedance calculators, this one is
  **forward only** — it never solves for an input. There is a single `calculate()` (no `solve_*`
  family), and the round-trip tests are replaced by the `f = n·f₀` identity and the closed-form parity
  test.

> [!IMPORTANT]
> Once `constexpr <cmath>` (`std::sin`) is broadly shipped, the entire `calculate()` becomes `constexpr`
> and a `static_assert(calculate({...}).value()...)` can pin a reference spectrum at *compile time* — the
> strongest possible regression guard.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — `emc::constants::pi`,
  `emc::units::{Frequency,Time,Voltage}`, `emc::Result`, `emc::Error`/`ErrorCode`, the
  `require_positive` / `require_nonzero` / `in_range` validators, the `ValidatedCalculator` concept, and
  the `emc::test::approx` helper.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — the mp-units
  vocabulary and typed `Time` inputs.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) — the
  `std::expected`/`ErrorCode` model.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/
  `calculate`/`validate` triple and the multi-output, forward-only calculator shape.
- [`05-component-resistance.md`](05-component-resistance.md) and
  [`03-component-capacitance.md`](03-component-capacitance.md) — sibling `emc::component` guides whose
  structure, naming, and test layout this guide mirrors.
