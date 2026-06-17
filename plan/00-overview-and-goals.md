# 🧭 Overview & Goals

> The front door to the **emc** library plan: what we are building — a GUI-free, type-safe, modern-C++ library of electromagnetic-compatibility (EMC) engineering calculations — what "done" looks like, and how to read the rest of this plan.

> [!NOTE]
> **emc** is a greenfield C++23 library that turns established EMC textbook formulas into pure, type-safe, headless functions. Every calculator is a free `calculate(const Input&) -> std::expected<Result, Error>` over an aggregate input, built on **mp-units** for physical quantities and `std::expected` for errors. No UI, no global state — correct physics you can call from a CLI, a test, a server, a notebook, or any front end.

---

## 1. Purpose / Executive Summary

**emc** (living in `/Users/sufuk/CLionProjects/emcpp`) is a modern C++23 library that implements the standard EMC engineering calculations: skin depth, antenna factor, decibel conversions, microstrip impedance, cavity resonance, shielding effectiveness, cable braid coverage, crosstalk, and roughly 52 calculators in total, organized into 9 categories. The math is well-understood textbook EMC physics; the value the library adds is *packaging it correctly*: exact constants, unit-safe quantities, a single material database, and explicit, recoverable error handling — all reusable and testable without any presentation layer.

It is a **traditional compiled C++23 library** — public headers in `include/emc/`, compiled translation units in `src/`, built into a static and/or shared library through CMake with proper `install()`/`export()` and a package config so any downstream project can `find_package(emc)` and link `emc::emc`. It depends on **mp-units** (the ISO/IEC 80000 standardization-track quantities-and-units library) for all physical quantities, and on **no GUI toolkit of any kind**. Every calculator is a pure free function over an aggregate input struct, returning `std::expected<Result, emc::Error>`.

> [!IMPORTANT]
> The library is **presentation-free by design**. The same EMC formula is needed from many front ends — a desktop GUI, a CLI tool, a regression test, a web service, a notebook. Tying a formula to a UI would make it callable from exactly one place. `emc` keeps the physics pure so it can be called from all of them.

This document establishes the vision, goals, non-goals, guiding principles, and success criteria. The detailed *how* lives in documents 01–09 (see §5).

---

## 2. 🎯 Goals and Non-Goals

### 2.1 Goals (numbered, testable)

1. **G1 — GUI-free, UI-free core.** The `emc` library compiles and links with **zero** GUI-toolkit headers and zero GUI link dependencies. *Test:* a CI grep for any GUI symbols across `include/` and `src/` returns nothing; the library builds on a toolchain with no GUI framework present at all.
2. **G2 — One source of truth for constants.** π, c, μ₀, ε₀, h, etc. are defined exactly once, as `constexpr` mp-units quantities in `emc::constants`. *Test:* every constant has exactly one definition site, usable in a `static_assert`.
3. **G3 — Exact constants.** π is the full-precision library value, c = 299 792 458 m/s exactly, μ₀/ε₀ are CODATA-consistent. *Test:* constant values match reference to full `double` precision in a `static_assert`/unit test. EMC results span the dB scale, where small constant errors compound — so constants must be exact, not rounded.
4. **G4 — Type-safe quantities.** Every physical input/output is an mp-units `quantity` with a unit baked into the type; conversions are compile-checked. *Test:* mixing incompatible units fails to compile; there are no bare `double` unit factors anywhere in the public API.
5. **G5 — One material database.** All material properties live once in `emc::materials` as `constexpr` data. *Test:* "Copper" has a single definition; all calculators that need copper read the same value.
6. **G6 — Value-based errors.** Fallible operations return `std::expected<Result, emc::Error>` and are `[[nodiscard]]`; no dialogs, no process-exit sentinels, no exceptions for expected validation failures. *Test:* every public `calculate` returns `std::expected`; no error sentinels in the library.
7. **G7 — Uniform calculator pattern.** Every calculator is the canonical (Input struct, Result struct, `calculate`, optional `validate`) free-function quadruple satisfying the `Calculator` concept. *Test:* a concept check (`static_assert(Calculator<...>)`) holds for every calculator.
8. **G8 — Headless, first-principles test coverage.** Every calculator has known-value, round-trip, property, and edge-case tests that run as ordinary tests against the pure `calculate()` functions with **no UI**. *Test:* `ctest` passes on a toolchain with no GUI present.
9. **G9 — Consumable library.** A separate downstream project can `find_package(emc)` and link `emc::emc` without knowing the source layout. *Test:* a minimal external CMake project builds against the installed package.
10. **G10 — Pure & thread-safe.** Calculators are pure functions with no global mutable state. *Test:* concurrent calls produce identical results; no static mutable state in the library.

