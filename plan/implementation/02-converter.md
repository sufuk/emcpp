# Implementation Guide — Converter (`emc::converter`) 📡

Code-level implementation (header + `.cpp` + tests) for the five `emc::converter` calculators.

| Calculator | Header | Impl | Tests |
|---|---|---|---|
| Antenna Factor → Gain | `include/emc/converter/antenna_factor.hpp` | `src/converter/antenna_factor.cpp` | `tests/converter/antenna_factor_test.cpp` |
| E-Field → Power Density | `include/emc/converter/efield_power_density.hpp` | `src/converter/efield_power_density.cpp` | `tests/converter/efield_power_density_test.cpp` |
| Energy ↔ Frequency (bidirectional) | `include/emc/converter/energy_frequency.hpp` | `src/converter/energy_frequency.cpp` | `tests/converter/energy_frequency_test.cpp` |
| Wavelength ↔ Frequency (bidirectional) | `include/emc/converter/wavelength_frequency.hpp` | `src/converter/wavelength_frequency.cpp` | `tests/converter/wavelength_frequency_test.cpp` |
| VSWR / RC / RL / ML / IL (multi-output) | `include/emc/converter/vswr.hpp` | `src/converter/vswr.cpp` | `tests/converter/vswr_test.cpp` |

All five live in `namespace emc::converter` and follow the canonical Input/Result/`calculate` triple from
[`00-foundation-code.md`](00-foundation-code.md), reusing the foundation surface
there: `emc::constants::{c,h}`, the `emc::units::*` quantity
aliases, `emc::Result<T>` / `emc::ErrorCode`, the `in_range` / `require_positive` validators, and the
`emc::test::approx` helper. The two bidirectional converters (Energy↔Frequency, Wavelength↔Frequency) are
**distinctly named free functions sharing a `detail::` core**.

> [!NOTE]
> Test expected values come from hand computation and textbook closed-form examples, augmented with
> round-trip, property, monotonicity, edge, and `constexpr` checks. The two bidirectional converters
> additionally assert that forward and inverse are exact inverses.

---

## Antenna Factor → Gain

### 1. Overview

Converts an antenna's **antenna factor** `AF` (in dB/m, the field-to-voltage transduction figure) at a
given frequency into its **realized gain** in dBi. This is a **forward-only** converter.

```text
lambda = c / f
gain   = 10 * log10( ( 9.73 / ( lambda * 10^(AF/20) ) )^2 )      [dBi]
```

`9.73` is the numeric form of the standard `AF = 9.73 / (λ·√(G))` antenna-factor relation, kept as a literal.

### 2. Public header — `include/emc/converter/antenna_factor.hpp`

```c++
// include/emc/converter/antenna_factor.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::converter {

/// Inputs for the antenna-factor → gain conversion.
/// `antenna_factor` is a dB/m log quantity, modeled with the canonical Decibel
/// wrapper — it is NOT a linear mp-units unit.
struct AntennaFactorInput {
    emc::units::Frequency frequency{};            ///< f  (e.g. 100.0 * MHz)
    emc::units::Decibel   antenna_factor{};       ///< AF [dB/m]
};

/// Result of the conversion.
struct AntennaFactorResult {
    emc::units::Decibel gain{};                   ///< realized gain [dBi]
    emc::units::Length  wavelength{};             ///< λ = c/f (handy intermediate, exposed)
};

/// Range / positivity checks. Frequency must be > 0 (λ = c/f). AF is unbounded in
/// principle but we reject non-finite input upstream via require_positive on f only.
[[nodiscard]] std::expected<void, emc::Error>
validate(const AntennaFactorInput& in);

/// Forward conversion AF (dB/m) @ f  ->  gain (dBi).
[[nodiscard]] emc::Result<AntennaFactorResult>
calculate(const AntennaFactorInput& in);

// --- bind to the Calculator concept (compile-time contract) ------------------
struct AntennaFactorToGain {
    using Input  = AntennaFactorInput;
    using Result = AntennaFactorResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::converter::validate(in); }
};
static_assert(emc::ValidatedCalculator<AntennaFactorToGain>);

} // namespace emc::converter
```

### 3. Implementation — `src/converter/antenna_factor.cpp`

