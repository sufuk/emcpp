# Quantities & Units with mp-units

> Purpose: define how the `emc` library represents every physical quantity and unit conversion using the **mp-units** library, eliminating the ~541 hand-wired unit conversions, the if/else unit chains, and the scattered magic factors that currently riddle NinjaEMC — and making every conversion compile-checked and zero-cost.

This is the single most leveraged document in the plan: units touch every calculator. Get the vocabulary right here and docs 06 (calculator pattern), 07 (inventory), and 09 (golden tests) inherit correctness for free.

---

## 1. The problem: units are hand-wired, and that is a bug factory

NinjaEMC has **no type-level notion of a unit**. Every quantity is a bare `qreal` (a `double`). Units exist only as:

1. **Combobox payloads.** A unit dropdown is filled with `addItem(label, factor)` where `factor` is the multiplier to SI base, and conversion happens at read time by multiplying. From `SkinDepthWidget.cpp:19-29`:

   ```cpp
   ui->frequencyUnitBox->addItem("Hz", 1);
   ui->frequencyUnitBox->addItem("kHz", 1e3);
   ui->frequencyUnitBox->addItem("MHz", 1e6);
   ui->frequencyUnitBox->addItem("GHz", 1e9);

   ui->skinDepth_unit->addItem("m", 1);
   ui->skinDepth_unit->addItem("cm", 0.01);
   ui->skinDepth_unit->addItem("mm", 0.001);
   ui->skinDepth_unit->addItem("um", 1e-6);
   ui->skinDepth_unit->addItem("inch", 0.0254);
   ui->skinDepth_unit->addItem("mils", 0.0000254);
   ```

   The shared-context audit counts **541 such `addItem(unit, factor)` calls** across the app. Each is a place a wrong number can hide, and nothing checks that the factor matches the label.

2. **Giant if/else string→factor chains.** When a combo can't carry the payload, the factor is recomputed from the *display string*. From `StandardGaugeWireWidget.cpp:42-78`:

   ```cpp
   if (ui->frequencyUnitBox->currentText() == "Hz")        fru = 1;
   else if (ui->frequencyUnitBox->currentText() == "kHz")  fru = 1000;
   else if (ui->frequencyUnitBox->currentText() == "MHz")  fru = 1000000;
   else if (ui->frequencyUnitBox->currentText() == "GHz")  fru = 1000000000;

   if (ui->lenghtUnit_box->currentText() == "km")        lu = 1000;
   else if (ui->lenghtUnit_box->currentText() == "m")    lu = 1;
   else if (ui->lenghtUnit_box->currentText() == "cm")   lu = 0.01;
   else if (ui->lenghtUnit_box->currentText() == "mm")   lu = 0.001;
   else if (ui->lenghtUnit_box->currentText() == "mile") lu = 1609.34;
   else if (ui->lenghtUnit_box->currentText() == "foot") lu = 0.3048;
   else if (ui->lenghtUnit_box->currentText() == "inch") lu = 0.0254;
   ```

   The same `Hz/kHz/MHz/GHz` ladder is re-typed in `SkinDepthWidget`, `StandardGaugeWireWidget`, `WavelengthvsFrequency`, and dozens of others — duplication with no single source of truth.

3. **Inline magic factors welded into the math.** From `MicrostripTraceWidget.cpp:87-106`, the *only* difference between the four "solve for X" methods is whether each input is multiplied by `39.37`:

   ```cpp
   if (ui->hunitRadioMMButton->isChecked()) { H = ui->h_lineEdit->text().toDouble(); }
   else { H = ui->h_lineEdit->text().toDouble() * 39.37; }      // mm -> mils
   // ... repeated for T and W ...
   ```

   `MicrostripTraceWidget` repeats `* 39.37` and `/ 39.37` **dozens of times** across `microstrip()`, `calH()`, `calT()`, `calW()`. Each of those four methods is an **8-branch nested if-tree** (`hunit × tunit × wunit = 2³`) whose *entire* reason to exist is applying or not applying `39.37`. That is roughly 32 copies of one conversion concept.

### The bug class this causes

