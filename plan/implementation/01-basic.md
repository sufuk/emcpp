# Implementation Guide — BasicCalculations (`emc::basic`) 📡

Complete, copy-paste-quality C++23 for the five foundational EMC calculators in the `BasicCalculations`
group — **Skin Depth**, the bidirectional **Decibel Calculator**, the multi-output **Dipole** and **Loop**
antenna near-field calculators, and the branch-driven **Far-Field Criteria** calculator. Each section gives
the public header, the compiled `.cpp`, the modern-C++ rationale, an example call site, and the Catch2 v3
tests (hand-computed + property + validation).

Everything lives in namespace `emc::basic`, with headers under `include/emc/basic/` and bodies under
`src/basic/`:

| Calculator | Header | Reference inputs |
|---|---|---|
| Skin Depth | `include/emc/basic/skin_depth.hpp` | `freq`, `material` |
| Decibel Calculator | `include/emc/basic/decibel.hpp` | `dB`, `dBm`, `load` |
| Dipole Antenna | `include/emc/basic/dipole_antenna.hpp` | `I₀`, `l`, `R`, `f`, `θ` |
| Loop Antenna | `include/emc/basic/loop_antenna.hpp` | `I₀`, `A`, `R`, `f`, `θ` |
| Far-Field Criteria | `include/emc/basic/far_field_criteria.hpp` | `freq`, `length` |

Every calculator returns `emc::Result<…>`, validates with `emc::in_range` / `emc::require_positive` /
`emc::require_nonzero`, pulls constants from `emc::constants`, materials from `emc::materials`, and compares
reference values with `emc::test::approx` — all defined in
[`00-foundation-code.md`](./00-foundation-code.md).

> [!NOTE]
> The foundation declarations are canonical. From `emc::constants`: `pi` (a `double`), `c` (m/s), `mu0`
> (H/m). From `emc::units`: `Frequency`, `Length`, `Area`, `Current`, `Voltage`, `Power`, `Impedance`,
> `ElectricField`, `MagneticField`, `Angle`, `Dimensionless`, and the typed `Decibel` / `Dbm` wrappers. From
> `emc::materials`: `Material`, `MaterialProperties`, `properties()`. The `Calculator` /
> `ValidatedCalculator` concepts and the `tests/support/` helpers (`approx`) come from the foundation too.
> This guide reuses their exact names and does not re-spell them.

---

## Skin Depth

### 1. Overview

Skin depth δ is the distance into a conductor at which a sinusoidal current density falls to 1/e of its
surface value. The standard closed form is

```text
δ = sqrt( 1 / (π · f · μ · σ) )      with  μ = μ₀ · μ_r
```

The user supplies frequency (Hz/kHz/MHz/GHz) and either a material (which fills σ and μ_r from the material
database) or a custom σ and μ_r. Forward-only, single output (length).

### 2. Public header — `include/emc/basic/skin_depth.hpp`

```c++
// include/emc/basic/skin_depth.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for a skin-depth calculation.
///
/// Two ways to specify the conductor:
///   * pick a `material` (Copper, Silver, …) and leave `conductivity`/`relative_permeability`
///     at their defaults — `calculate()` fills them from `emc::materials::properties()`; or
///   * set `material = Material::Custom` and supply `conductivity` and `relative_permeability`
///     yourself.
struct SkinDepthInput {
    emc::units::Frequency    frequency{};                       ///< f  > 0
    emc::materials::Material  material = emc::materials::Material::Copper;
    emc::units::Conductivity conductivity{};                    ///< σ, used iff material == Custom
    double                    relative_permeability = 1.0;      ///< μ_r, used iff material == Custom
};

/// Result of a skin-depth calculation.
struct SkinDepthResult {
    emc::units::Length skin_depth{};   ///< δ  [m]
};

/// Range/positivity checks on the physical inputs.
[[nodiscard]] std::expected<void, emc::Error> validate(const SkinDepthInput& in);

/// δ = sqrt(1 / (π·f·μ₀·μ_r·σ)). Resolves the material first, then validates, then computes.
[[nodiscard]] emc::Result<SkinDepthResult> calculate(const SkinDepthInput& in);

// --- bind to the Calculator concept (compile-time contract) ---
struct SkinDepth {
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<SkinDepth>);

} // namespace emc::basic
```

### 3. Implementation — `src/basic/skin_depth.cpp`

```c++
// src/basic/skin_depth.cpp
#include <emc/basic/skin_depth.hpp>

#include <cmath>   // std::sqrt (constexpr in C++23)

#include <mp-units/math.h>          // mp_units::sqrt for quantities
#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, S, m, ...

namespace {

// Resolve σ and μ_r either from the material table or from the Custom fields.
// Returns (conductivity, mu_r) on success.
[[nodiscard]] emc::Result<std::pair<emc::units::Conductivity, double>>
resolve_conductor(const SkinDepthInput& in) {
    if (in.material == emc::materials::Material::Custom)
        return std::pair{in.conductivity, in.relative_permeability};

    return emc::materials::properties(in.material)
        .transform([](const emc::materials::MaterialProperties& p) {
            return std::pair{p.conductivity, p.relative_permeability};
        });
}

} // namespace

std::expected<void, emc::Error> validate(const SkinDepthInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;

    if (in.material == emc::materials::Material::Custom) {
        const double sigma = in.conductivity.numerical_value_in(S / m);
        if (auto r = emc::require_positive(sigma, "conductivity"); !r) return r;
        if (auto r = emc::require_positive(in.relative_permeability, "relative_permeability"); !r)
            return r;
    }
    return {};
}

emc::Result<SkinDepthResult> calculate(const SkinDepthInput& in) {
    // 1) resolve material (typed UnknownMaterial error for Custom-without-props is handled by validate)
    return resolve_conductor(in).and_then(
        [&](std::pair<emc::units::Conductivity, double> conductor)
            -> emc::Result<SkinDepthResult> {
            const auto [sigma, mu_r] = conductor;

            // 2) validate using the resolved values.
            SkinDepthInput resolved = in;
            resolved.material              = emc::materials::Material::Custom; // force the field checks
            resolved.conductivity          = sigma;
            resolved.relative_permeability = mu_r;
            if (auto v = validate(resolved); !v)
                return std::unexpected(v.error());

            // 3) μ = μ₀ · μ_r   (μ_r dimensionless double, μ₀ an mp-units quantity)
            const auto mu = emc::constants::mu0 * mu_r;            // H/m

            // 4) denominator = π·f·μ·σ  →  carries units of 1/m^2 (a "per area").
            const auto denom = emc::constants::pi * in.frequency * mu * sigma;

            // 5) δ = sqrt(1/denom). mp_units::sqrt gives the unit-correct √(m²) = m.
            const emc::units::Length delta = sqrt(1.0 / denom);

            return SkinDepthResult{ .skin_depth = delta };
        });
}

} // namespace emc::basic
```