```c++
// src/converter/antenna_factor.cpp
#include <emc/converter/antenna_factor.hpp>

#include <cmath>       // std::log10, std::pow

#include <emc/core/constants.hpp>

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;

std::expected<void, emc::Error> validate(const AntennaFactorInput& in) {
    // λ = c/f requires f > 0.
    const double f_hz = in.frequency.numerical_value_in(Hz);
    return emc::require_positive(f_hz, "frequency");
}

emc::Result<AntennaFactorResult> calculate(const AntennaFactorInput& in) {
    return validate(in).and_then([&]() -> emc::Result<AntennaFactorResult> {
        // λ = c / f.
        const emc::units::Length lambda = (emc::constants::c / in.frequency).in(m);
        const double lambda_m = lambda.numerical_value_in(m);

        // gain = 10*log10( (9.73 / (λ * 10^(AF/20)))^2 )
        const double af   = in.antenna_factor.value;
        const double term = 9.73 / (lambda_m * std::pow(10.0, af / 20.0));
        const double gain = 10.0 * std::log10(term * term);

        return AntennaFactorResult{
            .gain       = emc::units::Decibel{gain},
            .wavelength = lambda,
        };
    });
}

} // namespace emc::converter
```

### 4. Modern C++ features used here — and why

- **`emc::constants::c` (mp-units quantity)** — `c / frequency` is dimension-checked: it *cannot* compile
  against a non-frequency, so a wrong-dimension operand is a compile error rather than a silent numeric bug.
- **`emc::units::Decibel` wrapper** — AF and gain are logarithmic dB values. Modeling them as the canonical
  typed wrapper (not a linear mp-units unit) keeps `10^(AF/20)` explicit and prevents the linear
  physics (`λ` in metres) and the log values from mixing silently.
- **Designated-initializer `AntennaFactorInput`** — call sites read
  `{.frequency = 100.0*MHz, .antenna_factor = Decibel{2.0}}`; the unit lives in the quantity, so there is no
  hand-applied MHz scale factor at the call site.
- **`std::expected` + `.and_then`** — the validation guard composes with the math in one expression; an
  out-of-domain input (`f ≤ 0`) is reported as a recoverable typed error, not an `inf`.
- **`[[nodiscard]]`** — a discarded `Result` is a compile warning, so a caller can't silently drop the gain.

### 5. Example usage

```c++
#include <print>
#include <emc/converter/antenna_factor.hpp>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void demo() {
    const emc::converter::AntennaFactorInput in{
        .frequency      = 100.0 * (mega<Hz>),     // 100 MHz
        .antenna_factor = emc::units::Decibel{12.0},  // 12 dB/m
    };

    if (auto r = emc::converter::calculate(in)) {
        std::print("gain = {:.3f} dBi  (λ = {:.4f} m)\n",
                   r->gain.value, r->wavelength.numerical_value_in(m));
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/converter/antenna_factor_test.cpp`

```c++
// tests/converter/antenna_factor_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>

#include <emc/converter/antenna_factor.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::converter::AntennaFactorInput;

// (a) Hand-computed value.
//     f = 300 MHz -> λ = c/f. With AF = 0 dB/m: gain = 10*log10((9.73/λ)^2).
TEST_CASE("antenna factor: known value", "[converter][antenna_factor]") {
    const double f   = 300.0e6;
    const double lam = emc::constants::c.numerical_value_in(m / s) / f;     // ≈ 0.99931 m
    const double expected = 10.0 * std::log10(std::pow(9.73 / lam, 2.0));   // AF = 0
    const AntennaFactorInput in{ .frequency = 300.0 * (mega<Hz>),
                                 .antenna_factor = emc::units::Decibel{0.0} };
    auto r = emc::converter::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->gain.value == Catch::Approx(expected).epsilon(1e-9));
    REQUIRE(emc::test::approx(r->wavelength, (emc::constants::c / (300.0 * (mega<Hz>))).in(m)));
}

// (b) Monotonicity property: raising AF by 20 dB/m lowers gain by exactly 20 dB
//     (10^(AF/20) doubles in the log argument's denominator -> -20 dB on gain).
TEST_CASE("antenna factor: +20 dB/m AF => -20 dBi gain", "[converter][antenna_factor][property]") {
    const auto g = [](double af) {
        return emc::converter::calculate(
                   {.frequency = 100.0 * (mega<Hz>), .antenna_factor = emc::units::Decibel{af}})
            ->gain.value;
    };
    REQUIRE((g(0.0) - g(20.0)) == Catch::Approx(20.0).epsilon(1e-9));
}

// (c) Validation: f = 0 must be OutOfRange, never inf.
TEST_CASE("antenna factor: zero frequency is rejected", "[converter][antenna_factor][validation]") {
    auto r = emc::converter::calculate({.frequency = 0.0 * Hz,
                                        .antenna_factor = emc::units::Decibel{2.0}});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);   // require_positive => OutOfRange
}
```

Test guards: (a) pins one closed-form number and the `λ = c/f` intermediate; (b) locks the dB-arithmetic
invariant that the `10^(AF/20)` term implies; (c) proves `validate()` rejects `f = 0` with
`ErrorCode::OutOfRange` instead of emitting `inf`.

> [!TIP]
> Frequency inputs default to MHz at the front end (`row * mega<Hz>`); assuming plain Hz would be six orders
> of magnitude off. Keep the unit attached to the quantity rather than scaling by hand.

