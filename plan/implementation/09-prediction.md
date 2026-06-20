# Implementation Guide — EMC Predictions (`emc::prediction`) ⚡

Copy-paste-quality C++23 implementation plan for the four **EMC prediction** calculators: induced-voltage
coupling from ESD and lightning, RF E-/H-field from EIRP, and Friis received power.

| Calculator | Header | Function(s) |
|---|---|---|
| ESD Coupling Level | `include/emc/prediction/esd_coupling.hpp` | `calculate` |
| Lightning Coupling Level | `include/emc/prediction/lightning_coupling.hpp` | `calculate` |
| RF E-Field (from EIRP) | `include/emc/prediction/rf_field.hpp` | `calculate` |
| Friis Transmission | `include/emc/prediction/friis.hpp` | `calculate` |

All four are **forward-only** pure functions sharing a single magnetic / electromagnetic core:

- A single `emc::constants::mu0` backs both coupling calculators — one CODATA value, one definition.
- The ESD model divides by the rise time `t_r`; `validate()` rejects `t_r = 0` with
  `ErrorCode::DivisionByZero` rather than emitting a silent `inf`.
- Friis uses the exact `emc::constants::c` (299 792 458 m/s) throughout the link budget.
- The E-field model names a free-space wave impedance explicitly and accepts geometry through designated
  initializers.
- Power and gain levels are the canonical `emc::units::Dbm` / `emc::units::Decibel` typed wrappers, so a
  dBm level can never be silently added to a linear watt.

> [!NOTE]
> Every header `#include`s the canonical core from [`00-foundation-code.md`](00-foundation-code.md) and
> uses its exact names: `emc::constants::{mu0, c, z0, pi}`, `emc::units::{Length, Current, Time, Frequency,
> Power, ElectricField, MagneticField, PowerDensity, Voltage, Decibel, Dbm, to_power}`, `emc::Result<T>`,
> `emc::in_range / require_positive / require_nonzero`, the `emc::Calculator` / `ValidatedCalculator`
> concepts, and the `emc::test::approx` helper.

---

## ESD Coupling Level

### 1. Overview

Computes the voltage induced in a small rectangular ground loop by the fast-rising current of an
electrostatic-discharge event, using the standard mutual-inductance / `dI/dt` model:

```text
V_ind = (mu0 * h) / (2*pi) * ln((r + d) / r) * (I_peak / t_r)
```

where `h` is the loop height, `r` the near-conductor radius/offset, `d` the loop-to-conductor distance,
`I_peak` the ESD peak current, and `t_r` the current rise time. Rise time is conventionally entered in
**nanoseconds** and the result reported in **millivolts**.

> [!WARNING]
> The formula divides by `t_r`. `validate()` makes `t_r = 0` an explicit `ErrorCode::DivisionByZero`, and
> guards that the `ln` argument `(r + d)` stays positive.

### 2. Public header — `include/emc/prediction/esd_coupling.hpp`

```c++
// include/emc/prediction/esd_coupling.hpp
#pragma once

#include <expected>

#include <emc/core/error.hpp>        // emc::Result, validators
#include <emc/core/units.hpp>        // emc::units::{Length, Current, Time, Voltage}
#include <emc/core/calculator.hpp>   // emc::ValidatedCalculator

namespace emc::prediction {

namespace si = mp_units::si;
using namespace mp_units::si::unit_symbols;   // m, A, ns, ... for the field defaults

/// Inputs for the ESD induced-coupling model. Lengths are real lengths (any unit),
/// I_peak is a current, t_r is a (rise) time. Defaults describe a valid, representative
/// ESD event so a default-constructed Input is immediately usable.
struct EsdCouplingInput {
    emc::units::Length  loop_height{0.03 * m};   ///< h   : loop height
    emc::units::Length  radius{1.0 * m};         ///< r   : near-conductor radius/offset
    emc::units::Length  distance{0.03 * m};      ///< d   : loop-to-conductor distance
    emc::units::Current peak_current{75.0 * A};  ///< I_peak
    emc::units::Time    rise_time{1.0 * ns};     ///< t_r : current rise time (entered in ns)
};

/// Output of the ESD coupling model.
struct EsdCouplingResult {
    emc::units::Voltage induced_voltage;   ///< V_ind (stored SI; display in mV)
};

/// Range / positivity / non-zero checks (rejects the t_r divide-by-zero).
[[nodiscard]] std::expected<void, emc::Error> validate(const EsdCouplingInput& in);

/// Forward solve: V_ind from the loop geometry and the ESD current ramp.
[[nodiscard]] emc::Result<EsdCouplingResult> calculate(const EsdCouplingInput& in);

// --- Calculator-concept binding (compile-time contract check) -----------------
struct EsdCoupling {
    using Input  = EsdCouplingInput;
    using Result = EsdCouplingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<EsdCoupling>);

} // namespace emc::prediction
```

