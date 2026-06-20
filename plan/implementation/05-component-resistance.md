# Implementation Guide — Component: Resistance (`emc::component`, `resistance.hpp`) ⚡

Complete, copy-paste-quality C++23 code (public header, compiled `.cpp`, example, Catch2 v3 tests) for the
four DC/AC conductor-resistance calculators.

All four land in namespace **`emc::component`** with a shared public header
**`include/emc/component/resistance.hpp`** and implementation **`src/component/resistance.cpp`**:

| Calculator | Free function | Inputs |
|---|---|---|
| Circuit Board Trace | `trace_resistance` | `f, l, w, t` |
| Cylindrical Conductor | `cylindrical_conductor_resistance` | `f, l, d, material, mu_r` |
| Rectangular Conductor | `rectangular_conductor_resistance` | `f, l, w, t, material, mu_r` |
| Standard Gauge Wire (AWG) | `standard_gauge_wire_resistance` | `f, l, gauge, material, mu_r` |

All four share the **same physics**: a DC resistance `R_dc = ρ·l / A`, a skin depth
`δ = 1 / √(π·f·μ·σ)`, and a branch that swaps `A` for an effective skin-limited cross-section `A_eff`
when `δ` is small compared to the conductor's geometry. Material lookup goes through `emc::materials`
+ `emc::Result` (see [`00-foundation-code.md`](./00-foundation-code.md) for the material database).

> [!NOTE]
> This guide reuses the canonical surface in [`00-foundation-code.md`](./00-foundation-code.md):
> `emc::constants::pi`/`mu0`, `emc::units::Frequency/Length/Resistivity/Conductivity/Impedance`,
> `emc::materials::Material`/`properties()`, `emc::Result<T>`, `emc::in_range/require_positive`,
> the `emc::Calculator` concept, and the `emc::test::approx` helper.

---

## Shared design: the `ResistanceMode` two-branch core

Every calculator computes the same two quantities — resistance per unit length (`Ω`/m) and total
resistance (`Ω`) — through the identical control flow:

```text
A      = geometric cross-section                       // circle / rectangle / AWG circle
R_dc   = ρ · l / A                                      // low-frequency resistance
δ      = 1 / sqrt(π · f · μ_r · μ0 · σ)                 // skin depth   (σ = 1/ρ)
if δ >= geometric_threshold:                            // skin depth bigger than the conductor → DC dominates
    R = R_dc
else:
    A_eff = perimeter_factor · δ                        // current crowds into a δ-thick shell
    R = ρ · l / A_eff
```

The calculators differ only in:

- the **cross-section `A`** and **skin-depth threshold** (circle uses `A=π(d/2)²`, threshold `δ ≥ d/4`;
  rectangle uses `A = w·t`, threshold `δ ≥ wt / (2(w+t))`), and
- the **effective perimeter** for `A_eff` (circle: `2π(d/2)·δ`; rectangle: `2(w+t)·δ`).

So the math factors into one private `detail::resistance_from_geometry(...)` core taking the
already-computed `A`, `perimeter`, and `δ`-threshold, shared by all four free functions — the
"distinct named free functions sharing a `detail::` core" shape of the calculator pattern in
[`00-foundation-code.md`](./00-foundation-code.md).

### The shared `detail::` core (one copy for all four)

```c++
// src/component/resistance.cpp  (anonymous-namespace detail, used by every calculate() below)
namespace emc::component::detail {

using namespace mp_units;
using mp_units::si::unit_symbols::m;
using mp_units::si::unit_symbols::ohm;
using mp_units::si::unit_symbols::Hz;

// All geometry resolved to SI metres before this is called.
struct GeometrySI {
    double area_m2;        // A          [m^2]
    double perimeter_m;    // 2*(w+t) or 2*pi*r  [m]  (for A_eff = perimeter * delta)
    double dc_threshold_m; // delta >= this  =>  DC branch  (d/4  or  wt/(2(w+t)))
};

struct CoreOut {
    emc::units::Resistivity   resistivity;   // rho      [ohm*m]
    emc::units::Conductivity  conductivity;  // sigma    [S/m]
    emc::units::Length        skin_depth;    // delta    [m]
    double r_per_metre;                       // Ohm / m  (resistance per unit length, SI metre)
    double r_total;                           // Ohm      (over length l)
    bool   skin_limited;                      // true if the A_eff branch was taken
};

// rho in ohm*m, length l in metres, f in Hz, mu_r dimensionless, SI geometry.
[[nodiscard]] inline CoreOut resistance_from_geometry(double rho, double l_m, double f_hz,
                                                      double mu_r, const GeometrySI& g) {
    const double sigma = 1.0 / rho;
    // skin depth: delta = 1 / sqrt(pi * f * mu_r * mu0 * sigma)
    const double mu0 = emc::constants::mu0.numerical_value_in(si::henry / si::metre);
    const double delta = 1.0 / std::sqrt(emc::constants::pi * f_hz * mu_r * mu0 * sigma);

    double r_per_m, r_total;
    bool skin_limited;
    if (delta >= g.dc_threshold_m) {                 // DC branch
        r_per_m      = rho / g.area_m2;              // Ohm / m
        r_total      = rho * l_m / g.area_m2;        // Ohm
        skin_limited = false;
    } else {                                         // skin-effect branch
        const double a_eff = g.perimeter_m * delta;  // delta-thick shell
        r_per_m      = rho / a_eff;
        r_total      = rho * l_m / a_eff;
        skin_limited = true;
    }
    return CoreOut{
        .resistivity  = rho   * (ohm * m),
        .conductivity = sigma * (si::siemens / m),
        .skin_depth   = delta * m,
        .r_per_metre  = r_per_m,
        .r_total      = r_total,
        .skin_limited = skin_limited,
    };
}

} // namespace emc::component::detail
```