---

## E-Field → Power Density

### 1. Overview

Converts a plane-wave **electric field strength** `E` and a **wave impedance** `η` into the far-field
**power density** `P_D = E²/η`. Forward-only.

```text
P_D = (1 / η) * E^2          [W/m^2]   with E in V/m, η in Ω
```

The default wave impedance is `377 Ω` (free space).

> [!NOTE]
> `(V/m)² / Ω = V²/(m²·Ω) = W/m²` exactly — mp-units derives this, so the result type is a true
> `PowerDensity` with no hand-fudged scale factor.

### 2. Public header — `include/emc/converter/efield_power_density.hpp`

```c++
// include/emc/converter/efield_power_density.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::converter {

/// E-field -> power-density inputs. wave_impedance defaults to free-space 377 Ω.
struct EFieldPowerDensityInput {
    emc::units::ElectricField electric_field{};                 ///< E [V/m]
    emc::units::Impedance     wave_impedance = default_eta();   ///< η [Ω], default 377

    static emc::units::Impedance default_eta();                 ///< 377 Ω (declared, defined in .cpp)
};

struct EFieldPowerDensityResult {
    emc::units::PowerDensity power_density{};                    ///< P_D [W/m^2]
};

[[nodiscard]] std::expected<void, emc::Error>
validate(const EFieldPowerDensityInput& in);

[[nodiscard]] emc::Result<EFieldPowerDensityResult>
calculate(const EFieldPowerDensityInput& in);

struct EFieldToPowerDensity {
    using Input  = EFieldPowerDensityInput;
    using Result = EFieldPowerDensityResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::converter::validate(in); }
};
static_assert(emc::ValidatedCalculator<EFieldToPowerDensity>);

} // namespace emc::converter
```

### 3. Implementation — `src/converter/efield_power_density.cpp`

```c++
// src/converter/efield_power_density.cpp
#include <emc/converter/efield_power_density.hpp>

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::ohm;
using mp_units::si::unit_symbols::V;
using mp_units::si::unit_symbols::m;
using mp_units::si::unit_symbols::W;

emc::units::Impedance EFieldPowerDensityInput::default_eta() {
    return 377.0 * ohm;     // free-space wave impedance default
}

std::expected<void, emc::Error> validate(const EFieldPowerDensityInput& in) {
    // η = 0 would divide by zero.
    return emc::require_nonzero(in.wave_impedance.numerical_value_in(ohm), "wave_impedance");
}

emc::Result<EFieldPowerDensityResult> calculate(const EFieldPowerDensityInput& in) {
    return validate(in).and_then([&]() -> emc::Result<EFieldPowerDensityResult> {
        // P_D = E^2 / η. mp-units derives (V/m)^2 / Ω = W/m^2 for us.
        const auto e  = in.electric_field;
        const auto pd = (e * e / in.wave_impedance).in(W / (m * m));
        return EFieldPowerDensityResult{ .power_density = pd };
    });
}

} // namespace emc::converter
```

### 4. Modern C++ features used here — and why

- **mp-units derived units** — `E*E/η` *computes* its own `W/m²` dimension instead of returning a bare
  number that a label has to assert is `W/m²`; a wrong field unit can no longer slip through.
- **Member default via `default_eta()`** — exposes the `377 Ω` free-space default as a typed quantity, so a
  call site can omit `wave_impedance` and still get free-space behavior, with no magic literal at the boundary.
- **`emc::require_nonzero`** — turns the unguarded `1/η` into an explicit `DivisionByZero` error: the inputs
  have physical domains, so an out-of-domain `η = 0` is reported as a recoverable typed error.
- **`std::expected` + `.and_then`** — one-expression validate-then-compute, with no error-reporting concern
  inside the math.

### 5. Example usage

```c++
#include <print>
#include <emc/converter/efield_power_density.hpp>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void demo() {
    // Uses the 377 Ω free-space default for η.
    const emc::converter::EFieldPowerDensityInput in{ .electric_field = 10.0 * (V / m) };

    if (auto r = emc::converter::calculate(in)) {
        std::print("P_D = {:.6f} W/m^2\n", r->power_density.numerical_value_in(W / (m * m)));
    } else {
        std::print("error: {}\n", r.error().message);
    }
}
```

### 6. Unit tests — `tests/converter/efield_power_density_test.cpp`