### 3. Implementation — `src/prediction/esd_coupling.cpp`

```c++
// src/prediction/esd_coupling.cpp
#include <emc/prediction/esd_coupling.hpp>

#include <cmath>   // std::log

#include <emc/core/constants.hpp>   // emc::constants::mu0, ::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

std::expected<void, emc::Error> validate(const EsdCouplingInput& in) {
    // r must be > 0: it is the denominator inside ln((r+d)/r) AND ln's argument base.
    if (auto ok = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !ok)
        return ok;
    // r+d > 0 (negative d that cancels r would put a non-positive value in ln()).
    const double r = in.radius.numerical_value_in(m);
    const double d = in.distance.numerical_value_in(m);
    if (!((r + d) > 0.0))
        return std::unexpected(emc::domain_error("ln argument (r + d) must be > 0", "distance"));
    // t_r != 0 : the formula divides by t_r.
    if (auto ok = emc::require_nonzero(in.rise_time.numerical_value_in(s), "rise_time"); !ok)
        return ok;
    return {};
}

emc::Result<EsdCouplingResult> calculate(const EsdCouplingInput& in) {
    return validate(in).and_then([&]() -> emc::Result<EsdCouplingResult> {
        // dI/dt from the ESD ramp:  I_peak / t_r  (a current / time -> A/s).
        const auto di_dt = in.peak_current / in.rise_time;   // mp-units: quantity of A/s

        // V_ind = (mu0 * h / (2*pi)) * ln((r+d)/r) * (I_peak / t_r)
        // ln() is a pure ratio, so evaluate it on the dimensionless (r+d)/r.
        const double r = in.radius.numerical_value_in(m);
        const double d = in.distance.numerical_value_in(m);
        const double ln_term = std::log((r + d) / r);

        const auto v_ind =
            (emc::constants::mu0 * in.loop_height / (2.0 * emc::constants::pi)) * ln_term * di_dt;

        // (H/m * m) * (A/s) = H*A/s = Wb/s = V  -> mp-units yields a voltage; pin it to volt.
        return EsdCouplingResult{ .induced_voltage = v_ind.in(si::volt) };
    });
}

} // namespace emc::prediction
```

### 4. Modern C++ features used here — and why

- **`emc::constants::mu0`** — both coupling calculators draw `mu0` from one CODATA constant, so they agree
  by construction. EMC work needs a single, exact magnetic constant rather than a per-formula literal.
- **mp-units quantity inputs (`Length`, `Current`, `Time`)** — EMC geometry spans mm..m and currents span
  A..kA, so typed inputs make the caller pass `0.79 * m` or `790 * mm` with implicit, exact conversion and
  no per-unit scale factors to get wrong.
- **`I_peak / t_r` as a typed quotient** — mp-units forms a genuine `A/s` quantity, and the final product
  is *checked* to be a voltage by dimensional analysis; a wrong factor cannot compile.
- **`std::expected` + `require_nonzero`** — calculator inputs have physical domains, so an out-of-domain
  `t_r = 0` is reported as a typed, `[[nodiscard]]` `ErrorCode::DivisionByZero` instead of a silent `inf`.
- **`and_then` monadic chaining** — runs `calculate()` only if `validate()` succeeded; the error
  short-circuits with no nested `if`.
- **Designated initializers + member defaults** — `EsdCouplingInput{ .loop_height = 8*m, ... }` reads
  self-documenting at the call site, with named, ordered, labeled fields.

### 5. Example usage

```c++
#include <print>
#include <emc/prediction/esd_coupling.hpp>

using namespace mp_units::si::unit_symbols;

int main() {
    const emc::prediction::EsdCouplingInput in{
        .loop_height  = 8.0 * m,
        .radius       = 0.20 * m,
        .distance     = 0.79 * m,
        .peak_current = 68.0 * A,
        .rise_time    = 9.0 * ns,
    };

    if (auto r = emc::prediction::calculate(in)) {
        // Pull the result out in the display unit (millivolts).
        std::println("V_ind = {} mV",
                     r->induced_voltage.numerical_value_in(mp_units::si::milli<mp_units::si::volt>));
    } else {
        std::println("ESD coupling failed: {} (field: {})",
                     r.error().message, r.error().field);
    }
}
```

### 6. Unit tests — `tests/prediction/esd_coupling_test.cpp`

Expected values come from hand computation against the closed-form `V_ind` expression, plus
proportionality and edge-case checks.

