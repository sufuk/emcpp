# Implementation Guide — Shielding: Cavity Resonance 📡

Copy-paste-quality C++23 code for three cavity-resonance calculators. Each computes the dominant resonant
modes of an enclosure as a closed-form, forward-only, multi-output calculation. The shared design choice:
treat the mode set as **data**, not code — a `constexpr` `(m,n,p)` table plus
`std::array<ModeFrequency, N>` filled by one `std::ranges::transform`. The speed-of-light prefactor is
sourced once from `emc::constants::c`.

All three land in namespace **`emc::shielding`** under header
**`include/emc/shielding/cavity_resonance.hpp`** / impl **`src/shielding/cavity_resonance.cpp`**:

| Calculator | Free function | Modes |
|---|---|---|
| Rectangular Enclosure | `emc::shielding::rectangular_cavity_modes` | 12 TE/TM `(m,n,p)` |
| Cylindrical Enclosure | `emc::shielding::cylindrical_cavity_modes` | 12 TM + 9 TE via Bessel roots |
| Circuit Board Planes | `emc::shielding::circuit_board_plane_modes` | 12 patch `(m,n)` |

They share the standard closed-form cavity-resonance kernel:

```text
f_mnp = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² + (p/h)² )           (rectangular box)
f      = (c / (2π·√ε_r)) · √( (χ/r)² + (pπ/l)² )                  (cylinder, χ = Bessel root)
f_mn   = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² )                    (parallel-plane patch)
```

so they reuse one `detail::` kernel. They follow the Foundation API from
[`00-foundation-code.md`](./00-foundation-code.md): `emc::constants::c`, `emc::units::{Length,Frequency}`,
`emc::Result<T>`, `emc::in_range`/`emc::require_positive`, `emc::test::{load_csv,approx}`.

> [!NOTE]
> The `(m,n,p)` integers and Bessel roots are *data*. Hoisting them into a `constexpr std::array<ModeSpec, N>`
> table and `std::ranges::transform`-ing into `std::array<ModeFrequency, N>` through one kernel lambda means
> adding the 13th mode is one table row, not a copy-pasted 4-line block.

---

## Shared kernel & mode-table types (top of the header)

```c++
// include/emc/shielding/cavity_resonance.hpp
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <emc/core/constants.hpp>     // emc::constants::c, emc::constants::pi
#include <emc/core/error.hpp>         // emc::Result, emc::in_range, emc::require_positive
#include <emc/core/units.hpp>         // emc::units::Length, emc::units::Frequency
#include <emc/core/calculator.hpp>    // emc::Calculator concept

#include <mp-units/systems/si.h>
#include <mp-units/math.h>            // mp_units::sqrt (quantity-aware), pow

namespace emc::shielding {

namespace mpu = mp_units;
namespace si  = mp_units::si;

// ---------------------------------------------------------------------------
//  ModeFrequency — one (label, frequency) pair. `label` is the mode id
//  ("f110","mf011","ef111",...) so result columns map 1:1 and a front end can
//  title each cell.
// ---------------------------------------------------------------------------
struct ModeFrequency {
    std::string_view       label;       // "f110", "mf011", "ef111", "f10", ...
    emc::units::Frequency  frequency{}; // resonant frequency [Hz]
};

namespace detail {

// Box kernel: f = (c / (2·√ε_r)) · √( sum of (index/dimension)² ).
// Absent axes pass index 0. Everything flows through mp-units so the result is a
// true Frequency: (1/length) has dimension 1/L; c·(1/L) has dimension 1/T. ✔
[[nodiscard]] inline emc::units::Frequency
box_mode(double eps_r,
         double m, emc::units::Length l,
         double n, emc::units::Length w,
         double p, emc::units::Length h) {
    using namespace mp_units;
    // Per-axis spatial frequencies (index / dimension), as quantities of 1/length.
    const auto km = (m == 0.0) ? 0.0 / si::metre : m / l;
    const auto kn = (n == 0.0) ? 0.0 / si::metre : n / w;
    const auto kp = (p == 0.0) ? 0.0 / si::metre : p / h;
    const auto k  = sqrt(km * km + kn * kn + kp * kp);     // 1/length
    return (emc::constants::c / (2.0 * std::sqrt(eps_r)) * k).in(si::hertz);
}

// Cylinder kernel: f = (c / (2π·√ε_r)) · √( (χ/r)² + (pπ/l)² ).
// χ is a Bessel-function root (TM: J_χ root; TE: J'_χ root); the axial term uses pπ/l.
[[nodiscard]] inline emc::units::Frequency
cyl_mode(double eps_r, double chi, emc::units::Length r,
         double p_axial_index, emc::units::Length l) {
    using namespace mp_units;
    const auto kr = chi / r;                                   // 1/length
    const auto kz = (p_axial_index == 0.0)
                      ? 0.0 / si::metre
                      : (p_axial_index * emc::constants::pi) / l;  // pπ/l
    const auto k = sqrt(kr * kr + kz * kz);
    return (emc::constants::c / (2.0 * emc::constants::pi * std::sqrt(eps_r)) * k).in(si::hertz);
}

} // namespace detail

} // namespace emc::shielding
```

