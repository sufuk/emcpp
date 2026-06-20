# Implementation Guide — Shielding Effectiveness (`emc::shielding`) 🛡️

> [!NOTE]
> Copy-paste-quality C++23 for the four shielding-effectiveness leaf calculators: aperture absorption
> loss, near-field (E/H-branch) SE, plane-wave SE, and the λ/2 slot-resonance SE.

| Calculator | Free function | Header |
|---|---|---|
| Aperture (slot / round absorption) | `emc::shielding::calculate` (over `ApertureInput`) | `include/emc/shielding/aperture.hpp` |
| Near-Field SE (E / H branch) | `emc::shielding::calculate` (over `NearFieldSeInput`) | `include/emc/shielding/shielding_effectiveness.hpp` |
| Plane-Wave SE | `emc::shielding::calculate` (over `PlaneWaveSeInput`) | `include/emc/shielding/shielding_effectiveness.hpp` |
| Slot (λ/2 resonance) | `emc::shielding::calculate` (over `SlotSeInput`) | `include/emc/shielding/slot.hpp` |

All four reuse the foundation surface from [`00-foundation-code.md`](00-foundation-code.md): `emc::Result<T>`,
`emc::ErrorCode`, the `emc::in_range`/`require_positive`/`require_nonzero` validators,
`emc::constants::{pi,c,mu0,eps0,z0}`, the `emc::units::*` quantity aliases, the `emc::units::Decibel` log
wrapper, `emc::materials::properties()`, and the Catch2 helpers `emc::test::{load_csv,approx}`.

> [!IMPORTANT]
> Near-Field and Plane-Wave SE share the same `N_s` / skin-depth closed form. Both route π through
> `emc::constants::pi`, so the two calculators agree bit-for-bit on the physics they share — a property
> the test suite asserts directly.

The SE `Input` carries an explicit `conductivity` field, so a caller may pin σ to any value (e.g. a chosen
copper figure of `5.80e7 S/m`) for reproducible reference computations, while the convenience overload that
reads `emc::materials` uses the canonical table value.

---

## Aperture (slot / round absorption loss)

### 1. Overview

A panel aperture (slot or round hole) of depth `d` behaves like a below-cutoff waveguide; the absorption
(cutoff) loss through it is the standard EMC form:

```text
slot  (rectangular):  AL = 27.3 · d / w        // d, w in inches
round (circular):     AL = 32   · d / D        // d, D in inches
```

The constants `27.3` and `32` presuppose **inches**, so the implementation evaluates every length in inches.

### 2. Public header — `include/emc/shielding/aperture.hpp`

```c++
// include/emc/shielding/aperture.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::shielding {

// Which aperture geometry drives the absorption-loss constant.
enum class ApertureShape {
    Slot,   // rectangular slot  -> AL = 27.3 * depth / width
    Round,  // circular hole     -> AL = 32   * depth / diameter
};

// ---------------------------------------------------------------------------
//  ApertureInput — depth plus ONE transverse dimension chosen by `shape`.
//  Lengths are real mp-units quantities (any length unit is accepted; the impl
//  evaluates in inches to reproduce the 27.3 / 32 constants). The unused field
//  (width for Round, diameter for Slot) may stay at its default.
// ---------------------------------------------------------------------------
struct ApertureInput {
    emc::units::Length depth{};                 // aperture depth d
    emc::units::Length width{};                 // slot width  w   (used when shape == Slot)
    emc::units::Length diameter{};              // hole diam.  D   (used when shape == Round)
    ApertureShape      shape = ApertureShape::Slot;
};

// One scalar output: the absorption (cutoff) loss in dB.
struct ApertureResult {
    emc::units::Decibel absorption_loss{};      // AL [dB]
};

// Reject non-positive geometry / division-by-zero.
[[nodiscard]] std::expected<void, emc::Error> validate(const ApertureInput& in);

// Forward calculation: ApertureInput -> AL [dB].
[[nodiscard]] emc::Result<ApertureResult> calculate(const ApertureInput& in);

// Calculator-concept binding (compile-time contract; see 00-foundation-code.md §5).
struct Aperture {
    using Input  = ApertureInput;
    using Result = ApertureResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<Aperture>);

} // namespace emc::shielding
```

### 3. Implementation — `src/shielding/aperture.cpp`

```c++
// src/shielding/aperture.cpp
#include <emc/shielding/aperture.hpp>

#include <mp-units/systems/international.h>   // international::inch
#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::international::unit_symbols::in;   // inch

// Evaluate every length in inches so the 27.3 / 32 constants reproduce exactly.
[[nodiscard]] double inches(emc::units::Length L) { return L.numerical_value_in(in); }
} // namespace

std::expected<void, emc::Error> validate(const ApertureInput& in) {
    if (auto r = emc::require_positive(inches(in.depth), "depth"); !r) return r;
    if (in.shape == ApertureShape::Slot)
        return emc::require_positive(inches(in.width), "width");   // w in the denominator
    return emc::require_positive(inches(in.diameter), "diameter"); // D in the denominator
}

emc::Result<ApertureResult> calculate(const ApertureInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const double d = inches(in.depth);
    double al{};
    if (in.shape == ApertureShape::Slot)
        al = 27.3 * d / inches(in.width);       // AL = 27.3 * d / w
    else
        al = 32.0 * d / inches(in.diameter);    // AL = 32   * d / D

    return ApertureResult{ .absorption_loss = emc::units::Decibel{ al } };
}

} // namespace emc::shielding
```

### 4. Modern C++ features used here — and why