```c++
// tests/prediction/esd_coupling_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <numbers>

#include <emc/prediction/esd_coupling.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed reference value.
//   h=8, r=0.2, d=0.79, Ipeak=68, tr=9 ns
//   V = (mu0*8/(2*pi)) * ln(0.99/0.2) * (68/(9e-9))  -> compute with the library mu0.
TEST_CASE("ESD coupling known value", "[prediction][esd]") {
    const double mu0 = 1.25663706212e-6;
    const double expected_V =
        (mu0 * 8.0 / (2.0 * std::numbers::pi)) * std::log(0.99 / 0.20) * (68.0 / 9.0e-9);

    const pred::EsdCouplingInput in{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m,
        .peak_current = 68.0 * A, .rise_time = 9.0 * ns,
    };
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->induced_voltage, expected_V * si::volt, 1e-9));
}

// (b) Property: V_ind is exactly proportional to dI/dt = I_peak / t_r.
//     Doubling I_peak doubles V; doubling t_r halves V.
TEST_CASE("ESD coupling scales with dI/dt", "[prediction][esd][property]") {
    const pred::EsdCouplingInput base{
        .loop_height = 5.0 * m, .radius = 0.5 * m, .distance = 0.5 * m,
        .peak_current = 50.0 * A, .rise_time = 2.0 * ns,
    };
    auto a = pred::calculate(base);
    auto b = pred::calculate(pred::EsdCouplingInput{
        .loop_height = base.loop_height, .radius = base.radius, .distance = base.distance,
        .peak_current = 100.0 * A, .rise_time = base.rise_time});       // 2x current
    auto c = pred::calculate(pred::EsdCouplingInput{
        .loop_height = base.loop_height, .radius = base.radius, .distance = base.distance,
        .peak_current = base.peak_current, .rise_time = 4.0 * ns});      // 2x rise time
    REQUIRE(a.has_value()); REQUIRE(b.has_value()); REQUIRE(c.has_value());
    REQUIRE(emc::test::approx(b->induced_voltage, 2.0 * a->induced_voltage, 1e-9));
    REQUIRE(emc::test::approx(c->induced_voltage, 0.5 * a->induced_voltage, 1e-9));
}

// (c) Validation / edge cases.
TEST_CASE("ESD coupling rejects t_r = 0", "[prediction][esd][error]") {
    pred::EsdCouplingInput in{};
    in.rise_time = 0.0 * ns;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

TEST_CASE("ESD coupling rejects r <= 0", "[prediction][esd][error]") {
    pred::EsdCouplingInput in{};
    in.radius = 0.0 * m;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);   // require_positive
}
```

These guard: (a) the closed-form arithmetic; (b) the linear `dI/dt` dependence; (c) the two failure paths
(`t_r = 0`, `r ≤ 0`).

### 7. Design notes

- `mu0` is the CODATA value `1.25663706212e-6`; expected values are hand-computed with that same constant,
  so the tests use a tight `1e-9` tolerance.
- The model stores SI volts; the display unit is the caller's choice via
  `.numerical_value_in(si::milli<si::volt>)`.

---

## Lightning Coupling Level

### 1. Overview

Identical loop-coupling geometry to ESD, but the lightning current is specified directly as a **slew rate
`dI/dt` [A/s]** rather than as `I_peak / t_r`, and the result is reported in **volts**:

```text
V_ind = (mu0 * h) / (2*pi) * ln((r + d) / r) * (dI/dt)
```

This is the simpler sibling of ESD: there is no `t_r` divide, so the only failure mode is the `ln`
argument.

### 2. Public header — `include/emc/prediction/lightning_coupling.hpp`

```c++
// include/emc/prediction/lightning_coupling.hpp
#pragma once

#include <expected>

#include <emc/core/error.hpp>
#include <emc/core/units.hpp>        // Length, Voltage
#include <emc/core/calculator.hpp>

namespace emc::prediction {

using namespace mp_units::si::unit_symbols;   // m, A, s

// dI/dt is a current slew rate: amperes per second. There is no named alias in the
// canonical units.hpp, so we name one locally from the ISQ derived quantity. (Kept in
// this header because only the coupling calculators need it.)
using CurrentSlewRate =
    mp_units::quantity<(mp_units::isq::current / mp_units::isq::time)
                       [mp_units::si::ampere / mp_units::si::second], double>;

/// Inputs for the lightning induced-coupling model.
struct LightningCouplingInput {
    emc::units::Length loop_height{1.0 * m};   ///< h
    emc::units::Length radius{1.0 * m};        ///< r
    emc::units::Length distance{1.0 * m};      ///< d
    CurrentSlewRate    di_dt{1.0 * (A / s)};   ///< dI/dt
};

struct LightningCouplingResult {
    emc::units::Voltage induced_voltage;   ///< V_ind (stored SI volt)
};

[[nodiscard]] std::expected<void, emc::Error> validate(const LightningCouplingInput& in);
[[nodiscard]] emc::Result<LightningCouplingResult> calculate(const LightningCouplingInput& in);

struct LightningCoupling {
    using Input  = LightningCouplingInput;
    using Result = LightningCouplingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<LightningCoupling>);

} // namespace emc::prediction
```

