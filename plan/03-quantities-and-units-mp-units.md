# 📐 Quantities & Units with mp-units

> Purpose: define how `emc` represents every physical quantity and unit conversion using **mp-units**, so every conversion is compile-checked, exact, and zero-cost.

Units touch every calculator, so this doc is the most leveraged in the plan. Get the vocabulary right here and the calculator pattern, catalog, and testing docs inherit correctness for free.

> [!IMPORTANT]
> EMC inputs span **Hz to GHz** and **metres to mils**, and a single formula routinely mixes frequency, length, conductivity, and permeability. Encoding the unit *and* quantity kind in the **type** — not as a runtime multiplier — is what makes the library type-safe. Everything below follows from that one rule.

---

## 1. 🎯 Why units belong in the type system

A skin-depth call mixes frequency (Hz–GHz), conductivity (S/m), and a dimensionless permeability; a microstrip call mixes three lengths (mm *or* mils) with εr. Model these as bare `double` and the compiler knows nothing: it lets you add a wavelength to a frequency, pass mils where mm is expected, or hard-code a wrong/truncated factor (`1e6`, `0.0254`, `39.37`) with nothing to catch it. The same `Hz/kHz/MHz/GHz` ladder gets re-derived in every calculator.

mp-units makes the unit and dimension part of the type, turning three mistake classes into **compile errors**:

| Error class | Example | With `double` | With mp-units |
|---|---|---|---|
| **Dimension mismatch** | length + frequency | compiles, garbage | ❌ compile error |
| **Unit mix-up** | mils where mm expected | off by ~25.4× | ✅ auto-converted, exact |
| **Wrong/imprecise factor** | hand-typed `3.281`/`39.37` | silent error | ✅ library-derived, exact |

> [!IMPORTANT]
> A factor a human never types can never be wrong. mp-units derives every factor from the unit *definitions*, so direction and precision are guaranteed by construction — the core reason we use it.

---

## 2. 🧩 The mp-units model we use