> [!TIP]
> The kernel takes the *integer* axial index and multiplies by the full-precision `emc::constants::pi`, so
> the axial wavenumber `pπ/l` is exact for every `p` — no truncated `π` literals creep in.

---

## Rectangular Enclosure (12 resonant modes)

### 1. Overview

Computes the resonant frequencies of the 12 dominant TE/TM modes of an air- or dielectric-filled
rectangular metal enclosure of inner dimensions length `l`, width `w`, height `h`, relative permittivity
`ε_r`. Each mode `(m,n,p)` has

```text
f_mnp = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² + (p/h)² )
```

The prefactor is `c/2 = 149 896 229 m/s`, sourced from `emc::constants::c`.

### 2. Public header

```c++
// include/emc/shielding/cavity_resonance.hpp  (continued)
namespace emc::shielding {

// ---- Rectangular enclosure ------------------------------------------------

/// Inputs for the rectangular cavity. Defaults (0.5 m × 0.4 m × 0.2 m, ε_r = 1)
/// so designated-initializer call sites read well.
struct RectangularCavityInput {
    emc::units::Length length{0.5 * si::metre};   // l  (interior)
    emc::units::Length width {0.4 * si::metre};   // w
    emc::units::Length height{0.2 * si::metre};   // h
    double             eps_r {1.0};               // relative permittivity (dimensionless)
};

inline constexpr int kRectangularModeCount = 12;

struct RectangularCavityResult {
    std::array<ModeFrequency, kRectangularModeCount> modes{};

    /// Convenience: lowest (dominant) resonance among the 12.
    [[nodiscard]] emc::units::Frequency dominant() const;
};

/// (m,n,p) table. constexpr so it lives in read-only data and is iterable at compile time.
inline constexpr std::array<std::array<int, 3>, kRectangularModeCount> kRectangularModes{{
    //  m  n  p     label
    {1,1,0},   // f110
    {1,0,1},   // f101
    {0,1,1},   // f011
    {1,1,1},   // f111
    {2,0,1},   // f201
    {1,2,0},   // f120
    {2,1,1},   // f211
    {2,1,0},   // f210
    {0,2,1},   // f021
    {2,2,0},   // f220
    {2,2,1},   // f221
    {1,2,1},   // f121
}};

inline constexpr std::array<std::string_view, kRectangularModeCount> kRectangularLabels{{
    "f110","f101","f011","f111","f201","f120","f211","f210","f021","f220","f221","f121"
}};

[[nodiscard]] std::expected<void, emc::Error> validate(const RectangularCavityInput& in);
[[nodiscard]] emc::Result<RectangularCavityResult> rectangular_cavity_modes(const RectangularCavityInput& in);

} // namespace emc::shielding
```

### 3. Implementation

```c++
// src/shielding/cavity_resonance.cpp
#include <emc/shielding/cavity_resonance.hpp>

#include <algorithm>   // std::ranges::min_element
#include <ranges>      // std::views::zip, std::ranges::transform

namespace emc::shielding {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz

// ---- Rectangular ----------------------------------------------------------

std::expected<void, emc::Error> validate(const RectangularCavityInput& in) {
    if (auto r = emc::require_positive(in.length.numerical_value_in(m),  "length"); !r) return r;
    if (auto r = emc::require_positive(in.width .numerical_value_in(m),  "width");  !r) return r;
    if (auto r = emc::require_positive(in.height.numerical_value_in(m),  "height"); !r) return r;
    // ε_r ≥ 1 is physical for a passive dielectric.
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;
    return {};
}

emc::units::Frequency RectangularCavityResult::dominant() const {
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<RectangularCavityResult> rectangular_cavity_modes(const RectangularCavityInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    RectangularCavityResult out{};

    // ONE transform over the (m,n,p) table fills all 12 modes.
    std::ranges::transform(
        std::views::zip(kRectangularModes, kRectangularLabels),
        out.modes.begin(),
        [&](const auto& pair) -> ModeFrequency {
            const auto& [mnp, label] = pair;
            return ModeFrequency{
                .label     = label,
                .frequency = detail::box_mode(in.eps_r,
                                              static_cast<double>(mnp[0]), in.length,
                                              static_cast<double>(mnp[1]), in.width,
                                              static_cast<double>(mnp[2]), in.height),
            };
        });

    return out;
}

} // namespace emc::shielding
```

### 4. Modern C++ features used here — and why

- **`constexpr std::array<std::array<int,3>, 12>` mode table** — the 12 `(m,n,p)` triples and their labels
  are *data*. A table removes ~70 lines of copy-paste and the transcription bugs that come with it (e.g. a
  `(2/l)²` mistyped as `(1/l)²`). It is `constexpr`, so it lives in read-only data and is iterable at compile
  time.
- **`std::ranges::transform` + `std::views::zip` (C++23)** — fills `out.modes` in one expression by zipping
  the index triples with their labels. Adding a 13th mode is one table row, with zero new control flow.
- **`std::array<ModeFrequency,12>` Result (not 12 named members)** — the result is iterable, so `dominant()`
  is a one-line `std::ranges::min_element`, and a front end binds a single repeater.