### 3. Implementation — `src/prediction/lightning_coupling.cpp`

```c++
// src/prediction/lightning_coupling.cpp
#include <emc/prediction/lightning_coupling.hpp>

#include <cmath>   // std::log

#include <emc/core/constants.hpp>

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

std::expected<void, emc::Error> validate(const LightningCouplingInput& in) {
    if (auto ok = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !ok)
        return ok;
    const double r = in.radius.numerical_value_in(m);
    const double d = in.distance.numerical_value_in(m);
    if (!((r + d) > 0.0))
        return std::unexpected(emc::domain_error("ln argument (r + d) must be > 0", "distance"));
    return {};
}

emc::Result<LightningCouplingResult> calculate(const LightningCouplingInput& in) {
    return validate(in).and_then([&]() -> emc::Result<LightningCouplingResult> {
        const double r = in.radius.numerical_value_in(m);
        const double d = in.distance.numerical_value_in(m);
        const double ln_term = std::log((r + d) / r);

        // V_ind = (mu0 * h / (2*pi)) * ln((r+d)/r) * dI/dt
        // (H/m * m) * (A/s) = V.
        const auto v_ind =
            (emc::constants::mu0 * in.loop_height / (2.0 * emc::constants::pi)) * ln_term * in.di_dt;

        return LightningCouplingResult{ .induced_voltage = v_ind.in(si::volt) };
    });
}

} // namespace emc::prediction
```

### 4. Modern C++ features used here — and why

- **`emc::constants::mu0`** — shared with the ESD model, so both coupling calculators use the same
  magnetic constant by construction and cannot drift apart.
- **A typed `CurrentSlewRate` (`A/s`)** — modeling `dI/dt` as a real `quantity` makes `(H/m * m) * (A/s)`
  *verified* to be volts. This is the one extra ISQ alias the prediction category needs, declared locally
  rather than polluting the canonical `units.hpp`.
- **`std::expected` validation** — a non-physical `r ≤ 0` / `r + d ≤ 0` returns a typed
  `DomainError`/`OutOfRange` rather than a `nan`, because the `ln` argument has a physical domain.
- **Designated initializers + defaults** (all 1 m, `dI/dt = 1 A/s`) give a valid default-constructed
  input.

### 5. Example usage

```c++
#include <print>
#include <emc/prediction/lightning_coupling.hpp>

using namespace mp_units::si::unit_symbols;

int main() {
    const emc::prediction::LightningCouplingInput in{
        .loop_height = 8.0 * m,
        .radius      = 0.20 * m,
        .distance    = 0.79 * m,
        .di_dt       = 1.13e9 * (A / s),     // 1.13 kA/us lightning slew
    };

    auto r = emc::prediction::calculate(in);
    if (!r) { std::println("error: {}", r.error().message); return 1; }
    std::println("V_ind = {} V", r->induced_voltage.numerical_value_in(volt));
}
```

### 6. Unit tests — `tests/prediction/lightning_coupling_test.cpp`

Reference values are hand-computed from the closed-form expression, plus a cross-consistency invariant
with the ESD kernel.

```c++
// tests/prediction/lightning_coupling_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include <emc/prediction/lightning_coupling.hpp>
#include <emc/prediction/esd_coupling.hpp>     // for the cross-consistency test
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed value: h=8, r=0.2, d=0.79, dI/dt=1.13e9.
TEST_CASE("Lightning coupling known value", "[prediction][lightning]") {
    const double mu0 = 1.25663706212e-6;
    const double V = (mu0 * 8.0 / (2.0 * std::numbers::pi)) * std::log(0.99 / 0.20) * 1.13e9;
    const pred::LightningCouplingInput in{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m, .di_dt = 1.13e9 * (A / s)};
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->induced_voltage, V * volt, 1e-9));
}

// (b) Cross-consistency with ESD: lightning with dI/dt == I_peak/t_r must equal the
//     ESD result for the same geometry. This pins both calculators to one shared core.
TEST_CASE("Lightning == ESD when dI/dt = I_peak / t_r", "[prediction][lightning][property]") {
    const double Ipeak = 68.0, tr_ns = 9.0;
    const double di_dt = Ipeak / (tr_ns * 1e-9);

    auto lc = pred::calculate(pred::LightningCouplingInput{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m, .di_dt = di_dt * (A / s)});
    auto esd = pred::calculate(pred::EsdCouplingInput{
        .loop_height = 8.0 * m, .radius = 0.20 * m, .distance = 0.79 * m,
        .peak_current = Ipeak * A, .rise_time = tr_ns * ns});
    REQUIRE(lc.has_value()); REQUIRE(esd.has_value());
    REQUIRE(emc::test::approx(lc->induced_voltage, esd->induced_voltage, 1e-9));
}

// (c) Validation edge: r <= 0 -> OutOfRange (ln domain protected).
TEST_CASE("Lightning coupling rejects r <= 0", "[prediction][lightning][error]") {
    pred::LightningCouplingInput in{};
    in.radius = 0.0 * m;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);
}
```