> [!NOTE]
> The core returns a clean **Ω/m** per-length value (`r_per_metre`); the caller asks for whatever
> display unit it wants via `.in(ohm/inch)`. `r_total` is resistance over the full length `l`.

---

## Circuit Board Trace

### 1. Overview

AC resistance of a flat PCB trace from frequency, length, width, and thickness, using a **fixed copper
resistivity** (`ρ = 1.72e-8 Ω·m`). Cross-section is the rectangle `A = w·t`; the skin-effect threshold
is `δ ≥ wt / (2(w+t))` and the effective area is `A_eff = 2(w+t)·δ`:

```text
A      = w * t
R_dc   = rho * l / A
delta  = 1 / sqrt(pi * f * mu0 / rho)        // == 1/sqrt(pi*f*mu0*sigma), mu_r = 1 (copper)
if delta >= wt/(2(w+t)):  R = R_dc
else:                      A_eff = 2(w+t)*delta;  R = rho * l / A_eff
```

Copper is baked in — there is no material selector for this calculator.

### 2. Public header — shared `include/emc/component/resistance.hpp` (part 1)

```c++
// include/emc/component/resistance.hpp
#pragma once

#include <expected>

#include <emc/core/calculator.hpp>
#include <emc/core/error.hpp>
#include <emc/core/materials.hpp>
#include <emc/core/units.hpp>

namespace emc::component {

// ---------------------------------------------------------------------------
//  Common AC/DC conductor result (every resistance calculator returns this).
// ---------------------------------------------------------------------------
struct ConductorResistanceResult {
    emc::units::Impedance  resistance_per_length{};   // Ohm/m  (resolve to Ohm/inch etc. at the call site)
    emc::units::Impedance  resistance_total{};        // Ohm    (over the full length l)
    emc::units::Length     skin_depth{};              // delta  [m]
    emc::units::Resistivity   resistivity{};          // rho    [ohm*m]   (back-filled)
    emc::units::Conductivity  conductivity{};         // sigma  [S/m]     (back-filled)
    bool                   skin_limited{false};       // true => A_eff branch (HF) was taken
};

// ===========================================================================
//  Circuit Board Trace  — fixed copper, rectangular cross-section.
// ===========================================================================
struct TraceResistanceInput {
    emc::units::Frequency frequency{};                // f   [Hz]
    emc::units::Length    length{};                   // l   [m]
    emc::units::Length    width{};                    // w   [m]
    emc::units::Length    thickness{};                // t   [m]
    // Copper by default; exposed as an override so call sites stay terse but can deviate.
    emc::units::Resistivity resistivity =
        1.72e-8 * (mp_units::si::ohm * mp_units::si::metre);
};

[[nodiscard]] std::expected<void, emc::Error> validate(const TraceResistanceInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult> trace_resistance(const TraceResistanceInput&);

} // namespace emc::component
```

### 3. Implementation — `src/component/resistance.cpp` (part 1)

```c++
// src/component/resistance.cpp  (the detail:: core from above precedes this)
#include <cmath>     // std::sqrt, std::pow  (constexpr in C++23)

#include <emc/component/resistance.hpp>
#include <emc/core/constants.hpp>

namespace emc::component {

using namespace mp_units;
using mp_units::si::unit_symbols::Hz;
using mp_units::si::unit_symbols::m;

std::expected<void, emc::Error> validate(const TraceResistanceInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    const double w = in.width.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);
    const double rho = in.resistivity.numerical_value_in(si::ohm * si::metre);

    if (auto r = emc::require_positive(f, "frequency");   !r) return r;
    if (auto r = emc::require_positive(l, "length");      !r) return r;
    if (auto r = emc::require_positive(w, "width");       !r) return r;
    if (auto r = emc::require_positive(t, "thickness");   !r) return r;
    if (auto r = emc::require_positive(rho, "resistivity"); !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult> trace_resistance(const TraceResistanceInput& in) {
    return validate(in).transform([&] {
        const double l   = in.length.numerical_value_in(m);
        const double f   = in.frequency.numerical_value_in(Hz);
        const double w   = in.width.numerical_value_in(m);
        const double t   = in.thickness.numerical_value_in(m);
        const double rho = in.resistivity.numerical_value_in(si::ohm * si::metre);

        const detail::GeometrySI g{
            .area_m2        = w * t,
            .perimeter_m    = 2.0 * (w + t),
            .dc_threshold_m = (w * t) / (2.0 * (w + t)),
        };
        const auto core = detail::resistance_from_geometry(rho, l, f, /*mu_r=*/1.0, g);

        return ConductorResistanceResult{
            .resistance_per_length = core.r_per_metre * (si::ohm / m),
            .resistance_total      = core.r_total     *  si::ohm,
            .skin_depth            = core.skin_depth,
            .resistivity           = core.resistivity,
            .conductivity          = core.conductivity,
            .skin_limited          = core.skin_limited,
        };
    });
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`std::expected` + `.transform`** — calculator inputs have physical domains, so an out-of-domain input
  is reported as a recoverable typed `Error` rather than a silent bad value.
  `validate(in).transform([&]{ ... })` runs the math only on the success path and threads the `Error`
  through automatically.
- **`emc::constants::pi` + `emc::constants::mu0`** — the skin-depth and `μ0` terms come from a single
  authoritative source, so every calculator agrees on these constants to full precision.
- **mp-units quantity inputs** — EMC inputs span Hz..GHz and m..mils, so a typed `Frequency`/`Length`
  arrives already in SI and `.numerical_value_in(Hz)` is one checked cast. Mixing a length factor into a
  width term is a compile error, not a runtime surprise.
- **Designated initializers** — `GeometrySI{ .area_m2 = ..., .perimeter_m = ..., .dc_threshold_m = ... }`
  reads as a labelled data sheet, making the geometry self-documenting.
- **`[[nodiscard]]`** — a discarded `ConductorResistanceResult` or an unhandled `Error` becomes a warning,
  catching silent-failure patterns at compile time.

### 5. Example usage

```c++
#include <print>
#include <emc/component/resistance.hpp>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