- **mp-units `Frequency`/`Length` quantities + `mp_units::sqrt`** — EMC enclosure dimensions span m..mils, so
  typed `Length` inputs give compile-time unit safety: the kernel multiplies `c` (`m/s`) by a `1/length`, and
  the type system *proves* the product is a frequency. No hand-carried unit factors exist.
- **`emc::constants::c`** — a single source of truth for the speed of light shared by all three kernels.
- **`std::expected<void,Error>` `validate()` + `emc::require_positive`/`emc::in_range`** — calculator inputs
  have physical domains, so an out-of-domain input (`ε_r = 0`, a zero dimension) is reported as a recoverable
  typed error on a value channel rather than producing silent `inf`/`nan`.
- **Structured bindings** (`const auto& [mnp, label]`) — read the zipped pair cleanly inside the lambda.

### 5. Example usage

```c++
#include <emc/shielding/cavity_resonance.hpp>
#include <print>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m

int main() {
    const emc::shielding::RectangularCavityInput in{
        .length = 0.30 * m,
        .width  = 0.12 * m,
        .height = 0.06 * m,
        .eps_r  = 1.0,
    };

    auto r = emc::shielding::rectangular_cavity_modes(in);
    if (!r) {                                   // structured error on the value channel
        std::println("error [{}]: {}", emc::to_string(r.error().code), r.error().message);
        return 1;
    }

    for (const auto& mode : r->modes)           // success arm — iterate the table
        std::println("{:<5} = {:8.3f} MHz",
                     mode.label, mode.frequency.numerical_value_in(mega<si::hertz>));

    std::println("dominant = {:.3f} MHz",
                 r->dominant().numerical_value_in(mega<si::hertz>));
}
```

### 6. Unit tests

```c++
// tests/shielding/cavity_resonance_rectangular_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>

#include <emc/shielding/cavity_resonance.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz
using emc::ErrorCode;
namespace sh = emc::shielding;

// Independent re-implementation of the kernel (the "second opinion") so the test
// does not just echo the library.
static double ref_box(double eps_r, double m_, double l, double n_, double w, double p_, double h) {
    const double c = 299'792'458.0;
    const double km = (m_ == 0) ? 0 : m_ / l;
    const double kn = (n_ == 0) ? 0 : n_ / w;
    const double kp = (p_ == 0) ? 0 : p_ / h;
    return (c / (2.0 * std::sqrt(eps_r))) * std::sqrt(km*km + kn*kn + kp*kp);
}

// (a) Cross-check against the independent reference over a spread of geometries.
//     Guards that every (m,n,p) row still maps to the right axis.
TEST_CASE("rectangular cavity matches independent reference", "[shielding][cavity]") {
    const std::array<sh::RectangularCavityInput, 3> cases{{
        {.length=0.5*m, .width=0.4*m, .height=0.2*m, .eps_r=1.0},
        {.length=0.3*m, .width=0.12*m, .height=0.06*m, .eps_r=4.3},
        {.length=1.2*m, .width=0.9*m, .height=0.5*m, .eps_r=2.2},
    }};
    auto in = GENERATE_REF(from_range(cases));
    auto r = sh::rectangular_cavity_modes(in);
    REQUIRE(r.has_value());

    // Compare each mode against the independent reference (guards table ordering).
    for (std::size_t i = 0; i < sh::kRectangularModeCount; ++i) {
        const auto& mnp = sh::kRectangularModes[i];
        const double expected = ref_box(in.eps_r,
            mnp[0], in.length.numerical_value_in(m),
            mnp[1], in.width .numerical_value_in(m),
            mnp[2], in.height.numerical_value_in(m));
        REQUIRE(emc::test::approx(r->modes[i].frequency, expected * Hz, 1e-9));
    }
}

// (b) Hand-computed known value: a 1 m air cube. f110 = c/2·√(1+1) = c/√2 ≈ 211.985 MHz.
TEST_CASE("rectangular cavity known value (1 m cube)", "[shielding][cavity]") {
    sh::RectangularCavityInput in{.length = 1.0*m, .width = 1.0*m, .height = 1.0*m, .eps_r = 1.0};
    auto r = sh::rectangular_cavity_modes(in);
    REQUIRE(r.has_value());
    REQUIRE(r->modes[0].label == "f110");
    REQUIRE(emc::test::approx(r->modes[0].frequency, 211.985'164e6 * Hz, 1e-5));   // c/√2
    // dominant of a cube is the f110/f101/f011 trio (all equal here).
    REQUIRE(emc::test::approx(r->dominant(), 211.985'164e6 * Hz, 1e-5));
}

// (c) Property: scaling all dimensions by k divides every frequency by k (geometric scaling).
TEST_CASE("rectangular cavity scales inversely with size", "[shielding][cavity][property]") {
    sh::RectangularCavityInput a{.length=0.4*m,.width=0.3*m,.height=0.2*m,.eps_r=1.0};
    sh::RectangularCavityInput b{.length=0.8*m,.width=0.6*m,.height=0.4*m,.eps_r=1.0};
    auto ra = sh::rectangular_cavity_modes(a);
    auto rb = sh::rectangular_cavity_modes(b);
    REQUIRE(ra.has_value()); REQUIRE(rb.has_value());
    for (std::size_t i = 0; i < sh::kRectangularModeCount; ++i)
        REQUIRE(emc::test::approx(rb->modes[i].frequency, ra->modes[i].frequency / 2.0, 1e-9));
}

// (d) Validation/edge: zero dimension and ε_r<1 are typed errors, not inf/nan.
TEST_CASE("rectangular cavity rejects bad input", "[shielding][cavity][validate]") {
    SECTION("zero height -> OutOfRange") {
        auto r = sh::rectangular_cavity_modes({.length=0.3*m,.width=0.2*m,.height=0.0*m,.eps_r=1.0});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "height");
    }
    SECTION("eps_r below 1 -> OutOfRange") {
        auto r = sh::rectangular_cavity_modes({.length=0.3*m,.width=0.2*m,.height=0.1*m,.eps_r=0.5});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "eps_r");
    }
}

// (e) Compile-time: the (m,n,p) table is constexpr and its labels line up.
static_assert(sh::kRectangularModes.size() == sh::kRectangularModeCount);
static_assert(sh::kRectangularLabels.size() == sh::kRectangularModeCount);
static_assert(sh::kRectangularModes[0] == std::array{1,1,0});   // f110 axis check
static_assert(sh::kRectangularLabels[0] == "f110");
```