(a) is a closed-form hand check; (b) is the key invariant tying ESD and lightning to the *same* coupling
kernel; (c) covers the `ln`-argument domain guard.

---

## RF E-Field (from EIRP)

### 1. Overview

Given a transmitter power level in **dBm**, an antenna gain in **dBi**, and a distance, computes the
far-field **electric field**, **magnetic field**, and **power density** in the wave. The log levels are
converted to linear watts, then the standard EIRP far-field relations apply:

```text
P_t_W = 10^(P_t_dBm / 10) / 1000        // dBm -> watt
G_t_W = 10^(G_t_dBi / 10)               // dBi -> linear gain
E   = sqrt(30 * P_t_W * G_t_W) / d      // V/m   (the "root-30-P-G over d" relation)
H   = E / Z,   Z = 120*pi (≈ 377 Ω)     // A/m
P_D = E * H                             // W/m^2
```

> [!NOTE]
> `Z = 120*pi ≈ 376.99 Ω` is the textbook approximation of free-space wave impedance. The exact
> `emc::constants::z0 = 376.730313668 Ω` is available if a future model wants physically exact `H` and
> `P_D` (about 0.07 % from `120*pi`).

### 2. Public header — `include/emc/prediction/rf_field.hpp`

```c++
// include/emc/prediction/rf_field.hpp
#pragma once

#include <expected>

#include <emc/core/error.hpp>
#include <emc/core/units.hpp>        // Length, ElectricField, MagneticField, PowerDensity, Dbm, Decibel
#include <emc/core/calculator.hpp>

namespace emc::prediction {

using namespace mp_units::si::unit_symbols;   // m, km

/// Inputs for the EIRP far-field E/H/power-density model.
/// Power level and gain are LOG quantities -> the canonical typed wrappers (never bare doubles):
///   transmit_power : emc::units::Dbm     (dB relative to 1 mW)
///   gain           : emc::units::Decibel (here a dBi gain, a pure power ratio in dB)
struct RfFieldInput {
    emc::units::Dbm     transmit_power{30.0};   ///< P_t [dBm]
    emc::units::Decibel gain{10.0};             ///< G_t [dBi]
    emc::units::Length  distance{10.0 * km};    ///< d
};

/// All three far-field outputs.
struct RfFieldResult {
    emc::units::ElectricField electric_field;   ///< E   [V/m]
    emc::units::MagneticField magnetic_field;   ///< H   [A/m]
    emc::units::PowerDensity  power_density;     ///< P_D [W/m^2]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const RfFieldInput& in);
[[nodiscard]] emc::Result<RfFieldResult> calculate(const RfFieldInput& in);

struct RfField {
    using Input  = RfFieldInput;
    using Result = RfFieldResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<RfField>);

} // namespace emc::prediction
```

### 3. Implementation — `src/prediction/rf_field.cpp`

```c++
// src/prediction/rf_field.cpp
#include <emc/prediction/rf_field.hpp>

#include <cmath>   // std::pow, std::sqrt

#include <emc/core/constants.hpp>   // emc::constants::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

std::expected<void, emc::Error> validate(const RfFieldInput& in) {
    // d must be > 0: it is the denominator of E = sqrt(30*P*G)/d.
    return emc::require_positive(in.distance.numerical_value_in(m), "distance");
}

emc::Result<RfFieldResult> calculate(const RfFieldInput& in) {
    return validate(in).and_then([&]() -> emc::Result<RfFieldResult> {
        // dBm -> watt, dBi -> linear gain. The conversions live in the typed wrappers:
        //   P_t_W = 10^(dBm/10) / 1000     (since dBm is referenced to 1 mW)
        //   G_t_W = 10^(dBi/10)
        const double Pt_W = std::pow(10.0, in.transmit_power.value / 10.0) / 1000.0;
        const double Gt_W = std::pow(10.0, in.gain.value / 10.0);

        const double d_m = in.distance.numerical_value_in(m);

        // Free-space wave impedance, 120*pi.
        const double Z = 120.0 * emc::constants::pi;

        const double E = std::sqrt(30.0 * Pt_W * Gt_W) / d_m;   // V/m
        const double H = E / Z;                                  // A/m
        const double PD = E * H;                                 // W/m^2

        return RfFieldResult{
            .electric_field = E  * (V / m),
            .magnetic_field = H  * (A / m),
            .power_density  = PD * (W / (m * m)),
        };
    });
}

} // namespace emc::prediction
```

### 4. Modern C++ features used here — and why