> [!NOTE]
> The two-step validate resolves σ/μ_r first, then runs the positivity checks on the resolved values, so a
> corrupt table row is caught before it can produce a nonsensical δ.

### 4. Modern C++ features used here — and why

- **mp-units quantity inputs (`Frequency`, `Conductivity`)** — EMC inputs span Hz..GHz, so carrying the unit
  in the type gives compile-time unit safety and removes any need for caller-side scale factors;
  `frequency.in(MHz)` / `delta.in(um)` derive display units on demand.
- **`mp_units::sqrt`** — `sqrt(1/denom)` returns a `Length`; the compiler proves √(of a per-area) is a
  length, so "meters-ness" is a type guarantee rather than a comment.
- **`emc::constants::mu0` / `emc::constants::pi`** — one CODATA-correct source of truth for μ₀ and π, shared
  by every calculator.
- **`emc::materials::properties()` + `std::expected`** — a single database lookup, and an unknown material
  becomes a typed, recoverable error instead of a silent fallback.
- **Monadic `transform` / `and_then`** — material lookup → validation → math reads as a pipeline; the error
  short-circuits cleanly.
- **Designated initializers** — `SkinDepthInput{ .frequency = 27.0 * MHz, .material = Material::Nickel }`
  names every field, so argument order can never be transposed.
- **`[[nodiscard]]`** — ignoring the `Result` is a compiler warning.

### 5. Example usage

```c++
#include <emc/basic/skin_depth.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units::si::unit_symbols;
    using emc::materials::Material;

    const emc::basic::SkinDepthInput in{
        .frequency = 100.0 * MHz,
        .material  = Material::Copper,
    };

    if (auto r = emc::basic::calculate(in)) {
        std::println("skin depth = {} µm", r->skin_depth.numerical_value_in(micro<m>));
    } else {
        std::println("error [{}]: {}", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/basic/skin_depth_test.cpp`

```c++
// tests/basic/skin_depth_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/basic/skin_depth.hpp>
#include "support/approx.hpp"

#include <mp-units/math.h>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
using emc::materials::Material;

// (a) Reference table: δ re-derived from the same material database, across several materials.
TEST_CASE("skin depth matches an independent re-derivation", "[basic][skin_depth][reference]") {
    const auto mat = GENERATE(Material::Copper, Material::Silver, Material::Gold,
                              Material::Aluminium, Material::Nickel);
    const auto f_mhz = GENERATE(1.0, 27.0, 100.0, 433.0);

    const emc::basic::SkinDepthInput in{ .frequency = f_mhz * MHz, .material = mat };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    // Independent re-derivation δ = sqrt(1/(π f μ0 μ_r σ)) from the same table.
    const auto props = emc::materials::properties(mat).value();
    const auto mu    = emc::constants::mu0 * props.relative_permeability;
    const emc::units::Length expected =
        sqrt(1.0 / (emc::constants::pi * (f_mhz * MHz) * mu * props.conductivity));

    REQUIRE(emc::test::approx(got->skin_depth, expected, 1e-9));
}

// (b) Hand-computed textbook value. Copper, 1 MHz.
//     σ = 5.96e7 S/m, μ_r ≈ 1, μ0 = 1.25663706212e-6 H/m, f = 1e6 Hz.
//     δ = sqrt(1/(π·1e6·(1.25663706212e-6)·5.96e7)) ≈ 6.521e-5 m = 65.21 µm.
TEST_CASE("skin depth: copper @ 1 MHz ~ 65 µm", "[basic][skin_depth][known]") {
    const emc::basic::SkinDepthInput in{ .frequency = 1.0 * MHz, .material = Material::Copper };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->skin_depth, 65.21 * micro<m>, 1e-3));
}

// (c) Property: δ scales as 1/√f. Quadrupling f halves δ.
TEST_CASE("skin depth halves when frequency quadruples", "[basic][skin_depth][property]") {
    const auto lo = emc::basic::calculate({ .frequency = 1.0 * MHz, .material = Material::Copper });
    const auto hi = emc::basic::calculate({ .frequency = 4.0 * MHz, .material = Material::Copper });
    REQUIRE(lo.has_value());
    REQUIRE(hi.has_value());
    REQUIRE(emc::test::approx(hi->skin_depth, lo->skin_depth / 2.0, 1e-9));
}

// (d) Validation/edge tests.
TEST_CASE("skin depth rejects bad input with typed errors", "[basic][skin_depth][validate]") {
    SECTION("zero frequency → not positive (OutOfRange)") {
        const auto r = emc::basic::calculate({ .frequency = 0.0 * Hz, .material = Material::Gold });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "frequency");
    }
    SECTION("Custom with non-positive σ → OutOfRange") {
        const auto r = emc::basic::calculate({
            .frequency = 1.0 * MHz, .material = Material::Custom,
            .conductivity = 0.0 * (S / m), .relative_permeability = 1.0 });
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
```

What each guards: (a) the formula against an independent re-derivation across materials; (b) the absolute
scale (a sign/constant slip would move δ by orders of magnitude); (c) the 1/√f physics law (catches an
exponent bug); (d) that bad input becomes the *right* `ErrorCode`.

---

## Decibel Calculator (bidirectional)

### 1. Overview

A bidirectional converter with **two independent panels**:

1. **Gain panel** — any of {dB, voltage gain, power gain} drives the other two:
   `dB = 20·log₁₀(V_gain) = 10·log₁₀(P_gain)`, so `V_gain = 10^(dB/20)`, `P_gain = 10^(dB/10)`.
2. **Power/level panel** — any of {dBm, power [W], voltage [V], Vp-of-sinusoid [V]} drives the rest, given a
   load R [Ω]:
   - `power = 10^((dBm − 30)/10)` watts,
   - `dBm = 10·log₁₀(power / 0.001)`,
   - `voltage = √(power · R)`   (RMS voltage into the load),
   - `Vp = voltage · √2`        (peak of the sinusoid).

Because every field can be a driver, this is **bidirectional**: per the foundation pattern we provide
distinctly-named free `*_from_*` functions sharing a `detail::` core, rather than one `calculate()`.

### 2. Public header — `include/emc/basic/decibel.hpp`