What each guards: **(a)** every geometry reproduces the per-mode frequency and the table ordering is correct;
**(b)** the absolute scale is right (`c/√2` for a unit cube); **(c)** the geometric 1/size scaling law holds
(catches a stray unit factor); **(d)** validation returns the right `ErrorCode`; **(e)** the mode table is a
compile-time constant with labels aligned to axes.

---

## Cylindrical Enclosure (TM/TE modes via Bessel roots)

### 1. Overview

Computes resonant frequencies of a dielectric-filled **cylindrical** cavity (length `l`, radius `r`,
relative permittivity `ε_r`) for the dominant TM and TE modes. Each mode's radial wavenumber is a root of a
Bessel function (TM modes use roots `χ_mn` of `J_m`; TE modes use roots `χ'_mn` of `J'_m`); the axial term is
`pπ/l`:

```text
f = (c / (2π·√ε_r)) · √( (χ/r)² + (pπ/l)² )
```

The prefactor `c/(2π) = 47 713 451.6 m/s` is derived from `emc::constants::c`, so it stays in lock-step with
the rectangular kernel.

### 2. Public header

```c++
// include/emc/shielding/cavity_resonance.hpp  (continued)
namespace emc::shielding {

// ---- Cylindrical enclosure ------------------------------------------------

/// A cylinder mode: family (TE/TM), the Bessel root χ used radially, and the
/// axial index p (multiplied by π/l). `label` is the mode id.
enum class CylFamily : std::uint8_t { TE, TM };

struct CylModeSpec {
    std::string_view label;   // "ef111", "mf011", ...
    CylFamily        family;  // TE (ef*) or TM (mf*)
    double           chi;     // Bessel root: J'_m root (TE) or J_m root (TM)
    int              p;       // axial index (axial term = p·π / l)
};

inline constexpr int kCylindricalModeCount = 21;

struct CylindricalCavityInput {
    emc::units::Length length{1.0 * si::metre};    // l
    emc::units::Length radius{0.05 * si::metre};   // r
    double             eps_r {1.0};
};

struct CylindricalCavityResult {
    std::array<ModeFrequency, kCylindricalModeCount> modes{};
    [[nodiscard]] emc::units::Frequency dominant() const;
};

// The Bessel-root table. χ values are J'_m roots (TE) / J_m roots (TM); see comments.
inline constexpr std::array<CylModeSpec, kCylindricalModeCount> kCylindricalModes{{
    // --- TE (ef*) : roots of J'_m -----------------------------------------
    {"ef111", CylFamily::TE, 1.841, 1},   // J'_1 first root
    {"ef021", CylFamily::TE, 5.331, 1},   // J'_2 second root
    {"ef211", CylFamily::TE, 3.054, 1},   // J'_2 first root
    {"ef011", CylFamily::TE, 3.832, 1},   // J'_0 first root
    {"ef022", CylFamily::TE, 7.016, 2},
    {"ef212", CylFamily::TE, 3.054, 2},
    {"ef012", CylFamily::TE, 3.832, 2},
    {"ef121", CylFamily::TE, 5.331, 1},
    {"ef221", CylFamily::TE, 6.706, 1},
    // --- TM (mf*) : roots of J_m ------------------------------------------
    {"mf011", CylFamily::TM, 2.405, 1},   // J_0 first root
    {"mf021", CylFamily::TM, 5.520, 1},   // J_0 second root
    {"mf211", CylFamily::TM, 5.135, 1},   // J_2 first root
    {"mf110", CylFamily::TM, 3.832, 0},   // J_1 first root, p=0
    {"mf022", CylFamily::TM, 5.520, 2},
    {"mf212", CylFamily::TM, 5.135, 2},
    {"mf111", CylFamily::TM, 3.832, 1},   // J_1 first root
    {"mf120", CylFamily::TM, 7.016, 0},   // J_1 second root, p=0
    {"mf210", CylFamily::TM, 5.135, 0},
    {"mf012", CylFamily::TM, 2.405, 2},
    {"mf121", CylFamily::TM, 7.016, 1},
    {"mf220", CylFamily::TM, 8.417, 0},   // J_2 second root, p=0
}};

[[nodiscard]] std::expected<void, emc::Error> validate(const CylindricalCavityInput& in);
[[nodiscard]] emc::Result<CylindricalCavityResult> cylindrical_cavity_modes(const CylindricalCavityInput& in);

} // namespace emc::shielding
```