- **`emc::units::Dbm` / `emc::units::Decibel` typed wrappers** — a dBm *level* and a dBi *gain* are
  distinct types that can never be silently summed with a linear watt; the log→linear conversion is
  centralized (see 00-foundation-code.md for the units vocabulary). Power levels are inherently
  logarithmic, so typing them removes a whole class of unit confusion.
- **mp-units `ElectricField` / `MagneticField` / `PowerDensity` outputs** — `E [V/m]`, `H [A/m]`,
  `P_D [W/m^2]` carry their dimensions; `P_D = E*H` is *checked* to be `(V/m)*(A/m) = W/m^2`.
- **Designated initializers + member defaults** — named `transmit_power` / `gain` / `distance` fields,
  with the `distance{10 * km}` default captured explicitly.
- **`std::expected` + `require_positive`** — `d = 0` (the `E = …/d` divide) becomes a typed `OutOfRange`
  because distance has a physical domain.
- **`emc::constants::pi`** in `120*pi` — full-precision pi for the wave impedance.

### 5. Example usage

```c++
#include <print>
#include <emc/prediction/rf_field.hpp>

using namespace mp_units::si::unit_symbols;

int main() {
    const emc::prediction::RfFieldInput in{
        .transmit_power = emc::units::Dbm{30.0},      // 1 W EIRP base
        .gain           = emc::units::Decibel{10.0},  // 10 dBi
        .distance       = 10.0 * km,
    };

    auto r = emc::prediction::calculate(in);
    if (!r) { std::println("error: {}", r.error().message); return 1; }

    auto [E, H, PD] = *r;   // structured bindings over the result aggregate
    std::println("E   = {} V/m",  E.numerical_value_in(V / m));
    std::println("H   = {} A/m",  H.numerical_value_in(A / m));
    std::println("P_D = {} W/m^2", PD.numerical_value_in(W / (m * m)));
}
```

### 6. Unit tests — `tests/prediction/rf_field_test.cpp`

Reference values are hand-computed for a clean 1 W / 0 dBi / 1 km scenario, plus internal-consistency and
edge checks.

```c++
// tests/prediction/rf_field_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include <emc/prediction/rf_field.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed value:
//   P_t = 30 dBm -> 1 W ; G_t = 0 dBi -> 1 ; d = 1 km = 1000 m
//   E = sqrt(30 * 1 * 1) / 1000 = sqrt(30)/1000
//   H = E / (120*pi) ; P_D = E*H
TEST_CASE("RF E-field known value", "[prediction][rffield]") {
    const double E = std::sqrt(30.0) / 1000.0;
    const double Z = 120.0 * std::numbers::pi;
    const double H = E / Z;
    const double PD = E * H;

    const pred::RfFieldInput in{
        .transmit_power = emc::units::Dbm{30.0},
        .gain           = emc::units::Decibel{0.0},
        .distance       = 1.0 * km,
    };
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(emc::test::approx(out->electric_field, E  * (V / m),       1e-9));
    REQUIRE(emc::test::approx(out->magnetic_field, H  * (A / m),       1e-9));
    REQUIRE(emc::test::approx(out->power_density,  PD * (W / (m * m)), 1e-9));
}

// (b) Property: P_D == E*H and H == E/(120*pi) for ANY input -> internal consistency.
TEST_CASE("RF E-field internal consistency", "[prediction][rffield][property]") {
    const pred::RfFieldInput in{
        .transmit_power = emc::units::Dbm{20.0}, .gain = emc::units::Decibel{6.0},
        .distance = 0.5 * km};
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    const double E = out->electric_field.numerical_value_in(V / m);
    const double H = out->magnetic_field.numerical_value_in(A / m);
    const double PD = out->power_density.numerical_value_in(W / (m * m));
    REQUIRE(H == Catch::Approx(E / (120.0 * std::numbers::pi)).epsilon(1e-12));
    REQUIRE(PD == Catch::Approx(E * H).epsilon(1e-12));
}

// (c) Validation edge: d = 0 -> OutOfRange (E = .../d divide).
TEST_CASE("RF E-field rejects d = 0", "[prediction][rffield][error]") {
    pred::RfFieldInput in{};
    in.distance = 0.0 * km;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);
}
```

(a) is a clean 1 W / 0 dBi / 1 km hand check; (b) asserts the cross-field relations hold for any input;
(c) covers the only divide.

> [!TIP]
> Distance defaults to **km**. Passing it as metres would scale `E` by 1000×, so always attach an explicit
> unit (`10.0 * km` or `1000.0 * m`) at the call site.

---

## Friis Transmission

### 1. Overview

The classic Friis link-budget equation, returning the received power **in dBm**:

```text
P_rx = 30 + 10*log10( P_tx * 10^(G_tx/10) * 10^(G_rx/10) * (c / (4*pi*R*f))^2 )
```