```c++
// include/emc/basic/decibel.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

// ---------------------------------------------------------------------------
//  Gain panel: dB ⇄ voltage gain ⇄ power gain. All three are dimensionless.
//  emc::units::Decibel is the typed log wrapper from the foundation (NOT a linear
//  mp-units unit), so a dB value can never be added to a voltage by accident.
// ---------------------------------------------------------------------------
struct GainResult {
    emc::units::Decibel decibels{};      ///< dB
    double              voltage_gain{};  ///< V₁/V₂  = 10^(dB/20)
    double              power_gain{};    ///< P₁/P₂  = 10^(dB/10)
};

[[nodiscard]] emc::Result<GainResult> gain_from_db(emc::units::Decibel db);
[[nodiscard]] emc::Result<GainResult> gain_from_voltage_gain(double voltage_gain);
[[nodiscard]] emc::Result<GainResult> gain_from_power_gain(double power_gain);

// ---------------------------------------------------------------------------
//  Power/level panel: dBm ⇄ power ⇄ voltage ⇄ Vp, all relative to a load R.
// ---------------------------------------------------------------------------
struct LevelResult {
    emc::units::Dbm     dbm{};        ///< dBm (= dB relative to 1 mW)
    emc::units::Power   power{};      ///< P   [W]
    emc::units::Voltage voltage{};    ///< RMS voltage into the load  [V]
    emc::units::Voltage peak{};       ///< Vp of the sinusoid = V·√2  [V]
};

/// Every solver needs the load R to convert between power and voltage.
[[nodiscard]] emc::Result<LevelResult> level_from_dbm(emc::units::Dbm dbm, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_power(emc::units::Power power, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_voltage(emc::units::Voltage v, emc::units::Impedance load);
[[nodiscard]] emc::Result<LevelResult> level_from_peak(emc::units::Voltage vp, emc::units::Impedance load);

} // namespace emc::basic
```

### 3. Implementation — `src/basic/decibel.cpp`

```c++
// src/basic/decibel.cpp
#include <emc/basic/decibel.hpp>

#include <cmath>   // std::pow, std::log10, std::sqrt

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // V, W, ohm

namespace detail {

// One reference: 1 mW. Used by both the dBm and the "power/0.001" forms.
inline constexpr auto one_milliwatt = 1.0 * milli<W>;

// Build the full gain triple from a dB value (the single source of truth).
[[nodiscard]] GainResult gain_core(double db) {
    return GainResult{
        .decibels     = emc::units::Decibel{db},
        .voltage_gain = std::pow(10.0, db / 20.0),   // V_gain = 10^(dB/20)
        .power_gain   = std::pow(10.0, db / 10.0),   // P_gain = 10^(dB/10)
    };
}

// Build the full level quad from a power and a load (the single source of truth).
[[nodiscard]] emc::Result<LevelResult> level_core(emc::units::Power power, emc::units::Impedance load) {
    const double p_w = power.numerical_value_in(W);
    const double r   = load.numerical_value_in(ohm);

    if (auto v = emc::require_positive(p_w, "power"); !v) return std::unexpected(v.error());
    if (auto v = emc::require_positive(r,   "load");  !v) return std::unexpected(v.error());

    // dBm = 10·log10(P / 1mW)
    const double dbm = 10.0 * std::log10(power.numerical_value_in(milli<W>));
    // V = √(P·R)   (RMS into the load);   Vp = V·√2
    const emc::units::Voltage v_rms = std::sqrt(p_w * r) * V;
    const emc::units::Voltage v_pk  = std::sqrt(2.0) * v_rms;

    return LevelResult{
        .dbm     = emc::units::Dbm{dbm},
        .power   = power,
        .voltage = v_rms,
        .peak    = v_pk,
    };
}

} // namespace detail

// ---- gain panel ----------------------------------------------------------
emc::Result<GainResult> gain_from_db(emc::units::Decibel db) {
    return detail::gain_core(db.value);
}

emc::Result<GainResult> gain_from_voltage_gain(double voltage_gain) {
    if (auto v = emc::require_positive(voltage_gain, "voltage_gain"); !v)
        return std::unexpected(v.error());          // log10(≤0) is a domain error
    return detail::gain_core(20.0 * std::log10(voltage_gain));   // dB = 20·log10(V_gain)
}

emc::Result<GainResult> gain_from_power_gain(double power_gain) {
    if (auto v = emc::require_positive(power_gain, "power_gain"); !v)
        return std::unexpected(v.error());
    return detail::gain_core(10.0 * std::log10(power_gain));     // dB = 10·log10(P_gain)
}

// ---- power / level panel -------------------------------------------------
emc::Result<LevelResult> level_from_dbm(emc::units::Dbm dbm, emc::units::Impedance load) {
    // P[W] = 10^((dBm − 30)/10)
    const emc::units::Power power = std::pow(10.0, (dbm.value - 30.0) / 10.0) * W;
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_power(emc::units::Power power, emc::units::Impedance load) {
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_voltage(emc::units::Voltage v, emc::units::Impedance load) {
    const double r = load.numerical_value_in(ohm);
    if (auto chk = emc::require_nonzero(r, "load"); !chk) return std::unexpected(chk.error());
    // P = V² / R
    const emc::units::Power power = (v * v / load).in(W);
    return detail::level_core(power, load);
}

emc::Result<LevelResult> level_from_peak(emc::units::Voltage vp, emc::units::Impedance load) {
    // V_rms = Vp / √2, then defer to the voltage solver.
    return level_from_voltage((vp / std::sqrt(2.0)).in(V), load);
}

} // namespace emc::basic
```

> [!IMPORTANT]
> The `dBm − 30` convention yields **watts** (−30 dB = factor 1e-3 = the mW→W shift), and
> `10·log10(power/0.001)` is its exact inverse. Both are paired so that the round-trip
> `dBm → power → dBm` is the identity (tested below). Do not "modernize" to a mW-based `to_power(Dbm)`
> without accounting for the mW/W factor.

### 4. Modern C++ features used here — and why

- **Distinct `*_from_*` free functions sharing a `detail::` core** — every field can drive the others, so a
  pure-function decomposition keeps the conversions composable with no shared mutable state to keep in sync.
- **Typed `emc::units::Decibel` / `Dbm` wrappers** — keep the logarithmic quantities from masquerading as
  linear mp-units units; you cannot accidentally add a `Decibel` to a `Voltage`. Linear power stays
  `emc::units::Power` in watts.
- **mp-units `Voltage`/`Power`/`Impedance`** — `V*V/R` is dimension-checked to a `Power`, so an ohms/volts
  mix-up is a compile error.
- **`std::expected` + `require_positive`/`require_nonzero`** — converter inputs have physical domains, so an
  out-of-domain value (`log10(≤0)` → `OutOfRange`, a zero load → `DivisionByZero`) is reported as a
  recoverable typed error instead of yielding `-inf`/`inf`.
- **`[[nodiscard]]`** on every solver — a discarded conversion result is a warning.

### 5. Example usage

```c++
#include <emc/basic/decibel.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units::si::unit_symbols;

    // Gain panel: how much voltage/power gain is +20 dB?
    if (auto g = emc::basic::gain_from_db(emc::units::Decibel{20.0})) {
        std::println("+20 dB  →  voltage gain {}, power gain {}", g->voltage_gain, g->power_gain);
        // → voltage gain 10, power gain 100
    }

    // Power panel: 10 dBm into a 50 Ω load.
    if (auto l = emc::basic::level_from_dbm(emc::units::Dbm{10.0}, 50.0 * ohm)) {
        std::println("10 dBm  →  P = {} mW,  V = {} V,  Vp = {} V",
                     l->power.numerical_value_in(milli<W>),
                     l->voltage.numerical_value_in(V),
                     l->peak.numerical_value_in(V));
    } else {
        std::println("error: {}", l.error().message);
    }
}
```