Hand-wired factors are wrong silently. Two real defects are visible in the source we read:

| Defect | Where | What's wrong |
|---|---|---|
| Wrong direction factor | `WavelengthvsFrequency.cpp:32` | `ui->wavelength_unit->addItem("ft", 3.281);` — `3.281` is **metres → feet** (1 m ≈ 3.281 ft). Every other entry in that combo is *unit → metre* (the factor you multiply by to get SI). So a wavelength entered in feet is multiplied by 3.281 instead of 0.3048 — off by **10.76×**. (`inch` two lines above is correct at `0.0254`.) |
| Imprecise factor | `MicrostripTraceWidget.cpp` (all four methods) | `39.37` is a truncation of the true mm→mils factor `39.3700787...`. Used as both forward and inverse factor, so round-trips drift. |
| Imprecise constant | `WavelengthvsFrequency.cpp:77,81` | uses `SPEEDOFLIGHT` = `3.0e8` (see doc 04), not `299 792 458 m/s`. |

Nothing in the type system can catch any of these. A `qreal` for a frequency and a `qreal` for a length are the same type, so the compiler will happily let you add a wavelength to a frequency, pass mils where mm is expected, or forget a conversion entirely. **mp-units makes all three classes of error a compile error.**

---

## 2. What mp-units is, and the model we use

