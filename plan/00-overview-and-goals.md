# Overview & Goals

> The front door to the **emc** library plan: why we are extracting a Qt-free, type-safe modern-C++ EMC calculation library out of the NinjaEMC app, what "done" looks like, and how to read the rest of this plan.

---

## 1. Purpose / Executive Summary

**NinjaEMC** (project name `emc-prediction`) is an existing Qt desktop/mobile application that performs electromagnetic-compatibility (EMC) engineering calculations: skin depth, antenna factor, decibel conversions, microstrip impedance, cavity resonance, shielding effectiveness, cable braid coverage, crosstalk, and roughly 52 calculators in total, organized into 9 categories. It is a genuinely useful body of EMC domain math. The problem is *where* that math lives: every formula is welded directly into a Qt widget. Each calculation sits inside a `button-clicked` lambda in a widget constructor, reading values straight out of `ui->spinbox->value()` and writing answers back to `ui->result->setValue()`. There is no separable core. You cannot call "compute skin depth" without instantiating a `QWidget`, a `QStackedWidget` parent, and the entire Qt event machinery.

**emc** (living in `/Users/sufuk/CLionProjects/emcpp`) is the clean library we will extract from that app. It will be a traditional compiled C++23 library — public headers in `include/emc/`, compiled translation units in `src/`, built into a static and/or shared library through CMake with proper `install()`/`export()` and a package config so any downstream project can `find_package(emc)` and link `emc::emc`. It depends on **mp-units** (the ISO/IEC 80000 standardization-track quantities-and-units library) for all physical quantities, and on nothing from Qt. Every calculator becomes a pure free function over an aggregate input struct, returning `std::expected<Result, emc::Error>`. The physics is preserved; only the packaging, the type safety, and the correctness of the constants change.

This plan does **not** rewrite the Qt app. The app stays. After the library exists, the app will be re-wired in a later phase to become a *thin consumer*: its widgets keep doing what widgets do (read fields, render results) and delegate every formula to `emc::...::calculate(...)`. The same EMC math then becomes reusable from a CLI, a test harness, another GUI, a server, or a notebook — because it no longer requires a UI to run. This document establishes the vision, the goals, the non-goals, the guiding principles, and the success criteria for that extraction. The detailed *how* lives in documents 01–10 (see §6).

---

## 2. Why Convert Now — Concrete Problems as Costs

Each item below is a measurable defect or liability in the current codebase. They are framed as costs because they are the justification for the rewrite: every one of them is eliminated or contained by the design in this plan.

### 2.1 A real precision **bug**: `#define PI 3.14`

`src/Utilites/HelperTypes.h` contains:

```cpp
#define PI 3.14            // wrong to 2 decimal places
#define SPEEDOFLIGHT 300000000.0
#define PLANCK_CONSTANT 6.62606957e-34
```

`PI` as `3.14` is not "imprecise" — it is **wrong in the third significant figure** (true π ≈ 3.14159265). Any formula that resolves to this macro carries ~0.05% error before any physics is even applied. This is a correctness bug hiding in a header, and because it is a macro it can silently shadow or collide with other definitions.

- **Cost:** wrong answers in an engineering tool, presented with full apparent confidence; impossible to unit-test in isolation because the math is in widgets.

### 2.2 Imprecise speed of light: `c = 3.0e8`

`SPEEDOFLIGHT 300000000.0` is the rounded textbook value. The defined SI value is **c = 299 792 458 m/s** exactly. The rounding introduces ~0.07% systematic error into every wavelength/frequency conversion, antenna calculation, and field-vs-power-density conversion that uses it.

- **Cost:** systematic bias across an entire category (Converter) and parts of others, again untestable in place.

### 2.3 `mu0` redefined in **8+ files**

`qreal mu0 = 4 * M_PI * 1e-7;` is independently re-declared in at least 8 files — `SkinDepthWidget.h`, `FerriteToroidWidget.h`, `ESDCouplingLevelWidget.h`, `LightningCouplingLevelWidget.h`, `WideTraceOverPlaneWidget.cpp`, and more. Separately, `#define PermofFreeSpace ((4 * M_PI) / 10000000.0)` is copy-pasted across **6** Inductance headers. So the *same* physical constant exists under two names, two spellings, and a dozen definition sites — some using `M_PI`, some using the buggy `PI`.