### 6. Unit tests — `tests/basic/decibel_test.cpp`

```c++
// tests/basic/decibel_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/basic/decibel.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

// (a) Reference table: the five outputs vs an independent closed-form re-derivation.
TEST_CASE("decibel matches an independent re-derivation", "[basic][decibel][reference]") {
    const double db   = GENERATE(-6.0, 0.0, 1.0, 3.0, 20.0);
    const double dbm  = GENERATE(-30.0, 0.0, 10.0, 13.0, 30.0);
    const double load = GENERATE(50.0, 75.0, 600.0);

    const auto g = emc::basic::gain_from_db(emc::units::Decibel{db});
    const auto l = emc::basic::level_from_dbm(emc::units::Dbm{dbm}, load * ohm);
    REQUIRE(g.has_value());
    REQUIRE(l.has_value());

    REQUIRE(g->voltage_gain == Catch::Approx(std::pow(10.0, db / 20.0)));
    REQUIRE(g->power_gain   == Catch::Approx(std::pow(10.0, db / 10.0)));

    const double p_w = std::pow(10.0, (dbm - 30.0) / 10.0);
    REQUIRE(emc::test::approx(l->power,   p_w * W, 1e-9));
    REQUIRE(emc::test::approx(l->voltage, std::sqrt(p_w * load) * V, 1e-9));
    REQUIRE(emc::test::approx(l->peak,    std::sqrt(2.0) * std::sqrt(p_w * load) * V, 1e-9));
}

// (b) Hand-computed: +20 dB ⇒ voltage gain 10, power gain 100.
TEST_CASE("decibel: +20 dB known value", "[basic][decibel][known]") {
    const auto g = emc::basic::gain_from_db(emc::units::Decibel{20.0});
    REQUIRE(g.has_value());
    REQUIRE(g->voltage_gain == Catch::Approx(10.0));
    REQUIRE(g->power_gain   == Catch::Approx(100.0));
}

// (c) Round-trip / involution tests (every field is a driver).
TEST_CASE("decibel solvers round-trip", "[basic][decibel][roundtrip]") {
    SECTION("dB → voltage gain → dB") {
        const auto g1 = emc::basic::gain_from_db(emc::units::Decibel{6.0}).value();
        const auto g2 = emc::basic::gain_from_voltage_gain(g1.voltage_gain).value();
        REQUIRE(g2.decibels.value == Catch::Approx(6.0));
    }
    SECTION("dBm → power → dBm into 50 Ω") {
        const auto a = emc::basic::level_from_dbm(emc::units::Dbm{13.0}, 50.0 * ohm).value();
        const auto b = emc::basic::level_from_power(a.power, 50.0 * ohm).value();
        REQUIRE(b.dbm.value == Catch::Approx(13.0));
    }
    SECTION("voltage → peak → voltage") {
        const auto a = emc::basic::level_from_voltage(2.0 * V, 50.0 * ohm).value();
        const auto b = emc::basic::level_from_peak(a.peak, 50.0 * ohm).value();
        REQUIRE(emc::test::approx(b.voltage, 2.0 * V, 1e-9));
    }
}

// (d) Validation/edge tests.
TEST_CASE("decibel rejects bad input", "[basic][decibel][validate]") {
    SECTION("voltage gain ≤ 0 → log domain (OutOfRange)") {
        const auto r = emc::basic::gain_from_voltage_gain(0.0);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
    SECTION("zero load on voltage solver → DivisionByZero") {
        const auto r = emc::basic::level_from_voltage(1.0 * V, 0.0 * ohm);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DivisionByZero);
    }
    SECTION("zero load on power solver → OutOfRange (must be > 0)") {
        const auto r = emc::basic::level_from_power(1.0 * W, 0.0 * ohm);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}
```

What each guards: (a) all five outputs against an independent re-derivation; (b) the textbook 20 dB anchor;
(c) the bidirectional involutions; (d) the domain (`log10`) and division-by-zero edges.

---

## Dipole Antenna (multi-output)

### 1. Overview

Near-field of a short electric dipole at distance R and polar angle θ. The standard three field components
are:

```text
E_r  = 60 · (I₀·l / R²) · cos θ · sqrt( 1 + (c / (2π f R))² )
E_θ  = 30 · (I₀·l / R)  · sin θ · sqrt( (1/R)² + ( (2π f / c) − c/(2π f R²) )² )
H_φ  = (f / (2c)) · (I₀·l / R) · sin θ · sqrt( 1 + (c / (2π f R))² )
```

with θ a polar angle in radians. Forward-only, three outputs (two E-fields in V/m, one H-field in A/m).

### 2. Public header — `include/emc/basic/dipole_antenna.hpp`

```c++
// include/emc/basic/dipole_antenna.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for the short-dipole near-field model.
struct DipoleAntennaInput {
    emc::units::Current   current{};      ///< I₀  > 0
    emc::units::Length    length{};       ///< l   > 0  (dipole length)
    emc::units::Length    distance{};     ///< R   > 0  (observation distance)
    emc::units::Frequency frequency{};    ///< f   > 0
    emc::units::Angle     theta{};        ///< θ  (store as Angle; build from degrees at the call site)
};

/// Three near-field components.
struct DipoleAntennaResult {
    emc::units::ElectricField e_r{};      ///< E_r   [V/m]
    emc::units::ElectricField e_theta{};  ///< E_θ   [V/m]
    emc::units::MagneticField h_phi{};    ///< H_φ   [A/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const DipoleAntennaInput& in);
[[nodiscard]] emc::Result<DipoleAntennaResult> calculate(const DipoleAntennaInput& in);

struct DipoleAntenna {
    using Input  = DipoleAntennaInput;
    using Result = DipoleAntennaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<DipoleAntenna>);

} // namespace emc::basic
```

### 3. Implementation — `src/basic/dipole_antenna.cpp`