int main() {
    const emc::component::TraceResistanceInput in{
        .frequency = 100.0 * MHz,
        .length    = 12.0  * cm,
        .width     = 1.0   * mm,
        .thickness = 35.0  * um,            // 1 oz/ft^2 copper
        // .resistivity defaults to copper 1.72e-8 ohm*m
    };

    if (auto r = emc::component::trace_resistance(in)) {
        std::print("R_total   = {} Ω\n",   r->resistance_total.numerical_value_in(ohm));
        std::print("R per cm  = {} Ω/cm\n", r->resistance_per_length.numerical_value_in(ohm / cm));
        std::print("skin depth= {} µm  (HF branch: {})\n",
                   r->skin_depth.numerical_value_in(um), r->skin_limited);
    } else {
        std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
    }
}
```

### 6. Unit tests — `tests/component/resistance_test.cpp` (part 1)

Expected values are hand-computed closed-form references plus property and edge checks.

```c++
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <emc/component/resistance.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;
namespace ec = emc::component;

// (a) KNOWN-VALUE — pure DC regime (low frequency => delta huge => DC branch).
TEST_CASE("trace_resistance DC limit equals rho*l/(w*t)", "[component][resistance]") {
    const ec::TraceResistanceInput in{
        .frequency = 1.0 * Hz,             // delta is enormous -> DC branch guaranteed
        .length    = 1.0 * m,
        .width     = 1.0 * mm,
        .thickness = 1.0 * mm,
        // copper 1.72e-8
    };
    auto r = ec::trace_resistance(in);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    // R = 1.72e-8 * 1 / (1e-3 * 1e-3) = 0.0172 Ohm
    REQUIRE(emc::test::approx(r->resistance_total, 0.0172 * ohm, 1e-9));
}

// (b) MONOTONICITY — higher frequency never lowers AC resistance.
TEST_CASE("trace_resistance is monotone non-decreasing in frequency", "[component][resistance]") {
    auto R = [](double f_hz) {
        return ec::trace_resistance({ .frequency = f_hz * Hz, .length = 1.0 * m,
                                      .width = 5.0 * mm, .thickness = 35.0 * um })
            ->resistance_total.numerical_value_in(ohm);
    };
    REQUIRE(R(1.0e9) >= R(1.0e6));
    REQUIRE(R(1.0e6) >= R(1.0e3));
}

// (c) VALIDATION — zero thickness => not positive => OutOfRange.
TEST_CASE("trace_resistance rejects non-positive geometry", "[component][resistance]") {
    auto r = ec::trace_resistance({ .frequency = 1.0 * MHz, .length = 1.0 * m,
                                    .width = 1.0 * mm, .thickness = 0.0 * mm });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);   // require_positive failed
    REQUIRE(r.error().field == "thickness");
}
```

What each guards: **(a)** the DC branch against a hand-computed `ρl/A`; **(b)** the physical invariant
that skin effect only ever *raises* resistance; **(c)** that a non-positive geometry yields a typed
`OutOfRange` error rather than a silent `inf`.

---

## Cylindrical Conductor

### 1. Overview

AC resistance of a round wire of diameter `d`, length `l`, at frequency `f`, with material-derived
resistivity `ρ` and relative permeability `μ_r`. Circle cross-section `A = π(d/2)²`; skin-effect
threshold `δ ≥ d/4`; effective area `A_eff = 2π(d/2)·δ`:

```text
a      = d/2
A      = pi * a^2
R_dc   = rho * l / A
delta  = 1 / sqrt(pi * f * mu_r * mu0 * sigma)
if delta >= d/4:  R = R_dc
else:              A_eff = 2*pi*a*delta;  R = rho * l / A_eff
```

Material logic: for a real material, `ρ = properties(material).resistivity` and `μ_r` is **forced to 1**;
for `Custom`, the caller-supplied resistivity (or `1/σ`) is used and the supplied `μ_r` is honored.

### 2. Public header — `resistance.hpp` (part 2)

```c++
// include/emc/component/resistance.hpp  (continued, inside namespace emc::component)

