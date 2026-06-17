# 📐 Quantities & Units with mp-units

> Purpose: define how the `emc` library represents every physical quantity and unit conversion using the **mp-units** library, so that every conversion is compile-checked, exact, and zero-cost.

This is the single most leveraged document in the plan: units touch every calculator. Get the vocabulary right here and docs 06 (calculator pattern), 07 (calculator catalog), and 09 (testing strategy) inherit correctness for free.

> [!IMPORTANT]
> EMC inputs span **Hz to GHz** and **metres to mils**, and a single formula routinely mixes frequency, length, conductivity, and permeability. Encoding the unit *and* the quantity kind in the type — rather than as a runtime multiplier — is what makes the whole library type-safe. Everything below follows from that one design rule.

---

## 1. 🎯 Why units belong in the type system

EMC calculators are unit-dense by nature. A skin-depth call mixes a frequency (Hz–GHz), a conductivity (S/m), and a dimensionless relative permeability; a microstrip call mixes three lengths (which a user may enter in mm *or* mils) with a relative permittivity. If a quantity is modelled as a bare `double`, the type system knows nothing about any of it:

- A `double` for a frequency and a `double` for a length are the **same type**, so the compiler will happily let you add a wavelength to a frequency, pass mils where mm is expected, or forget a conversion entirely.
- Conversion factors (`1e6` for MHz→Hz, `0.0254` for inch→m, `39.3700787…` for mm→mils) have to be written by hand somewhere, and a hand-typed factor can be wrong silently — wrong magnitude, wrong direction, or a truncated constant — with nothing to catch it.
- The same `Hz / kHz / MHz / GHz` ladder and the same `mm ↔ mils` arithmetic get re-derived in every calculator, with no single source of truth.

mp-units removes the entire class of problem by making the **unit and the dimension part of the type**. Three kinds of mistake become *compile errors* instead of wrong numbers:

| Error class | Example | With `double` | With mp-units |
|---|---|---|---|
| **Dimension mismatch** | adding a length to a frequency | compiles, garbage result | ❌ compile error |
| **Unit mix-up** | passing mils where mm expected | compiles, off by ~25.4× | ✅ auto-converted, exact |
| **Wrong/imprecise factor** | a hand-typed `3.281` or `39.37` | silent error | ✅ factor derived by the library, exact |

> [!IMPORTANT]
> A conversion factor a human never types is a factor that can never be wrong. mp-units derives every factor from the unit *definitions*, so direction and precision are guaranteed by construction — this is the core reason the library uses it.

---

## 2. 🧩 What mp-units is, and the model we use