Here `P_tx` is the **linear** transmit power (the `30 + 10*log10` prefix converts the whole bracket to
dBm), while `G_tx` and `G_rx` are **dBi** gains. `c` is the exact free-space speed of light, `R` is range,
`f` is frequency.

> [!IMPORTANT]
> `P_tx` is a *linear watt*, not a dBm level — the gains are the only dB terms in the bracket. The `Input`
> types it as `emc::units::Power` to make that explicit.

### 2. Public header — `include/emc/prediction/friis.hpp`

```c++
// include/emc/prediction/friis.hpp
#pragma once

#include <expected>

#include <emc/core/error.hpp>
#include <emc/core/units.hpp>        // Power, Frequency, Length, Decibel, Dbm
#include <emc/core/calculator.hpp>

namespace emc::prediction {

using namespace mp_units::si::unit_symbols;   // W, Hz, m

/// Inputs for the Friis link-budget. P_tx is a linear transmit power; the two gains are
/// dBi (modeled with the canonical Decibel wrapper).
struct FriisInput {
    emc::units::Power     tx_power{1.0 * W};        ///< P_tx  (linear watt)
    emc::units::Decibel   tx_gain{0.5};             ///< G_tx  [dBi]
    emc::units::Decibel   rx_gain{0.25};            ///< G_rx  [dBi]
    emc::units::Frequency frequency{1000.0 * Hz};   ///< f
    emc::units::Length    range{10.0 * m};          ///< R
};

/// Friis output: received power level, as a typed dBm wrapper.
struct FriisResult {
    emc::units::Dbm received_power;   ///< P_rx [dBm]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const FriisInput& in);
[[nodiscard]] emc::Result<FriisResult> calculate(const FriisInput& in);

struct Friis {
    using Input  = FriisInput;
    using Result = FriisResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::prediction::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::prediction::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Friis>);

} // namespace emc::prediction
```

### 3. Implementation — `src/prediction/friis.cpp`

```c++
// src/prediction/friis.cpp
#include <emc/prediction/friis.hpp>

#include <cmath>   // std::pow, std::log10

#include <emc/core/constants.hpp>   // emc::constants::c, ::pi

namespace emc::prediction {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

std::expected<void, emc::Error> validate(const FriisInput& in) {
    // P_tx > 0 : it is inside log10(); P_tx <= 0 is a math-domain error.
    if (auto ok = emc::require_positive(in.tx_power.numerical_value_in(W), "tx_power"); !ok)
        return ok;
    // R and f appear in the denominator (c/(4*pi*R*f))^2 ; both must be non-zero.
    if (auto ok = emc::require_nonzero(in.range.numerical_value_in(m), "range"); !ok)
        return ok;
    if (auto ok = emc::require_nonzero(in.frequency.numerical_value_in(Hz), "frequency"); !ok)
        return ok;
    return {};
}

emc::Result<FriisResult> calculate(const FriisInput& in) {
    return validate(in).and_then([&]() -> emc::Result<FriisResult> {
        const double Ptx = in.tx_power.numerical_value_in(W);
        const double Gtx = in.tx_gain.value;     // dBi
        const double Grx = in.rx_gain.value;     // dBi
        const double R   = in.range.numerical_value_in(m);
        const double f   = in.frequency.numerical_value_in(Hz);

        // c is the exact speed of light.
        const double c = emc::constants::c.numerical_value_in(m / s);

        const double fspl_term = std::pow(c / (4.0 * emc::constants::pi * R * f), 2.0);

        const double Prx_dBm =
            30.0 + 10.0 * std::log10(Ptx
                                     * std::pow(10.0, Gtx / 10.0)
                                     * std::pow(10.0, Grx / 10.0)
                                     * fspl_term);

        return FriisResult{ .received_power = emc::units::Dbm{Prx_dBm} };
    });
}

} // namespace emc::prediction
```

### 4. Modern C++ features used here — and why

- **`emc::constants::c` (exact)** — one namespaced, exact, unit-bearing speed-of-light constant. Because
  `c` enters squared inside the `log10`, full precision keeps the link budget accurate to the dB.
- **`emc::units::Dbm` return type** — `P_rx` is genuinely a dBm *level*; returning a typed `Dbm` (not a
  bare double) means a downstream consumer cannot accidentally feed it into a linear-watt formula.
- **`emc::units::Decibel` gain inputs + `emc::units::Power` linear input** — the type distinction encodes
  the formula's asymmetry (gains are dB, `P_tx` is linear watts) explicitly.
- **`std::expected` + `require_positive` / `require_nonzero`** — `P_tx ≤ 0` (log domain), `R = 0`, and
  `f = 0` (the `1/(R·f)` divide) are typed failures instead of `nan`/`inf`, because each input has a
  physical domain.
- **`emc::constants::pi`** in `4*pi*R*f` — full-precision pi, uniform across the library.