```c++
// src/basic/dipole_antenna.cpp
#include <emc/basic/dipole_antenna.hpp>

#include <cmath>   // std::sin, std::cos, std::sqrt

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // A, m, Hz, V, ...

std::expected<void, emc::Error> validate(const DipoleAntennaInput& in) {
    if (auto r = emc::require_positive(in.current.numerical_value_in(A),    "current");   !r) return r;
    if (auto r = emc::require_positive(in.length.numerical_value_in(m),     "length");    !r) return r;
    if (auto r = emc::require_positive(in.distance.numerical_value_in(m),   "distance");  !r) return r;  // R² and 1/R
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r) return r;  // 1/(fR) terms
    return {};
}

emc::Result<DipoleAntennaResult> calculate(const DipoleAntennaInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    // Pull everything into base SI scalars; the 60/30 coefficients assume SI.
    const double I0   = in.current.numerical_value_in(A);
    const double l    = in.length.numerical_value_in(m);
    const double R    = in.distance.numerical_value_in(m);
    const double f    = in.frequency.numerical_value_in(Hz);
    const double th   = in.theta.numerical_value_in(si::radian);
    const double cc   = emc::constants::c.numerical_value_in(m / s);   // speed of light
    const double pi   = emc::constants::pi;

    const double cosT = std::cos(th);
    const double sinT = std::sin(th);

    // common radical √(1 + (c/(2π f R))²) — shared by E_r and H_φ
    const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));

    const double Er = 60.0 * ((I0 * l) / (R * R)) * cosT * rad_common;

    const double Etheta = 30.0 * ((I0 * l) / R) * sinT *
        std::sqrt(std::pow(1.0 / R, 2.0) +
                  std::pow((2.0 * pi * f) / cc - cc / (2.0 * pi * f * R * R), 2.0));

    const double Hphi = (f / (2.0 * cc)) * ((I0 * l) / R) * sinT * rad_common;

    return DipoleAntennaResult{
        .e_r     = Er     * (V / m),
        .e_theta = Etheta * (V / m),
        .h_phi   = Hphi   * (A / m),
    };
}

} // namespace emc::basic
```

> [!NOTE]
> The `60`, `30`, and `f/(2c)` coefficients are stated for SI base units (A, m, Hz, V/m, A/m). Evaluating
> with `numerical_value_in(SI)` and re-attaching units at the boundary (`* (V/m)`, `* (A/m)`) keeps the
> public surface fully typed while the inner arithmetic stays the plain closed form.

### 4. Modern C++ features used here — and why

- **mp-units quantity inputs** — EMC distances span m..mils and frequencies span Hz..GHz, so units in the
  type give compile-time safety; `distance = 6.7 * ft` derives the exact ft→m factor, so a wrong manual
  conversion factor cannot occur.
- **`emc::constants::c` / `emc::constants::pi`** — the antenna result uses the true CODATA c and a single π.
- **`emc::units::Angle` + `numerical_value_in(si::radian)`** — θ is a typed angle; the call site builds it
  from degrees once (see example) instead of converting at every use.
- **Designated-initializer `DipoleAntennaResult`** — the three outputs are named, so the E/H fields can
  never be written in the wrong slot.
- **`std::expected` validation** — R appears as `R²` and `1/R`, and f appears in `1/(fR)`; both have a
  physical domain (must be > 0), so an out-of-domain input is a typed error rather than an `inf`/`nan`.
- **Shared `rad_common` via a structured local** — the `√(1+(c/2πfR)²)` radical appears in both `E_r` and
  `H_φ`; computing it once documents that they share it.

### 5. Example usage

```c++
#include <emc/basic/dipole_antenna.hpp>
#include <mp-units/systems/si.h>
#include <mp-units/systems/angular.h>   // degree → radian
#include <print>

int main() {
    using namespace mp_units::si::unit_symbols;
    using mp_units::angular::unit_symbols::deg;

    const emc::basic::DipoleAntennaInput in{
        .current   = 1.0 * A,
        .length    = 6.6 * m,
        .distance  = 6.7 * m,
        .frequency = 6.3 * MHz,
        .theta     = (80.0 * deg).in(si::radian),   // build the Angle from degrees once
    };

    if (auto r = emc::basic::calculate(in)) {
        std::println("E_r   = {} V/m", r->e_r.numerical_value_in(V / m));
        std::println("E_θ   = {} V/m", r->e_theta.numerical_value_in(V / m));
        std::println("H_φ   = {} A/m", r->h_phi.numerical_value_in(A / m));
    } else {
        std::println("error: {}", r.error().message);
    }
}
```

### 6. Unit tests — `tests/basic/dipole_antenna_test.cpp`

```c++
// tests/basic/dipole_antenna_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/basic/dipole_antenna.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

namespace {
emc::units::Angle deg(double d) { return (d * emc::constants::pi / 180.0) * si::radian; }
} // namespace

// (a) Reference: E_r vs an independent closed-form re-derivation with the true c.
TEST_CASE("dipole antenna matches an independent re-derivation", "[basic][dipole][reference]") {
    const double I0 = 1.0, l = 6.6, R = 6.7, f_mhz = 6.3, theta_deg = 80.0;

    const emc::basic::DipoleAntennaInput in{
        .current   = I0 * A,
        .length    = l  * m,
        .distance  = R  * m,
        .frequency = f_mhz * MHz,
        .theta     = deg(theta_deg),
    };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    const double f  = f_mhz * 1e6, th = theta_deg * emc::constants::pi / 180.0;
    const double cc = emc::constants::c.numerical_value_in(m / s), pi = emc::constants::pi;
    const double rad = std::sqrt(1.0 + std::pow(cc / (2 * pi * f * R), 2));
    const double Er  = 60.0 * (I0 * l / (R * R)) * std::cos(th) * rad;

    REQUIRE(emc::test::approx(got->e_r, Er * (V / m), 1e-9));
}

// (b) Hand-computed sanity: at θ = 90°, cos θ = 0 ⇒ E_r = 0 exactly.
TEST_CASE("dipole: E_r vanishes at θ = 90°", "[basic][dipole][known]") {
    const emc::basic::DipoleAntennaInput in{
        .current = 1.0 * A, .length = 1.0 * m, .distance = 10.0 * m,
        .frequency = 100.0 * MHz, .theta = deg(90.0) };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->e_r, 0.0 * (V / m), 1e-6));
}

// (c) Property: the sin-driven fields E_θ and H_φ vanish on axis (θ = 0). Guards the cos/sin wiring.
TEST_CASE("dipole: sin-driven fields vanish on axis (θ = 0)", "[basic][dipole][property]") {
    const emc::basic::DipoleAntennaInput in{
        .current = 2.0 * A, .length = 1.0 * m, .distance = 5.0 * m,
        .frequency = 50.0 * MHz, .theta = deg(0.0) };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->e_theta, 0.0 * (V / m), 1e-6));
    REQUIRE(emc::test::approx(r->h_phi,   0.0 * (A / m), 1e-6));
}

// (d) Validation: R = 0 → R² and 1/R blow up; must be rejected.
TEST_CASE("dipole rejects zero distance", "[basic][dipole][validate]") {
    const emc::basic::DipoleAntennaInput in{
        .current = 1.0 * A, .length = 1.0 * m, .distance = 0.0 * m,
        .frequency = 100.0 * MHz, .theta = deg(45.0) };
    const auto r = emc::basic::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "distance");
}
```