- **Cost:** there is no single source of truth. Fixing or refining one constant requires hunting down every copy; copies drift; a partial fix produces *inconsistent* results between calculators that should agree.

### 2.4 **541** hand-wired unit conversions

Unit handling is done by populating combo boxes with `(label, factor)` pairs — there are **541** calls of the form `comboBox->addItem(unit, factor)`. On top of that there are giant `if/else` chains mapping unit *strings* to numeric factors (e.g. `StandardGaugeWireWidget.cpp` ~lines 43–77), and scattered magic multipliers like `*39.37` (metres→inches) repeated dozens of times in `MicrostripTraceWidget.cpp`.

- **Cost:** units live as bare `double` factors with no type checking. A wrong factor, a transposed conversion, or a metres-where-millimetres-expected bug compiles cleanly and ships. Bidirectional solvers (`MicrostripTraceWidget` has 4 near-identical methods, each with an ~8-branch nested `if` tree *just to apply unit conversions*) multiply this surface area.

### 2.5 Duplicated, **inconsistent** material data across ~14 files

Material electrical properties (conductivity, permeability, resistivity) are hardcoded independently in roughly 14 files, with *inconsistent representations*: `SkinDepthWidget.cpp` uses inline `if (material == "Copper") { conductivity = 5.8005E+7; ur = 0.999991; }` string comparisons; `StandardGaugeWireWidget.cpp` uses an `enum Material` plus `GetResistivity()` with *different* numeric values; `CylindricalConductorWidget`, `RectangularConductorWidget`, `WireOverPlaneWidget` each repeat their own tables again.

- **Cost:** "Copper" is not one thing in this codebase — it is ~14 slightly different things. Cross-calculator results are not guaranteed to be self-consistent, and adding or correcting a material means editing a dozen files.

### 2.6 Validation (`QMessageBox`) **mixed into the math** in 10 files

Range checks are interleaved with the formulas via `QMessageBox::warning(...)` in ~10 files. For example `MicrostripTraceWidget.cpp` checks permittivity ∈ [1,15] and W/H ∈ [0.1,3] and pops a modal dialog *mid-formula*. Worse, `StandardGaugeWireWidget.cpp` returns `EXIT_FAILURE` from `GetResistivity()` as an error sentinel — a process-exit code used as an in-band "bad material" signal.

- **Cost:** validation cannot be reused or tested without a GUI event loop; error reporting is a side effect (a dialog) rather than a value; the `EXIT_FAILURE` sentinel can flow into arithmetic and silently corrupt a result.

### 2.7 Tests are **UI-coupled**

Golden-file tests exist (52 CSV fixtures under `resources/data/`), but they only run under `#ifdef TEST_MODE` by *programmatically driving the widgets* — `ui->solveButton->clicked()` — then scraping `ui->result->text()`. Running the regression suite therefore requires constructing widgets and firing Qt signals.

- **Cost:** tests need the whole UI stack; they are slow, fragile, and untestable headless/CI-friendly. The valuable part — the input→output vectors — is trapped behind the UI driver.

> **These 52 CSVs are gold.** The data is reusable; the harness is not. This plan keeps the vectors and throws away the driver (see [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md)). Because of bugs like §2.1/§2.2, some golden outputs are *wrong* and must be **re-blessed** against corrected math — every such change will be flagged explicitly.

---

## 3. Goals and Non-Goals

### 3.1 Goals (numbered, testable)