```c++
// tests/converter/efield_power_density_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/converter/efield_power_density.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::converter::EFieldPowerDensityInput;

// (a) Hand-computed free-space value: 19.4 V/m into free space (377 Ω) -> ~1.0 W/m^2.
TEST_CASE("efield->power: known free-space value", "[converter][efield_pd]") {
    auto r = emc::converter::calculate({ .electric_field = 19.4 * (V / m) });  // η defaults to 377
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->power_density,
                              (19.4 * 19.4 / 377.0) * (W / (m * m)), 1e-9));
}

// (b) Property: P_D scales with E^2 (double E => quadruple P_D).
TEST_CASE("efield->power: quadratic in E", "[converter][efield_pd][property]") {
    auto pd = [](double e) {
        return emc::converter::calculate({ .electric_field = e * (V / m) })
                   ->power_density.numerical_value_in(W / (m * m));
    };
    REQUIRE(pd(20.0) == Catch::Approx(4.0 * pd(10.0)).epsilon(1e-12));
}

// (c) Validation: η = 0 => DivisionByZero.
TEST_CASE("efield->power: zero impedance rejected", "[converter][efield_pd][validation]") {
    auto r = emc::converter::calculate({ .electric_field = 10.0 * (V / m),
                                         .wave_impedance = 0.0 * ohm });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::DivisionByZero);
}
```

Test guards: (a) a textbook free-space number; (b) the quadratic-in-E invariant; (c) the div-by-zero guard.

---

## Energy ↔ Frequency (bidirectional)

### 1. Overview

Photon energy/frequency converter using `E = h·f`. We model the two-way conversion as **two distinctly named
free functions** sharing a `detail::` core (bidirectional ⇒ named solvers, not a magic `std::optional` target):

```text
E[J] = h * f                    (forward:  frequency -> energy)
f    = E[J] / h                 (inverse:  energy    -> frequency)
energy is exposed to the user in eV:  E[eV] = E[J] / 1.602176634e-19
```

> [!NOTE]
> eV is not an SI base unit here. Energy stays in mp-units `Energy` (joules) internally, with a thin
> `electronvolt` boundary using the exact 2019-SI elementary charge, `1 J = 6.241509074e18 eV`.

### 2. Public header — `include/emc/converter/energy_frequency.hpp`

```c++
// include/emc/converter/energy_frequency.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace emc::converter {

// Energy in joules (mp-units). eV is a display-only boundary handled by helpers.
using Energy = emc::units::Q<mp_units::isq::energy[mp_units::si::joule]>;

/// J <-> eV display boundary. 1 J = 6.241509074e18 eV (CODATA).
inline constexpr double joules_per_ev = 1.602'176'634e-19;   // exact (2019 SI elementary charge)
[[nodiscard]] constexpr double ev_to_joule(double ev) noexcept { return ev * joules_per_ev; }
[[nodiscard]] constexpr double joule_to_ev(double j)  noexcept { return j  / joules_per_ev; }

// ----- forward: frequency -> energy ------------------------------------------
struct FrequencyToEnergyInput { emc::units::Frequency frequency{}; };
struct EnergyResult          { Energy energy{}; };

[[nodiscard]] std::expected<void, emc::Error> validate(const FrequencyToEnergyInput& in);
[[nodiscard]] emc::Result<EnergyResult>       solve_energy(const FrequencyToEnergyInput& in);

// ----- inverse: energy -> frequency ------------------------------------------
struct EnergyToFrequencyInput { Energy energy{}; };
struct FrequencyResult        { emc::units::Frequency frequency{}; };

[[nodiscard]] std::expected<void, emc::Error> validate(const EnergyToFrequencyInput& in);
[[nodiscard]] emc::Result<FrequencyResult>    solve_frequency(const EnergyToFrequencyInput& in);

} // namespace emc::converter
```

### 3. Implementation — `src/converter/energy_frequency.cpp`

```c++
// src/converter/energy_frequency.cpp
#include <emc/converter/energy_frequency.hpp>

#include <emc/core/constants.hpp>

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::J;

namespace detail {
// One shared core: E = h * f. Both solvers route through h to stay consistent.
[[nodiscard]] inline Energy energy_from_frequency(emc::units::Frequency f) {
    return (emc::constants::h * f).in(J);              // [J*s] * [1/s] = [J]
}
[[nodiscard]] inline emc::units::Frequency frequency_from_energy(Energy e) {
    return (e / emc::constants::h).in(Hz);             // [J] / [J*s] = [1/s]
}
} // namespace detail

// ----- forward -----
std::expected<void, emc::Error> validate(const FrequencyToEnergyInput& in) {
    return emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency");
}
emc::Result<EnergyResult> solve_energy(const FrequencyToEnergyInput& in) {
    return validate(in).and_then([&]() -> emc::Result<EnergyResult> {
        return EnergyResult{ .energy = detail::energy_from_frequency(in.frequency) };
    });
}

// ----- inverse -----
std::expected<void, emc::Error> validate(const EnergyToFrequencyInput& in) {
    return emc::require_positive(in.energy.numerical_value_in(J), "energy");
}
emc::Result<FrequencyResult> solve_frequency(const EnergyToFrequencyInput& in) {
    return validate(in).and_then([&]() -> emc::Result<FrequencyResult> {
        return FrequencyResult{ .frequency = detail::frequency_from_energy(in.energy) };
    });
}

} // namespace emc::converter
```