### 2.2 Non-Goals (explicitly out of scope)

- **N1 — No UI in the library.** `emc` contains zero rendering and zero presentation logic. Formatting numbers for display is a *front-end* concern.
- **N2 — No GUI-toolkit dependency in the core.** Not even "optional" convenience headers. A front end may depend on `emc`; `emc` must never depend on a GUI toolkit.
- **N3 — No novel EMC physics / no formula invention.** We implement the standard, textbook EMC formulas for each quantity. The library implements known physics correctly and safely; it does not derive new physics.
- **N4 — Not header-only, not modules-first.** The library is a conventional compiled lib (headers + `.cpp`). C++20 modules may be noted as a *future* option but are not the target here.

---

## 3. 🧱 Guiding Principles

> [!TIP]
> These principles are the lens for every design decision in the rest of the plan. When a later document weighs two options, it picks the one that best honours the row below.

| Principle | What it means in practice | Why it fits EMC |
|---|---|---|
| **Pure, presentation-free functions** | Each calculator is a free function with no `this`, no UI, no I/O. Same inputs → same outputs, always. | EMC formulas are needed from many front ends; purity makes them reusable and trivially testable. |
| **Type-safe quantities** | `quantity<isq::frequency[si::hertz]>`, not `double`. Units live in the type; conversions are compile-checked via mp-units. | EMC inputs span Hz→GHz and metres→mils; mp-units makes unit handling type-safe and conversion automatic. |
| **One source of truth** | Constants in `emc::constants`, materials in `emc::materials`, each defined exactly once as `constexpr`. | Cross-calculator results must be self-consistent: "Copper" and μ₀ must mean exactly one thing everywhere. |
| **Errors are values** | `std::expected<Result, emc::Error>`; `validate()` is separable; `[[nodiscard]]`. | A calculation may be called with out-of-domain geometry; returning `std::expected` makes failure explicit and recoverable. |
| **Testability first** | Pure functions are trivially testable headless, against hand-computed and property-based expectations. | Engineering tools must be verifiable; a headless test suite is the safety net for the physics. |
| **Modern C++23** | `std::expected`, `std::format`/`std::print`, concepts, designated initializers, `constexpr` `<cmath>`, `[[nodiscard]]`. C++26 (reflection, contracts, senders) flagged as forward-looking. | The C++23 toolbox maps almost one-to-one onto the needs of a numerical, fallible, type-safe library. |
| **Stable, minimal public surface** | Only the (Input, Result, `calculate`) triples and the vocabulary (`emc::units`, `emc::constants`, `emc::materials`, `emc::Error`) are public; internals hide in `emc::detail`. | A small, intentional API is easier to keep stable, document, and reason about. |

---

## 4. 🗺️ At-a-Glance Architecture

### 4.1 The shape of the library

The library is a single GUI-free compiled artifact. Front ends build an aggregate `Input`, call a pure `calculate`, and receive either a typed `Result` or an `emc::Error` — all without the library knowing or caring what the front end is.