> [!IMPORTANT]
> The axial term is `p·π/l` with full-precision `emc::constants::pi` for **all** axial indices, so every
> `p=2` mode is exact. Keeping the roots as reviewable table data makes the Bessel-root set easy to audit.

### 3. Implementation

```c++
// src/shielding/cavity_resonance.cpp  (continued)
namespace emc::shielding {

std::expected<void, emc::Error> validate(const CylindricalCavityInput& in) {
    if (auto r = emc::require_positive(in.length.numerical_value_in(m), "length"); !r) return r;
    if (auto r = emc::require_positive(in.radius.numerical_value_in(m), "radius"); !r) return r;
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;
    return {};
}

emc::units::Frequency CylindricalCavityResult::dominant() const {
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<CylindricalCavityResult> cylindrical_cavity_modes(const CylindricalCavityInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    CylindricalCavityResult out{};
    std::ranges::transform(
        kCylindricalModes, out.modes.begin(),
        [&](const CylModeSpec& s) -> ModeFrequency {
            return ModeFrequency{
                .label     = s.label,
                .frequency = detail::cyl_mode(in.eps_r, s.chi, in.radius,
                                              static_cast<double>(s.p), in.length),
            };
        });
    return out;
}

} // namespace emc::shielding
```

### 4. Modern C++ features used here — and why

- **`enum class CylFamily` + `constexpr std::array<CylModeSpec,21>`** — the 21 modes differ only in
  `(family, χ, p)`. A table makes the Bessel roots reviewable data and the family a typed tag instead of a
  naming convention, with `χ` values audited in one place.
- **`std::ranges::transform` over the spec table** — one expression fills all 21 `ModeFrequency` entries.
- **`emc::constants::pi` in the axial term** — `p·π/l` is computed once with full precision, identical for
  every axial index.
- **`emc::constants::c`** — the cylinder prefactor `c/(2π)` is derived from the single source of truth, so it
  cannot drift from the rectangular constant.
- **mp-units `Frequency` result + `mp_units::sqrt`** — `χ/r` and `pπ/l` are `1/length`; `c·(1/length)` is a
  frequency by construction, so a unit slip cannot compile.
- **`std::array` Result + `dominant()` via `min_element`** — same multi-output ergonomics as the rectangular
  case: 21 outputs in one iterable container.

### 5. Example usage

```c++
#include <emc/shielding/cavity_resonance.hpp>
#include <print>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m

int main() {
    const emc::shielding::CylindricalCavityInput in{
        .length = 1.0 * m, .radius = 0.05 * m, .eps_r = 1.0,
    };
    auto r = emc::shielding::cylindrical_cavity_modes(in);
    if (!r) { std::println("error: {}", r.error().message); return 1; }

    for (const auto& mode : r->modes)
        std::println("{:<6} = {:8.2f} GHz",
                     mode.label, mode.frequency.numerical_value_in(giga<si::hertz>));
    std::println("dominant = {:.3f} GHz", r->dominant().numerical_value_in(giga<si::hertz>));
}
```

### 6. Unit tests