### 4. Modern C++ features used here — and why

- **`emc::constants::h` (mp-units quantity)** — one exact Planck value `6.626 070 15e-34 J·s`. `h * f` yields
  joules by construction, with no hand-tracked "is this J or eV?" bookkeeping.
- **Two named `solve_*` free functions sharing `detail::`** — the pattern for bidirectional calculators:
  `solve_energy` / `solve_frequency` route through one `detail::energy_from_frequency` / inverse, so the
  forward and inverse can never drift out of being exact inverses (a duplicated factor in each direction is an
  easy place to typo).
- **`constexpr ev_to_joule` / `joule_to_ev` with the exact `joules_per_ev`** — the eV boundary folds at
  compile time (C++23 constexpr math).
- **`std::expected` + `.and_then`** — validate-then-compute composes; the physical domain (frequency and
  energy are positive) is enforced, so a non-positive input is a recoverable typed error.

### 5. Example usage

```c++
#include <print>
#include <emc/converter/energy_frequency.hpp>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void demo() {
    namespace cv = emc::converter;

    // 2.0 eV photon -> frequency.
    const cv::EnergyToFrequencyInput e_in{ .energy = cv::ev_to_joule(2.0) * J };
    if (auto r = cv::solve_frequency(e_in))
        std::print("f = {:.4e} Hz\n", r->frequency.numerical_value_in(Hz));

    // Round-trip: that frequency back to energy (in eV).
    auto f  = cv::solve_frequency(e_in)->frequency;
    auto e2 = cv::solve_energy({ .frequency = f });
    std::print("E = {:.6f} eV\n", cv::joule_to_ev(e2->energy.numerical_value_in(J)));
}
```

### 6. Unit tests — `tests/converter/energy_frequency_test.cpp`

```c++
// tests/converter/energy_frequency_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/converter/energy_frequency.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
namespace cv = emc::converter;

// (a) Hand-computed value: 1 eV photon ~ 2.417989e14 Hz (textbook reference).
TEST_CASE("energy<->freq: 1 eV", "[converter][energy_freq]") {
    auto r = cv::solve_frequency({ .energy = cv::ev_to_joule(1.0) * J });
    REQUIRE(r.has_value());
    REQUIRE(r->frequency.numerical_value_in(Hz) ==
            Catch::Approx(2.417989e14).epsilon(1e-4));
}

// (b) Round-trip: f -> E -> f is the identity.
TEST_CASE("energy<->freq: round-trip identity", "[converter][energy_freq][property]") {
    const auto f0 = 5.0e14 * Hz;
    auto e  = cv::solve_energy({ .frequency = f0 });
    REQUIRE(e.has_value());
    auto f1 = cv::solve_frequency({ .energy = e->energy });
    REQUIRE(f1.has_value());
    REQUIRE(emc::test::approx(f1->frequency, f0, 1e-12));
}

// (c) Validation: zero / negative frequency rejected.
TEST_CASE("energy<->freq: nonpositive frequency rejected", "[converter][energy_freq][validation]") {
    auto r = cv::solve_energy({ .frequency = 0.0 * Hz });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}

// (d) constexpr boundary check.
static_assert(cv::ev_to_joule(1.0) == 1.602'176'634e-19, "eV->J boundary must be exact");
```

Test guards: (a) a textbook photon number under the exact `h`; (b) the forward/inverse really are inverses
(shared `detail::` core); (c) the positivity guard; (d) the eV boundary folds correctly at compile time.

---

## Wavelength ↔ Frequency (bidirectional)

### 1. Overview

The classic `λ = c/f` ⇄ `f = c/λ` converter, modeled as two named solvers over one `detail::` core:

```text
f = c / λ        (wavelength -> frequency)
λ = c / f        (frequency  -> wavelength)
```

> [!TIP]
> Unit selection (metres, nanometres, feet, …) belongs at the boundary via `q.in(si::...)` /
> `q.in(international::foot)`. The foot is a derived mp-units unit with the exact `0.3048 m/ft` factor, so a
> hand-typed conversion factor — and the whole class of reciprocal-factor mistakes — cannot occur. The core
> converter below works in metres and is unaffected.

### 2. Public header — `include/emc/converter/wavelength_frequency.hpp`