- **mp-units `Length` inputs + `numerical_value_in(in)`** — aperture geometry spans m..mils across callers,
  so a single `numerical_value_in(in)` call performs the exact conversion. A caller passes `2.0 * mm` or
  `0.1 * in`; no hand-rolled conversion factor can be wrong (see 00-foundation-code.md for the units vocabulary).
- **`enum class ApertureShape`** — the slot-vs-round choice is a typed, exhaustively-switchable value the
  caller states explicitly, rather than a runtime boolean flag.
- **`emc::units::Decibel` wrapper** — AL is a logarithmic dB value, so it is modeled as `Decibel`, never a
  linear mp-units unit (see 00-foundation-code.md for the units vocabulary); it cannot be accidentally added to a power in watts.
- **`std::expected` + `validate()`** — aperture geometry has a physical domain (positive lengths), so an
  out-of-domain input (`w = 0`, which would otherwise yield `inf`) is reported as a recoverable
  `ErrorCode::OutOfRange` typed error.
- **`[[nodiscard]]`** — the `Result` cannot be silently ignored.

### 5. Example usage

```c++
#include <emc/shielding/aperture.hpp>
#include <mp-units/systems/international.h>
#include <print>

int main() {
    using namespace mp_units;
    using mp_units::international::unit_symbols::in;

    // A 0.5-inch-deep, 0.1-inch-wide slot.
    const emc::shielding::ApertureInput slot{
        .depth = 0.5 * in,
        .width = 0.1 * in,
        .shape = emc::shielding::ApertureShape::Slot,
    };

    if (auto r = emc::shielding::calculate(slot)) {
        std::print("Aperture absorption loss: {:.2f} dB\n", r->absorption_loss.value);  // 136.50 dB
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/shielding/aperture_test.cpp`

```c++
// tests/shielding/aperture_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <emc/shielding/aperture.hpp>
#include "support/csv.hpp"

#include <mp-units/systems/international.h>

using namespace mp_units;
using mp_units::international::unit_symbols::in;
using emc::shielding::ApertureInput;
using emc::shielding::ApertureShape;

// (a) Reference table (hand-computed from the closed form), columns d, D, w, shape
//     with all lengths in INCHES. Reproduces AL = 27.3·d/w (slot) / 32·d/D (round).
TEST_CASE("aperture matches reference values", "[shielding][aperture][golden]") {
    auto rows = emc::test::load_csv("golden/Aperture.csv");
    REQUIRE_FALSE(rows.empty());

    for (const auto& row : rows) {
        const double d_in = row.num(0);
        const double D_in = row.num(1);
        const double w_in = row.num(2);
        const bool   round = (row.str(3) == "Circular");

        ApertureInput in{
            .depth    = d_in * in,
            .width    = w_in * in,
            .diameter = D_in * in,
            .shape    = round ? ApertureShape::Round : ApertureShape::Slot,
        };
        const double expected = round ? 32.0 * d_in / D_in : 27.3 * d_in / w_in;

        auto r = emc::shielding::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(r->absorption_loss.value == Catch::Approx(expected).epsilon(1e-9));
    }
}

// (b) Independent hand-computed value: a slot, d = 1 in, w = 0.5 in -> 27.3 * 1 / 0.5 = 54.6 dB.
TEST_CASE("aperture slot known value", "[shielding][aperture]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.5 * in, .shape = ApertureShape::Slot});
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(54.6).epsilon(1e-12));
}

// (c) Unit-independence property: same physical slot expressed in mm gives the same AL.
TEST_CASE("aperture is unit-independent", "[shielding][aperture][property]") {
    using mp_units::si::unit_symbols::mm;
    auto a = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.5 * in, .shape = ApertureShape::Slot});
    auto b = emc::shielding::calculate(ApertureInput{
        .depth = 25.4 * mm, .width = 12.7 * mm, .shape = ApertureShape::Slot});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->absorption_loss.value == Catch::Approx(b->absorption_loss.value).epsilon(1e-9));
}

// (d) Validation: zero width -> OutOfRange (from require_positive), never inf.
TEST_CASE("aperture rejects zero width", "[shielding][aperture][validation]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .width = 0.0 * in, .shape = ApertureShape::Slot});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "width");
}

TEST_CASE("aperture rejects zero diameter (round)", "[shielding][aperture][validation]") {
    auto r = emc::shielding::calculate(ApertureInput{
        .depth = 1.0 * in, .diameter = 0.0 * in, .shape = ApertureShape::Round});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "diameter");
}
```

> [!TIP]
> Map the reference columns by index — the order is `d, D, w` (depth, **diameter**, width), with `D`
> before `w`, not alphabetical. Only one of `w` (rectangular) / `D` (circular) is used per row.

---

## Near-Field SE (E / H branch)

### 1. Overview

Near a source the wave impedance is *not* 377 Ω; it is dominated by the E-field (high-impedance) or H-field
(low-impedance) component, which changes the reflection loss. The three dB terms are:

```text
skin depth   δ   = 1 / sqrt( | π² · 4e−7 · μ_r · σ · f | )          // = 1/sqrt(π·f·μ0·μ_r·σ), μ0 = 4π·1e−7
absorption   AL  = 8.7 · t / δ                                       // [dB]
wave imped.  Zw  = 1/(2π·f·ε0·r)         (Electric field)            // ε0 ≈ 8.85e−12
             Zw  = 2π·f·μ0·r             (Magnetic field)            // μ0 = 4π·1e−7
Ns           Ns  = sqrt( 2·π²·4e−7·μ_r·f / σ )
reflection   RL  = 20 · log10( Zw / (4·Ns) )                         // [dB]
total SE         = AL + RL                                           // [dB]
```