// ===========================================================================
//  Cylindrical Conductor — round wire, material-derived rho/mu_r.
// ===========================================================================
struct CylindricalConductorInput {
    emc::units::Frequency      frequency{};                 // f   [Hz]
    emc::units::Length         length{};                    // l   [m]
    emc::units::Length         diameter{};                  // d   [m]
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0; // mu_r, used only for Custom
    // Custom overrides (ignored unless material == Custom):
    emc::units::Resistivity    custom_resistivity{};        // rho [ohm*m]; 0 => derive from conductivity
    emc::units::Conductivity   custom_conductivity{};       // sigma [S/m]
};

[[nodiscard]] std::expected<void, emc::Error> validate(const CylindricalConductorInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
cylindrical_conductor_resistance(const CylindricalConductorInput&);
```

### 3. Implementation — `resistance.cpp` (part 2)

A small shared helper resolves `(material, custom overrides)` → `(rho, mu_r)` as a `Result`:

```c++
// src/component/resistance.cpp  (continued)
namespace emc::component::detail {

// Resolve effective resistivity + mu_r.  Custom => use overrides + given mu_r;
// real material => table rho, mu_r forced to 1.
struct MatResolved { double rho; double mu_r; };

[[nodiscard]] inline emc::Result<MatResolved>
resolve_material(emc::materials::Material mat, double mu_r,
                 double custom_rho, double custom_sigma) {
    using emc::materials::Material;
    if (mat == Material::Custom) {
        double rho = custom_rho;
        if (rho == 0.0 && custom_sigma != 0.0) rho = 1.0 / custom_sigma;
        if (!(rho > 0.0))
            return std::unexpected(emc::invalid_input(
                "Custom material needs a positive resistivity or conductivity", "resistivity"));
        return MatResolved{ .rho = rho, .mu_r = mu_r };
    }
    // Real material: pull rho from the single source of truth; mu_r forced to 1.
    auto props = emc::materials::properties(mat);     // Result<MaterialProperties>
    if (!props) return std::unexpected(props.error()); // typed UnknownMaterial
    const double rho = props->resistivity.numerical_value_in(mp_units::si::ohm * mp_units::si::metre);
    return MatResolved{ .rho = rho, .mu_r = 1.0 };
}

} // namespace emc::component::detail

namespace emc::component {

std::expected<void, emc::Error> validate(const CylindricalConductorInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    const double d = in.diameter.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto r = emc::require_positive(d, "diameter");  !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult>
cylindrical_conductor_resistance(const CylindricalConductorInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho =
        in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma =
        in.custom_conductivity.numerical_value_in(si::siemens / m);

    return detail::resolve_material(in.material, in.relative_permeability,
                                    custom_rho, custom_sigma)
        .transform([&](detail::MatResolved mr) {
            const double l = in.length.numerical_value_in(m);
            const double f = in.frequency.numerical_value_in(Hz);
            const double d = in.diameter.numerical_value_in(m);
            const double a = d / 2.0;

            const detail::GeometrySI g{
                .area_m2        = emc::constants::pi * a * a,
                .perimeter_m    = 2.0 * emc::constants::pi * a,
                .dc_threshold_m = d / 4.0,
            };
            const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, g);
            return ConductorResistanceResult{
                .resistance_per_length = core.r_per_metre * (si::ohm / m),
                .resistance_total      = core.r_total     *  si::ohm,
                .skin_depth            = core.skin_depth,
                .resistivity           = core.resistivity,
                .conductivity          = core.conductivity,
                .skin_limited          = core.skin_limited,
            };
        });
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`emc::materials::properties()` returning `emc::Result`** — material data lives in one table; an unknown
  material is a typed `ErrorCode::UnknownMaterial`, never a silently plausible resistivity.
- **`enum class Material`** — a scoped enum means no implicit int↔enum conversion, so a stray index cannot
  masquerade as a material.
- **`std::expected::transform` chaining** — `resolve_material(...).transform([&]{ math })` expresses
  "resolve the material, then compute" as a clean monadic pipeline; an `UnknownMaterial`/`InvalidInput`
  error short-circuits the math with no manual sentinel guard.
- **mp-units typed inputs** — frequency and diameter arrive in SI regardless of the caller's display unit,
  so unit conversion is a single checked cast.
- **`emc::constants::pi`/`mu0`** — `A`, `δ`, and `A_eff` all draw `π` and `μ0` from the same source.

### 5. Example usage

```c++
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

const emc::component::CylindricalConductorInput in{
    .frequency = 1.0 * MHz,
    .length    = 1.0 * m,
    .diameter  = 2.0 * mm,
    .material  = emc::materials::Material::Copper,   // mu_r forced to 1 internally
};

if (auto r = emc::component::cylindrical_conductor_resistance(in)) {
    std::print("R = {} Ω,  δ = {} µm,  skin-limited: {}\n",
               r->resistance_total.numerical_value_in(ohm),
               r->skin_depth.numerical_value_in(um), r->skin_limited);
} else {
    std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
}
```

### 6. Unit tests — `resistance_test.cpp` (part 2)

```c++
// (a) KNOWN-VALUE — DC copper wire, hand-computed.
TEST_CASE("cylindrical_conductor DC copper", "[component][resistance]") {
    const ec::CylindricalConductorInput in{
        .frequency = 1.0 * Hz, .length = 1.0 * m, .diameter = 1.0 * mm,
        .material  = emc::materials::Material::Copper,
    };
    auto r = ec::cylindrical_conductor_resistance(in);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    // A = pi*(0.5e-3)^2 = 7.853981e-7 m^2 ; rho_cu = 1/5.96e7 = 1.6779e-8 ohm*m
    // R = rho*l/A = 1.6779e-8 / 7.853981e-7 = 0.021364 Ohm
    REQUIRE(emc::test::approx(r->resistance_total, 0.021364 * ohm, 1e-3));
}

// (b) ROUND-TRIP / CONSISTENCY — back-filled rho and sigma are reciprocals.
TEST_CASE("cylindrical_conductor back-fills consistent rho/sigma", "[component][resistance]") {
    auto r = ec::cylindrical_conductor_resistance(
        { .frequency = 1.0 * MHz, .length = 1.0 * m, .diameter = 1.0 * mm,
          .material = emc::materials::Material::Silver });
    REQUIRE(r.has_value());
    const double rho   = r->resistivity.numerical_value_in(ohm * m);
    const double sigma = r->conductivity.numerical_value_in(S / m);
    REQUIRE(rho * sigma == Catch::Approx(1.0).epsilon(1e-12));
}

// (c) VALIDATION — Custom with no overrides => InvalidInput.
TEST_CASE("cylindrical_conductor Custom without props errors", "[component][resistance]") {
    auto r = ec::cylindrical_conductor_resistance(
        { .frequency = 1.0 * MHz, .length = 1.0 * m, .diameter = 1.0 * mm,
          .material = emc::materials::Material::Custom });   // no rho/sigma supplied
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidInput);
}
```

What each guards: **(a)** the DC circle formula against a hand-computed number; **(b)** the back-filled
`ρ`/`σ` reciprocity; **(c)** that `Custom` without data is a clean typed error.

> [!IMPORTANT]
> For real materials `μ_r` is forced to 1, so Nickel's table `μ_r = 600` is deliberately *not* applied
> here. Treat "honor μ_r for magnetic wire" as a conscious future enhancement, not an oversight.

---

## Rectangular Conductor

### 1. Overview

Identical physics to Circuit Board Trace, but with **material-derived `ρ`/`μ_r`** (like Cylindrical) and a
rectangular cross-section `A = w·t`, threshold `δ ≥ wt/(2(w+t))`, `A_eff = 2(w+t)·δ`:

```text
A      = w * t
delta  = 1 / sqrt(pi * f * mu_r * mu0 * sigma)
if delta >= wt/(2(w+t)):  R = rho*l/A
else:                      A_eff = 2(w+t)*delta;  R = rho*l/A_eff
```

Material resolution is the same as Cylindrical (real material → table `ρ`, `μ_r := 1`; `Custom` → user
`ρ` or `1/σ`).

### 2. Public header — `resistance.hpp` (part 3)

```c++
// include/emc/component/resistance.hpp  (continued, inside namespace emc::component)

// ===========================================================================
//  Rectangular Conductor — bar/trace, material-derived rho/mu_r.
// ===========================================================================
struct RectangularConductorInput {
    emc::units::Frequency      frequency{};
    emc::units::Length         length{};
    emc::units::Length         width{};
    emc::units::Length         thickness{};
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0;
    emc::units::Resistivity    custom_resistivity{};
    emc::units::Conductivity   custom_conductivity{};
};

[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularConductorInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
rectangular_conductor_resistance(const RectangularConductorInput&);
```

### 3. Implementation — `resistance.cpp` (part 3)

```c++
// src/component/resistance.cpp  (continued — reuses detail::resolve_material + resistance_from_geometry)
namespace emc::component {

std::expected<void, emc::Error> validate(const RectangularConductorInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    const double w = in.width.numerical_value_in(m);
    const double t = in.thickness.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto r = emc::require_positive(w, "width");     !r) return r;
    if (auto r = emc::require_positive(t, "thickness"); !r) return r;
    return {};
}

emc::Result<ConductorResistanceResult>
rectangular_conductor_resistance(const RectangularConductorInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho   = in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma = in.custom_conductivity.numerical_value_in(si::siemens / m);

    return detail::resolve_material(in.material, in.relative_permeability, custom_rho, custom_sigma)
        .transform([&](detail::MatResolved mr) {
            const double l = in.length.numerical_value_in(m);
            const double f = in.frequency.numerical_value_in(Hz);
            const double w = in.width.numerical_value_in(m);
            const double t = in.thickness.numerical_value_in(m);

            const detail::GeometrySI g{
                .area_m2        = w * t,
                .perimeter_m    = 2.0 * (w + t),
                .dc_threshold_m = (w * t) / (2.0 * (w + t)),
            };
            const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, g);
            return ConductorResistanceResult{
                .resistance_per_length = core.r_per_metre * (si::ohm / m),
                .resistance_total      = core.r_total     *  si::ohm,
                .skin_depth            = core.skin_depth,
                .resistivity           = core.resistivity,
                .conductivity          = core.conductivity,
                .skin_limited          = core.skin_limited,
            };
        });
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **Shared `detail::resistance_from_geometry` + `detail::resolve_material`** — rectangular and cylindrical
  share their skin-depth math and material resolution, so one `detail::` core keeps the two from ever
  drifting apart.
- **`emc::materials` table lookup** — a single source of truth for resistivity, used identically by every
  material-dependent calculator here.
- **`emc::constants::pi`/`mu0`** — `δ` draws both constants from the foundation.
- **Designated-initializer `Input`** — six labelled fields make width/thickness impossible to transpose at
  the call site.

### 5. Example usage

```c++
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

const emc::component::RectangularConductorInput in{
    .frequency = 50.0 * MHz,
    .length    = 0.5  * m,
    .width     = 2.0  * mm,
    .thickness = 0.5  * mm,
    .material  = emc::materials::Material::Aluminium,
};
auto r = emc::component::rectangular_conductor_resistance(in);
if (r) std::print("R = {} mΩ\n", r->resistance_total.numerical_value_in(si::milli<ohm>));
```

### 6. Unit tests — `resistance_test.cpp` (part 3)

```c++
// (a) KNOWN-VALUE — square cross-section DC, hand-computed.
TEST_CASE("rectangular_conductor DC square copper", "[component][resistance]") {
    const ec::RectangularConductorInput in{
        .frequency = 1.0 * Hz, .length = 1.0 * m, .width = 1.0 * mm, .thickness = 1.0 * mm,
        .material  = emc::materials::Material::Copper,
    };
    auto r = ec::rectangular_conductor_resistance(in);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    // rho_cu = 1/5.96e7 = 1.67785e-8 ; A = 1e-6 m^2 ; R = 0.0167785 Ohm
    REQUIRE(emc::test::approx(r->resistance_total, 0.0167785 * ohm, 1e-4));
}

// (b) EQUIVALENCE — a square rectangle equals the trace formula at the same rho.
TEST_CASE("rectangular reduces to trace at the same resistivity", "[component][resistance]") {
    const auto rect = ec::rectangular_conductor_resistance(
        { .frequency = 10.0 * MHz, .length = 1.0 * m, .width = 3.0 * mm, .thickness = 1.0 * mm,
          .material = emc::materials::Material::Copper });
    // copper table rho:
    const double rho = emc::materials::copper.resistivity.numerical_value_in(ohm * m);
    const auto trace = ec::trace_resistance(
        { .frequency = 10.0 * MHz, .length = 1.0 * m, .width = 3.0 * mm, .thickness = 1.0 * mm,
          .resistivity = rho * (ohm * m) });
    REQUIRE(rect.has_value()); REQUIRE(trace.has_value());
    REQUIRE(emc::test::approx(rect->resistance_total, trace->resistance_total, 1e-9));
}

// (c) VALIDATION — zero width => OutOfRange.
TEST_CASE("rectangular_conductor rejects zero width", "[component][resistance]") {
    auto r = ec::rectangular_conductor_resistance(
        { .frequency = 1.0 * MHz, .length = 1.0 * m, .width = 0.0 * mm, .thickness = 1.0 * mm });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::OutOfRange);
    REQUIRE(r.error().field == "width");
}
```

What each guards: **(a)** the DC rectangle formula against a hand-computed number; **(b)** that the
rectangular and trace cores agree when resistivity matches (proving the shared `detail::` core is wired
identically); **(c)** the typed validation path.

---

## Standard Gauge Wire (AWG)

### 1. Overview

Same round-wire physics as Cylindrical, but the diameter is derived from an **AWG gauge** string. A gauge
may be `OOOO/OOO/OO/O` (the "aught" sizes, mapping to `−3/−2/−1/0`) or a plain integer:

```text
g  = parsed gauge (OOOO=-3, OOO=-2, OO=-1, O=0, else integer)
dm = 0.0254 * 0.005 * 92^((36 - g) / 39)        // wire diameter in metres
A  = pi * (dm/2)^2
delta = 1 / sqrt(pi * f * mu_r * mu0 * sigma)
if delta >= dm/4:  R = rho*l/A
else:               A_eff = 2*pi*(dm/2)*delta;  R = rho*l/A_eff
```

Material resolution and back-filled `ρ`/`σ`/`μ_r` are identical to Cylindrical.

### 2. Public header — `resistance.hpp` (part 4)

The AWG gauge is parsed by a free `parse_awg_gauge(std::string_view)` returning
`std::expected<int, emc::Error>`, so a malformed gauge is a typed error rather than a silent fallback.

```c++
// include/emc/component/resistance.hpp  (continued, inside namespace emc::component)
#include <string_view>

// ---------------------------------------------------------------------------
//  AWG gauge parser. Accepts "OOOO"/"OOO"/"OO"/"O" (aught sizes -> -3/-2/-1/0)
//  and signed integer strings ("0", "8", "39", "-3"). Anything else is an Error.
//  Returns the integer gauge used in the 92^((36-g)/39) diameter law.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<int, emc::Error> parse_awg_gauge(std::string_view gauge);

// Convert a parsed gauge to a wire diameter (the 92^((36-g)/39) law).
[[nodiscard]] emc::units::Length awg_diameter(int gauge);

// ===========================================================================
//  Standard Gauge Wire — round wire sized by AWG gauge.
// ===========================================================================
struct StandardGaugeWireInput {
    emc::units::Frequency      frequency{};
    emc::units::Length         length{};
    std::string_view           gauge{"0"};            // "OOOO".."O" or an integer string
    emc::materials::Material   material = emc::materials::Material::Copper;
    double                     relative_permeability = 1.0;
    emc::units::Resistivity    custom_resistivity{};
    emc::units::Conductivity   custom_conductivity{};
};

[[nodiscard]] std::expected<void, emc::Error> validate(const StandardGaugeWireInput&);
[[nodiscard]] emc::Result<ConductorResistanceResult>
standard_gauge_wire_resistance(const StandardGaugeWireInput&);
```

### 3. Implementation — `resistance.cpp` (part 4)

```c++
// src/component/resistance.cpp  (continued)
#include <charconv>

namespace emc::component {

std::expected<int, emc::Error> parse_awg_gauge(std::string_view g) {
    // Aught sizes first (exact match, case-sensitive 'O').
    if (g == "OOOO") return -3;
    if (g == "OOO")  return -2;
    if (g == "OO")   return -1;
    if (g == "O")    return  0;
    // Otherwise: a signed integer ("0", "8", "39", "-3").
    int value{};
    const auto [ptr, ec] = std::from_chars(g.data(), g.data() + g.size(), value);
    if (ec != std::errc{} || ptr != g.data() + g.size())
        return std::unexpected(emc::invalid_input(
            "gauge must be OOOO/OOO/OO/O or an integer", "gauge"));
    return value;
}

emc::units::Length awg_diameter(int gauge) {
    // dm = 0.0254 * 0.005 * 92^((36 - g) / 39)   [metres]
    const double dm = 0.0254 * 0.005 * std::pow(92.0, (36.0 - gauge) / 39.0);
    return dm * mp_units::si::metre;
}

std::expected<void, emc::Error> validate(const StandardGaugeWireInput& in) {
    const double f = in.frequency.numerical_value_in(Hz);
    const double l = in.length.numerical_value_in(m);
    if (auto r = emc::require_positive(f, "frequency"); !r) return r;
    if (auto r = emc::require_positive(l, "length");    !r) return r;
    if (auto g = parse_awg_gauge(in.gauge); !g)
        return std::unexpected(g.error());
    return {};
}

emc::Result<ConductorResistanceResult>
standard_gauge_wire_resistance(const StandardGaugeWireInput& in) {
    if (auto v = validate(in); !v) return std::unexpected(v.error());

    const double custom_rho   = in.custom_resistivity.numerical_value_in(si::ohm * si::metre);
    const double custom_sigma = in.custom_conductivity.numerical_value_in(si::siemens / m);

    // gauge already validated; parse_awg_gauge here cannot fail, but stay honest with and_then.
    return parse_awg_gauge(in.gauge)
        .and_then([&](int g) {
            return detail::resolve_material(in.material, in.relative_permeability,
                                            custom_rho, custom_sigma)
                .transform([&](detail::MatResolved mr) {
                    const double l  = in.length.numerical_value_in(m);
                    const double f  = in.frequency.numerical_value_in(Hz);
                    const double dm = awg_diameter(g).numerical_value_in(m);
                    const double a  = dm / 2.0;

                    const detail::GeometrySI geo{
                        .area_m2        = emc::constants::pi * a * a,
                        .perimeter_m    = 2.0 * emc::constants::pi * a,
                        .dc_threshold_m = dm / 4.0,
                    };
                    const auto core = detail::resistance_from_geometry(mr.rho, l, f, mr.mu_r, geo);
                    return ConductorResistanceResult{
                        .resistance_per_length = core.r_per_metre * (si::ohm / m),
                        .resistance_total      = core.r_total     *  si::ohm,
                        .skin_depth            = core.skin_depth,
                        .resistivity           = core.resistivity,
                        .conductivity          = core.conductivity,
                        .skin_limited          = core.skin_limited,
                    };
                });
        });
}

} // namespace emc::component
```

### 4. Modern C++ features used here — and why

- **`std::expected<int, Error>` from `parse_awg_gauge`** — the gauge is free-form text, so parsing returns
  a typed `InvalidInput` on bad input and rejects trailing garbage (`ptr != end`) rather than producing a
  plausible-but-wrong gauge.
- **`std::from_chars`** — locale-independent integer parsing with no allocation and explicit end-of-input
  checking.
- **`std::expected::and_then` then `.transform`** — chains "parse gauge → resolve material → compute" as a
  flat monadic pipeline; any link's error short-circuits, replacing nested conditionals.
- **`emc::materials::properties()` + `emc::Result`** — material data is a single table lookup with a typed
  `UnknownMaterial` failure mode.
- **`emc::constants::pi`/`mu0`** — diameter, area, `δ`, and `A_eff` all draw their constants from the
  foundation.

> [!TIP]
> `parse_awg_gauge` rejects trailing garbage like `"12x"` outright. A locale-sensitive `toInt`-style parse
> would silently accept the leading `12`; the explicit end-of-input check here closes that gap.

### 5. Example usage

```c++
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

const emc::component::StandardGaugeWireInput in{
    .frequency = 1.0 * MHz,
    .length    = 10.0 * m,
    .gauge     = "12",                              // 12 AWG
    .material  = emc::materials::Material::Copper,
};
if (auto r = emc::component::standard_gauge_wire_resistance(in)) {
    std::print("AWG-12, 10 m, 1 MHz : R = {} Ω\n", r->resistance_total.numerical_value_in(ohm));
} else {
    std::print("error [{}]: {}\n", emc::to_string(r.error().code), r.error().message);
}

// Aught gauges and bad input:
auto g4  = emc::component::parse_awg_gauge("OOOO");  // -> aught == -3
auto bad = emc::component::parse_awg_gauge("12x");   // -> std::unexpected(InvalidInput)
```

### 6. Unit tests — `resistance_test.cpp` (part 4)

```c++
// (a) GAUGE PARSER — aught sizes and integers; trailing garbage rejected.
TEST_CASE("parse_awg_gauge handles aught and integer gauges", "[component][resistance][gauge]") {
    using ec::parse_awg_gauge;
    REQUIRE(parse_awg_gauge("OOOO").value() == -3);
    REQUIRE(parse_awg_gauge("OOO").value()  == -2);
    REQUIRE(parse_awg_gauge("OO").value()   == -1);
    REQUIRE(parse_awg_gauge("O").value()    ==  0);
    REQUIRE(parse_awg_gauge("0").value()    ==  0);
    REQUIRE(parse_awg_gauge("12").value()   == 12);
    REQUIRE(parse_awg_gauge("-3").value()   == -3);
    REQUIRE_FALSE(parse_awg_gauge("12x").has_value());   // trailing garbage rejected
    REQUIRE(parse_awg_gauge("oops").error().code == ErrorCode::InvalidInput);
}

// (b) KNOWN DIAMETER — AWG 36 is the law's anchor: 92^0 = 1 => dm = 0.0254*0.005 = 127 um.
TEST_CASE("awg_diameter at gauge 36 is the 5-mil anchor", "[component][resistance][gauge]") {
    REQUIRE(emc::test::approx(ec::awg_diameter(36), 127.0 * um, 1e-6));
    // AWG 0 diameter ~ 8.25 mm (handbook value).
    REQUIRE(emc::test::approx(ec::awg_diameter(0), 8.2515 * mm, 1e-3));
}

// (c) KNOWN-VALUE — DC AWG-24 copper resistance per metre (handbook ~ 0.0842 Ohm/m).
TEST_CASE("standard_gauge_wire AWG-24 DC copper", "[component][resistance]") {
    auto r = ec::standard_gauge_wire_resistance(
        { .frequency = 1.0 * Hz, .length = 1.0 * m, .gauge = "24",
          .material = emc::materials::Material::Copper });
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->skin_limited);
    // table-copper value (~0.0820 Ohm/m); loose tol because table sigma differs slightly from handbook.
    REQUIRE(r->resistance_total.numerical_value_in(ohm) == Catch::Approx(0.0820).epsilon(0.05));
}

// (d) VALIDATION — bad gauge string => InvalidInput.
TEST_CASE("standard_gauge_wire rejects bad gauge", "[component][resistance]") {
    auto r = ec::standard_gauge_wire_resistance(
        { .frequency = 1.0 * MHz, .length = 1.0 * m, .gauge = "12x",
          .material = emc::materials::Material::Copper });
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidInput);
    REQUIRE(r.error().field == "gauge");
}
```

What each guards: **(a)** the parser contract over aught/integer gauges and trailing garbage; **(b)** the
diameter law at its 36-AWG anchor plus a handbook 0-AWG value; **(c)** an independent handbook resistance;
**(d)** that a malformed gauge is a typed error rather than a silent fallback.

---

## Concept binding for all four (bottom of `resistance.hpp`)

Each calculator gets a zero-data tag struct + `static_assert` so the `emc::Calculator` contract is a
compile-time tripwire in the header (see [`00-foundation-code.md`](./00-foundation-code.md) for the calculator pattern):

```c++
// include/emc/component/resistance.hpp  (bottom, inside namespace emc::component)
struct TraceResistance {
    using Input  = TraceResistanceInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return trace_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<TraceResistance>);

struct CylindricalConductor {
    using Input  = CylindricalConductorInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return cylindrical_conductor_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<CylindricalConductor>);

struct RectangularConductor {
    using Input  = RectangularConductorInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return rectangular_conductor_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<RectangularConductor>);

struct StandardGaugeWire {
    using Input  = StandardGaugeWireInput;
    using Result = ConductorResistanceResult;
    static emc::Result<Result> calculate(const Input& in) { return standard_gauge_wire_resistance(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::component::validate(in); }
};
static_assert(emc::ValidatedCalculator<StandardGaugeWire>);
```

> [!NOTE]
> All four `validate()` overloads share the namespace; the tag's `validate(in)` resolves by `Input` type.
> Because the calculators differ in their `Input` struct, the overload set is unambiguous.

---

## Cross-references

- [`00-foundation-code.md`](./00-foundation-code.md) — `emc::constants` (`pi`, `mu0`), `emc::units`,
  `emc::materials::Material`/`properties()`, `emc::Result`, the validators, the `Calculator` concept, and
  `emc::test::approx`. This guide reuses those names verbatim.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the testing workflow.