```c++
// include/emc/converter/wavelength_frequency.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::converter {

// ----- wavelength -> frequency -----
struct WavelengthToFrequencyInput { emc::units::Length wavelength{}; };
struct FreqResult                 { emc::units::Frequency frequency{}; };

[[nodiscard]] std::expected<void, emc::Error> validate(const WavelengthToFrequencyInput& in);
[[nodiscard]] emc::Result<FreqResult>         solve_frequency(const WavelengthToFrequencyInput& in);

// ----- frequency -> wavelength -----
struct FrequencyToWavelengthInput { emc::units::Frequency frequency{}; };
struct WaveResult                 { emc::units::Length wavelength{}; };

[[nodiscard]] std::expected<void, emc::Error> validate(const FrequencyToWavelengthInput& in);
[[nodiscard]] emc::Result<WaveResult>         solve_wavelength(const FrequencyToWavelengthInput& in);

} // namespace emc::converter
```

### 3. Implementation — `src/converter/wavelength_frequency.cpp`

```c++
// src/converter/wavelength_frequency.cpp
#include <emc/converter/wavelength_frequency.hpp>

#include <emc/core/constants.hpp>

namespace emc::converter {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;

namespace detail {
// Shared core: c = λ·f. Both directions divide the SAME exact c.
[[nodiscard]] inline emc::units::Frequency freq_from_wavelength(emc::units::Length lambda) {
    return (emc::constants::c / lambda).in(Hz);     // [m/s] / [m] = [1/s]
}
[[nodiscard]] inline emc::units::Length wavelength_from_freq(emc::units::Frequency f) {
    return (emc::constants::c / f).in(m);           // [m/s] / [1/s] = [m]
}
} // namespace detail

std::expected<void, emc::Error> validate(const WavelengthToFrequencyInput& in) {
    return emc::require_positive(in.wavelength.numerical_value_in(m), "wavelength");
}
emc::Result<FreqResult> solve_frequency(const WavelengthToFrequencyInput& in) {
    return validate(in).and_then([&]() -> emc::Result<FreqResult> {
        return FreqResult{ .frequency = detail::freq_from_wavelength(in.wavelength) };
    });
}

std::expected<void, emc::Error> validate(const FrequencyToWavelengthInput& in) {
    return emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency");
}
emc::Result<WaveResult> solve_wavelength(const FrequencyToWavelengthInput& in) {
    return validate(in).and_then([&]() -> emc::Result<WaveResult> {
        return WaveResult{ .wavelength = detail::wavelength_from_freq(in.frequency) };
    });
}

} // namespace emc::converter
```

### 4. Modern C++ features used here — and why

- **`emc::constants::c`** — one exact `299 792 458 m/s` for *both* directions. Because both solvers divide the
  *same* `c`, `λ→f→λ` is an exact round-trip.
- **Two named `solve_*` functions sharing `detail::`** — the bidirectional pattern; sharing the core makes
  inverse-consistency structural rather than a property two separate bodies have to maintain by hand.
- **mp-units `Length`/`Frequency` inputs** — callers pass `500.0 * nm` or `2.4 * GHz` and the library converts
  via `q.in(...)`; a wrong conversion factor is impossible.
- **`emc::require_positive`** — guards `λ = 0` / `f = 0`, the divide-by-zero domain edges.

### 5. Example usage

```c++
#include <print>
#include <emc/converter/wavelength_frequency.hpp>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

void demo() {
    namespace cv = emc::converter;

    // 2.4 GHz -> wavelength.
    if (auto r = cv::solve_wavelength({ .frequency = 2.4 * (giga<Hz>) }))
        std::print("λ = {:.4f} m\n", r->wavelength.numerical_value_in(m));     // ~0.1249 m

    // 1 m wavelength -> frequency.
    if (auto r = cv::solve_frequency({ .wavelength = 1.0 * m }))
        std::print("f = {:.6e} Hz\n", r->frequency.numerical_value_in(Hz));    // c
}
```

### 6. Unit tests — `tests/converter/wavelength_frequency_test.cpp`

```c++
// tests/converter/wavelength_frequency_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/converter/wavelength_frequency.hpp>
#include <emc/core/constants.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
namespace cv = emc::converter;

// (a) Hand-computed value: λ = 1 m -> f = c.
TEST_CASE("wavelength<->freq: 1 m", "[converter][wavelength_freq]") {
    auto r = cv::solve_frequency({ .wavelength = 1.0 * m });
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->frequency, emc::constants::c.numerical_value_in(m / s) * Hz));
}

// (b) Round-trip: f -> λ -> f identity.
TEST_CASE("wavelength<->freq: round-trip", "[converter][wavelength_freq][property]") {
    const auto f0 = 2.4 * (giga<Hz>);
    auto lam = cv::solve_wavelength({ .frequency = f0 });
    REQUIRE(lam.has_value());
    auto f1 = cv::solve_frequency({ .wavelength = lam->wavelength });
    REQUIRE(f1.has_value());
    REQUIRE(emc::test::approx(f1->frequency, f0, 1e-12));
}

// (c) Validation: zero wavelength rejected.
TEST_CASE("wavelength<->freq: zero wavelength rejected", "[converter][wavelength_freq][validation]") {
    auto r = cv::solve_frequency({ .wavelength = 0.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
}
```