[mp-units](https://mpusz.github.io/mp-units/) is the standardization-track C++ quantities library (basis of ISO proposals P1935/P3045). We target **mp-units 2.x** with the **`isq` + `si`** systems. It gives us:

- **Strong dimensional analysis** — every quantity carries its *dimension* (length, time, …) and *quantity kind* (ISO/IEC 80000, e.g. `isq::frequency`) in the type. `length / time` is automatically a `speed`.
- **Compile-time unit checking** — same-dimension units auto-convert; cross-dimension fails to compile.
- **Zero runtime cost** — a `quantity<si::hertz, double>` is a single `double`. No tag, no dispatch; static conversions fold to a multiply.

The core type is `mp_units::quantity<Reference, Rep>`:

- **`Reference`** is a unit-with-quantity-kind. The bracket form `isq::frequency[si::hertz]` pins both *kind* (frequency) and *unit* (Hz) — used in public API types so frequency and `isq::activity` (also 1/s) never silently interconvert.
- **`Rep`** is the stored numeric type, defaulting to and standardized on `double` (§9).

Everyday conversion uses two members:

- **`.in(unit)`** — returns the same value expressed in `unit`, value-preserving and dimension-checked. The everyday tool; there is no place to put a wrong factor.
- **`.numerical_value_in(unit)`** — raw `double` escape hatch, only when handing a number to `std::sqrt`/`std::log`. `value_cast<int>(…)` is the explicit, opt-in narrowing.

```cpp
quantity f = 27.0 * MHz;
quantity f_hz = f.in(Hz);                 // 27'000'000 Hz, exact
double n = f.numerical_value_in(Hz);      // 27000000.0, escape hatch
```

> [!TIP]
> Prefer `.in(unit)` at call boundaries; keep the typed `quantity` as long as possible so the compiler keeps checking dimensions.

**`quantity_point` (affine).** A plain `quantity` is a *vector* quantity (deltas, magnitudes). Absolute points on an affine scale (absolute temperature, timestamps) need `quantity_point`, which distinguishes "30 °C" (point) from "a 30 K rise" (delta). EMC inputs are overwhelmingly vector-like, so we use plain `quantity` everywhere. Flag any future **absolute-temperature** input individually.

---

## 3. 📚 The `emc::units` vocabulary

`include/emc/units.hpp` defines a curated **project vocabulary** of named aliases — we do not scatter raw `mp_units::quantity<...>` spellings through signatures. Benefits: one place to read the domain's quantities; readable signatures (`Frequency`, not `quantity<isq::frequency[si::hertz], double>`); and `Rep` changes in a single header.

The header pins **both** the ISQ quantity kind and the SI unit via a `template <auto Reference> using Q = mpu::quantity<Reference, Rep>` helper. The vocabulary:

| Alias | Reference | Unit |
|---|---|---|
| `Frequency` | `isq::frequency[si::hertz]` | Hz |
| `Length` | `isq::length[si::metre]` | m |
| `Time` | `isq::time[si::second]` | s |
| `Velocity` | `isq::speed[m/s]` | m/s (c) |
| `Voltage` | `isq::voltage[si::volt]` | V |
| `Current` | `isq::current[si::ampere]` | A |
| `Power` | `isq::power[si::watt]` | W |
| `Impedance` / `Resistance` | `isq::resistance[si::ohm]` | Ω (Z0, R, RL) |
| `Capacitance` | `isq::capacitance[si::farad]` | F |
| `Inductance` | `isq::inductance[si::henry]` | H |
| `Conductivity` | `isq::electrical_conductivity[S/m]` | S/m |
| `Resistivity` | `isq::resistivity[Ω·m]` | Ω·m |
| `Permeability` | `isq::permeability[H/m]` | H/m (μ = μr·μ0) |
| `Permittivity` | `isq::permittivity[F/m]` | F/m (ε = εr·ε0) |
| `ElectricFieldStrength` | `isq::electric_field_strength[V/m]` | V/m |
| `MagneticFieldStrength` | `isq::magnetic_field_strength[A/m]` | A/m |
| `PowerDensity` | `(power/area)[W/m²]` | W/m² |
| `ResistancePerLength` | `(resistance/length)[Ω/m]` | Ω/m |
| `CapacitancePerLength` | `(capacitance/length)[F/m]` | F/m |
| `TimePerLength` | `(time/length)[s/m]` | s/m (Tpd) |
| `Dimensionless` | `mpu::one` | 1 |
| `RelativePermittivity` / `RelativePermeability` | `mpu::one` | 1 (εr, μr) |
| `Vswr` / `Ratio` | `mpu::one` | 1 (≥1; gains, coverage) |

### Notes on the subtle ones

- **Relative permittivity / permeability are dimensionless.** εr is the *ratio* ε/ε₀, dimension one — modelled as `quantity<one>`, **not** a bare `double`, so it still flows through the type system (`.relative_permittivity = 4.7 * one`). The *physical* `μ = μr · μ0` uses the single `mu0` constexpr constant from `emc::constants` (doc 04).
- **Antenna factor** has SI dimension **1/m** (relates an incident V/m field to a received V): `using AntennaFactor = Q<(mpu::one / isq::length)[mpu::one / si::metre]>`. Often *displayed* in dB/m, handled as a log wrapper (§9), not a linear unit.
- **dBm / dB are logarithmic — NOT mp-units linear units** (§9). Keep power linear (`Power`, watts) with a typed wrapper at the boundary.
- **VSWR, optical coverage, gain ratio** are dimensionless → `Dimensionless`.

---

## 4. 🔌 Inputs as quantities

Per the calculator pattern (doc 06), each calculator takes an **aggregate input struct** whose members are `emc::units` quantities with sensible defaults — so units are part of the API and conversions are automatic.

```cpp
struct SkinDepthInput {
    emc::units::Frequency            frequency;
    emc::units::Conductivity         conductivity;
    emc::units::RelativePermeability relative_permeability = 1.0 * mp_units::one;
};
```

Call sites are explicit and compiler-verified. `.frequency = 27.0 * mm` (a length) **does not compile**; `27.0 * kHz` compiles and auto-converts to the formula's working unit. No `* 1e6` and no runtime factor appears. Designated initializers plus member defaults mean a caller specifies only what they care about (`relative_permeability` defaults to 1 for non-magnetic materials).

---

## 5. 🧮 Formulas stay unit-clean

Textbook EMC formulas are implemented directly against the typed vocabulary — the units *are* the implementation, with no separate "conversion layer". Two patterns recur (full implementations in docs 06/07):

- **Dimension-tracked through the math** (e.g. skin depth `δ = sqrt(1/(π·f·μ·σ))`). The product `f·μ·σ` carries units `1/m²`; `mp_units::sqrt` returns a quantity whose unit is the square-root of the argument's, so `sqrt(m²) = m` is *enforced*, not assumed. The result is stored in SI metres; choosing a display unit is the boundary's job (§6).
- **Unit-relative formulas** (e.g. Wheeler microstrip, which depends only on ratios `H/W`, `T/W`). Pick one working unit and call `numerical_value_in(mm)` once per length; mp-units supplies the exact factor regardless of what the caller passed. No per-unit branching, and inverse solves reuse the same typed signatures.

> [!CAUTION]
> Model constants like microstrip's `5.98`, `0.67`, `87`, `1.41` bake in cm/pF/ps assumptions, so the output units (`pF/cm`, `ps/cm`) are part of the formula's identity. Attach those output units explicitly — never let a result land in raw SI and assume the magnitude matches.

---

## 6. 🪟 The presentation boundary: parse in, format out

The library speaks **quantities**. A front end shows unit dropdowns; conversion between *(value, unit-string)* and `quantity` lives in a **thin adapter at the boundary**, never in the core.

```cpp
// UI-layer adapter, NOT in libemc
std::expected<emc::units::Length, emc::Error>
parse_length(double value, std::string_view unit) {
    if (unit == "mm")   return value * mm;
    if (unit == "inch") return value * inch;
    if (unit == "mils") return value * non_si::thou;   // exact by definition
    // ...
    return std::unexpected(emc::Error{emc::ErrorCode::UnknownUnit, std::string{unit}});
}
```

Properties:

- **No factor numbers appear** — the adapter maps a *string* to an mp-units *unit*; mp-units computes the factor, so wrong-direction/truncated factors cannot occur.
- It is the **only** place strings touch units — a parse/format pair per dimension (length, frequency, time), keeping the whole string↔unit surface small and centralized. (`std::format`/`std::print` on a quantity prints `1.5 mm`; for numeric fields use `numerical_value_in`.)

> [!TIP]
> Return `std::expected<Length, Error>` from `parse_length` so an unrecognized unit is an explicit `ErrorCode::UnknownUnit`, never a silently guessed default.

---

## 7. 📋 Accepted units → canonical SI storage

Every quantity is stored in canonical SI; these are the accepted *input/display* spellings and the exact mp-units unit each maps to.

### Frequency

| Input | mp-units unit | SI |
|---|---|---|
| `Hz` / `kHz` / `MHz` / `GHz` | `si::hertz`, `si::kilo<…>`, `si::mega<…>`, `si::giga<…>` | Hz |

### Length

| Input | mp-units unit | Note |
|---|---|---|
| `m` `cm` `mm` `um` `nm` `km` | `si::metre` with SI prefixes | m base |
| `inch` / `in` | `international::inch` | `= 0.0254 m` exactly ✅ |
| `mils` | `international::thou` | `= 2.54e-5 m` exactly ✅ |
| `foot` / `ft` | `international::foot` | `= 0.3048 m` exactly ✅ |
| `mile` / `mi` | `international::mile` | `= 1609.344 m` exactly ✅ |

> [!NOTE]
> Imperial units live in mp-units' `international`/`usc` systems (`<mp-units/systems/international.h>`). If a build lacks `thou`, define it once: `inline constexpr struct thou final : named_unit<"thou", mag_ratio<1,1000> * international::inch> {} thou;` — still a single exact definition.

### Microstrip outputs

| Input | mp-units expression |
|---|---|
| `pF/cm` / `pF/inch` | `si::pico<si::farad> / (si::centi<si::metre>` or `international::inch)` |
| `psec/cm` / `psec/inch` | `si::pico<si::second> / (si::centi<si::metre>` or `international::inch)` |

### NOT units — keep as enum/int (doc 04)

| Input | Why not a unit | How we model it |
|---|---|---|
| AWG gauge (`OOOO`…`4`,`8`) | gauge *index*, via `dm = 0.0254·0.005·pow(92,(36−g)/39)` | `enum class Awg` → `Length` diameter (doc 04) |
| Material names (`Copper`, `Nickel`) | table selectors | `enum class Material` → typed `Conductivity`/`Permeability` (doc 04) |
| `dBm` / `dB` / `dB/m` | **logarithmic** (§9) | typed `Decibel`/`Dbm` wrapper; convert at boundary |

---

## 8. 🛠️ Build integration (→ doc 08)

mp-units is declared once via `find_package(mp-units CONFIG REQUIRED)` (preferred) or `FetchContent` with an exact pinned tag, then linked **`PUBLIC`** because `emc::units` types appear in public headers:

```cmake
target_link_libraries(emc PUBLIC mp-units::mp-units)
```

The exported `emcConfig.cmake` must `find_dependency(mp-units)`. Full install/export wiring and version pinning are in **doc 08**.

---

## 9. ⚖️ Trade-offs and pitfalls

**Representation = `double`** (one alias in `units.hpp`). Natural for EMC tolerances and consistent across platforms. `long double` buys little and is inconsistent (64-bit on MSVC); integer reps break on the first `sqrt`/`log`. Use `value_cast<int>` only at display points.

**Compile time.** mp-units is template-heavy. Mitigations: ship a **compiled library** (heavy instantiations in `src/*.cpp`, compiled once); keep `units.hpp` lean (aliases only); use precompiled headers for `<mp-units/...>` (doc 08).

**Error verbosity.** Dimension mismatches produce long template errors — the cost of moving the bug to compile time. mp-units 2.x improves diagnostics, the `emc::units` aliases make messages name `emc::units::Frequency` rather than a raw blob, and `.in(unit)` at boundaries surfaces mismatches at the call site.

**The dependency.** mp-units tracks evolving proposals and can change between 2.x minors. Mitigation: **pin an exact tag** and isolate every mp-units spelling behind `emc::units` so an API shift is a one-header edit.

**Pitfall: εr/μr are dimensionless, not bare doubles.** Modelled as `quantity<one>` so the "ratio or physical value?" ambiguity never reopens. Physical ε/μ is `εr·ε0` / `μr·μ0` using the single constexpr constants in `emc::constants` (doc 04).

### Pitfall: dB and dBm are logarithmic

> [!WARNING]
> A decibel is `10·log₁₀(ratio)`; dBm is dB relative to 1 mW. These are **not** linear scalings, so they must never be mp-units `quantity` units — `3 dB + 3 dB` is *not* `6 dB` of power, and scaling a dB value is meaningless.

Strategy: keep the physical quantity **linear** internally (power as `Power` in watts, antenna factor as `AntennaFactor` in 1/m), and provide a small **typed log wrapper** for the boundary with explicit conversion functions (never implicit arithmetic):

```cpp
struct Dbm { double value; };                          // NOT an mp-units unit
constexpr Power to_power(Dbm x) noexcept;               // dBm -> W
inline   Dbm   to_dbm(Power p) noexcept;                // W -> dBm
```

The same pattern covers plain dB ratios and dB/m: store linear, wrap for display. The Decibel/VSWR/return-loss converter family (doc 07) uses exactly this approach.

---

## 🔗 Cross-references

Doc 04 (constants `c`/`pi`/`mu0`/`eps0`, `Material`/`Awg` tables) · doc 06 (calculator pattern; full SkinDepth/Microstrip implementations) · doc 07 (catalog; dB/dBm/AntennaFactor families) · doc 08 (CMake/mp-units propagation) · doc 09 (golden-vector and round-trip tests).