[mp-units](https://mpusz.github.io/mp-units/) is the standardization-track C++ quantities-and-units library (the basis of the ISO C++ proposals P1935/P3045). We target **mp-units 2.x** with the **`isq` + `si` systems**. It gives us:

- **Strong dimensional analysis** — every quantity carries its *dimension* (length, time, ...) and *quantity kind* (ISO/IEC 80000 quantity, e.g. `isq::frequency`) in the type. Adding a length to a time is a compile error; the result of `length / time` is automatically a `speed`.
- **Compile-time unit checking** — a `quantity` knows its unit (`si::hertz`, `si::metre`, `si::milli<si::metre>`). Mixing units of the same dimension auto-converts; mixing dimensions fails to compile.
- **Zero runtime cost** — a `quantity<si::hertz, double>` is a single `double`. The unit and dimension live entirely in the type. There is no per-value tag, no virtual dispatch; conversions that are statically known fold to a multiply (often constant-folded away).

### The core type

```cpp
mp_units::quantity<Reference, Rep>
```

- `Reference` is a **unit-with-quantity-kind**, e.g. `si::hertz`, `isq::frequency[si::hertz]`, `si::metre`, `si::milli<si::metre>`. The bracket form `isq::frequency[si::hertz]` pins both the *kind* (frequency) and the *unit* (Hz), which is what we want for public API types so that, e.g., a frequency and an `isq::activity` (also 1/s, becquerel) never silently interconvert.
- `Rep` is the **representation type** (the stored numeric type), defaulting to `double`. We standardize on `double` (see §9).

### Constructing quantities

```cpp
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

```cpp
quantity f = 27.0 * MHz;
quantity f_hz = f.in(Hz);               // -> 27'000'000 Hz  (exact, value-preserving)
double hz_number = f.numerical_value_in(Hz);  // -> 27000000.0 (raw number, escape hatch)

quantity len = 1.5 * mm;
quantity len_in_inch = len.in(inch);    // auto conversion, compile-checked dimension

// value_cast forces a representation/unit change that might be lossy (e.g. to integer):
auto n = value_cast<int>(len.in(mm));   // explicit, opt-in narrowing
```

`.in(u)` is the everyday tool — it returns the same value expressed in `u`. There is no place to put a *wrong* factor: the factor between `mm` and `inch` is derived by the library from the unit definitions, not typed by a human. That is exactly what kills the `3.281`/`39.37` bug class.

### `quantity_point` (affine quantities)

A plain `quantity` is a *vector* quantity (differences, magnitudes). For **absolute points on an affine scale** — temperatures referenced to a zero, absolute timestamps — use `quantity_point`, which separates "30 °C" (a point) from "a 30 K rise" (a delta). NinjaEMC's quantities are overwhelmingly vector-like (frequencies, lengths, fields), so we use `quantity` almost everywhere. The one place `quantity_point` *could* matter is if a calculator ever takes an **absolute temperature** input (e.g. a future conductivity-vs-temperature feature); flag those individually. For the current 52 calculators, plain `quantity` is correct.

---

## 3. The `emc::units` project vocabulary

We do **not** scatter raw `mp_units::quantity<...>` spellings through calculator signatures. Instead `include/emc/units.hpp` defines a curated **project vocabulary** of named aliases for exactly the EMC quantities the app needs. Benefits:

- One place to read what quantities the domain uses.
- Calculator signatures stay readable (`Frequency`, not `quantity<isq::frequency[si::hertz], double>`).
- If we ever change `Rep` (double → something), it changes in one header.

```cpp
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

### Notes on the awkward ones

- **Relative permittivity / permeability are dimensionless.** εr is the *ratio* ε/ε₀ and carries dimension one. We model it as `RelativePermittivity = quantity<one>`, **not** a bare `double`, so it still flows through the type system and a designated-initializer call site reads `.relative_permittivity = 4.7 * one`. The *physical* permeability used in formulas is `μ = μr · μ0` where `μ0` is a `Permeability` constant from `emc::constants` (doc 04) — so the eight-files-redefine-`mu0` problem (`SkinDepthWidget.cpp:68` etc.) collapses to one constant.

- **Antenna factor** (used by the AntennaFactor↔Gain converter) has SI dimension **1/m** (it relates an incident E-field in V/m to a received voltage in V). We give it a named alias:

  ```cpp
  using AntennaFactor = Q<(mpu::one / isq::length)[mpu::one / si::metre]>;  // 1/m
  ```

  It is frequently *displayed* in dB (dB/m). dB is handled per §- (logarithmic wrapper), not as a linear unit.

- **dBm / dB are logarithmic — they are NOT mp-units linear units.** See the dedicated subsection in §9. We keep power in linear watts internally (`Power`) and provide a typed `Decibel`/`Dbm` wrapper for the boundary.

- **VSWR, optical coverage, gain ratio** are dimensionless ratios → `Dimensionless`.

---

## 4. Inputs as quantities

Per the canonical calculator pattern (doc 06), each calculator has an **aggregate input struct** whose members are `emc::units` quantities with sensible defaults. Units are therefore part of the API and conversions are automatic.

```cpp
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

```cpp
using namespace mp_units::si::unit_symbols;   // MHz, etc.
using mp_units::one;

auto r = emc::basic::calculate(emc::basic::SkinDepthInput{
    .frequency             = 27.0 * MHz,        // could equally be 27e6 * Hz
    .conductivity          = 1.4493e7 * (S / m),// Nickel; from emc::materials, doc 04
    .relative_permeability = 600.0 * one,
});
```

If a caller writes `.frequency = 27.0 * mm` (a length), it **does not compile** — the dimension is wrong. If they write `27.0 * kHz`, it compiles and is converted to the formula's working unit automatically. There is no `* 1e6` and no combobox factor anywhere in this code path.

Designated initializers + member defaults (a C++20 feature we lean on hard, see doc 02) mean a caller only specifies what they care about; `relative_permeability` defaults to 1 for non-magnetic materials.

---

## 5. Worked transformation: the unit trees disappear

### 5a. SkinDepth — before → after

**Before** (`SkinDepthWidget.cpp:64-72`): factor pulled from combobox payload, `mu0` a file-local magic constant, result divided by an output-combobox factor.

```cpp
qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
qreal conductivity = ui->conductivity_spinbox->value();
qreal relativePermeability = ui->ur->value() * mu0;        // mu0 redefined in 8+ files
qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
ui->skinDepth->setValue(skinDepth / ui->skinDepth_unit->currentData().toReal());
```

**After** — the entire formula is unit-correct by construction; no factors:

```cpp
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
    // The product f*mu*sigma carries units (Hz * H/m * S/m); 1/that is m^2;
    // sqrt -> metres. mp-units tracks this; we extract a unit-correct number to call std::sqrt.
    const auto under = constants::pi * in.frequency * mu * in.conductivity;  // -> 1/m^2
    const quantity<si::metre, double> delta =
        sqrt(1.0 * one / under);   // mp-units provides sqrt for quantities (C++23 constexpr <cmath>)

    return SkinDepthResult{ .skin_depth = delta };
}

}  // namespace emc::basic
```

`mp_units::sqrt` returns a quantity whose unit is the square-root of the argument's unit — so `sqrt(m²) = m` is enforced, not assumed. The output is always SI metres; *display* unit selection (cm, mils, ...) is the UI's job (§6), not the core's.

### 5b. MicrostripTrace — the 8-branch `× 39.37` tree evaporates

**Before** (`MicrostripTraceWidget.cpp:81-138`, plus `calH/calT/calW`): four methods, each an 8-branch nested if where the only variable is whether each of H/T/W gets `* 39.37`. ~32 conversion copies + inline validation.

**After** — inputs are `Length` quantities; the formula works in one consistent unit by calling `.in(...)` *once*. The mm-vs-mils choice no longer exists in the math at all:

```cpp
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
    // So we pick ONE working unit and express all three lengths in it -- ONE line each,
    // replacing the entire 2x2x2 if-tree. mp-units guarantees consistency.
    const double H = in.h.numerical_value_in(mm);
    const double T = in.t.numerical_value_in(mm);
    const double W = in.w.numerical_value_in(mm);
    const double eps = in.relative_permittivity.numerical_value_in(one);

    const double Z = 87.0 * std::log(5.98 * H / (0.8 * W + T)) / std::sqrt(eps + 1.41);
    const double C = 0.67 * (eps + 1.41) / std::log(5.98 * H / (0.8 * W + T)); // pF/cm in the legacy model
    const double Tpd = C * Z;                                                  // ps/cm in the legacy model

    return MicrostripTraceResult{
        .z0                = Z * si::ohm,
        .c0                = (C * si::pico<si::farad>) / si::centi<si::metre>,  // typed, no guessing
        .propagation_delay = (Tpd * si::pico<si::second>) / si::centi<si::metre>,
    };
}
```

Two structural wins:

1. **The 8-branch tree is gone.** Because the formula is ratio-based, we choose one working unit (mm) and call `numerical_value_in(mm)` once per length. Whether the *caller* supplied mm or mils is irrelevant — mp-units converts. The `× 39.37` literal never appears.
2. **Bidirectional solving stays DRY.** The inverse solves (H/T/W from Z0) become three more small functions that likewise take/return quantities; none of them re-implements unit handling. (The exact inverse signatures live in doc 06/07.)

> Note on re-blessing: the legacy constants `5.98`, `0.67`, `87`, `1.41` bake in cm/pF assumptions and the old `39.37`. Because we now convert exactly, some golden CSV outputs will shift in the last digits and must be re-blessed (doc 09).

---

## 6. The UI boundary: parse in, format out

The library speaks **quantities**. The Qt app still shows unit dropdowns. Conversion between *(value, unit-string)* and `quantity` lives in a **thin adapter at the boundary** — never in the core. This keeps the 541 combobox factors out of the domain code while preserving the UI's freedom to display any unit.

```cpp
// app-side adapter (lives in the Qt consumer, NOT in libemc)
#include <mp-units/systems/si.h>
#include <string_view>
#include <expected>