```c++
// tests/shielding/cavity_resonance_cylindrical_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <numbers>

#include <emc/shielding/cavity_resonance.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz
using emc::ErrorCode;
namespace sh = emc::shielding;

static double ref_cyl(double eps_r, double chi, double r, double p, double l) {
    const double c  = 299'792'458.0;
    const double pi = std::numbers::pi;
    const double kr = chi / r;
    const double kz = (p == 0) ? 0 : (p * pi) / l;
    return (c / (2.0 * pi * std::sqrt(eps_r))) * std::sqrt(kr*kr + kz*kz);
}

// (a) Cross-check against the independent reference over several geometries.
TEST_CASE("cylindrical cavity matches independent reference", "[shielding][cavity]") {
    const std::array<sh::CylindricalCavityInput, 3> cases{{
        {.length=1.0*m, .radius=0.05*m, .eps_r=1.0},
        {.length=0.3*m, .radius=0.02*m, .eps_r=2.2},
        {.length=2.0*m, .radius=0.10*m, .eps_r=4.3},
    }};
    auto in = GENERATE_REF(from_range(cases));
    auto r = sh::cylindrical_cavity_modes(in);
    REQUIRE(r.has_value());
    for (std::size_t i = 0; i < sh::kCylindricalModeCount; ++i) {
        const auto& s = sh::kCylindricalModes[i];
        const double expected = ref_cyl(in.eps_r, s.chi,
            in.radius.numerical_value_in(m), s.p, in.length.numerical_value_in(m));
        REQUIRE(emc::test::approx(r->modes[i].frequency, expected * Hz, 1e-9));
    }
}

// (b) Hand-computed: mf011 (χ=2.405, p=1) of a 0.05 m radius, 1 m long cavity.
//     f = c/(2π) · √( (2.405/0.05)² + (π/1)² ).
TEST_CASE("cylindrical cavity mf011 known value", "[shielding][cavity]") {
    sh::CylindricalCavityInput in{.length = 1.0*m, .radius = 0.05*m, .eps_r = 1.0};
    auto r = sh::cylindrical_cavity_modes(in);
    REQUIRE(r.has_value());
    const double f_expected = 299'792'458.0 / (2.0 * std::numbers::pi)
        * std::sqrt(std::pow(2.405/0.05, 2) + std::pow(std::numbers::pi/1.0, 2));
    auto it = std::ranges::find(r->modes, std::string_view{"mf011"}, &sh::ModeFrequency::label);
    REQUIRE(it != r->modes.end());
    REQUIRE(emc::test::approx(it->frequency, f_expected * Hz, 1e-6));
}

// (c) Property: doubling the radius and length halves every frequency (geometric scaling).
TEST_CASE("cylindrical cavity scales inversely with size", "[shielding][cavity][property]") {
    sh::CylindricalCavityInput a{.length=1.0*m, .radius=0.05*m, .eps_r=1.0};
    sh::CylindricalCavityInput b{.length=2.0*m, .radius=0.10*m, .eps_r=1.0};
    auto ra = sh::cylindrical_cavity_modes(a);
    auto rb = sh::cylindrical_cavity_modes(b);
    REQUIRE(ra.has_value()); REQUIRE(rb.has_value());
    for (std::size_t i = 0; i < sh::kCylindricalModeCount; ++i)
        REQUIRE(emc::test::approx(rb->modes[i].frequency, ra->modes[i].frequency / 2.0, 1e-9));
}

// (d) Validation/edge: zero radius and ε_r<1 are typed errors.
TEST_CASE("cylindrical cavity rejects bad input", "[shielding][cavity][validate]") {
    auto r1 = sh::cylindrical_cavity_modes({.length=1.0*m, .radius=0.0*m, .eps_r=1.0});
    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r1.error().code == ErrorCode::OutOfRange);
    REQUIRE(r1.error().field == "radius");

    auto r2 = sh::cylindrical_cavity_modes({.length=1.0*m, .radius=0.05*m, .eps_r=0.0});
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error().code == ErrorCode::OutOfRange);
    REQUIRE(r2.error().field == "eps_r");
}

// (e) Compile-time: table sane, families correct.
static_assert(sh::kCylindricalModes.size() == sh::kCylindricalModeCount);
static_assert(sh::kCylindricalModes[0].family == sh::CylFamily::TE);   // ef111
static_assert(sh::kCylindricalModes[9].family == sh::CylFamily::TM);   // mf011
static_assert(sh::kCylindricalModes[9].chi == 2.405);                  // J_0 first root
```

What each guards: **(a)** every geometry reproduces all 21 mode frequencies and the Bessel-root/axial table
is wired correctly; **(b)** the absolute scale and the `√(radial²+axial²)` combination are right for a named
TM mode; **(c)** the geometric scaling law (catches stray unit factors); **(d)** validation returns the right
`ErrorCode`; **(e)** the spec table's families and roots are a compile-time constant.

---

## Circuit Board Planes (mode grid)

### 1. Overview

Computes the parallel-plane resonance grid of a PCB power/ground plane pair of length `l`, width `w`,
separation `s`, relative permittivity `ε_r`. With the plane separation small, only the lateral `(m,n)` modes
matter:

```text
f_mn = (c / (2·√ε_r)) · √( (m/l)² + (n/w)² )
```

The thin-plane assumption requires `s < l/10` and `s < w/10`; both are enforced in `validate()`.

### 2. Public header

```c++
// include/emc/shielding/cavity_resonance.hpp  (continued)
namespace emc::shielding {

// ---- Circuit board planes -------------------------------------------------

inline constexpr int kBoardPlaneModeCount = 12;

struct BoardPlaneInput {
    emc::units::Length length    {1.0 * si::metre};   // l
    emc::units::Length width     {1.0 * si::metre};   // w
    emc::units::Length separation{1.0 * si::metre};   // s  (plane spacing)
    double             eps_r     {1.0};
};

struct BoardPlaneResult {
    std::array<ModeFrequency, kBoardPlaneModeCount> modes{};
    [[nodiscard]] emc::units::Frequency dominant() const;
};

// (m,n) lateral mode grid.
inline constexpr std::array<std::array<int, 2>, kBoardPlaneModeCount> kBoardPlaneModes{{
    {1,0}, {2,1}, {3,1}, {0,1}, {1,2}, {3,2},
    {1,1}, {2,2}, {2,3}, {2,0}, {3,0}, {3,3},
}};
inline constexpr std::array<std::string_view, kBoardPlaneModeCount> kBoardPlaneLabels{{
    "f10","f21","f31","f01","f12","f32","f11","f22","f23","f20","f30","f33"
}};

[[nodiscard]] std::expected<void, emc::Error> validate(const BoardPlaneInput& in);
[[nodiscard]] emc::Result<BoardPlaneResult> circuit_board_plane_modes(const BoardPlaneInput& in);

} // namespace emc::shielding
```