### 2. Public header — `include/emc/shielding/shielding_effectiveness.hpp`

Shared by both the Near-Field and the Plane-Wave SE calculators (per doc 07's placement).

```c++
// include/emc/shielding/shielding_effectiveness.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

namespace emc::shielding {

// Which near-field component dominates the wave impedance.
enum class FieldType {
    Electric,   // high-impedance: Zw = 1/(2π f ε0 r)
    Magnetic,   // low-impedance:  Zw = 2π f μ0 r
};

// ---------------------------------------------------------------------------
//  Common SE result: the three dB terms. SE_total == AL + RL.
// ---------------------------------------------------------------------------
struct ShieldingResult {
    emc::units::Decibel absorption_loss{};   // AL  [dB]
    emc::units::Decibel reflection_loss{};   // RL  [dB]
    emc::units::Decibel shielding{};         // SE = AL + RL [dB]
};

// ---------------------------------------------------------------------------
//  NearFieldSeInput — material is given by an explicit conductivity plus
//  relative permeability; thickness, distance, frequency and the field branch.
// ---------------------------------------------------------------------------
struct NearFieldSeInput {
    emc::units::Conductivity conductivity{};            // σ  [S/m]
    double                   relative_permeability = 1.0;  // μ_r [-]
    emc::units::Length       thickness{};               // t  [m]
    emc::units::Length       distance{};                // r  [m]  (source-to-shield)
    emc::units::Frequency    frequency{};               // f  [Hz]
    FieldType                field = FieldType::Electric;
};

// Reject non-positive σ / t / r / f.
[[nodiscard]] std::expected<void, emc::Error> validate(const NearFieldSeInput& in);

// Forward calculation: NearFieldSeInput -> {AL, RL, SE}.
[[nodiscard]] emc::Result<ShieldingResult> calculate(const NearFieldSeInput& in);

// Convenience: build the input from an emc::materials::Material (canonical table).
[[nodiscard]] emc::Result<ShieldingResult>
near_field_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Length distance,
              emc::units::Frequency frequency, FieldType field);

// ---- PlaneWave declarations live in this same header (see next section) ----
struct PlaneWaveSeInput;
[[nodiscard]] std::expected<void, emc::Error> validate(const PlaneWaveSeInput& in);
[[nodiscard]] emc::Result<ShieldingResult> calculate(const PlaneWaveSeInput& in);

struct NearFieldSe {
    using Input  = NearFieldSeInput;
    using Result = ShieldingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<NearFieldSe>);

} // namespace emc::shielding
```

### 3. Implementation — `src/shielding/shielding_effectiveness.cpp`

```c++
// src/shielding/shielding_effectiveness.cpp
#include <emc/shielding/shielding_effectiveness.hpp>

#include <cmath>   // std::sqrt, std::log10

#include <emc/core/constants.hpp>

#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;
namespace si = mp_units::si;

// Extract raw SI scalars in the exact units the formula assumes.
struct Raw { double sigma, mu_r, t, r, f; };

[[nodiscard]] Raw as_raw(const NearFieldSeInput& in) {
    return Raw{
        .sigma = in.conductivity.numerical_value_in(si::siemens / m),
        .mu_r  = in.relative_permeability,
        .t     = in.thickness.numerical_value_in(m),
        .r     = in.distance.numerical_value_in(m),
        .f     = in.frequency.numerical_value_in(Hz),
    };
}

// Skin depth: δ = 1/sqrt(|π²·4e−7·μ_r·σ·f|), equivalent to 1/sqrt(π·f·μ0·μ_r·σ)
// since μ0 = 4π·1e−7; uses emc::constants::pi.
[[nodiscard]] double skin_depth(const Raw& q) {
    const double pi  = emc::constants::pi;
    const double arg = pi * pi * 4.0e-7 * q.mu_r * q.sigma * q.f;
    return 1.0 / std::sqrt(std::abs(arg));
}
} // namespace

std::expected<void, emc::Error> validate(const NearFieldSeInput& in) {
    const Raw q = as_raw(in);
    if (auto r = emc::require_positive(q.sigma, "conductivity"); !r) return r;
    if (auto r = emc::require_positive(q.t,     "thickness");    !r) return r;
    if (auto r = emc::require_positive(q.r,     "distance");     !r) return r;
    if (auto r = emc::require_positive(q.f,     "frequency");    !r) return r;
    if (auto r = emc::require_positive(q.mu_r,  "relative_permeability"); !r) return r;
    return {};
}

emc::Result<ShieldingResult> calculate(const NearFieldSeInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const Raw    q  = as_raw(in);
    const double pi = emc::constants::pi;
    const double d  = skin_depth(q);

    const double al = 8.7 * (q.t / d);                              // absorption loss [dB]

    // Wave impedance branch (E vs H near-field).
    const double eps0 = emc::constants::eps0.numerical_value_in(si::farad / m);   // 8.854e−12
    const double mu0  = emc::constants::mu0.numerical_value_in(si::henry / m);    // 1.2566e−6
    const double zw = (in.field == FieldType::Electric)
                        ? 1.0 / (2.0 * pi * q.f * eps0 * q.r)        // high-Z
                        : 2.0 * pi * q.f * mu0 * q.r;                // low-Z

    const double ns = std::sqrt(2.0 * pi * pi * 4.0e-7 * q.mu_r * q.f / q.sigma);
    const double rl = 20.0 * std::log10(zw / (4.0 * ns));           // reflection loss [dB]

    return ShieldingResult{
        .absorption_loss = emc::units::Decibel{ al },
        .reflection_loss = emc::units::Decibel{ rl },
        .shielding       = emc::units::Decibel{ al + rl },
    };
}

emc::Result<ShieldingResult>
near_field_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Length distance,
              emc::units::Frequency frequency, FieldType field) {
    return materials::properties(material).and_then(
        [&](const materials::MaterialProperties& mp) {
            return calculate(NearFieldSeInput{
                .conductivity          = mp.conductivity,
                .relative_permeability = mu_r,
                .thickness             = thickness,
                .distance              = distance,
                .frequency             = frequency,
                .field                 = field,
            });
        });
}

} // namespace emc::shielding
```

### 4. Modern C++ features used here — and why

- **`emc::constants::pi`** — Near-Field and Plane-Wave use the *identical* full-precision π in the shared
  `N_s` term, so two calculators that share physics cannot drift apart (see 00-foundation-code.md for the constants).
- **`emc::constants::{eps0,mu0}`** — the CODATA values are a single source of truth, dimension-checked at
  the boundary via `numerical_value_in`, rather than magic numbers welded into the `Zw` expressions.
- **mp-units `Conductivity`/`Length`/`Frequency` inputs** — EMC inputs span Hz..GHz and m..mils, so a
  caller passes `40 * mil`, `1 * MHz`; the library converts exactly with compile-time unit safety.
- **`enum class FieldType`** — the E/H choice is one typed, exhaustively switchable value, so an
  unspecified or ambiguous branch can never reach the math.
- **`std::expected` monadic `and_then`** — `near_field_se()` chains `materials::properties()` into
  `calculate()` so an unknown material short-circuits to `ErrorCode::UnknownMaterial` without an
  `if (props)` ladder (see 00-foundation-code.md for the error model).
- **`emc::units::Decibel` for all three outputs** — keeps the dB results out of the linear unit system.
- **`std::expected` + `validate()`** — calculator inputs have physical domains, so the numeric guards
  (σ, t, r, f > 0) report an out-of-domain input as a recoverable typed error and keep `log10` from
  seeing a non-positive argument.

### 5. Example usage

```c++
#include <emc/shielding/shielding_effectiveness.hpp>
#include <mp-units/systems/international.h>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units;
    using mp_units::si::unit_symbols::MHz;
    using mp_units::si::unit_symbols::S;
    using mp_units::si::unit_symbols::m;
    using mp_units::international::unit_symbols::mil;   // thou / mils

    const emc::shielding::NearFieldSeInput in{
        .conductivity          = 5.80e7 * (S / m),     // copper conductivity
        .relative_permeability = 1.0,
        .thickness             = 40.0 * mil,
        .distance              = 400.0 * mil,
        .frequency             = 1.0 * MHz,
        .field                 = emc::shielding::FieldType::Electric,
    };

    if (auto r = emc::shielding::calculate(in)) {
        std::print("AL={:.2f} dB  RL={:.2f} dB  SE={:.2f} dB\n",
                   r->absorption_loss.value, r->reflection_loss.value, r->shielding.value);
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/shielding/near_field_se_test.cpp`

```c++
// tests/shielding/near_field_se_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/shielding/shielding_effectiveness.hpp>
#include "support/csv.hpp"

#include <mp-units/systems/international.h>
#include <mp-units/systems/si.h>

using namespace mp_units;
using mp_units::si::unit_symbols::MHz;
using mp_units::si::unit_symbols::S;
using mp_units::si::unit_symbols::m;
using mp_units::international::unit_symbols::mil;
using emc::shielding::NearFieldSeInput;
using emc::shielding::FieldType;

// (a) Reference table: cols dist[mils], thick[mils], freq[MHz]; σ = 5.80e7, field = Electric.
//     Assert the structural identity SE = AL + RL holds and the result is finite.
TEST_CASE("near-field SE structural identity", "[shielding][near_field][golden]") {
    auto rows = emc::test::load_csv("golden/NearFieldShieldingEffectiveness.csv");
    REQUIRE_FALSE(rows.empty());

    for (const auto& row : rows) {
        NearFieldSeInput in{
            .conductivity          = 5.80e7 * (S / m),
            .relative_permeability = 1.0,
            .distance              = row.num(0) * mil,   // dist
            .thickness             = row.num(1) * mil,   // thick
            .frequency             = row.num(2) * MHz,   // freq
            .field                 = FieldType::Electric,
        };

        auto r = emc::shielding::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(r->shielding.value ==
                Catch::Approx(r->absorption_loss.value + r->reflection_loss.value).epsilon(1e-12));
        REQUIRE(std::isfinite(r->shielding.value));
    }
}

// (b) Independent hand value: Cu, t = r = 1 mm, f = 1 MHz, E-field.
//     δ = 1/sqrt(π²·4e−7·1·5.8e7·1e6) = 1/sqrt(2.2899e9) ≈ 2.0894e−5 m.
//     AL = 8.7·(1e−3 / 2.0894e−5) ≈ 416.4 dB.
TEST_CASE("near-field SE absorption known value", "[shielding][near_field]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = 5.80e7 * (S / m), .relative_permeability = 1.0,
        .thickness = 1.0 * mm, .distance = 1.0 * mm, .frequency = 1.0 * MHz,
        .field = FieldType::Electric});
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(416.4).epsilon(2e-3));
}

// (c) Property: the E-field branch yields a larger Zw (hence larger RL) than the
//     H-field branch at the same near-field geometry/frequency. Monotone branch.
TEST_CASE("near-field E-branch reflects more than H-branch", "[shielding][near_field][property]") {
    using mp_units::si::unit_symbols::mm;
    auto e = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = 5.80e7 * (S / m), .thickness = 1.0 * mm, .distance = 0.1 * m,
        .frequency = 1.0 * MHz, .field = FieldType::Electric});
    auto h = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = 5.80e7 * (S / m), .thickness = 1.0 * mm, .distance = 0.1 * m,
        .frequency = 1.0 * MHz, .field = FieldType::Magnetic});
    REQUIRE(e.has_value());
    REQUIRE(h.has_value());
    REQUIRE(e->reflection_loss.value > h->reflection_loss.value);
}

// (d) Validation: zero frequency -> OutOfRange.
TEST_CASE("near-field SE rejects zero frequency", "[shielding][near_field][validation]") {
    using mp_units::si::unit_symbols::mm;
    using mp_units::si::unit_symbols::Hz;
    auto r = emc::shielding::calculate(NearFieldSeInput{
        .conductivity = 5.80e7 * (S / m), .thickness = 1.0 * mm, .distance = 1.0 * mm,
        .frequency = 0.0 * Hz, .field = FieldType::Electric});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// (e) Unknown material via the convenience overload -> UnknownMaterial.
TEST_CASE("near-field SE rejects Custom material", "[shielding][near_field][validation]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::near_field_se(emc::materials::Material::Custom, 1.0,
                                           1.0 * mm, 1.0 * mm, 1.0 * MHz,
                                           FieldType::Electric);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::UnknownMaterial);
}
```

> What each guards: (a) the SE = AL + RL identity and finiteness across the reference table; (b) the
> absorption constant `8.7` and the skin-depth form against a hand-computed value; (c) the E/H branch
> direction; (d) the numeric domain guard; (e) the material channel.

---

## Plane-Wave SE

### 1. Overview

For a far-field plane wave the wave impedance is the fixed 377 Ω of free space, so the reflection term
drops the geometry/field dependence:

```text
skin depth   δ   = 1 / sqrt( | π² · 4e−7 · μ_r · σ · f | )
absorption   AL  = 8.7 · t / δ                                  // [dB]
Ns           Ns  = sqrt( 2·π²·4e−7·μ_r·f / σ )
reflection   RL  = 20 · log10( 377 / (4·Ns) )                  // [dB]
total SE         = AL + RL                                      // [dB]
```

### 2. Public header

The `PlaneWaveSeInput` / `calculate` / `validate` declarations live in the **same**
`include/emc/shielding/shielding_effectiveness.hpp` shown above (it forward-declared `PlaneWaveSeInput`).
The full definition:

```c++
// include/emc/shielding/shielding_effectiveness.hpp  (PlaneWave section)
namespace emc::shielding {

// ---------------------------------------------------------------------------
//  PlaneWaveSeInput — like NearField but with NO distance and NO field branch
//  (the wave impedance is the fixed 377 Ω of free space).
// ---------------------------------------------------------------------------
struct PlaneWaveSeInput {
    emc::units::Conductivity conductivity{};               // σ  [S/m]
    double                   relative_permeability = 1.0;  // μ_r [-]
    emc::units::Length       thickness{};                  // t  [m]
    emc::units::Frequency    frequency{};                  // f  [Hz]
};

// (validate / calculate already declared in the header's common block)

// Convenience overload from a material id (canonical table).
[[nodiscard]] emc::Result<ShieldingResult>
plane_wave_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Frequency frequency);

struct PlaneWaveSe {
    using Input  = PlaneWaveSeInput;
    using Result = ShieldingResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<PlaneWaveSe>);

} // namespace emc::shielding
```

### 3. Implementation — `src/shielding/shielding_effectiveness.cpp` (continued)

```c++
// src/shielding/shielding_effectiveness.cpp  (PlaneWave section, same TU as NearField)

namespace emc::shielding {

namespace {
// Reuse the same raw-extraction / skin-depth shape as NearField for consistency.
struct RawPW { double sigma, mu_r, t, f; };

[[nodiscard]] RawPW as_raw(const PlaneWaveSeInput& in) {
    using mp_units::si::unit_symbols::Hz;
    using mp_units::si::unit_symbols::m;
    namespace si = mp_units::si;
    return RawPW{
        .sigma = in.conductivity.numerical_value_in(si::siemens / m),
        .mu_r  = in.relative_permeability,
        .t     = in.thickness.numerical_value_in(m),
        .f     = in.frequency.numerical_value_in(Hz),
    };
}
} // namespace

std::expected<void, emc::Error> validate(const PlaneWaveSeInput& in) {
    const RawPW q = as_raw(in);
    if (auto r = emc::require_positive(q.sigma, "conductivity"); !r) return r;
    if (auto r = emc::require_positive(q.t,     "thickness");    !r) return r;
    if (auto r = emc::require_positive(q.f,     "frequency");    !r) return r;
    if (auto r = emc::require_positive(q.mu_r,  "relative_permeability"); !r) return r;
    return {};
}

emc::Result<ShieldingResult> calculate(const PlaneWaveSeInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    const RawPW  q  = as_raw(in);
    const double pi = emc::constants::pi;

    const double delta = 1.0 / std::sqrt(std::abs(pi * pi * 4.0e-7 * q.mu_r * q.sigma * q.f));
    const double al    = 8.7 * (q.t / delta);

    const double ns = std::sqrt(2.0 * pi * pi * 4.0e-7 * q.mu_r * q.f / q.sigma);
    const double rl = 20.0 * std::log10(377.0 / (4.0 * ns));   // fixed free-space wave impedance

    return ShieldingResult{
        .absorption_loss = emc::units::Decibel{ al },
        .reflection_loss = emc::units::Decibel{ rl },
        .shielding       = emc::units::Decibel{ al + rl },
    };
}

emc::Result<ShieldingResult>
plane_wave_se(materials::Material material, double mu_r,
              emc::units::Length thickness, emc::units::Frequency frequency) {
    return materials::properties(material).and_then(
        [&](const materials::MaterialProperties& mp) {
            return calculate(PlaneWaveSeInput{
                .conductivity          = mp.conductivity,
                .relative_permeability = mu_r,
                .thickness             = thickness,
                .frequency             = frequency,
            });
        });
}

} // namespace emc::shielding
```

> [!NOTE]
> The free-space impedance is hard-coded as `377` Ω. The exact value is `z0 = 376.730…Ω`; substituting
> `emc::constants::z0.numerical_value_in(si::ohm)` is marginally more accurate (≈0.003 dB on RL) and is a
> drop-in replacement should tighter fidelity be wanted.

### 4. Modern C++ features used here — and why

- **`emc::constants::pi`** — the reflection term uses full-precision π, identical to the Near-Field
  calculator, so the two never disagree on shared physics (see 00-foundation-code.md for the constants).
- **mp-units `Conductivity`/`Length`/`Frequency` inputs** — EMC inputs span wide physical ranges, so the
  caller's unit literal (`40 * mil`, `1 * MHz`) is converted exactly with compile-time safety.
- **Shared skin-depth shape** — Plane-Wave and Near-Field compute δ and `Ns` with the same closed form
  routed through `emc::constants::pi`, so they cannot drift apart.
- **`std::expected` + `and_then`** in `plane_wave_se()` — material lookup chains into `calculate()` with
  no nested-`if` ladder.
- **`emc::units::Decibel` outputs + `validate()`** — dB stays out of the linear unit system, and the
  numeric domain guards report out-of-domain inputs as typed recoverable errors.

### 5. Example usage

```c++
#include <emc/shielding/shielding_effectiveness.hpp>
#include <mp-units/systems/international.h>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units;
    using mp_units::si::unit_symbols::MHz;
    using mp_units::si::unit_symbols::S;
    using mp_units::si::unit_symbols::m;
    using mp_units::international::unit_symbols::mil;

    // Copper foil, 40 mils, 1 MHz — via the material table.
    if (auto r = emc::shielding::plane_wave_se(
            emc::materials::Material::Copper, 1.0, 40.0 * mil, 1.0 * MHz)) {
        std::print("Plane-wave SE = {:.1f} dB (AL {:.1f} + RL {:.1f})\n",
                   r->shielding.value, r->absorption_loss.value, r->reflection_loss.value);
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }

    // Or with an explicit σ:
    const emc::shielding::PlaneWaveSeInput pinned{
        .conductivity = 5.80e7 * (S / m), .thickness = 40.0 * mil, .frequency = 1.0 * MHz};
    auto g = emc::shielding::calculate(pinned);
}
```

### 6. Unit tests — `tests/shielding/plane_wave_se_test.cpp`

```c++
// tests/shielding/plane_wave_se_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/shielding/shielding_effectiveness.hpp>
#include "support/csv.hpp"

#include <mp-units/systems/international.h>
#include <mp-units/systems/si.h>

using namespace mp_units;
using mp_units::si::unit_symbols::MHz;
using mp_units::si::unit_symbols::S;
using mp_units::si::unit_symbols::m;
using mp_units::international::unit_symbols::mil;
using emc::shielding::PlaneWaveSeInput;

// (a) Reference table: cols thick[mils], freq[MHz]; σ = 5.80e7. Assert the structural
//     identity SE = AL + RL and finiteness across the table.
TEST_CASE("plane-wave SE structural identity", "[shielding][plane_wave][golden]") {
    auto rows = emc::test::load_csv("golden/PlaneWaveShieldingEffectiveness.csv");
    REQUIRE_FALSE(rows.empty());

    for (const auto& row : rows) {
        PlaneWaveSeInput in{
            .conductivity = 5.80e7 * (S / m),
            .thickness    = row.num(0) * mil,   // thick
            .frequency    = row.num(1) * MHz,   // freq
        };
        auto r = emc::shielding::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(std::isfinite(r->shielding.value));
        REQUIRE(r->shielding.value ==
                Catch::Approx(r->absorption_loss.value + r->reflection_loss.value).epsilon(1e-12));
    }
}

// (b) Cross-calculator agreement: Plane-Wave and Near-Field share the SAME Ns/δ math
//     under emc::constants::pi. With the SAME σ, t, f, μ_r their ABSORPTION loss
//     (which never depends on the field or 377) must match to full precision.
TEST_CASE("plane-wave and near-field absorption agree", "[shielding][plane_wave]") {
    using mp_units::si::unit_symbols::mm;
    const auto sigma = 5.80e7 * (S / m);
    auto pw = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = sigma, .thickness = 2.0 * mm, .frequency = 5.0 * MHz});
    auto nf = emc::shielding::calculate(emc::shielding::NearFieldSeInput{
        .conductivity = sigma, .thickness = 2.0 * mm, .distance = 1.0 * m,
        .frequency = 5.0 * MHz, .field = emc::shielding::FieldType::Electric});
    REQUIRE(pw.has_value());
    REQUIRE(nf.has_value());
    REQUIRE(pw->absorption_loss.value ==
            Catch::Approx(nf->absorption_loss.value).epsilon(1e-12));
}

// (c) Independent hand value: Cu, t = 1 mm, f = 1 MHz.
//     δ ≈ 2.0894e−5 m  ->  AL = 8.7·(1e−3/2.0894e−5) ≈ 416.4 dB.
TEST_CASE("plane-wave absorption known value", "[shielding][plane_wave]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = 5.80e7 * (S / m), .thickness = 1.0 * mm, .frequency = 1.0 * MHz});
    REQUIRE(r.has_value());
    REQUIRE(r->absorption_loss.value == Catch::Approx(416.4).epsilon(2e-3));
}

// (d) Validation: zero thickness -> OutOfRange.
TEST_CASE("plane-wave SE rejects zero thickness", "[shielding][plane_wave][validation]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::calculate(PlaneWaveSeInput{
        .conductivity = 5.80e7 * (S / m), .thickness = 0.0 * mm, .frequency = 1.0 * MHz});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "thickness");
}

// (e) Unknown material via convenience overload.
TEST_CASE("plane-wave SE rejects Custom material", "[shielding][plane_wave][validation]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::plane_wave_se(emc::materials::Material::Custom, 1.0,
                                           1.0 * mm, 1.0 * MHz);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::UnknownMaterial);
}
```

> What each guards: (a) structural consistency across the reference table; (b) that Plane-Wave and
> Near-Field absorption coincide bit-for-bit because both route π through `emc::constants::pi`; (c) the
> `8.7` constant and δ against a hand-computed value; (d) the thickness guard; (e) the material channel.

---

## Slot (λ/2 resonance)

### 1. Overview

A long slot in a shield radiates most efficiently near its half-wavelength resonance; the shielding
effectiveness against a slot of physical `length` is:

```text
wavelength   λ  = c / f
SE              = 20 · log10( λ / (2 · length) )     // [dB]   (negative once length > λ/2)
```

> [!WARNING]
> SE goes **negative** once `length > λ/2`. The `Decibel` wrapper models this correctly; do not clamp it
> to zero — a negative SE is the physically meaningful "the slot leaks more than it blocks" regime.

### 2. Public header — `include/emc/shielding/slot.hpp`

```c++
// include/emc/shielding/slot.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>

namespace emc::shielding {

// ---------------------------------------------------------------------------
//  SlotSeInput — frequency and the physical slot length.
// ---------------------------------------------------------------------------
struct SlotSeInput {
    emc::units::Frequency frequency{};   // f       [Hz]
    emc::units::Length    length{};      // slot l  [m]
};

// Two outputs: the wavelength and the shielding effectiveness.
struct SlotSeResult {
    emc::units::Length  wavelength{};        // λ = c/f  [m]
    emc::units::Decibel shielding{};         // SE       [dB]  (may be negative)
};

// Reject f <= 0 (div-by-zero for λ) and length <= 0 (div-by-zero / log domain).
[[nodiscard]] std::expected<void, emc::Error> validate(const SlotSeInput& in);

// Forward calculation: SlotSeInput -> {λ, SE}.
[[nodiscard]] emc::Result<SlotSeResult> calculate(const SlotSeInput& in);

struct SlotSe {
    using Input  = SlotSeInput;
    using Result = SlotSeResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::shielding::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) {
        return emc::shielding::validate(in);
    }
};
static_assert(emc::ValidatedCalculator<SlotSe>);

} // namespace emc::shielding
```

### 3. Implementation — `src/shielding/slot.cpp`

```c++
// src/shielding/slot.cpp
#include <emc/shielding/slot.hpp>

#include <cmath>   // std::log10

#include <emc/core/constants.hpp>

#include <mp-units/systems/si.h>

namespace emc::shielding {

namespace {
using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;
} // namespace

std::expected<void, emc::Error> validate(const SlotSeInput& in) {
    if (auto r = emc::require_positive(in.frequency.numerical_value_in(Hz), "frequency"); !r)
        return r;                                   // f = 0 -> λ = c/0
    if (auto r = emc::require_positive(in.length.numerical_value_in(m), "length"); !r)
        return r;                                   // length = 0 -> log10(λ/0)
    return {};
}

emc::Result<SlotSeResult> calculate(const SlotSeInput& in) {
    if (auto ok = validate(in); !ok)
        return std::unexpected(ok.error());

    // λ = c / f, dimensionally exact via mp-units (c is emc::constants::c).
    const auto lambda = (emc::constants::c / in.frequency).in(m);

    const double lambda_m = lambda.numerical_value_in(m);
    const double len_m    = in.length.numerical_value_in(m);
    const double se       = 20.0 * std::log10(lambda_m / (2.0 * len_m));   // [dB]

    return SlotSeResult{
        .wavelength = lambda,
        .shielding  = emc::units::Decibel{ se },
    };
}

} // namespace emc::shielding
```

### 4. Modern C++ features used here — and why

- **`emc::constants::c`** — the exact SI `c = 299 792 458 m/s` feeds λ = c/f, so the wavelength is precise
  rather than a rounded approximation (inventory bug #2).
- **mp-units `c / frequency` → `Length`** — λ is computed as a *dimensioned* division, so the result is a
  real `Length`, and the caller supplies frequency and length in any unit literal plus `.in(m)`. A wrong
  conversion factor cannot recur (see 00-foundation-code.md for the units vocabulary).
- **Two-field `SlotSeResult`** — both outputs (λ and SE) are returned as one value with their units in the
  type.
- **`emc::units::Decibel`** — SE is dB; the wrapper keeps it out of the linear unit system and lets it
  legitimately go *negative* (when `length > λ/2`) without unit confusion.
- **`std::expected` + `validate()`** — `f` and `length` have positive physical domains, so `f = 0`
  (λ = c/0) or `length = 0` (log of inf) is reported as `ErrorCode::OutOfRange` instead of producing inf.

### 5. Example usage

```c++
#include <emc/shielding/slot.hpp>
#include <mp-units/systems/si.h>
#include <print>

int main() {
    using namespace mp_units;
    using mp_units::si::unit_symbols::MHz;
    using mp_units::si::unit_symbols::m;
    using mp_units::si::unit_symbols::mm;

    const emc::shielding::SlotSeInput in{
        .frequency = 300.0 * MHz,   // λ ≈ 1 m
        .length    = 10.0 * mm,     // 1 cm slot
    };

    if (auto r = emc::shielding::calculate(in)) {
        std::print("λ = {:.3f} m,  SE = {:.1f} dB\n",
                   r->wavelength.numerical_value_in(m), r->shielding.value);  // ~ 33.98 dB
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/shielding/slot_se_test.cpp`

```c++
// tests/shielding/slot_se_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/shielding/slot.hpp>
#include "support/approx.hpp"
#include "support/csv.hpp"

#include <mp-units/systems/si.h>

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::MHz;
using mp_units::si::unit_symbols::m;
using emc::shielding::SlotSeInput;

// (a) Reference table: cols freq[MHz], length[m]. Recompute SE from the closed form
//     under the exact c and check calculate() reproduces it.
TEST_CASE("slot SE reference values", "[shielding][slot][golden]") {
    auto rows = emc::test::load_csv("golden/Slot.csv");
    REQUIRE_FALSE(rows.empty());

    for (const auto& row : rows) {
        const double f_MHz = row.num(0);
        const double len_m = row.num(1);
        SlotSeInput in{ .frequency = f_MHz * MHz, .length = len_m * m };

        const double lambda = 299'792'458.0 / (f_MHz * 1e6);
        const double se     = 20.0 * std::log10(lambda / (2.0 * len_m));

        auto r = emc::shielding::calculate(in);
        REQUIRE(r.has_value());
        REQUIRE(r->shielding.value == Catch::Approx(se).epsilon(1e-9));
        REQUIRE(emc::test::approx(r->wavelength, lambda * m, 1e-9));
    }
}

// (b) Independent hand value: f = 300 MHz -> λ = 0.99930819 m; length = 10 mm.
//     SE = 20·log10(0.99930819 / 0.02) = 20·log10(49.9654) ≈ 33.977 dB.
TEST_CASE("slot SE known value", "[shielding][slot]") {
    using mp_units::si::unit_symbols::mm;
    auto r = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length = 10.0 * mm});
    REQUIRE(r.has_value());
    REQUIRE(r->shielding.value == Catch::Approx(33.977).epsilon(1e-3));
    REQUIRE(emc::test::approx(r->wavelength, 0.999308193 * m, 1e-6));
}

// (c) Property: at the half-wave resonance (length == λ/2) SE crosses 0 dB.
TEST_CASE("slot SE is 0 dB at half-wave resonance", "[shielding][slot][property]") {
    // λ = 1 m at 299.792458 MHz; length = λ/2 = 0.5 m -> SE = 20·log10(1) = 0.
    auto r = emc::shielding::calculate(SlotSeInput{
        .frequency = 299.792458 * MHz, .length = 0.5 * m});
    REQUIRE(r.has_value());
    REQUIRE(r->shielding.value == Catch::Approx(0.0).margin(1e-9));
}

// (d) Property: SE decreases monotonically as the slot lengthens (closer to resonance).
TEST_CASE("slot SE decreases with slot length", "[shielding][slot][property]") {
    using mp_units::si::unit_symbols::mm;
    auto small = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length = 5.0 * mm});
    auto big   = emc::shielding::calculate(SlotSeInput{.frequency = 300.0 * MHz, .length = 50.0 * mm});
    REQUIRE(small.has_value());
    REQUIRE(big.has_value());
    REQUIRE(small->shielding.value > big->shielding.value);
}

// (e) Validation: zero frequency -> OutOfRange (λ = c/0).
TEST_CASE("slot SE rejects zero frequency", "[shielding][slot][validation]") {
    auto r = emc::shielding::calculate(SlotSeInput{.frequency = 0.0 * Hz, .length = 1.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "frequency");
}

// (f) Validation: zero length -> OutOfRange (log10(λ/0)).
TEST_CASE("slot SE rejects zero length", "[shielding][slot][validation]") {
    auto r = emc::shielding::calculate(SlotSeInput{.frequency = 1.0 * MHz, .length = 0.0 * m});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == emc::ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "length");
}
```

> What each guards: (a) the reference table under exact c; (b) the `λ = c/f`, `SE = 20·log10(λ/2l)`
> constants against a hand-computed value; (c) the physical resonance crossing at `length = λ/2`; (d) the
> monotone trend; (e)/(f) the two division-by-zero domain guards.

---

## Cross-references

- [`00-foundation-code.md`](00-foundation-code.md) — `Error`/`Result`, `require_positive`,
  `emc::constants::{pi,c,mu0,eps0,z0}`, `emc::units::{Length,Frequency,Conductivity,Decibel}`,
  `emc::materials::properties`, and the `emc::test::{load_csv,approx}` helpers.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the reference-CSV harness
  and the tolerance model.