What each guards: (a) the full E_r formula vs an independent re-derivation; (b) the cos θ factor (a sin/cos
swap would break it); (c) the sin θ factors on E_θ/H_φ; (d) the R = 0 singularity is rejected, not emitted
as `inf`.

---

## Loop Antenna (multi-output)

### 1. Overview

Near-field of a small magnetic (loop) antenna of area A, the magnetic dual of the dipole. The standard three
components are:

```text
H_r  = (f / c) · (I₀·A / R²) · cos θ · sqrt( 1 + (c/(2π f R))² )
H_θ  = (f / (2c)) · (I₀·A / R) · sin θ · sqrt( (1/R)² + ( (2π f / c) − c/(2π f R²) )² )
E_φ  = 120 · (π f / c)² · (I₀·A / R) · sin θ · sqrt( 1 + (c/(2π f R))² )
```

Forward-only, three outputs (two H-fields in A/m, one E-field in V/m).

### 2. Public header — `include/emc/basic/loop_antenna.hpp`

```c++
// include/emc/basic/loop_antenna.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for the small-loop near-field model.
struct LoopAntennaInput {
    emc::units::Current   current{};      ///< I₀  > 0
    emc::units::Area      loop_area{};    ///< A   > 0  (loop area)
    emc::units::Length    distance{};     ///< R   > 0
    emc::units::Frequency frequency{};    ///< f   > 0
    emc::units::Angle     theta{};        ///< θ
};

/// Three near-field components (dual of the dipole).
struct LoopAntennaResult {
    emc::units::MagneticField h_r{};       ///< H_r   [A/m]
    emc::units::MagneticField h_theta{};   ///< H_θ   [A/m]
    emc::units::ElectricField e_phi{};     ///< E_φ   [V/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const LoopAntennaInput& in);
[[nodiscard]] emc::Result<LoopAntennaResult> calculate(const LoopAntennaInput& in);

struct LoopAntenna {
    using Input  = LoopAntennaInput;
    using Result = LoopAntennaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<LoopAntenna>);

} // namespace emc::basic
```

### 3. Implementation — `src/basic/loop_antenna.cpp`

```c++
// src/basic/loop_antenna.cpp
#include <emc/basic/loop_antenna.hpp>

#include <cmath>   // std::sin, std::cos, std::sqrt, std::pow

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // A, m, Hz, V, ...

std::expected<void, emc::Error> validate(const LoopAntennaInput& in) {
    if (auto r = emc::require_positive(in.current.numerical_value_in(A),                   "current");   !r) return r;
    if (auto r = emc::require_positive(in.loop_area.numerical_value_in(square(m)),         "loop_area"); !r) return r;
    if (auto r = emc::require_positive(in.distance.numerical_value_in(m),                  "distance");  !r) return r;
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz),                "frequency"); !r) return r;
    return {};
}

emc::Result<LoopAntennaResult> calculate(const LoopAntennaInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    const double I0 = in.current.numerical_value_in(A);
    const double A_ = in.loop_area.numerical_value_in(square(m));
    const double R  = in.distance.numerical_value_in(m);
    const double f  = in.frequency.numerical_value_in(Hz);
    const double th = in.theta.numerical_value_in(si::radian);
    const double cc = emc::constants::c.numerical_value_in(m / s);
    const double pi = emc::constants::pi;

    const double cosT = std::cos(th);
    const double sinT = std::sin(th);

    // shared radical √(1 + (c/(2π f R))²) — appears in H_r and E_φ
    const double rad_common = std::sqrt(1.0 + std::pow(cc / (2.0 * pi * f * R), 2.0));

    const double Hr = (f / cc) * ((I0 * A_) / (R * R)) * cosT * rad_common;

    const double Htheta = (f / (2.0 * cc)) * ((I0 * A_) / R) * sinT *
        std::sqrt(std::pow(1.0 / R, 2.0) +
                  std::pow((2.0 * pi * f) / cc - cc / (2.0 * pi * f * R * R), 2.0));

    const double Ephi = 120.0 * std::pow((pi * f) / cc, 2.0) * ((I0 * A_) / R) * sinT * rad_common;

    return LoopAntennaResult{
        .h_r     = Hr     * (A / m),
        .h_theta = Htheta * (A / m),
        .e_phi   = Ephi   * (V / m),
    };
}

} // namespace emc::basic
```

### 4. Modern C++ features used here — and why

- **`emc::units::Area` input** — the loop area is a *typed area*, so a "cm² scaled by a length factor"
  mistake is impossible; `2.5 * square(cm)` derives the correct 1e-4 area factor automatically. Typed
  quantities eliminate this whole class of unit error.
- **`emc::constants::c` / `pi`** — the `(π f / c)²` term and the `f/c` prefactor use the true c and a single
  π.
- **mp-units `Current` / `MagneticField` / `ElectricField`** — the dual outputs (H in A/m, E in V/m) are
  distinguished by type, so a copy-paste that put an H output into an E field would not compile.
- **Shared `rad_common`** — same √-radical in H_r and E_φ, computed once.
- **`std::expected` validation + `[[nodiscard]]`** — R, f, A all have a physical domain (> 0); an
  out-of-domain input is a typed, recoverable error.
- **Designated initializers** for both `Input` and `Result` — every field is named.

### 5. Example usage

```c++
#include <emc/basic/loop_antenna.hpp>
#include <mp-units/systems/si.h>
#include <mp-units/systems/angular.h>
#include <print>

int main() {
    using namespace mp_units::si::unit_symbols;
    using mp_units::angular::unit_symbols::deg;

    const emc::basic::LoopAntennaInput in{
        .current   = 10.0 * A,
        .loop_area = 1.0  * square(m),
        .distance  = 10.0 * m,
        .frequency = 1.0  * MHz,
        .theta     = (45.0 * deg).in(si::radian),
    };

    if (auto r = emc::basic::calculate(in)) {
        std::println("H_r = {} A/m, H_θ = {} A/m, E_φ = {} V/m",
                     r->h_r.numerical_value_in(A / m),
                     r->h_theta.numerical_value_in(A / m),
                     r->e_phi.numerical_value_in(V / m));
    } else {
        std::println("error: {}", r.error().message);
    }
}
```

### 6. Unit tests — `tests/basic/loop_antenna_test.cpp`