1. **G1 — Qt-free core.** The `emc` library compiles and links with **zero** Qt headers and zero Qt link dependencies. *Test:* `grep -r "Qt\|QString\|QWidget" include/ src/` returns nothing; the library builds with Qt entirely absent from the toolchain.
2. **G2 — One source of truth for constants.** π, c, μ₀, ε₀, h, etc. are defined exactly once, as `constexpr` mp-units quantities in `emc::constants`. *Test:* every constant has exactly one definition site; `PI`, `SPEEDOFLIGHT`, `mu0`, `PermofFreeSpace` macros/locals do not exist anywhere.
3. **G3 — Correct constants.** π is the full-precision library value, c = 299 792 458 m/s exactly, μ₀/ε₀ are CODATA-consistent. *Test:* constant values match reference to full `double` precision in a `static_assert`/unit test.
4. **G4 — Type-safe quantities.** Every physical input/output is an mp-units `quantity` with a unit baked into the type; conversions are compile-checked. *Test:* mixing incompatible units fails to compile; the 541 hand-wired factors are gone.
5. **G5 — One material database.** All material properties live once in `emc::materials` as `constexpr` data. *Test:* "Copper" has a single definition; all calculators that need copper read the same value.
6. **G6 — Value-based errors.** Fallible operations return `std::expected<Result, emc::Error>` and are `[[nodiscard]]`; no dialogs, no `EXIT_FAILURE` sentinels, no exceptions for expected validation failures. *Test:* no `QMessageBox`, no `exit()`/`EXIT_FAILURE` in the library; every public `calculate` returns `std::expected`.
7. **G7 — Uniform calculator pattern.** Every calculator is the canonical (Input struct, Result struct, `calculate`, optional `validate`) free-function quadruple satisfying the `Calculator` concept. *Test:* a concept check (`static_assert(Calculator<...>)`) holds for every calculator.
8. **G8 — Headless regression coverage.** All 52 golden CSV vectors run as ordinary tests against the pure `calculate()` functions with **no UI**. *Test:* `ctest` passes with Qt absent; re-blessed vectors are documented.
9. **G9 — Consumable library.** A separate downstream project can `find_package(emc)` and link `emc::emc` without knowing the source layout. *Test:* a minimal external CMake project builds against the installed package.
10. **G10 — Pure & thread-safe.** Calculators are pure functions with no global mutable state. *Test:* concurrent calls produce identical results; no static mutable state in the library.

### 3.2 Non-Goals (explicitly out of scope for this plan)

- **N1 — No UI in the library.** `emc` contains zero widgets, zero rendering, zero presentation logic. Formatting numbers for display is a *consumer* concern.
- **N2 — No Qt dependency in the core.** Not even "optional" Qt convenience headers. The app may depend on `emc`; `emc` must never depend on Qt.
- **N3 — Not a rewrite of the Qt app (yet).** This phase extracts the library. Re-wiring the existing widgets to call `emc` is a *later* phase (sketched in [10-migration-roadmap.md](10-migration-roadmap.md)), not part of the initial extraction.
- **N4 — No new EMC physics / no formula changes.** We preserve the existing formulas exactly — **except** to fix clear, demonstrable bugs (e.g. π=3.14, c=3e8). Every such correction must be (a) flagged in the code/plan and (b) accompanied by a *re-blessed* golden vector with a note explaining the delta. We do not "improve" or re-derive formulas on a whim.
- **N5 — Not header-only, not modules-first.** The library is a conventional compiled lib (headers + `.cpp`). C++20 modules may be noted as a *future* option but are not the target here.

---

## 4. Guiding Principles

| Principle | What it means in practice | Replaces |
|---|---|---|
| **Pure, Qt-free functions** | Each calculator is a free function with no `this`, no UI, no I/O. Same inputs → same outputs, always. | Math trapped in widget `clicked` lambdas. |
| **Type-safe quantities** | `quantity<isq::frequency[si::hertz]>`, not `double`. Units in the type; conversions compile-checked via mp-units. | 541 `addItem(unit, factor)` + magic `*39.37`. |
| **One source of truth** | Constants in `emc::constants`, materials in `emc::materials`, each defined exactly once as `constexpr`. | `PI`/`mu0`/`PermofFreeSpace` across 8–14 files. |
| **Errors are values** | `std::expected<Result, emc::Error>`; `validate()` is separable; `[[nodiscard]]`. | `QMessageBox` mid-formula; `EXIT_FAILURE` sentinel. |
| **Testability first** | Pure functions are trivially testable headless; golden CSVs become plain vectors. | `ui->solveButton->clicked()` test driver. |
| **Modern C++23** | `std::expected`, `std::format`/`std::print`, concepts, designated initializers, `constexpr` `<cmath>`, `[[nodiscard]]`. C++26 (reflection, contracts, senders) flagged as forward-looking. | C++20 + Qt idioms. |
| **Stable, minimal public surface** | Only the (Input, Result, `calculate`) triples and the vocabulary (`emc::units`, `emc::constants`, `emc::materials`, `emc::Error`) are public; internals hide in `emc::detail`. | No API boundary at all. |