Test guards: (a) the canonical `λ=1 m ⇒ f=c` sanity under exact `c`; (b) exact inverse round-trip;
(c) the positivity guard.

---

## VSWR / RC / RL / ML / IL (multi-output)

### 1. Overview

From a single **voltage standing-wave ratio** `VSWR (≥ 1)`, computes four mismatch figures in one shot: the
reflection coefficient `Γ`, return loss `RL`, mismatch loss `ML`, and insertion loss `IL`. Forward-only,
multi-output.

```text
Γ  = (VSWR - 1) / (VSWR + 1)                 [-]
RL = -20 * log10(Γ)                           [dB]   (== -10*log10(Γ^2) for Γ>0)
ML = -10 * log10(1 - Γ^2)                     [dB]
IL = -10 * log10(|1 + Γ|^2)                   [dB]
```

> [!IMPORTANT]
> `VSWR == 1` (a perfect match) makes `Γ = 0` and `RL = -20·log10(0) = +∞`. We reject it as a `DomainError`
> rather than returning a poisoned `-∞`/`+∞`, and reject `VSWR < 1` (unphysical) as `OutOfRange`. The math
> itself uses the `-20·log10(Γ)` form, equal to `-10·log10(|Γ|²)` for `Γ > 0`.

### 2. Public header — `include/emc/converter/vswr.hpp`

```c++
// include/emc/converter/vswr.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::converter {

/// VSWR is a pure dimensionless ratio (>= 1). Stored as a Dimensionless quantity
/// so the "ratio-ness" is in the type; the four losses are dB log values.
struct VswrInput {
    emc::units::Dimensionless vswr{};     ///< VSWR [-], must be >= 1
};

struct VswrResult {
    emc::units::Dimensionless reflection_coefficient{};  ///< Γ [-]
    emc::units::Decibel       return_loss{};             ///< RL [dB]
    emc::units::Decibel       mismatch_loss{};           ///< ML [dB]
    emc::units::Decibel       insertion_loss{};          ///< IL [dB]
};

/// Guards VSWR >= 1 (and strictly > 1 for finite return loss).
[[nodiscard]] std::expected<void, emc::Error> validate(const VswrInput& in);

[[nodiscard]] emc::Result<VswrResult> calculate(const VswrInput& in);

struct Vswr {
    using Input  = VswrInput;
    using Result = VswrResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::converter::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::converter::validate(in); }
};
static_assert(emc::ValidatedCalculator<Vswr>);

} // namespace emc::converter
```

### 3. Implementation — `src/converter/vswr.cpp`

```c++
// src/converter/vswr.cpp
#include <emc/converter/vswr.hpp>

#include <cmath>       // std::log10, std::abs

namespace emc::converter {

using namespace mp_units;
using mp_units::one;

std::expected<void, emc::Error> validate(const VswrInput& in) {
    const double vswr = in.vswr.numerical_value_in(one);
    // VSWR >= 1 is the physical floor. We require STRICTLY > 1 because VSWR == 1
    // means Γ = 0 -> RL = -20*log10(0) = +∞.
    if (!(vswr >= 1.0))
        return std::unexpected(emc::out_of_range(1.0, 1e9, "vswr"));
    if (vswr == 1.0)
        return std::unexpected(emc::domain_error(
            "VSWR == 1 implies infinite return loss (perfect match)", "vswr"));
    return {};
}

emc::Result<VswrResult> calculate(const VswrInput& in) {
    return validate(in).and_then([&]() -> emc::Result<VswrResult> {
        const double vswr  = in.vswr.numerical_value_in(one);
        const double gamma = (vswr - 1.0) / (vswr + 1.0);            // Γ

        // RL uses the -20*log10(Γ) form.
        const double rl = -20.0 * std::log10(gamma);
        const double ml = -10.0 * std::log10(1.0 - gamma * gamma);
        const double t  = std::abs(1.0 + gamma);
        const double il = -10.0 * std::log10(t * t);

        return VswrResult{
            .reflection_coefficient = gamma * one,
            .return_loss            = emc::units::Decibel{rl},
            .mismatch_loss          = emc::units::Decibel{ml},
            .insertion_loss         = emc::units::Decibel{il},
        };
    });
}

} // namespace emc::converter
```

### 4. Modern C++ features used here — and why

- **`std::expected` + a real `validate()`** — the headline design choice: `VSWR = 1` (`Γ = 0`) would otherwise
  yield `RL = -20·log10(0) = +∞`. `validate()` rejects `VSWR == 1` with `DomainError` and `VSWR < 1` with
  `OutOfRange` — a value the caller must handle, never a poisoned `-∞`.