```c++
// tests/basic/loop_antenna_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/basic/loop_antenna.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

namespace {
emc::units::Angle deg(double d) { return (d * emc::constants::pi / 180.0) * si::radian; }
} // namespace

// (a) Reference: E_φ (with its distinctive (πf/c)² term) vs an independent re-derivation.
TEST_CASE("loop antenna matches an independent re-derivation", "[basic][loop][reference]") {
    const double I0 = 41.8, A_ = 4.2, R = 36.2, f_mhz = 6.1, theta_deg = 157.0;

    const emc::basic::LoopAntennaInput in{
        .current   = I0 * A,
        .loop_area = A_ * square(m),
        .distance  = R  * m,
        .frequency = f_mhz * MHz,
        .theta     = deg(theta_deg),
    };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    const double f  = f_mhz * 1e6, th = theta_deg * emc::constants::pi / 180.0;
    const double cc = emc::constants::c.numerical_value_in(m / s), pi = emc::constants::pi;
    const double rad = std::sqrt(1.0 + std::pow(cc / (2 * pi * f * R), 2));
    const double Ephi = 120.0 * std::pow(pi * f / cc, 2) * (I0 * A_ / R) * std::sin(th) * rad;

    REQUIRE(emc::test::approx(got->e_phi, Ephi * (V / m), 1e-9));
}

// (b) Hand-computed: H_r ∝ cos θ ⇒ vanishes at θ = 90°.
TEST_CASE("loop: H_r vanishes at θ = 90°", "[basic][loop][known]") {
    const emc::basic::LoopAntennaInput in{
        .current = 10.0 * A, .loop_area = 1.0 * square(m), .distance = 10.0 * m,
        .frequency = 1.0 * MHz, .theta = deg(90.0) };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(emc::test::approx(r->h_r, 0.0 * (A / m), 1e-6));
}

// (c) Property: linearity in I₀·A. Doubling current doubles every field.
TEST_CASE("loop: fields scale linearly with current", "[basic][loop][property]") {
    const emc::basic::LoopAntennaInput base{
        .current = 5.0 * A, .loop_area = 2.0 * square(m), .distance = 8.0 * m,
        .frequency = 3.0 * MHz, .theta = deg(60.0) };
    auto dbl = base; dbl.current = 10.0 * A;

    const auto a = emc::basic::calculate(base);
    const auto b = emc::basic::calculate(dbl);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(emc::test::approx(b->e_phi, a->e_phi * 2.0, 1e-9));
    REQUIRE(emc::test::approx(b->h_r,   a->h_r   * 2.0, 1e-9));
}

// (d) Validation: zero area rejected.
TEST_CASE("loop rejects zero area", "[basic][loop][validate]") {
    const emc::basic::LoopAntennaInput in{
        .current = 1.0 * A, .loop_area = 0.0 * square(m), .distance = 5.0 * m,
        .frequency = 10.0 * MHz, .theta = deg(45.0) };
    const auto r = emc::basic::calculate(in);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "loop_area");
}
```

What each guards: (a) the E_φ formula (which has the distinctive `(πf/c)²` term) vs an independent
re-derivation; (b) the cos θ wiring on H_r; (c) linearity in the I₀·A product (a stray square on a field
would break it); (d) zero-area rejection.

> [!NOTE]
> The dipole and loop are mathematical duals: the `rad_common` radical and the H_θ/E_θ middle term share the
> same structure. They are kept as **separate calculators** because their inputs differ (length vs area) and
> their output kinds differ (E-dominant vs H-dominant).

---

## Far-Field Criteria (branch on D vs λ/10)

### 1. Overview

Given a frequency and the antenna's maximum dimension D, compute the wavelength and the boundaries of the
reactive and radiating near-field regions. The standard criteria branch on whether D is electrically large
(D > λ/10):

```text
λ = c / f
if (D > λ/10):                       # electrically large
    reactive_near_field  = 0.62 · sqrt(D³ / λ)
    radiating_near_field = 2 · D² / λ
else:                                # electrically small
    reactive_near_field  = λ / 50
    radiating_near_field = λ
```

Forward-only, three length outputs (m). The branch is the defining feature.

### 2. Public header — `include/emc/basic/far_field_criteria.hpp`

```c++
// include/emc/basic/far_field_criteria.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/constants.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::basic {

/// Inputs for the far-field / near-field boundary criteria.
struct FarFieldCriteriaInput {
    emc::units::Frequency frequency{};     ///< f  > 0
    emc::units::Length    max_dimension{}; ///< D  > 0  (largest antenna dimension)
};

/// Wavelength plus the two near-field region boundaries.
struct FarFieldCriteriaResult {
    emc::units::Length wavelength{};            ///< λ = c/f               [m]
    emc::units::Length reactive_near_field{};   ///< reactive boundary     [m]
    emc::units::Length radiating_near_field{};  ///< radiating boundary    [m]
    bool               electrically_large{};    ///< true iff D > λ/10 (which branch was taken)
};

[[nodiscard]] std::expected<void, emc::Error> validate(const FarFieldCriteriaInput& in);
[[nodiscard]] emc::Result<FarFieldCriteriaResult> calculate(const FarFieldCriteriaInput& in);

struct FarFieldCriteria {
    using Input  = FarFieldCriteriaInput;
    using Result = FarFieldCriteriaResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<FarFieldCriteria>);

} // namespace emc::basic
```

### 3. Implementation — `src/basic/far_field_criteria.cpp`

```c++
// src/basic/far_field_criteria.cpp
#include <emc/basic/far_field_criteria.hpp>

#include <cmath>   // std::sqrt, std::pow

#include <mp-units/systems/si.h>

namespace emc::basic {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // Hz, m

std::expected<void, emc::Error> validate(const FarFieldCriteriaInput& in) {
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r)
        return r;                                  // λ = c/f needs f > 0
    if (auto r = emc::require_positive(in.max_dimension.numerical_value_in(m), "max_dimension"); !r)
        return r;                                  // D³ / branch comparison need D > 0
    return {};
}

emc::Result<FarFieldCriteriaResult> calculate(const FarFieldCriteriaInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    const double f  = in.frequency.numerical_value_in(Hz);
    const double D  = in.max_dimension.numerical_value_in(m);
    const double cc = emc::constants::c.numerical_value_in(m / s);

    const double lambda = cc / f;                 // λ = c/f

    double reactive  = 0.0;
    double radiating = 0.0;
    const bool large = (D > lambda / 10.0);

    if (large) {                                  // electrically large antenna
        reactive  = 0.62 * std::sqrt(std::pow(D, 3.0) / lambda);
        radiating = (2.0 * std::pow(D, 2.0)) / lambda;
    } else {                                      // electrically small antenna
        reactive  = lambda / 50.0;
        radiating = lambda;
    }

    return FarFieldCriteriaResult{
        .wavelength           = lambda    * m,
        .reactive_near_field  = reactive  * m,
        .radiating_near_field = radiating * m,
        .electrically_large   = large,
    };
}

} // namespace emc::basic
```

> [!IMPORTANT]
> The condition is strict `D > λ/10`: at exact equality the electrically-small branch runs. Exposing
> `electrically_large` as a result field lets a front end label which regime it reported and lets a test
> pin the branch deterministically.

### 4. Modern C++ features used here — and why

- **`emc::constants::c`** — λ = c/f uses the true CODATA c from a single source of truth.
- **mp-units `Frequency` / `Length`** — inputs and the three outputs carry units in the type; `.in(cm)` /
  `.in(ft)` derive display factors on demand with no manual conversion constants.