---

## 5. Before / After Architecture

### 5.1 The shape of the change

```text
                       BEFORE (today)
   ┌───────────────────────────────────────────────────────┐
   │  SkinDepthWidget : QWidget                             │
   │  ┌─────────────────────────────────────────────────┐  │
   │  │  UI layout (ui->...)                             │  │
   │  │  combo addItem("MHz", 1e6) ...   ← unit factors │  │
   │  │  if (material=="Copper") cond=5.8e7  ← materials│  │
   │  │  connect(solveButton, [] {                       │  │
   │  │      qreal d = qSqrt(1/(M_PI*f*mu*sigma));  ← MATH  │
   │  │      ui->result->setValue(d/factor);            │  │
   │  │  });                                             │  │
   │  └─────────────────────────────────────────────────┘  │
   │   UI + units + materials + validation + MATH = 1 blob  │
   └───────────────────────────────────────────────────────┘
            (cannot run the math without Qt)

                       AFTER (target)
   ┌──────────────────────────┐        ┌─────────────────────────────────┐
   │  SkinDepthWidget : QWidget│        │  emc  (Qt-free compiled library) │
   │  read ui fields           │        │  emc::constants  (π, c, μ₀ once) │
   │  build SkinDepthInput{…}   │──────▶ │  emc::materials  (Copper once)   │
   │  call emc::basic::         │ calls  │  emc::units      (mp-units vocab)│
   │      calculate(in)         │◀────── │  emc::basic::calculate(Input)    │
   │  render Result / Error     │ value  │    -> expected<Result, Error>    │
   └──────────────────────────┘        └─────────────────────────────────┘
        thin consumer (UI only)              pure, testable, reusable
                                         (CLI / tests / server / notebook
                                          can call it too — no Qt needed)
```

### 5.2 Concrete before/after: Skin Depth

Skin depth is δ = √(1 / (π f μ σ)). Here is the actual current implementation, condensed from `src/BasicCalculations/SkinDepth/SkinDepthWidget.cpp`:

**BEFORE** — math fused to the widget (real code, abridged):

```cpp
// In SkinDepthWidget.h: mu0 redefined here (one of 8+ copies)
qreal mu0 = 4 * M_PI * 1e-7;

// In the constructor: units as combo factors, materials as inline strings
ui->frequencyUnitBox->addItem("MHz", 1e6);          // 1 of 541 such calls
ui->skinDepth_unit->addItem("cm", 0.01);
if (material == "Copper") {                          // material table copy #1 of ~14
    ui->conductivity_spinbox->setValue(5.8005E+7);
    ui->ur->setValue(0.999991);
}
// ... and the math, locked inside a clicked lambda:
connect(ui->solveButton, &RichButton::clicked, [this]() {
    qreal frequency    = ui->frequency_spinbox->value()
                       * ui->frequencyUnitBox->currentData().toReal();   // unit factor
    qreal conductivity = ui->conductivity_spinbox->value();
    qreal mu           = ui->ur->value() * mu0;
    qreal skinDepth    = qSqrt(1 / (M_PI * frequency * mu * conductivity));
    ui->skinDepth->setValue(skinDepth / ui->skinDepth_unit->currentData().toReal());
});
```

To exercise this formula you must build a widget, a stacked-widget parent, populate combo boxes, and fire a signal. The constant `mu0` is one of eight copies. The unit handling is two of the 541 factor pairs. The material is one of fourteen tables.

**AFTER** — pure library function (illustrative target code; see [06-calculator-design-pattern.md](06-calculator-design-pattern.md) for the full pattern):