namespace ninja::ui {

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// PARSE: (number from spinbox, unit label from combobox) -> Length quantity
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
    if (unit == "mile") return value * (international::mile);
    return std::unexpected(emc::Error{emc::ErrorCode::UnknownUnit, std::string{unit}});
}

// FORMAT: Length quantity -> number to push into the spinbox, in the chosen display unit
double format_length(emc::units::Length q, std::string_view unit) {
    if (unit == "mm")   return q.numerical_value_in(mm);
    if (unit == "cm")   return q.numerical_value_in(si::centi<si::metre>);
    if (unit == "inch") return q.numerical_value_in(inch);
    if (unit == "mils") return q.numerical_value_in(non_si::thou);
    // ... etc; one switch, one source of truth, factors supplied BY mp-units
    return q.numerical_value_in(m);
}

}  // namespace ninja::ui
```

Crucially:

- **The factor numbers are gone.** `parse_length`/`format_length` map a *string* to an mp-units *unit*; the numeric factor is computed by mp-units, so the `3.281` ft bug literally cannot recur — `international::foot` is exact.
- This adapter is the *only* place strings touch units. It is ~50 lines total for the whole app (one parse/format pair per dimension: length, frequency, time, etc.), replacing 541 scattered `addItem` factors.
- For richer round-trips you can use mp-units text I/O (`std::format`/`std::print` with `{}` on a quantity prints `1.5 mm`), but for driving Qt spinboxes we want a raw number, so we use `numerical_value_in`.

---

## 7. Complete unit mapping table

Every unit string that appears in NinjaEMC dropdowns/chains, mapped to its mp-units unit. (Strings gathered from the four representative widgets and the shared audit.)

### Frequency

| App string | mp-units unit | Note |
|---|---|---|
| `Hz`  | `si::hertz` | base |
| `kHz` | `si::kilo<si::hertz>` | replaces `1e3` |
| `MHz` | `si::mega<si::hertz>` | replaces `1e6` |
| `GHz` | `si::giga<si::hertz>` | replaces `1e9` |

### Length

| App string | mp-units unit | Note |
|---|---|---|
| `m`    | `si::metre` | base |
| `cm`   | `si::centi<si::metre>` | replaces `0.01` |
| `mm`   | `si::milli<si::metre>` | replaces `0.001` / `1e-3` |
| `um`   | `si::micro<si::metre>` | replaces `1e-6` |
| `nm`   | `si::nano<si::metre>` | replaces `1e-9` |
| `km`   | `si::kilo<si::metre>` | replaces `1e3` |
| `inch` / `in` | `international::inch` (== `0.0254 m` exactly) | replaces `0.0254` |
| `mils` | `international::thou` (1 mil = 1/1000 inch = `2.54e-5 m` exactly) | replaces `0.0000254`; **and the `× 39.37` factor in MicrostripTrace** |
| `foot` / `ft` | `international::foot` (== `0.3048 m` exactly) | **fixes the `3.281` bug** at `WavelengthvsFrequency.cpp:32` |
| `mile` | `international::mile` (== `1609.344 m` exactly) | replaces `1609.34` (also imprecise in legacy) |

> mp-units provides imperial units in its `international`/`usc` systems (`<mp-units/systems/international.h>`). If a given build doesn't ship `thou`, define it locally as `inline constexpr struct thou final : named_unit<"thou", mag_ratio<1,1000> * international::inch> {} thou;` — still a single exact definition, not a typed factor.

### Capacitance-per-length / time-per-length (Microstrip outputs)

| App string | mp-units expression |
|---|---|
| `pF/cm`  | `si::pico<si::farad> / si::centi<si::metre>` |
| `pF/inch`| `si::pico<si::farad> / international::inch` |
| `psec/cm`| `si::pico<si::second> / si::centi<si::metre>` |
| `psec/inch`| `si::pico<si::second> / international::inch` |

### NOT units — keep as enum/int (cross-ref doc 04)

| App string(s) | Why it is not a unit | How we model it |
|---|---|---|
| AWG gauge: `OOOO`,`OOO`,`OO`,`O`,`4`,`8`,…  (`StandardGaugeWireWidget.cpp:67-77`) | A wire *gauge index*, not a physical unit. The legacy code maps `OOOO→-3 … O→0`, then computes a diameter via `dm = .0254*.005*pow(92,(36-g)/39)`. | An `enum class Awg` (or plain `int` gauge) → a `Length` *diameter* via a pure function. Stays an index; see doc 04. |
| Material names: `Copper`,`Nickel`,… | Selectors into a material table, not units. | `enum class Material` → `emc::materials` lookup returning typed `Conductivity`/`Permeability`. See doc 04. |
| `dBm`, `dB`, `dB/m` | **Logarithmic**, not linear units (see §9). | Typed `Decibel`/`Dbm` wrapper; convert to/from linear `Power`/`AntennaFactor` at the boundary. |

---

## 8. Build/dependency integration (teaser → doc 08)

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

## 9. Trade-offs and pitfalls

### Representation type: `double`

We fix `Rep = double` library-wide (one alias in `units.hpp`, §3). Rationale:

- Matches the legacy `qreal` (= `double`) exactly → minimal behaviour change while re-blessing goldens.
- `long double` buys little for EMC engineering tolerances and is slow/inconsistent across platforms (it's 64-bit on MSVC anyway). Not worth it.
- Integer reps are tempting for exactness but break the moment a formula needs `sqrt`/`log`; we keep `double` and use `value_cast<int>` only at explicit display points.

### Compile time

mp-units is template-heavy; expect a measurable hit to incremental builds, *especially* in headers that instantiate many quantity types. Mitigations:

- **Compiled library, not header-only** (a locked decision): the heavy instantiations live in `src/*.cpp`, compiled once. Consumers including `emc/units.hpp` pay only for the alias declarations they touch.
- Keep `units.hpp` lean (aliases only; no algorithms).
- Use precompiled headers for `<mp-units/...>` in the library's own build (doc 08).

### Template error verbosity

A dimension mismatch produces a long template error. This is the cost of moving the bug to compile time (vs. a silent runtime `× 39.37`). Mitigations:

- mp-units 2.x has deliberately improved diagnostics; the first error line usually names the offending quantity kinds.
- The `emc::units` aliases make messages mention `emc::units::Frequency` rather than a raw `quantity<...>` blob.
- Encourage `.in(unit)` at call boundaries so mismatches surface at the call site, not deep in a formula.

### The dependency itself

Adding mp-units is a real third-party dependency on a pre-1.0-of-the-standard library (its API tracks evolving proposals and can change between 2.x minors). Mitigation: **pin an exact tag** (§8), and isolate every mp-units spelling behind `emc::units` so a future API shift is a one-header edit.

### Pitfall: relative permittivity/permeability are dimensionless — not bare doubles

εr and μr have dimension **one**. Modeling them as `double` would re-open the "is this a ratio or a physical value?" ambiguity. We model them as `quantity<one>` (`RelativePermittivity`, `RelativePermeability`). The *physical* permittivity/permeability used in formulas is `εr · ε0` / `μr · μ0`, where `ε0`/`μ0` are the single constexpr constants in `emc::constants` (doc 04) — replacing the `mu0` redefinition in 8+ legacy files.

### Pitfall: dB and dBm are logarithmic — do **not** make them mp-units linear units

A decibel is `10·log₁₀(ratio)`; dBm is dB relative to 1 mW. These are **not** linear scalings of a unit and must never be a `quantity` unit (you'd get nonsense from adding/scaling them). Strategy:

- Keep the physical quantity **linear** internally: power as `emc::units::Power` (watts), antenna factor as `AntennaFactor` (1/m).
- Provide a small **typed logarithmic wrapper** for the boundary:

  ```cpp
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

## Cross-references

- `02-modern-cpp-feature-catalog.md` — designated initializers, `[[nodiscard]]`, C++23 `constexpr <cmath>`, concepts (the `Calculator` concept the (Input, Result, calculate) triple satisfies).
- `04-constants-and-material-database.md` — the single constexpr `c`, `pi`, `mu0`, `eps0`, and the `Material`/`Awg` tables returning typed quantities (resolves the duplicated-constants and material-table pain).
- `05-error-handling-and-validation.md` — `emc::Error`/`ErrorCode` used by `parse_length` and `validate`, replacing `QMessageBox`/`EXIT_FAILURE`.
- `06-calculator-design-pattern.md` — the per-calculator Input/Result/`calculate` pattern these quantity types plug into; full SkinDepth and MicrostripTrace conversions.
- `07-calculator-inventory.md` — which `emc::units` quantity each of the ~52 calculators consumes/produces (incl. the dB/dBm and AntennaFactor families).
- `08-build-system-cmake.md` — `find_package`/`FetchContent` for mp-units, PUBLIC propagation, `find_dependency` in the package config.
- `09-testing-and-golden-vectors.md` — re-blessing goldens that shift because conversions are now exact (the `39.37`/`3.281` fixes).