### 3. Implementation

```c++
// src/shielding/cavity_resonance.cpp  (continued)
namespace emc::shielding {

std::expected<void, emc::Error> validate(const BoardPlaneInput& in) {
    const double l = in.length.numerical_value_in(m);
    const double w = in.width.numerical_value_in(m);
    const double s = in.separation.numerical_value_in(m);

    if (auto r = emc::require_positive(l, "length"); !r) return r;
    if (auto r = emc::require_positive(w, "width");  !r) return r;
    if (auto r = emc::require_positive(s, "separation"); !r) return r;

    // ε_r cannot be < 1.
    if (auto r = emc::in_range(in.eps_r, 1.0, 1.0e4, "eps_r"); !r) return r;

    // Thin-plane assumption s < L/10 and s < W/10, surfaced as DomainError
    // (model assumption violated), with the offending field named.
    if (s > 0.1 * l)
        return std::unexpected(emc::domain_error(
            "separation must be < length/10 (thin-plane assumption)", "separation"));
    if (s > 0.1 * w)
        return std::unexpected(emc::domain_error(
            "separation must be < width/10 (thin-plane assumption)", "separation"));
    return {};
}

emc::units::Frequency BoardPlaneResult::dominant() const {
    const auto it = std::ranges::min_element(
        modes, {}, [](const ModeFrequency& mf) { return mf.frequency; });
    return it->frequency;
}

emc::Result<BoardPlaneResult> circuit_board_plane_modes(const BoardPlaneInput& in) {
    if (auto v = validate(in); !v)
        return std::unexpected(v.error());

    BoardPlaneResult out{};
    std::ranges::transform(
        std::views::zip(kBoardPlaneModes, kBoardPlaneLabels),
        out.modes.begin(),
        [&](const auto& pair) -> ModeFrequency {
            const auto& [mn, label] = pair;
            return ModeFrequency{
                .label     = label,
                // height index/dimension = 0 → the box kernel drops the p-axis cleanly.
                .frequency = detail::box_mode(in.eps_r,
                                              static_cast<double>(mn[0]), in.length,
                                              static_cast<double>(mn[1]), in.width,
                                              0.0, in.length /* unused */),
            };
        });
    return out;
}

} // namespace emc::shielding
```

> [!NOTE]
> `box_mode` is reused: passing `p = 0` makes the `(p/h)²` term vanish (the zero index short-circuits to
> `0/m`), so the board-plane formula is the rectangular kernel with one axis dropped — no separate 2-D kernel
> needed. The unused `height` argument only satisfies the signature; its index `0.0` guarantees it never
> contributes.

### 4. Modern C++ features used here — and why

- **`constexpr std::array<std::array<int,2>,12>` mode grid + `std::ranges::transform`** — same table-driven
  refactor as the rectangular case: the 12 `(m,n)` pairs become data filled by one transform. Reusing
  `detail::box_mode` with `p=0` proves the two calculators share one kernel.
- **`emc::constants::c`** — the shared speed-of-light source, identical to the rectangular kernel.
- **`std::expected<void,Error>` `validate()`** — the three model checks (`ε_r<1`, `s>l/10`, `s>w/10`) live in
  a pure `validate()` returning structured errors: `OutOfRange` for `ε_r`, `DomainError` (thin-plane
  assumption) for the separation checks, each carrying the offending `field`. The math never emits a string
  into a numeric field.
- **`emc::in_range` / `emc::require_positive` / `emc::domain_error`** — the three Foundation validators
  express the model guards declaratively.
- **`std::array` Result + `dominant()`** — 12 outputs in one iterable container; the dominant resonance is
  `min_element`.

### 5. Example usage

```c++
#include <emc/shielding/cavity_resonance.hpp>
#include <print>

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m

int main() {
    const emc::shielding::BoardPlaneInput in{
        .length     = 0.100 * m,            // 100 mm board
        .width      = 0.080 * m,
        .separation = 0.001 * m,            // 1 mm plane spacing (< L/10, W/10 ✓)
        .eps_r      = 4.3,                  // FR-4
    };
    auto r = emc::shielding::circuit_board_plane_modes(in);
    if (!r) {                               // e.g. separation too large -> DomainError
        std::println("error [{}] on '{}': {}",
                     emc::to_string(r.error().code), r.error().field, r.error().message);
        return 1;
    }
    for (const auto& mode : r->modes)
        std::println("{:<4} = {:7.1f} MHz",
                     mode.label, mode.frequency.numerical_value_in(mega<si::hertz>));
}
```

### 6. Unit tests