- **Multi-output aggregate `VswrResult`** — all four figures come back in one struct via designated
  initializers, and structured bindings let a consumer pull them cleanly: `auto [g, rl, ml, il] = *r;`.
- **`emc::units::Decibel` wrappers for the losses** — RL/ML/IL are dB, not linear; the typed wrapper keeps
  them from being mistaken for the dimensionless `Γ` or for a linear power.
- **`emc::units::Dimensionless` for VSWR/Γ** — the ratio-ness is in the type; `numerical_value_in(one)`
  extracts the raw number for the closed-form math.
- **`[[nodiscard]]`** — the four-figure result can't be silently dropped.

### 5. Example usage

```c++
#include <print>
#include <emc/converter/vswr.hpp>

using namespace mp_units;

void demo() {
    const emc::converter::VswrInput in{ .vswr = 5.83 * one };

    if (auto r = emc::converter::calculate(in)) {
        auto [g, rl, ml, il] = *r;   // structured bindings over the multi-output result
        std::print("Γ={:.4f}  RL={:.3f} dB  ML={:.3f} dB  IL={:.3f} dB\n",
                   g.numerical_value_in(one), rl.value, ml.value, il.value);
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/converter/vswr_test.cpp`

```c++
// tests/converter/vswr_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <emc/converter/vswr.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using emc::converter::VswrInput;

// helper: closed-form expecteds for a given VSWR (Γ>0).
namespace {
struct Expected { double gamma, rl, ml, il; };
Expected expect(double vswr) {
    const double g = (vswr - 1.0) / (vswr + 1.0);
    return { g,
             -20.0 * std::log10(g),
             -10.0 * std::log10(1.0 - g * g),
             -10.0 * std::log10(std::pow(std::abs(1.0 + g), 2.0)) };
}
} // namespace

// (a) Hand-computed value: VSWR = 2 -> Γ = 1/3, RL ≈ 9.542 dB.
TEST_CASE("vswr: VSWR=2 known value", "[converter][vswr]") {
    auto r = emc::converter::calculate({ .vswr = 2.0 * one });
    REQUIRE(r.has_value());
    REQUIRE(r->reflection_coefficient.numerical_value_in(one)
            == Catch::Approx(1.0 / 3.0).epsilon(1e-12));
    REQUIRE(r->return_loss.value == Catch::Approx(9.5424250944).epsilon(1e-7));
}

// (b) Cross-check all four figures against the closed form for a representative VSWR.
TEST_CASE("vswr: four-figure closed form", "[converter][vswr]") {
    const double vswr = 5.83;
    auto r = emc::converter::calculate({ .vswr = vswr * one });
    REQUIRE(r.has_value());
    const auto e = expect(vswr);
    REQUIRE(r->reflection_coefficient.numerical_value_in(one) == Catch::Approx(e.gamma).epsilon(1e-9));
    REQUIRE(r->return_loss.value    == Catch::Approx(e.rl).epsilon(1e-9));
    REQUIRE(r->mismatch_loss.value  == Catch::Approx(e.ml).epsilon(1e-9));
    REQUIRE(r->insertion_loss.value == Catch::Approx(e.il).epsilon(1e-9));
}

// (c) Monotonicity property: larger VSWR => smaller return loss (worse match).
TEST_CASE("vswr: return loss decreases with VSWR", "[converter][vswr][property]") {
    const double rl2 = emc::converter::calculate({ .vswr = 2.0  * one })->return_loss.value;
    const double rl6 = emc::converter::calculate({ .vswr = 6.0  * one })->return_loss.value;
    REQUIRE(rl6 < rl2);
}

// (d) Validation: VSWR == 1 => DomainError (log(0)); VSWR < 1 => OutOfRange.
TEST_CASE("vswr: degenerate inputs rejected", "[converter][vswr][validation]") {
    auto perfect = emc::converter::calculate({ .vswr = 1.0 * one });
    REQUIRE_FALSE(perfect.has_value());
    REQUIRE(perfect.error().code == emc::ErrorCode::DomainError);

    auto below = emc::converter::calculate({ .vswr = 0.5 * one });
    REQUIRE_FALSE(below.has_value());
    REQUIRE(below.error().code == emc::ErrorCode::OutOfRange);
}
```

Test guards: (a) a textbook `VSWR = 2` case; (b) all four figures against the closed form; (c) the physical
"worse match ⇒ less return loss" monotonicity; (d) the two guards (`==1` ⇒ `DomainError`, `<1` ⇒
`OutOfRange`) that keep `log(0)` from poisoning the result.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — the canonical `emc::constants::{c,h}`, the
  `emc::units::*` quantity aliases, `emc::Result` / `emc::ErrorCode`, the `require_positive` /
  `require_nonzero` validators, and `emc::test::approx`.