- **`bool electrically_large` in the result** — exposes *which branch ran* as data, so tests and any front
  end can reason about the regime.
- **`std::expected` validation** — f appears in `c/f` and D in `D³` and the comparison; both have a physical
  domain (> 0), so an out-of-domain input is a typed error.
- **`std::pow`/`std::sqrt` are `constexpr` in C++23** — the whole closed-form branch can fold at compile
  time for literal inputs (exercised by the `static_assert` test).
- **Designated initializers** — the four result fields are named.

### 5. Example usage

```c++
#include <emc/basic/far_field_criteria.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units::si::unit_symbols;

    const emc::basic::FarFieldCriteriaInput in{
        .frequency     = 1000.0 * MHz,   // 1 GHz → λ ≈ 0.30 m
        .max_dimension = 10.0   * m,     // large antenna: D ≫ λ/10
    };

    if (auto r = emc::basic::calculate(in)) {
        std::println("λ = {} m  ({})", r->wavelength.numerical_value_in(m),
                     r->electrically_large ? "electrically large" : "electrically small");
        std::println("reactive  near-field ≤ {} m", r->reactive_near_field.numerical_value_in(m));
        std::println("radiating near-field ≤ {} m", r->radiating_near_field.numerical_value_in(m));
    } else {
        std::println("error: {}", r.error().message);
    }
}
```

### 6. Unit tests — `tests/basic/far_field_criteria_test.cpp`

```c++
// tests/basic/far_field_criteria_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

#include <emc/basic/far_field_criteria.hpp>
#include "support/approx.hpp"

#include <cmath>
#include <mp-units/systems/si.h>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

// (a) Reference: both branches vs an independent closed-form re-derivation with the true c.
TEST_CASE("far-field criteria matches an independent re-derivation", "[basic][farfield][reference]") {
    const double f_mhz = GENERATE(89.1, 300.0, 1000.0);
    const double D_m   = GENERATE(0.1, 1.0, 5.7, 10.0);

    const emc::basic::FarFieldCriteriaInput in{ .frequency = f_mhz * MHz, .max_dimension = D_m * m };
    const auto got = emc::basic::calculate(in);
    REQUIRE(got.has_value());

    const double cc = emc::constants::c.numerical_value_in(m / s);
    const double lambda = cc / (f_mhz * 1e6);
    double reactive, radiating;
    if (D_m > lambda / 10.0) {
        reactive  = 0.62 * std::sqrt(std::pow(D_m, 3) / lambda);
        radiating = 2.0 * std::pow(D_m, 2) / lambda;
    } else {
        reactive  = lambda / 50.0;
        radiating = lambda;
    }
    REQUIRE(emc::test::approx(got->wavelength,           lambda    * m, 1e-9));
    REQUIRE(emc::test::approx(got->reactive_near_field,  reactive  * m, 1e-9));
    REQUIRE(emc::test::approx(got->radiating_near_field, radiating * m, 1e-9));
}

// (b) Hand-computed, electrically-large branch. f = 300 MHz ⇒ λ = c/3e8 ≈ 0.99931 m;
//     D = 1 m > λ/10 = 0.0999 m. reactive = 0.62·√(1³/λ); radiating = 2·1²/λ.
TEST_CASE("far-field: large-antenna branch known value", "[basic][farfield][known]") {
    const emc::basic::FarFieldCriteriaInput in{ .frequency = 300.0 * MHz, .max_dimension = 1.0 * m };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE(r->electrically_large);
    const double lambda = emc::constants::c.numerical_value_in(m / s) / 300e6;
    REQUIRE(emc::test::approx(r->reactive_near_field,  (0.62 * std::sqrt(1.0 / lambda)) * m, 1e-9));
    REQUIRE(emc::test::approx(r->radiating_near_field, (2.0 / lambda) * m, 1e-9));
}

// (c) Branch test: force the small-antenna path (D ≤ λ/10) and check the λ/50, λ limits.
TEST_CASE("far-field: small-antenna branch uses λ/50 and λ", "[basic][farfield][branch]") {
    // 100 MHz → λ ≈ 3 m, λ/10 = 0.3 m; pick D = 0.1 m < 0.3 m.
    const emc::basic::FarFieldCriteriaInput in{ .frequency = 100.0 * MHz, .max_dimension = 0.1 * m };
    const auto r = emc::basic::calculate(in);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->electrically_large);
    REQUIRE(emc::test::approx(r->reactive_near_field,  r->wavelength / 50.0, 1e-12));
    REQUIRE(emc::test::approx(r->radiating_near_field, r->wavelength,        1e-12));
}

// (d) Validation: zero frequency (λ = c/0) rejected.
TEST_CASE("far-field rejects zero frequency", "[basic][farfield][validate]") {
    const auto r = emc::basic::calculate({ .frequency = 0.0 * Hz, .max_dimension = 1.0 * m });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// (e) constexpr-friendliness: λ = c/f folds at compile time for literal inputs.
namespace {
constexpr double lambda_at_300mhz =
    emc::constants::c.numerical_value_in(m / s) / 300e6;
static_assert(lambda_at_300mhz > 0.999 && lambda_at_300mhz < 1.000,
              "λ at 300 MHz must be ~0.9993 m with the true c");
}
```

What each guards: (a) both branches against an independent re-derivation; (b) the large-antenna
`0.62√(D³/λ)` and `2D²/λ` formulas with hand numbers; (c) the small-antenna `λ/50`/`λ` branch is actually
taken and produces those limits; (d) the f = 0 singularity is rejected; (e) the closed form is
constant-evaluable, the strongest guard against a `c` regression.

> [!TIP]
> The reactive/radiating values are upper bounds on the extent of each region. Present them with a "≤"
> framing in any front end (e.g. with `std::print`); the library returns plain `Length` values.

---

## Cross-references

- [`00-foundation-code.md`](./00-foundation-code.md) — the canonical `error.hpp` / `constants.hpp` /
  `units.hpp` / `materials.hpp` / `calculator.hpp` and the `tests/support/` (`approx`) helpers reused above.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — the mp-units
  vocabulary, the dimensionless `mu_r`/`eps_r` rule, and the typed `Decibel`/`Dbm` wrappers.
- [`../04-constants-and-material-database.md`](../04-constants-and-material-database.md) — the
  conductivity/μ_r table behind Skin Depth and the `c`/`mu0`/`pi` constants the antenna and far-field
  formulas consume.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) — the `Error` /
  `ErrorCode` / `std::expected` model.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/`calculate`
  triple, the bidirectional `*_from_*` convention, and the `Calculator` / `ValidatedCalculator` concepts.
- [`../07-calculator-inventory.md`](../07-calculator-inventory.md) — the SPEC rows (naming, placement,
  validation ranges).
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the `emc::test::approx`
  tolerance model and the hand-computed / textbook reference-value workflow these calculators use.