```text
   ┌────────────────────────────┐        ┌─────────────────────────────────┐
   │  Any front end             │        │  emc  (GUI-free compiled library)│
   │  (GUI / CLI / test /       │        │  emc::constants  (π, c, μ₀ once) │
   │   server / notebook)       │        │  emc::materials  (Copper once)   │
   │                            │        │  emc::units      (mp-units vocab)│
   │  build SkinDepthInput{…}   │──────▶ │  emc::basic::calculate(Input)    │
   │  call emc::basic::          │ calls  │    -> expected<Result, Error>    │
   │      calculate(in)         │◀────── │                                  │
   │  render Result / Error     │ value  │  pure · testable · reusable      │
   └────────────────────────────┘        └─────────────────────────────────┘
        thin front end (any UI)               no GUI toolkit required
```

The category namespaces (`emc::basic`, `emc::converter`, `emc::component`, `emc::prediction`, `emc::shielding`, `emc::filtering`, `emc::cabling`, `emc::grounding`, `emc::testing`) each hold their family of calculators; the shared vocabulary (`emc::constants`, `emc::units`, `emc::materials`, `emc::Error`) is common to all. See [01-architecture-and-layout.md](01-architecture-and-layout.md) for the full layering.

### 4.2 Worked example: Skin Depth

Skin depth is the standard EMC result for the depth at which AC current density in a conductor falls to 1/e of its surface value:

```text
δ = √( 1 / (π · f · μ · σ) )      with  μ = μ₀ · μ_r
```

The library shape for that formula, with units in the types and μ₀ drawn from the single source of truth in `emc::constants`:

```c++
#include <emc/basic/skin_depth.hpp>
#include <emc/units.hpp>
#include <emc/materials.hpp>
using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// Library side — one definition, no UI, fully testable:
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

```c++
// Front-end side — designated initializers, units in the type, errors as values:
auto r = emc::basic::calculate({
    .frequency             = 27.0 * MHz,                           // unit checked at compile time
    .conductivity          = emc::materials::nickel.conductivity,
    .relative_permeability = emc::materials::nickel.relative_permeability,
});
if (r) {
    std::print("{}\n", r->skin_depth.numerical_value_in(cm));      // present in cm
} else {
    report(r.error());                                             // Error is a value, not a dialog
}
```

> [!NOTE]
> μ₀ comes from `emc::constants` (one exact definition), the material from `emc::materials` (one definition), `27.0 * MHz` carries its unit in the type, and a bad input yields an `emc::Error` value. The same `calculate` runs in a known-value test with no UI at all. The full per-calculator pattern lives in [06-calculator-design-pattern.md](06-calculator-design-pattern.md).

---

## 5. 📚 Audience & How to Read This Plan

Read **00** (this file) first for the vision, then **01** and **02** for the structural and language foundations, then the topic documents as needed. **07** is the full calculator catalog; **09** is the testing strategy.

| Doc | One-line description | Primary reader |
|---|---|---|
| [README.md](README.md) | Index and recommended reading order (written last). | Everyone — start here or here-then-00. |
| **[00-overview-and-goals.md](00-overview-and-goals.md)** *(this file)* | Vision, goals/non-goals, principles, at-a-glance architecture, glossary, success criteria. | Everyone, especially stakeholders & new contributors. |
| [01-architecture-and-layout.md](01-architecture-and-layout.md) | Layers, directory/namespace layout, dependency rules, the compiled-lib shape. | Architects, anyone adding code. |
| [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md) | The catalog: every modern C++ feature → where it fits → example → why. | All implementers. |
| [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) | The mp-units subsystem providing type-safe quantities and conversions. | Anyone touching quantities. |
| [04-constants-and-material-database.md](04-constants-and-material-database.md) | One `constexpr` source of truth for constants + materials. | Anyone needing π, c, μ₀, or a material. |
| [05-error-handling-and-validation.md](05-error-handling-and-validation.md) | `std::expected` + `emc::Error` and the `validate()` design. | All implementers. |
| [06-calculator-design-pattern.md](06-calculator-design-pattern.md) | The repeatable per-calculator pattern + worked examples. | Anyone building a calculator. |
| [07-calculator-inventory.md](07-calculator-inventory.md) | **Calculator Catalog** — the complete set of ~52 calculators and their library mapping. | Implementers, planners. |
| [08-build-system-cmake.md](08-build-system-cmake.md) | Modern CMake compiled lib, mp-units dependency, install/export, presets. | Build engineers. |
| [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) | **Testing Strategy** — known-value, property, round-trip, `constexpr`, and edge-case tests. | Anyone writing tests. |

---

## 6. ✅ Success Criteria / Definition of Done

The library is **done** when all of the following measurable conditions hold:

1. **Builds without a GUI toolkit.** `emc` configures, builds, and installs via CMake on a toolchain with **no GUI framework present**. (Validates G1, N2.)
2. **Clean dependency graph.** `include/emc/**` and `src/**` contain zero references to any GUI symbols. A static CI grep for GUI symbols passes. (G1.)
3. **Constants are exact and singular.** `emc::constants` defines π, c (= 299 792 458 m/s), μ₀, ε₀, h exactly once each, as `constexpr` mp-units quantities; `static_assert`s confirm values. (G2, G3.)
4. **Materials are singular.** `emc::materials` is the only place material properties are defined; every material used by the calculators is present, each defined once. (G5.)
5. **All quantities are typed.** Every public Input/Result member is an mp-units `quantity`; no bare-`double` physical quantity in the public API. A deliberate unit-mismatch test fails to compile. (G4.)
6. **Uniform pattern + concept.** Every calculator satisfies `static_assert(Calculator<Input, Result>)`; every `calculate` is `[[nodiscard]]` and returns `std::expected<Result, Error>`. (G6, G7.)
7. **No UI-style error handling.** No dialogs, no `exit()` sentinels, no error sentinels in the library; validation is a separable `validate()` returning `std::expected<void, Error>`. (G6.)
8. **Tests pass headless.** Every calculator's known-value, round-trip, property, and edge-case tests run under `ctest` against pure `calculate()` functions with no GUI present. (G8.)
9. **Consumable downstream.** A standalone sample CMake project `find_package(emc)` + `target_link_libraries(app emc::emc)` builds and runs against the *installed* package. (G9.)
10. **Pure & thread-safe.** No global mutable state; the same inputs always yield the same outputs; concurrent invocation is safe by construction. (G10.)
11. **Coverage of the catalog.** Every calculator listed in [07-calculator-inventory.md](07-calculator-inventory.md) that contains real math is implemented in the library, or explicitly deferred with a reason.

---

## 7. 📖 Glossary

### 7.1 EMC / domain terms

| Term | Meaning (as used in this plan) |
|---|---|
| **EMC** | Electromagnetic Compatibility — the discipline of making electronics work without emitting or being disrupted by electromagnetic interference. |
| **Skin depth (δ)** | The depth at which AC current density in a conductor falls to 1/e of its surface value: δ = √(1 / (π f μ σ)). Higher frequency → shallower current. |
| **Antenna factor** | Ratio relating the incident E-field to the voltage at an antenna's terminals (dB/m); convertible to/from antenna gain. |
| **Decibel (dB)** | Logarithmic ratio of two power/field quantities; the Converter category does dB↔ratio math. |
| **VSWR** | Voltage Standing Wave Ratio — a measure of impedance mismatch on a transmission line; interconvertible with reflection coefficient (RC), return loss (RL), mismatch loss (ML), and transmission loss (TL). |
| **Microstrip** | A PCB trace of width *W* and thickness *T* over a dielectric of height *H* and relative permittivity εᵣ above a ground plane; its characteristic impedance Z₀ is solved (and inverse-solved for H/T/W). |
| **Characteristic impedance (Z₀)** | The impedance a transmission line presents to a traveling wave; central to PCB trace and transmission-line calculators. |
| **Relative permittivity (εᵣ)** | Dielectric constant of a material relative to vacuum; an input to impedance calculations. |
| **Permeability (μ), relative permeability (μᵣ)** | A material's response to magnetic fields; μ = μ₀·μᵣ. μ₀ is one of the exact constants in `emc::constants`. |
| **Conductivity (σ) / resistivity (ρ)** | A material's ability/inability to conduct current (ρ = 1/σ); part of the per-material data in `emc::materials`. |
| **Shielding effectiveness (SE)** | How much an enclosure attenuates an EM field (in dB); a Shielding-category calculator. |
| **Cavity / cavity resonance** | The discrete frequencies (resonant modes, e.g. f₁₁₀…f₁₂₁) at which a metallic enclosure resonates; a rectangular enclosure yields 12 modes. |
| **Cable braid optical coverage** | The fraction of a cable shield physically covered by its braid weave; a Cabling calculator. |
| **Crosstalk** | Unwanted coupling between adjacent conductors; a Cabling calculator. |
| **Ferrite** | A magnetic material used for EMI suppression (e.g. ferrite toroids/beads); a Filtering calculator. |
| **ESD / lightning coupling** | Coupling of electrostatic-discharge or lightning transients into circuits; Prediction-category calculators. |

### 7.2 Modern-C++ terms

| Term | Meaning (as used in this plan) |
|---|---|
| **mp-units `quantity`** | A value from the mp-units library carrying both a number and a unit/dimension in its *type*, e.g. `quantity<isq::frequency[si::hertz]>`. Conversions are compile-checked; mixing incompatible units fails to compile. |
| **`isq` / SI** | mp-units' International System of Quantities (dimensions like frequency, length) and SI unit set (hertz, metre). A quantity binds a quantity-kind (`isq::frequency`) to a unit (`si::hertz`). |
| **`std::expected<T, E>`** | C++23 sum type holding either a value `T` or an error `E`. Our fallible `calculate()` returns `std::expected<Result, emc::Error>` — errors are *values*. |
| **`emc::Error` / `emc::ErrorCode`** | The single library error type: an `enum class ErrorCode` plus context. Carried as the `E` in every `std::expected`. |
| **`[[nodiscard]]`** | Attribute making it a compile warning to ignore a function's return value — applied to every `calculate()` so a result (or error) cannot be silently dropped. |
| **concept / `Calculator` concept** | A C++20/23 compile-time predicate over types. The `Calculator` concept checks that an (Input, Result, `calculate`) triple has the required shape; `static_assert(Calculator<...>)` enforces the pattern. |
| **aggregate + designated initializers** | An aggregate `struct` (no user constructors) initialized like `SkinDepthInput{ .frequency = 27.0 * MHz, ... }`. Gives readable, self-documenting, order-independent call sites with member defaults. |
| **deducing `this`** | C++23 feature letting a member function deduce the type/value-category of its object parameter; used for forward-looking generic helpers (see [02-modern-cpp-feature-catalog.md](02-modern-cpp-feature-catalog.md)). |
| **`constexpr` (incl. `constexpr` `<cmath>`)** | Compile-time evaluation; C++23 makes much of `<cmath>` `constexpr`, so constants and some results can be computed and `static_assert`-checked at compile time. |
| **`std::format` / `std::print`** | C++23 type-safe formatted output, used in tests/diagnostics. |
| **C++26 forward-looking** | Features (reflection, contracts, `std::execution`/senders, pattern matching) noted as future opportunities in clearly-marked callouts — not part of the C++23 baseline. |

---

## Cross-references

- [01-architecture-and-layout.md](01-architecture-and-layout.md) — concrete layers, namespaces, and the compiled-library shape (§4).
- [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — type-safe quantities and conversions (G4).
- [04-constants-and-material-database.md](04-constants-and-material-database.md) — single source of truth for constants and materials (G2, G3, G5).
- [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — the `std::expected`/`emc::Error` design (G6).
- [06-calculator-design-pattern.md](06-calculator-design-pattern.md) — the full pattern behind the skin-depth example (§4.2).
- [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — the Testing Strategy: known-value, property, round-trip, and edge-case tests (G8).