```c++
// tests/shielding/cavity_resonance_board_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>

#include <emc/shielding/cavity_resonance.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // m, Hz
using emc::ErrorCode;
namespace sh = emc::shielding;

static double ref_plane(double eps_r, double m_, double l, double n_, double w) {
    const double c = 299'792'458.0;
    const double km = (m_ == 0) ? 0 : m_ / l;
    const double kn = (n_ == 0) ? 0 : n_ / w;
    return (c / (2.0 * std::sqrt(eps_r))) * std::sqrt(km*km + kn*kn);
}

// (a) Cross-check against the independent reference over guard-passing geometries.
TEST_CASE("board-plane modes match independent reference", "[shielding][cavity]") {
    const std::array<sh::BoardPlaneInput, 3> cases{{
        {.length=1.0*m, .width=1.0*m, .separation=0.001*m, .eps_r=1.0},
        {.length=0.10*m, .width=0.08*m, .separation=0.001*m, .eps_r=4.3},
        {.length=0.20*m, .width=0.15*m, .separation=0.002*m, .eps_r=2.2},
    }};
    auto in = GENERATE_REF(from_range(cases));
    auto r = sh::circuit_board_plane_modes(in);
    REQUIRE(r.has_value());
    for (std::size_t i = 0; i < sh::kBoardPlaneModeCount; ++i) {
        const auto& mn = sh::kBoardPlaneModes[i];
        const double expected = ref_plane(in.eps_r,
            mn[0], in.length.numerical_value_in(m),
            mn[1], in.width .numerical_value_in(m));
        REQUIRE(emc::test::approx(r->modes[i].frequency, expected * Hz, 1e-9));
    }
}

// (b) Hand-computed: 1 m × 1 m air plane. f10 = c/2 / 1 = 149.896229 MHz.
TEST_CASE("board-plane known value", "[shielding][cavity]") {
    sh::BoardPlaneInput in{.length=1.0*m, .width=1.0*m, .separation=0.001*m, .eps_r=1.0};
    auto r = sh::circuit_board_plane_modes(in);
    REQUIRE(r.has_value());
    REQUIRE(r->modes[0].label == "f10");
    REQUIRE(emc::test::approx(r->modes[0].frequency, 149.896'229e6 * Hz, 1e-6));   // c/2
    // f11 = c/2·√2 = 211.985 MHz (index 6 in the table)
    REQUIRE(r->modes[6].label == "f11");
    REQUIRE(emc::test::approx(r->modes[6].frequency, 211.985'164e6 * Hz, 1e-5));
}

// (c) Property: board-plane f_mn equals the rectangular kernel with the height axis dropped.
//     Cross-checks that reusing box_mode with p=0 is exact.
TEST_CASE("board-plane is the rectangular kernel with p=0", "[shielding][cavity][property]") {
    sh::BoardPlaneInput bp{.length=0.10*m, .width=0.08*m, .separation=0.001*m, .eps_r=4.3};
    sh::RectangularCavityInput rc{.length=0.10*m, .width=0.08*m, .height=0.5*m, .eps_r=4.3};
    auto rb = sh::circuit_board_plane_modes(bp);
    auto rr = sh::rectangular_cavity_modes(rc);
    REQUIRE(rb.has_value()); REQUIRE(rr.has_value());
    // f11 (board, p=0) must equal the rectangular f110 (same m,n; p=0).
    REQUIRE(emc::test::approx(rb->modes[6].frequency, rr->modes[0].frequency, 1e-9)); // f11 vs f110
}

// (d) Validation/edge: the three model checks return typed errors.
TEST_CASE("board-plane validation returns typed errors", "[shielding][cavity][validate]") {
    SECTION("eps_r < 1 -> OutOfRange") {
        auto r = sh::circuit_board_plane_modes({.length=0.1*m,.width=0.1*m,.separation=0.001*m,.eps_r=0.5});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
        REQUIRE(r.error().field == "eps_r");
    }
    SECTION("separation > L/10 -> DomainError (thin-plane assumption)") {
        auto r = sh::circuit_board_plane_modes({.length=0.1*m,.width=0.1*m,.separation=0.02*m,.eps_r=4.3});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::DomainError);
        REQUIRE(r.error().field == "separation");
    }
    SECTION("zero separation -> OutOfRange") {
        auto r = sh::circuit_board_plane_modes({.length=0.1*m,.width=0.1*m,.separation=0.0*m,.eps_r=4.3});
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == ErrorCode::OutOfRange);
    }
}

// (e) Compile-time: table + labels.
static_assert(sh::kBoardPlaneModes.size() == sh::kBoardPlaneModeCount);
static_assert(sh::kBoardPlaneLabels[0] == "f10");
static_assert(sh::kBoardPlaneModes[6] == std::array{1,1});   // f11 axis check
```

What each guards: **(a)** guard-passing geometries reproduce the `(m,n)` grid; **(b)** the absolute scale
(`c/2` for a 1 m plane); **(c)** the board kernel is *literally* the rectangular kernel with `p=0` — a strong
cross-calculator invariant that proves the shared `detail::box_mode`; **(d)** all three model checks return
the right typed `ErrorCode` on the right `field`; **(e)** the grid/labels are compile-time constants.

---

## Cross-references

- [`./00-foundation-code.md`](./00-foundation-code.md) — `emc::constants::c`/`pi`,
  `emc::units::{Length,Frequency}`, `emc::Result`, `emc::in_range`/`require_positive`/`domain_error`,
  `emc::test::{load_csv,approx}`.