```cpp
#include <emc/basic/skin_depth.hpp>
#include <emc/units.hpp>
#include <emc/materials.hpp>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Library side — defined once, no Qt, fully testable:
namespace emc::basic {

struct SkinDepthInput {
    quantity<isq::frequency[si::hertz]>             frequency;
    quantity<isq::electrical_conductivity[/*S/m*/]> conductivity;
    quantity<dimensionless[one]>                    relative_permeability{1};
};

struct SkinDepthResult {
    quantity<isq::length[si::metre]> skin_depth;
};

[[nodiscard]] std::expected<SkinDepthResult, Error>
calculate(const SkinDepthInput& in);   // δ = √(1 / (π f μ₀μ_r σ)),
                                       // using emc::constants::pi, emc::constants::mu0
}
```

```cpp
// Consumer side (the rewired widget, later phase) — designated initializers,
// units in the type, errors as values:
auto r = emc::basic::calculate({
    .frequency        = 27.0 * MHz,                       // unit is checked at compile time
    .conductivity     = emc::materials::nickel.conductivity,
    .relative_permeability = emc::materials::nickel.relative_permeability,
});
if (r) {
    ui->skinDepth->setValue((r->skin_depth).numerical_value_in(cm));  // present in cm
} else {
    showError(r.error());                                // Error is a value, not a dialog
}
```

The math is now callable from anywhere, the constant μ₀ comes from `emc::constants` (one definition, correct value), the material comes from `emc::materials` (one definition), `27.0 * MHz` carries its unit in the type (no `*1e6` factor), and a bad input yields an `emc::Error` value instead of a modal dialog. The same `calculate` runs in the golden-vector test with no UI at all.

---

## 6. Audience & How to Read This Plan

This plan is a set of cross-linked markdown documents. Read **00** (this file) first for the vision, then **01** and **02** for the structural and language foundations, then the topic documents as needed. **10** is the sequencing if you are doing the work; **07** is the work-list if you want the full calculator inventory.

| Doc | One-line description | Primary reader |
|---|---|---|
| [README.md](README.md) | Index and recommended reading order (written last). | Everyone — start here or here-then-00. |
| **[00-overview-and-goals.md](00-overview-and-goals.md)** *(this file)* | Vision, problems-as-costs, goals/non-goals, before/after, glossary, success criteria. | Everyone, especially stakeholders & new contributors. |
| [01-architecture-and-layout.md](01-architecture-and-layout.md) | Layers, directory/namespace layout, dependency rules, the compiled-lib shape. | Architects, anyone adding code. |
| [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md) | The catalog: every modern C++ feature → pain point → example → why. | All implementers. |
| [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) | The mp-units subsystem replacing the 541 hand-wired conversions. | Anyone touching quantities. |
| [04-constants-and-material-database.md](04-constants-and-material-database.md) | One `constexpr` source of truth for constants + materials. | Anyone needing π, c, μ₀, or a material. |
| [05-error-handling-and-validation.md](05-error-handling-and-validation.md) | `std::expected` + `emc::Error`, replacing `QMessageBox`/`EXIT_FAILURE`. | All implementers. |
| [06-calculator-design-pattern.md](06-calculator-design-pattern.md) | The repeatable per-calculator pattern + 2 full worked conversions. | Anyone porting a calculator. |
| [07-calculator-inventory.md](07-calculator-inventory.md) | Complete catalog of all ~52 calculators → library mapping (work-list). | Implementers, planners. |
| [08-build-system-cmake.md](08-build-system-cmake.md) | Modern CMake compiled lib, mp-units dep, install/export, presets, app rewire. | Build engineers. |
| [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) | Reuse the 52 CSVs as Qt-free regression vectors + property/`constexpr` tests. | Anyone writing or re-blessing tests. |
| [10-migration-roadmap.md](10-migration-roadmap.md) | Phased sequencing, exit criteria, effort, risks, definition of done. | Project leads, contributors. |

---

## 7. Success Criteria / Definition of Done (program level)

The extraction is **done** when all of the following measurable conditions hold:

1. **Builds without Qt.** `emc` configures, builds, and installs via CMake on a toolchain with **no Qt present**. (Validates G1, N2.)
2. **Clean dependency graph.** `include/emc/**` and `src/**` contain zero references to Qt symbols (`Q*`, `<QtWidgets>`, etc.). Static check passes in CI. (G1.)
3. **Constants are correct and singular.** `emc::constants` defines π, c (= 299 792 458 m/s), μ₀, ε₀, h exactly once each, as `constexpr` mp-units quantities; `static_assert`s confirm values. The strings `PI 3.14`, `SPEEDOFLIGHT`, `mu0 =`, `PermofFreeSpace` appear **nowhere** in the library. (G2, G3.)
4. **Materials are singular.** `emc::materials` is the only place material properties are defined; ≥ the materials used by the 52 calculators are present, each defined once. (G5.)
5. **All quantities are typed.** Every public Input/Result member is an mp-units `quantity`; no bare-`double` physical quantity in the public API. A deliberate unit-mismatch test fails to compile. (G4.)
6. **Uniform pattern + concept.** Every calculator satisfies `static_assert(Calculator<Input, Result>)`; every `calculate` is `[[nodiscard]]` and returns `std::expected<Result, Error>`. (G6, G7.)
7. **No UI-style error handling.** No `QMessageBox`, no `exit()`/`EXIT_FAILURE`, no error sentinels in the library; validation is a separable `validate()` returning `std::expected<void, Error>`. (G6.)
8. **Golden vectors pass headless.** All 52 CSV fixtures run under `ctest` against pure `calculate()` functions with Qt absent. Every output that differs from the legacy app due to a fixed bug is **re-blessed** and documented with a rationale. (G8, N4.)
9. **Consumable downstream.** A standalone sample CMake project `find_package(emc)` + `target_link_libraries(app emc::emc)` builds and runs against the *installed* package. (G9.)
10. **Pure & thread-safe.** No global mutable state; the same inputs always yield the same outputs; concurrent invocation is safe by construction. (G10.)
11. **Coverage of the inventory.** Every calculator listed in [07-calculator-inventory.md](07-calculator-inventory.md) that contains real math is implemented in the library (the 44 math-bearing leaves at minimum), or explicitly deferred with a reason.

> The Qt app re-wire is **not** a precondition for "done" on the library (see N3). It is tracked separately in [10-migration-roadmap.md](10-migration-roadmap.md).

---

## 8. Glossary

### 8.1 EMC / domain terms

| Term | Meaning (as used in this plan) |
|---|---|
| **EMC** | Electromagnetic Compatibility — the discipline of making electronics work without emitting or being disrupted by electromagnetic interference. |
| **Skin depth (δ)** | The depth at which AC current density in a conductor falls to 1/e of its surface value: δ = √(1 / (π f μ σ)). Higher frequency → shallower current. |
| **Antenna factor** | Ratio relating the incident E-field to the voltage at an antenna's terminals (dB/m); convertible to/from antenna gain. |
| **Decibel (dB)** | Logarithmic ratio of two power/field quantities; the Converter category does dB↔ratio math. |
| **VSWR** | Voltage Standing Wave Ratio — a measure of impedance mismatch on a transmission line; interconvertible with reflection coefficient (RC), return loss (RL), mismatch loss (ML), and transmission loss (TL). |
| **Microstrip** | A PCB trace of width *W* and thickness *T* over a dielectric of height *H* and relative permittivity εᵣ above a ground plane; its characteristic impedance Z₀ is solved (and inverse-solved for H/T/W) in `MicrostripTraceWidget`. |
| **Characteristic impedance (Z₀)** | The impedance a transmission line presents to a traveling wave; central to PCB trace and transmission-line calculators. |
| **Relative permittivity (εᵣ)** | Dielectric constant of a material relative to vacuum; an input to impedance calculations. |
| **Permeability (μ), relative permeability (μᵣ)** | A material's response to magnetic fields; μ = μ₀·μᵣ. μ₀ is the constant redefined 8+ times today. |
| **Conductivity (σ) / resistivity (ρ)** | A material's ability/inability to conduct current (ρ = 1/σ); the per-material data duplicated across ~14 files. |
| **Shielding effectiveness (SE)** | How much an enclosure attenuates an EM field (in dB); a Shielding-category calculator. |
| **Cavity / cavity resonance** | The discrete frequencies (resonant modes, e.g. f₁₁₀…f₁₂₁) at which a metallic enclosure resonates; `RectangularEnclosureWidget` outputs 12 modes. |
| **Cable braid optical coverage** | The fraction of a cable shield physically covered by its braid weave; a Cabling calculator. |
| **Crosstalk** | Unwanted coupling between adjacent conductors; a Cabling calculator. |
| **Ferrite** | A magnetic material used for EMI suppression (e.g. ferrite toroids/beads); a Filtering calculator. |
| **ESD / lightning coupling** | Coupling of electrostatic-discharge or lightning transients into circuits; EMCPredictions calculators. |