[mp-units](https://mpusz.github.io/mp-units/) is the standardization-track C++ quantities-and-units library (the basis of the ISO C++ proposals P1935/P3045). We target **mp-units 2.x** with the **`isq` + `si` systems**. It gives us:

- **Strong dimensional analysis** — every quantity carries its *dimension* (length, time, …) and *quantity kind* (ISO/IEC 80000 quantity, e.g. `isq::frequency`) in the type. Adding a length to a time is a compile error; the result of `length / time` is automatically a `speed`.
- **Compile-time unit checking** — a `quantity` knows its unit (`si::hertz`, `si::metre`, `si::milli<si::metre>`). Mixing units of the same dimension auto-converts; mixing dimensions fails to compile.
- **Zero runtime cost** — a `quantity<si::hertz, double>` is a single `double`. The unit and dimension live entirely in the type. There is no per-value tag, no virtual dispatch; conversions that are statically known fold to a multiply (often constant-folded away).

### The core type

```c++
mp_units::quantity<Reference, Rep>
```

- `Reference` is a **unit-with-quantity-kind**, e.g. `si::hertz`, `isq::frequency[si::hertz]`, `si::metre`, `si::milli<si::metre>`. The bracket form `isq::frequency[si::hertz]` pins both the *kind* (frequency) and the *unit* (Hz), which is what we want for public API types so that, e.g., a frequency and an `isq::activity` (also 1/s, becquerel) never silently interconvert.
- `Rep` is the **representation type** (the stored numeric type), defaulting to `double`. We standardize on `double` (see §9).

### Constructing quantities

```c++
#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>
using namespace mp_units;
using mp_units::si::unit_symbols::Hz;   // brings in MHz, kHz, GHz, m, mm, ...
using namespace mp_units::si::unit_symbols;

quantity f1 = 27.0 * MHz;               // CTAD: quantity<si::mega<si::hertz>, double>
quantity len = 1.5 * mm;                // length in millimetres
auto f2 = 27 * MHz;                     // integer rep is fine too (we cast to double)
```

### Conversions: `.in(unit)` and `value_cast`

```c++
quantity f = 27.0 * MHz;
quantity f_hz = f.in(Hz);               // -> 27'000'000 Hz  (exact, value-preserving)
double hz_number = f.numerical_value_in(Hz);  // -> 27000000.0 (raw number, escape hatch)

quantity len = 1.5 * mm;
quantity len_in_inch = len.in(inch);    // auto conversion, compile-checked dimension

// value_cast forces a representation/unit change that might be lossy (e.g. to integer):
auto n = value_cast<int>(len.in(mm));   // explicit, opt-in narrowing
```

`.in(u)` is the everyday tool — it returns the same value expressed in `u`. There is **no place to put a wrong factor**: the factor between `mm` and `inch` is derived by the library from the unit definitions, not typed by a human.

> [!TIP]
> Prefer `.in(unit)` at call boundaries and reach for `numerical_value_in(unit)` only when you genuinely need a raw `double` to hand to `std::sqrt`/`std::log`. Keeping the typed `quantity` as long as possible keeps the compiler checking your dimensions.

### `quantity_point` (affine quantities)

A plain `quantity` is a *vector* quantity (differences, magnitudes). For **absolute points on an affine scale** — temperatures referenced to a zero, absolute timestamps — use `quantity_point`, which separates "30 °C" (a point) from "a 30 K rise" (a delta). EMC inputs are overwhelmingly vector-like (frequencies, lengths, fields), so we use `quantity` almost everywhere. The one place `quantity_point` *could* matter is if a calculator ever takes an **absolute temperature** input (e.g. a future conductivity-vs-temperature feature); flag those individually. For the present calculator set, plain `quantity` is correct.

---

## 3. 📚 The `emc::units` project vocabulary

We do **not** scatter raw `mp_units::quantity<...>` spellings through calculator signatures. Instead `include/emc/units.hpp` defines a curated **project vocabulary** of named aliases for exactly the EMC quantities the domain needs. Benefits:

- One place to read what quantities the domain uses.
- Calculator signatures stay readable (`Frequency`, not `quantity<isq::frequency[si::hertz], double>`).
- If we ever change `Rep` (double → something), it changes in one header.

```c++
// include/emc/units.hpp
#pragma once

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/iec.h>   // for dB-related helpers if needed

namespace emc::units {

namespace mpu = mp_units;
namespace isq = mp_units::isq;
namespace si  = mp_units::si;

// --- Representation type: one knob for the whole library (see §9) ---
using Rep = double;

// --- Helper alias so every line below stays short ---
template <auto Reference>
using Q = mpu::quantity<Reference, Rep>;

// ----------------------------------------------------------------------
//  Core EMC quantities (pin BOTH the ISQ quantity kind AND the SI unit)
// ----------------------------------------------------------------------
using Frequency   = Q<isq::frequency[si::hertz]>;                 // Hz
using Length      = Q<isq::length[si::metre]>;                    // m
using Time        = Q<isq::time[si::second]>;                     // s
using Velocity    = Q<isq::speed[si::metre / si::second]>;        // m/s (e.g. c)

using Voltage     = Q<isq::voltage[si::volt]>;                    // V
using Current     = Q<isq::current[si::ampere]>;                  // A
using Power       = Q<isq::power[si::watt]>;                      // W
using Impedance   = Q<isq::resistance[si::ohm]>;                  // Ω  (Z0, R, RL...)
using Resistance  = Impedance;                                    // same dimension
using Capacitance = Q<isq::capacitance[si::farad]>;              // F
using Inductance  = Q<isq::inductance[si::henry]>;               // H

// Material / field quantities
using Conductivity =
    Q<isq::electrical_conductivity[si::siemens / si::metre]>;     // S/m
using Resistivity =
    Q<isq::resistivity[si::ohm * si::metre]>;                     // Ω·m
using Permeability =
    Q<isq::permeability[si::henry / si::metre]>;                  // H/m  (μ = μr·μ0)
using Permittivity =
    Q<isq::permittivity[si::farad / si::metre]>;                  // F/m  (ε = εr·ε0)

using ElectricFieldStrength =
    Q<isq::electric_field_strength[si::volt / si::metre]>;        // V/m
using MagneticFieldStrength =
    Q<isq::magnetic_field_strength[si::ampere / si::metre]>;      // A/m
using PowerDensity =
    Q<(isq::power / isq::area)[si::watt / (si::metre * si::metre)]>; // W/m^2

// Per-length results (e.g. resistance/metre, capacitance/cm, propagation delay/cm)
using ResistancePerLength =
    Q<(isq::resistance / isq::length)[si::ohm / si::metre]>;      // Ω/m
using CapacitancePerLength =
    Q<(isq::capacitance / isq::length)[si::farad / si::metre]>;   // F/m
using TimePerLength =
    Q<(isq::time / isq::length)[si::second / si::metre]>;         // s/m (Tpd)

// ----------------------------------------------------------------------
//  Dimensionless quantities  (relative permittivity/permeability, VSWR,
//  gain ratio, optical coverage). These are quantities of dimension ONE,
//  NOT bare doubles -- so they still get a kind and cannot be confused.
// ----------------------------------------------------------------------
using Dimensionless = Q<mpu::one>;                                // generic ratio
using RelativePermittivity = Dimensionless;                       // εr  (1..~15)
using RelativePermeability = Dimensionless;                       // μr
using Vswr                 = Dimensionless;                       // >= 1
using Ratio                = Dimensionless;                       // gains, coverage fraction

}  // namespace emc::units
```

### Notes on the subtle ones

- **Relative permittivity / permeability are dimensionless.** εr is the *ratio* ε/ε₀ and carries dimension one. We model it as `RelativePermittivity = quantity<one>`, **not** a bare `double`, so it still flows through the type system and a designated-initializer call site reads `.relative_permittivity = 4.7 * one`. The *physical* permeability used in formulas is `μ = μr · μ0` where `μ0` is a `Permeability` constant from `emc::constants` (doc 04) — a single constexpr definition shared by every calculator.

- **Antenna factor** (used by the AntennaFactor↔Gain converter) has SI dimension **1/m** (it relates an incident E-field in V/m to a received voltage in V). We give it a named alias:

  ```c++
  using AntennaFactor = Q<(mpu::one / isq::length)[mpu::one / si::metre]>;  // 1/m
  ```

  It is frequently *displayed* in dB (dB/m). dB is handled per §9 (logarithmic wrapper), not as a linear unit.

- **dBm / dB are logarithmic — they are NOT mp-units linear units.** See the dedicated subsection in §9. We keep power in linear watts internally (`Power`) and provide a typed `Decibel`/`Dbm` wrapper for the boundary.

- **VSWR, optical coverage, gain ratio** are dimensionless ratios → `Dimensionless`.

---

## 4. 🔌 Inputs as quantities

Per the canonical calculator pattern (doc 06), each calculator has an **aggregate input struct** whose members are `emc::units` quantities with sensible defaults. Units are therefore part of the API and conversions are automatic.

```c++
// include/emc/basic/skin_depth.hpp
#pragma once
#include <expected>
#include "emc/units.hpp"
#include "emc/error.hpp"

namespace emc::basic {

struct SkinDepthInput {
    emc::units::Frequency            frequency;
    emc::units::Conductivity         conductivity;
    emc::units::RelativePermeability relative_permeability = 1.0 * mp_units::one;
};

struct SkinDepthResult {
    emc::units::Length skin_depth;   // δ, always SI internally (metres)
};

[[nodiscard]] std::expected<SkinDepthResult, emc::Error>
calculate(const SkinDepthInput&);

}  // namespace emc::basic
```

Call sites are explicit about units, and the compiler verifies them:

```c++
using namespace mp_units::si::unit_symbols;   // MHz, etc.
using mp_units::one;

auto r = emc::basic::calculate(emc::basic::SkinDepthInput{
    .frequency             = 27.0 * MHz,        // could equally be 27e6 * Hz
    .conductivity          = 1.4493e7 * (S / m),// Nickel; from emc::materials, doc 04
    .relative_permeability = 600.0 * one,
});
```

If a caller writes `.frequency = 27.0 * mm` (a length), it **does not compile** — the dimension is wrong. If they write `27.0 * kHz`, it compiles and is converted to the formula's working unit automatically. No `* 1e6` and no runtime unit factor appears anywhere on this path.

Designated initializers + member defaults (a feature we lean on hard, see doc 02) mean a caller only specifies what they care about; `relative_permeability` defaults to 1 for non-magnetic materials.

---

## 5. 🧮 Worked examples: formulas, unit-clean

These are the standard textbook EMC formulas, implemented directly against the typed vocabulary. There is no separate "conversion layer" — the units *are* the implementation.

### 5a. Skin depth

The skin depth of a good conductor is the standard expression

```text
δ = sqrt( 1 / (π · f · μ · σ) ),   with  μ = μr · μ0
```

where `f` is frequency (Hz), `σ` conductivity (S/m), `μ` permeability (H/m). Dimensionally the product `f · μ · σ` has units of `1/m²`, so its reciprocal is `m²` and the square root is metres — and mp-units enforces exactly that chain.

```c++
// src/basic/skin_depth.cpp
#include "emc/basic/skin_depth.hpp"
#include "emc/constants.hpp"
#include <cmath>

namespace emc::basic {

std::expected<SkinDepthResult, emc::Error>
calculate(const SkinDepthInput& in) {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());

    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    // mu = mu_r * mu0   (mu0 is the ONE constexpr definition, doc 04)
    const auto mu = in.relative_permeability * emc::constants::mu0;  // -> H/m

    // delta = sqrt( 1 / (pi * f * mu * sigma) )
    // The product f*mu*sigma carries units (Hz * H/m * S/m); 1/that is 1/m^2;
    // sqrt -> metres. mp-units tracks this; we never extract a raw number until the end.
    const auto under = constants::pi * in.frequency * mu * in.conductivity;  // -> 1/m^2
    const quantity<si::metre, double> delta =
        sqrt(1.0 * one / under);   // mp-units provides sqrt for quantities (C++23 constexpr <cmath>)

    return SkinDepthResult{ .skin_depth = delta };
}

}  // namespace emc::basic
```

> [!NOTE]
> `mp_units::sqrt` returns a quantity whose unit is the square-root of the argument's unit — so `sqrt(m²) = m` is *enforced*, not assumed. The result is always stored in SI metres; choosing a *display* unit (cm, mils, …) is the boundary's job (§6), never the core's.

### 5b. Microstrip trace

For a microstrip line, the classic Wheeler/IPC closed-form gives characteristic impedance, capacitance per length, and propagation delay from the dielectric height `H`, conductor thickness `T`, trace width `W`, and relative permittivity `εr`:

```text
Z0  = 87 / sqrt(εr + 1.41) · ln( 5.98·H / (0.8·W + T) )      [Ω]
C0  = 0.67·(εr + 1.41) / ln( 5.98·H / (0.8·W + T) )          [pF/cm]
Tpd = C0 · Z0                                                 [ps/cm]
```

The formula is **unit-relative**: it depends only on the ratios `H/W` and `T/W`, so the three lengths must merely be expressed in one *consistent* unit. We pick a working unit (mm) and express all three lengths in it with a single `numerical_value_in(mm)` each. Whether the caller supplied mm or mils is irrelevant — mp-units converts exactly.

```c++
// include/emc/component/microstrip_trace.hpp
struct MicrostripTraceInput {
    emc::units::Length              h;   // dielectric height
    emc::units::Length              t;   // trace thickness
    emc::units::Length              w;   // trace width
    emc::units::RelativePermittivity relative_permittivity;
};
struct MicrostripTraceResult {
    emc::units::Impedance           z0;            // characteristic impedance, Ω
    emc::units::CapacitancePerLength c0;           // F/m  (display as pF/cm or pF/inch)
    emc::units::TimePerLength        propagation_delay;  // s/m (display ps/cm or ps/inch)
};

// src/component/microstrip_trace.cpp  (forward solve)
std::expected<MicrostripTraceResult, emc::Error>
calculate(const MicrostripTraceInput& in) {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    // Wheeler's formula is unit-RELATIVE: it only uses the ratios H/W, T/W.
    // So we pick ONE working unit and express all three lengths in it -- ONE line each.
    // mp-units guarantees consistency regardless of what unit the caller supplied.
    const double H = in.h.numerical_value_in(mm);
    const double T = in.t.numerical_value_in(mm);
    const double W = in.w.numerical_value_in(mm);
    const double eps = in.relative_permittivity.numerical_value_in(one);

    const double Z = 87.0 * std::log(5.98 * H / (0.8 * W + T)) / std::sqrt(eps + 1.41);
    const double C = 0.67 * (eps + 1.41) / std::log(5.98 * H / (0.8 * W + T)); // pF/cm
    const double Tpd = C * Z;                                                  // ps/cm

    return MicrostripTraceResult{
        .z0                = Z * si::ohm,
        .c0                = (C * si::pico<si::farad>) / si::centi<si::metre>,  // typed, no guessing
        .propagation_delay = (Tpd * si::pico<si::second>) / si::centi<si::metre>,
    };
}
```

Two structural properties fall out of the design:

1. **No per-unit branching.** Because the formula is ratio-based, we choose one working unit and call `numerical_value_in(mm)` once per length. The mm-vs-mils choice never enters the math; mp-units supplies the exact factor.
2. **Bidirectional solving stays DRY.** The inverse solves (H/T/W from Z0) are additional small functions that likewise take/return quantities; none re-implements unit handling. (The exact inverse signatures live in docs 06/07.)

> [!CAUTION]
> The model constants `5.98`, `0.67`, `87`, `1.41` bake in cm/pF/ps assumptions, so the result units (`pF/cm`, `ps/cm`) are part of the formula's identity. Attach those output units explicitly as shown — do not let an output silently land in raw SI and assume the magnitude is the same.

---

## 6. 🪟 The presentation boundary: parse in, format out

The library speaks **quantities**. A user-facing front end shows unit dropdowns. Conversion between *(value, unit-string)* and `quantity` lives in a **thin adapter at the boundary** — never in the core. This keeps string→unit handling out of the domain code while preserving the front end's freedom to display any unit.

```c++
// presentation-side adapter (lives in the UI layer, NOT in libemc)
#include <mp-units/systems/si.h>
#include <string_view>
#include <expected>

namespace emc_ui {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// PARSE: (number from a field, unit label from a dropdown) -> Length quantity
std::expected<emc::units::Length, emc::Error>
parse_length(double value, std::string_view unit) {
    if (unit == "m")    return value * m;
    if (unit == "cm")   return value * (si::centi<si::metre>);
    if (unit == "mm")   return value * mm;
    if (unit == "um")   return value * (si::micro<si::metre>);
    if (unit == "nm")   return value * (si::nano<si::metre>);
    if (unit == "km")   return value * km;
    if (unit == "inch" || unit == "in") return value * inch;
    if (unit == "mils") return value * (non_si::thou);   // 1 mil = 1/1000 inch
    if (unit == "foot" || unit == "ft") return value * (international::foot);
    if (unit == "mile" || unit == "mi") return value * (international::mile);
    return std::unexpected(emc::Error{emc::ErrorCode::UnknownUnit, std::string{unit}});
}

// FORMAT: Length quantity -> number to display, in the chosen display unit
double format_length(emc::units::Length q, std::string_view unit) {
    if (unit == "mm")   return q.numerical_value_in(mm);
    if (unit == "cm")   return q.numerical_value_in(si::centi<si::metre>);
    if (unit == "inch") return q.numerical_value_in(inch);
    if (unit == "mils") return q.numerical_value_in(non_si::thou);
    // ... etc; one switch, one source of truth, factors supplied BY mp-units
    return q.numerical_value_in(m);
}

}  // namespace emc_ui
```

Key properties of this boundary:

- **No factor numbers appear.** `parse_length`/`format_length` map a *string* to an mp-units *unit*; the numeric factor is computed by mp-units, so a wrong-direction or truncated factor cannot occur — `international::foot` and `non_si::thou` are exact by definition.
- This adapter is the **only** place strings touch units. It is a handful of lines per dimension (one parse/format pair for length, frequency, time, etc.), so the entire string↔unit surface of an app is small and centralized.
- For richer round-trips you can use mp-units text I/O (`std::format`/`std::print` with `{}` on a quantity prints `1.5 mm`); for driving numeric fields we want a raw number, so we use `numerical_value_in`.

> [!TIP]
> Returning `std::expected<Length, Error>` from `parse_length` makes an unrecognized unit string an explicit, recoverable `ErrorCode::UnknownUnit` rather than a guessed default — the boundary should fail loudly, never silently.

---

## 7. 📋 Units the library accepts and their canonical SI storage

Every unit string a front end is likely to offer, mapped to its mp-units unit. Internally, every quantity is stored in its canonical SI form; the strings below are the accepted *input/display* spellings, and the right column is the exact unit mp-units uses to convert them.

### Frequency

| Input string | mp-units unit | Canonical SI |
|---|---|---|
| `Hz`  | `si::hertz` | Hz (base) |
| `kHz` | `si::kilo<si::hertz>` | Hz |
| `MHz` | `si::mega<si::hertz>` | Hz |
| `GHz` | `si::giga<si::hertz>` | Hz |

### Length

| Input string | mp-units unit | Canonical SI / note |
|---|---|---|
| `m`    | `si::metre` | m (base) |
| `cm`   | `si::centi<si::metre>` | m |
| `mm`   | `si::milli<si::metre>` | m |
| `um`   | `si::micro<si::metre>` | m |
| `nm`   | `si::nano<si::metre>` | m |
| `km`   | `si::kilo<si::metre>` | m |
| `inch` / `in` | `international::inch` | `= 0.0254 m` exactly ✅ |
| `mils` | `international::thou` | 1 mil = 1/1000 inch = `2.54e-5 m` exactly ✅ |
| `foot` / `ft` | `international::foot` | `= 0.3048 m` exactly ✅ |
| `mile` / `mi` | `international::mile` | `= 1609.344 m` exactly ✅ |

> [!NOTE]
> mp-units provides imperial units in its `international`/`usc` systems (`<mp-units/systems/international.h>`). If a given build doesn't ship `thou`, define it locally as `inline constexpr struct thou final : named_unit<"thou", mag_ratio<1,1000> * international::inch> {} thou;` — still a single exact definition, not a typed factor.

### Capacitance-per-length / time-per-length (Microstrip outputs)

| Input string | mp-units expression |
|---|---|
| `pF/cm`  | `si::pico<si::farad> / si::centi<si::metre>` |
| `pF/inch`| `si::pico<si::farad> / international::inch` |
| `psec/cm`| `si::pico<si::second> / si::centi<si::metre>` |
| `psec/inch`| `si::pico<si::second> / international::inch` |

### NOT units — keep as enum/int (cross-ref doc 04)

| Input string(s) | Why it is not a unit | How we model it |
|---|---|---|
| AWG gauge: `OOOO`, `OOO`, `OO`, `O`, `4`, `8`, … | A wire *gauge index*, not a physical unit. The standard relation maps a gauge `g` to a diameter via `dm = 0.0254 · 0.005 · pow(92, (36 − g)/39)`. | An `enum class Awg` (or plain `int` gauge) → a `Length` *diameter* via a pure function. Stays an index; see doc 04. |
| Material names: `Copper`, `Nickel`, … | Selectors into a material table, not units. | `enum class Material` → `emc::materials` lookup returning typed `Conductivity`/`Permeability`. See doc 04. |
| `dBm`, `dB`, `dB/m` | **Logarithmic**, not linear units (see §9). | Typed `Decibel`/`Dbm` wrapper; convert to/from linear `Power`/`AntennaFactor` at the boundary. |

---

## 8. 🛠️ Build/dependency integration (teaser → doc 08)

mp-units is a header-heavy library distributed via CMake/Conan/vcpkg. The dependency is declared once and propagated transitively so downstream `find_package(emc)` users get it automatically:

```cmake
# Option A: package manager (preferred for CI reproducibility)
find_package(mp-units CONFIG REQUIRED)

# Option B: FetchContent (zero-setup for contributors)
include(FetchContent)
FetchContent_Declare(mp-units
    GIT_REPOSITORY https://github.com/mpusz/mp-units.git
    GIT_TAG        v2.x.y)             # pin an exact tag
FetchContent_MakeAvailable(mp-units)

target_link_libraries(emc PUBLIC mp-units::mp-units)   # PUBLIC: it's in our headers
```

Because `emc::units` types appear in **public headers**, mp-units must be a `PUBLIC` (interface-propagated) dependency, and the exported `emcConfig.cmake` must `find_dependency(mp-units)`. Full install/export wiring, version pinning, and the `find_dependency` plumbing are in **doc 08**.

---

## 9. ⚖️ Trade-offs and pitfalls

### Representation type: `double`

We fix `Rep = double` library-wide (one alias in `units.hpp`, §3). Rationale:

- `double` is the natural representation for EMC engineering tolerances and is consistent across platforms.
- `long double` buys little here and is slow/inconsistent across platforms (it's 64-bit on MSVC anyway). Not worth it.
- Integer reps are tempting for exactness but break the moment a formula needs `sqrt`/`log`; we keep `double` and use `value_cast<int>` only at explicit display points.

### Compile time

mp-units is template-heavy; expect a measurable hit to incremental builds, *especially* in headers that instantiate many quantity types. Mitigations:

- **Compiled library, not header-only** (a locked decision): the heavy instantiations live in `src/*.cpp`, compiled once. Consumers including `emc/units.hpp` pay only for the alias declarations they touch.
- Keep `units.hpp` lean (aliases only; no algorithms).
- Use precompiled headers for `<mp-units/...>` in the library's own build (doc 08).

### Template error verbosity

A dimension mismatch produces a long template error. This is the cost of moving the bug to compile time (vs. a silent runtime factor). Mitigations:

- mp-units 2.x has deliberately improved diagnostics; the first error line usually names the offending quantity kinds.
- The `emc::units` aliases make messages mention `emc::units::Frequency` rather than a raw `quantity<...>` blob.
- Encourage `.in(unit)` at call boundaries so mismatches surface at the call site, not deep in a formula.

### The dependency itself

mp-units is a real third-party dependency on a pre-1.0-of-the-standard library (its API tracks evolving proposals and can change between 2.x minors). Mitigation: **pin an exact tag** (§8), and isolate every mp-units spelling behind `emc::units` so a future API shift is a one-header edit.

### Pitfall: relative permittivity/permeability are dimensionless — not bare doubles

εr and μr have dimension **one**. Modeling them as `double` would re-open the "is this a ratio or a physical value?" ambiguity. We model them as `quantity<one>` (`RelativePermittivity`, `RelativePermeability`). The *physical* permittivity/permeability used in formulas is `εr · ε0` / `μr · μ0`, where `ε0`/`μ0` are the single constexpr constants in `emc::constants` (doc 04).

### Pitfall: dB and dBm are logarithmic — do **not** make them mp-units linear units

> [!WARNING]
> A decibel is `10·log₁₀(ratio)`; dBm is dB relative to 1 mW. These are **not** linear scalings of a unit, so they must never be modelled as an mp-units `quantity` unit — doing so would make ordinary arithmetic produce nonsense (`3 dB + 3 dB` is *not* `6 dB` of power, and scaling a dB value is meaningless). Keep the physical quantity linear; treat the dB form as a separate, explicitly-converted representation.

Strategy:

- Keep the physical quantity **linear** internally: power as `emc::units::Power` (watts), antenna factor as `AntennaFactor` (1/m).
- Provide a small **typed logarithmic wrapper** for the boundary:

  ```c++
  namespace emc::units {

  // A value on a decibel scale. NOT an mp-units unit; an explicit log wrapper.
  struct Dbm {
      double value;   // dB relative to 1 mW
  };

  // Conversions are explicit functions, never implicit arithmetic.
  [[nodiscard]] constexpr Power  to_power(Dbm x) noexcept {     // dBm -> W
      return std::pow(10.0, x.value / 10.0) * mp_units::si::milli<mp_units::si::watt>;
  }
  [[nodiscard]] inline Dbm       to_dbm(Power p) noexcept {     // W -> dBm
      return Dbm{ 10.0 * std::log10(p.numerical_value_in(mp_units::si::milli<mp_units::si::watt>)) };
  }

  }  // namespace emc::units
  ```

- Same pattern for plain dB ratios and dB/m antenna factor: store linear, wrap for display. This keeps logarithmic semantics correct and keeps the linear formulas type-safe under mp-units. (The Decibel/VSWR/return-loss converter family in doc 07 uses exactly this approach.)

---

## 🔗 Cross-references

- `02-modern-cpp-feature-catalog.md` — designated initializers, `[[nodiscard]]`, C++23 `constexpr <cmath>`, concepts (the `Calculator` concept the (Input, Result, calculate) triple satisfies).
- `04-constants-and-material-database.md` — the single constexpr `c`, `pi`, `mu0`, `eps0`, and the `Material`/`Awg` tables returning typed quantities.
- `05-error-handling-and-validation.md` — `emc::Error`/`ErrorCode` used by `parse_length` and `validate`.
- `06-calculator-design-pattern.md` — the per-calculator Input/Result/`calculate` pattern these quantity types plug into; full SkinDepth and MicrostripTrace implementations.
- `07-calculator-inventory.md` — the Calculator Catalog: which `emc::units` quantity each calculator consumes/produces (incl. the dB/dBm and AntennaFactor families).
- `08-build-system-cmake.md` — `find_package`/`FetchContent` for mp-units, PUBLIC propagation, `find_dependency` in the package config.
- `09-testing-and-golden-vectors.md` — the Testing Strategy: known-value, round-trip, property, and constexpr tests for these quantities, with hand-computed expected numbers.