### 5. Example usage

```c++
#include <print>
#include <emc/prediction/friis.hpp>

using namespace mp_units::si::unit_symbols;

int main() {
    const emc::prediction::FriisInput in{
        .tx_power  = 1.0 * W,
        .tx_gain   = emc::units::Decibel{2.0},     // 2 dBi
        .rx_gain   = emc::units::Decibel{2.0},     // 2 dBi
        .frequency = 2.4 * mega<Hz>,               // 2.4 GHz expressed in MHz literal
        .range     = 100.0 * m,
    };

    auto r = emc::prediction::calculate(in);
    if (!r) { std::println("error: {}", r.error().message); return 1; }

    // received_power is a typed Dbm; .value is the dBm number, to_power() gives linear watts.
    std::println("P_rx = {:.2f} dBm", r->received_power.value);
    std::println("P_rx = {} W", emc::units::to_power(r->received_power).numerical_value_in(W));
}
```

### 6. Unit tests — `tests/prediction/friis_test.cpp`

Reference values are hand-computed with exact `c`, plus the textbook free-space path-loss property
(6.02 dB per octave of range or frequency) and the three failure modes.

```c++
// tests/prediction/friis_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include <emc/prediction/friis.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace pred = emc::prediction;

// (a) Hand-computed value with exact c.
//   P_tx=1 W, G_tx=G_rx=0 dBi, f=1 GHz, R=1 m
//   bracket = 1 * 1 * 1 * (c/(4*pi*1*1e9))^2
//   P_rx = 30 + 10*log10(bracket)
TEST_CASE("Friis known value (exact c)", "[prediction][friis]") {
    const double c = 299792458.0;
    const double term = std::pow(c / (4.0 * std::numbers::pi * 1.0 * 1.0e9), 2.0);
    const double expected = 30.0 + 10.0 * std::log10(1.0 * 1.0 * 1.0 * term);

    const pred::FriisInput in{
        .tx_power  = 1.0 * W,
        .tx_gain   = emc::units::Decibel{0.0},
        .rx_gain   = emc::units::Decibel{0.0},
        .frequency = 1.0e9 * Hz,
        .range     = 1.0 * m,
    };
    auto out = pred::calculate(in);
    REQUIRE(out.has_value());
    REQUIRE(out->received_power.value == Catch::Approx(expected).epsilon(1e-12));
}

// (b) Property: free-space path loss. Doubling R drops P_rx by 20*log10(2) ≈ 6.0206 dB;
//     doubling f drops it by the same. Monotone-decreasing in both.
TEST_CASE("Friis path-loss properties", "[prediction][friis][property]") {
    const pred::FriisInput base{
        .tx_power = 1.0 * W, .tx_gain = emc::units::Decibel{3.0}, .rx_gain = emc::units::Decibel{3.0},
        .frequency = 1.0e9 * Hz, .range = 10.0 * m};
    auto p0 = pred::calculate(base);
    auto p2R = pred::calculate(pred::FriisInput{
        .tx_power = base.tx_power, .tx_gain = base.tx_gain, .rx_gain = base.rx_gain,
        .frequency = base.frequency, .range = 20.0 * m});
    auto p2f = pred::calculate(pred::FriisInput{
        .tx_power = base.tx_power, .tx_gain = base.tx_gain, .rx_gain = base.rx_gain,
        .frequency = 2.0e9 * Hz, .range = base.range});
    REQUIRE(p0.has_value()); REQUIRE(p2R.has_value()); REQUIRE(p2f.has_value());

    const double drop = 20.0 * std::log10(2.0);   // ~6.0206 dB
    REQUIRE(p2R->received_power.value == Catch::Approx(p0->received_power.value - drop).margin(1e-9));
    REQUIRE(p2f->received_power.value == Catch::Approx(p0->received_power.value - drop).margin(1e-9));
}

// (c) Validation edges.
TEST_CASE("Friis rejects R = 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.range = 0.0 * m;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

TEST_CASE("Friis rejects f = 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.frequency = 0.0 * Hz;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::DivisionByZero);
}

TEST_CASE("Friis rejects P_tx <= 0", "[prediction][friis][error]") {
    pred::FriisInput in{};
    in.tx_power = 0.0 * W;
    auto out = pred::calculate(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().code == ErrorCode::OutOfRange);   // require_positive
}
```

(a) is a clean 1 W / 1 GHz / 1 m check pinned to exact `c`; (b) verifies the `1/R^2` and `1/f^2` path-loss
laws via the 6.02 dB octave drop; (c) covers all three failure modes.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `emc::constants`, `emc::units` (incl.
  the `Decibel` / `Dbm` log wrappers + `to_power`), `emc::Error` / `Result` / validators, the
  `Calculator` / `ValidatedCalculator` concepts, and `emc::test::approx`.