### 8.2 Modern-C++ terms

| Term | Meaning (as used in this plan) |
|---|---|
| **mp-units `quantity`** | A value from the mp-units library carrying both a number and a unit/dimension in its *type*, e.g. `quantity<isq::frequency[si::hertz]>`. Conversions are compile-checked; mixing incompatible units fails to compile. Replaces bare `double` + factor pairs. |
| **`isq` / SI** | mp-units' International System of Quantities (dimensions like frequency, length) and SI unit set (hertz, metre). A quantity binds a quantity-kind (`isq::frequency`) to a unit (`si::hertz`). |
| **`std::expected<T, E>`** | C++23 sum type holding either a value `T` or an error `E`. Our fallible `calculate()` returns `std::expected<Result, emc::Error>` — errors are *values*, not dialogs or sentinels. |
| **`emc::Error` / `emc::ErrorCode`** | The single library error type: an `enum class ErrorCode` plus context. Carried as the `E` in every `std::expected`. |
| **`[[nodiscard]]`** | Attribute making it a compile warning to ignore a function's return value — applied to every `calculate()` so a result (or error) cannot be silently dropped. |
| **concept / `Calculator` concept** | A C++20/23 compile-time predicate over types. The `Calculator` concept checks that an (Input, Result, `calculate`) triple has the required shape; `static_assert(Calculator<...>)` enforces the pattern. |
| **aggregate + designated initializers** | An aggregate `struct` (no user constructors) initialized like `SkinDepthInput{ .frequency = 27.0 * MHz, ... }`. Gives readable, self-documenting, order-independent call sites with member defaults. |
| **deducing `this`** | C++23 feature letting a member function deduce the type/value-category of its object parameter; used in this plan for forward-looking generic/CRTP-free helpers (see [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md)). |
| **`constexpr` (incl. `constexpr` `<cmath>`)** | Compile-time evaluation; C++23 makes much of `<cmath>` `constexpr`, so constants and some results can be computed and `static_assert`-checked at compile time. |
| **`std::format` / `std::print`** | C++23 type-safe formatted output, used in tests/diagnostics instead of stream or Qt string formatting. |
| **C++26 forward-looking** | Features (reflection, contracts, `std::execution`/senders, pattern matching) noted as future opportunities in clearly-marked callouts — not part of the C++23 baseline. |

---

## Cross-references

- [01-architecture-and-layout.md](01-architecture-and-layout.md) — concrete layers, namespaces, and the compiled-library shape behind the "After" diagram.
- [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md) — the full feature → pain-point → example mapping referenced throughout §4.
- [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — how mp-units replaces the 541 unit conversions (§2.4, G4).
- [04-constants-and-material-database.md](04-constants-and-material-database.md) — the single source of truth for the constants/materials in §2.1–2.5.
- [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — the `std::expected`/`emc::Error` design replacing §2.6.
- [06-calculator-design-pattern.md](06-calculator-design-pattern.md) — the full pattern behind the "After" skin-depth snippet (§5.2).
- [07-calculator-inventory.md](07-calculator-inventory.md) — the work-list of all ~52 calculators (success criterion 11).
- [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — reusing/re-blessing the 52 CSVs (§2.7, G8, N4).
- [10-migration-roadmap.md](10-migration-roadmap.md) — sequencing and the program-level definition of done (§7).
