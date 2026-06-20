# 🧭 Overview & Goals

> The front door to the **emc** library plan: what we are building — a GUI-free, type-safe, modern-C++ library of electromagnetic-compatibility (EMC) engineering calculations — what "done" looks like, and how to read the rest of this plan.

> [!NOTE]
> **emc** is a greenfield C++23 library that turns established EMC textbook formulas into pure, type-safe, headless functions. Every calculator is a free `calculate(const Input&) -> std::expected<Result, Error>` over an aggregate input, built on **mp-units** for physical quantities and `std::expected` for errors. No UI, no global state — correct physics you can call from a CLI, a test, a server, a notebook, or any front end.

---

## 1. Purpose

**emc** (at `/Users/sufuk/CLionProjects/emcpp`) implements the standard EMC engineering calculations — skin depth, antenna factor, decibel conversions, microstrip impedance, cavity resonance, shielding effectiveness, braid coverage, crosstalk, 51 calculators across 9 categories. The math is well-understood textbook physics; the value is *packaging it correctly*: exact constants, unit-safe quantities, a single material database, and explicit recoverable errors — reusable and testable without any presentation layer.

It is a **traditional compiled C++23 library**: public headers in `include/emc/`, translation units in `src/`, built static/shared via CMake with proper `install()`/`export()` so downstream projects can `find_package(emc)` and link `emc::emc`. It depends on **mp-units** (the ISO/IEC 80000 standardization-track quantities library) and on **no GUI toolkit of any kind**.

> [!IMPORTANT]
> The library is **presentation-free by design**. The same EMC formula is needed from many front ends — CLI, test, web service, notebook. Tying a formula to a UI would make it callable from exactly one place. `emc` keeps the physics pure so it can be called from all of them.

The detailed *how* lives in the remaining top-level documents and the `implementation/` guides (see §5).

---

## 2. 🎯 Goals and Non-Goals

### 2.1 Goals (numbered, testable)

1. **G1 — GUI-free, UI-free core.** Zero GUI-toolkit headers, zero GUI link dependencies. *Test:* a CI grep for GUI symbols across `include/`/`src/` returns nothing; the library builds with no GUI framework present.
2. **G2 — One source of truth for constants.** π, c, μ₀, ε₀, h are defined exactly once as `constexpr` mp-units quantities in `emc::constants`. *Test:* one definition site each, usable in a `static_assert`.
3. **G3 — Exact constants.** π full-precision, c = 299 792 458 m/s exactly, μ₀/ε₀ CODATA-consistent. EMC results span the dB scale where small errors compound. *Test:* values match reference to full `double` precision.
4. **G4 — Type-safe quantities.** Every physical input/output is an mp-units `quantity` with its unit in the type; conversions are compile-checked. *Test:* incompatible units fail to compile; no bare `double` unit factors in the public API.
5. **G5 — One material database.** All material properties live once in `emc::materials` as `constexpr` data. *Test:* "Copper" has a single definition read by all calculators.
6. **G6 — Value-based errors.** Fallible operations return `[[nodiscard]] std::expected<Result, emc::Error>`; no dialogs, no exit sentinels, no exceptions for expected validation failures. *Test:* every public `calculate` returns `std::expected`.
7. **G7 — Uniform calculator pattern.** Every calculator is the canonical (Input, Result, `calculate`, optional `validate`) quadruple satisfying the `Calculator` concept. *Test:* `static_assert(Calculator<...>)` holds for every calculator.
8. **G8 — Headless, first-principles tests.** Every calculator has known-value, round-trip, property, and edge-case tests against the pure `calculate()` with **no UI**. *Test:* `ctest` passes with no GUI present.
9. **G9 — Consumable library.** A downstream project can `find_package(emc)` + link `emc::emc` without knowing the source layout. *Test:* a minimal external CMake project builds against the installed package.
10. **G10 — Pure & thread-safe.** Calculators are pure functions, no global mutable state. *Test:* concurrent calls produce identical results.

### 2.2 Non-Goals

- **N1 — No UI in the library.** Zero rendering, zero presentation logic. Formatting numbers for display is a front-end concern.
- **N2 — No GUI-toolkit dependency in the core.** Not even "optional" convenience headers. A front end may depend on `emc`; `emc` must never depend on a GUI toolkit.
- **N3 — No novel EMC physics.** We implement standard textbook formulas correctly and safely; we do not derive new physics.
- **N4 — Not header-only, not modules-first.** A conventional compiled lib (headers + `.cpp`). C++20 modules are a *future* option, not the target.

---

## 3. 🧱 Guiding Principles

> [!TIP]
> These principles are the lens for every design decision in the rest of the plan. When a later document weighs two options, it picks the one that best honours the row below.

| Principle | What it means | Why it fits EMC |
|---|---|---|
| **Pure, presentation-free functions** | Free functions: no `this`, no UI, no I/O. Same inputs → same outputs. | Formulas are needed from many front ends; purity makes them reusable and trivially testable. |
| **Type-safe quantities** | `quantity<isq::frequency[si::hertz]>`, not `double`. Units in the type, conversions compile-checked. | Inputs span Hz→GHz and metres→mils; mp-units makes unit handling safe and automatic. |
| **One source of truth** | Constants in `emc::constants`, materials in `emc::materials`, each `constexpr` once. | Cross-calculator results must agree: "Copper" and μ₀ mean one thing everywhere. |
| **Errors are values** | `std::expected<Result, emc::Error>`; separable `validate()`; `[[nodiscard]]`. | Out-of-domain geometry must fail explicitly and recoverably. |
| **Testability first** | Pure functions are trivially testable headless against hand-computed and property-based expectations. | A headless test suite is the safety net for the physics. |
| **Modern C++23** | `std::expected`, `std::format`/`std::print`, concepts, designated initializers, `constexpr` `<cmath>`, `[[nodiscard]]`. C++26 flagged as forward-looking. | The C++23 toolbox maps almost one-to-one onto a numerical, fallible, type-safe library. |
| **Stable, minimal public surface** | Only the (Input, Result, `calculate`) triples and vocabulary (`emc::units`, `constants`, `materials`, `Error`) are public; internals hide in `emc::detail`. | A small, intentional API is easier to keep stable and reason about. |

---

## 4. 🗺️ At-a-Glance Architecture

The library is a single GUI-free compiled artifact. Front ends build an aggregate `Input`, call a pure `calculate`, and receive either a typed `Result` or an `emc::Error` — all without the library knowing what the front end is.

```text
   ┌────────────────────────────┐        ┌─────────────────────────────────┐
   │  Any front end             │        │  emc  (GUI-free compiled library)│
   │  (GUI / CLI / test /       │        │  emc::constants  (π, c, μ₀ once) │
   │   server / notebook)       │        │  emc::materials  (Copper once)   │
   │  build SkinDepthInput{…}   │──────▶ │  emc::units      (mp-units vocab)│
   │  call emc::basic::          │ calls  │  emc::basic::calculate(Input)    │
   │      calculate(in)         │◀────── │    -> expected<Result, Error>    │
   │  render Result / Error     │ value  │  pure · testable · reusable      │
   └────────────────────────────┘        └─────────────────────────────────┘
        thin front end (any UI)               no GUI toolkit required
```

The category namespaces (`emc::basic`, `converter`, `component`, `prediction`, `shielding`, `filtering`, `cabling`, `grounding`, `testing`) each hold their family of calculators; the shared vocabulary (`emc::constants`, `units`, `materials`, `Error`) is common to all. Full layering: [01-architecture-and-layout.md](01-architecture-and-layout.md).

**Worked example — skin depth.** The standard result for the depth at which AC current density falls to 1/e of its surface value:

```text
δ = √( 1 / (π · f · μ · σ) )      with  μ = μ₀ · μ_r
```

The call site shows the design in one line: units in the type, μ₀ from the single source of truth, errors as values.

```cpp
auto r = emc::basic::calculate({
    .frequency             = 27.0 * MHz,                       // unit checked at compile time
    .conductivity          = emc::materials::nickel.conductivity,
    .relative_permeability = emc::materials::nickel.relative_permeability,
});
if (r) std::print("{}\n", r->skin_depth.numerical_value_in(cm));
else   report(r.error());                                      // Error is a value, not a dialog
```

The full per-calculator pattern and the foundation code live in [implementation/00-foundation-code.md](implementation/00-foundation-code.md).

---

## 5. 📚 How to Read This Plan

Read **00** (this file) first for the vision, then **01** for the structural foundation, then topic documents as needed. **07** is the full calculator catalog; **09** is the testing strategy.

| Doc | One-line description |
|---|---|
| [README.md](README.md) | Index and recommended reading order. |
| **[00-overview-and-goals.md](00-overview-and-goals.md)** *(this file)* | Vision, goals/non-goals, principles, architecture, glossary, success criteria. |
| [01-architecture-and-layout.md](01-architecture-and-layout.md) | Layers, directory/namespace layout, dependency rules, compiled-lib shape. |
| [implementation/](implementation/) | **Per-calculator implementation guides** + [00-foundation-code.md](implementation/00-foundation-code.md): the units, constants, materials, error model, and calculator pattern *as code*. |
| [07-calculator-inventory.md](07-calculator-inventory.md) | **Calculator Catalog** — all 51 calculators and their mapping. |
| [implementation/16-build-and-scaffolding.md](implementation/16-build-and-scaffolding.md) | Build & scaffolding — folder tree, CMake, install/export, presets, CI, bootstrap. |
| [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) | **Testing Strategy** — known-value, property, round-trip, `constexpr`, edge-case tests. |

---

## 6. ✅ Definition of Done

The library is **done** when all of the following hold:

1. **Builds without a GUI toolkit** via CMake on a toolchain with no GUI framework present. (G1, N2.)
2. **Clean dependency graph.** A static CI grep for GUI symbols across `include/emc/**` and `src/**` passes. (G1.)
3. **Constants exact and singular.** `emc::constants` defines π, c, μ₀, ε₀, h once each as `constexpr` quantities; `static_assert`s confirm values. (G2, G3.)
4. **Materials singular.** `emc::materials` is the only definition site; every material used is present, each once. (G5.)
5. **All quantities typed.** Every public Input/Result member is a `quantity`; a deliberate unit-mismatch test fails to compile. (G4.)
6. **Uniform pattern + concept.** Every calculator satisfies `static_assert(Calculator<Input, Result>)`; every `calculate` is `[[nodiscard]]` and returns `std::expected`. (G6, G7.)
7. **No UI-style error handling.** No dialogs, no exit sentinels; validation is a separable `validate()` returning `std::expected<void, Error>`. (G6.)
8. **Tests pass headless** under `ctest` against pure `calculate()` with no GUI present. (G8.)
9. **Consumable downstream.** A standalone sample project `find_package(emc)` + link `emc::emc` builds against the *installed* package. (G9.)
10. **Pure & thread-safe.** No global mutable state; same inputs → same outputs; concurrent invocation safe by construction. (G10.)
11. **Catalog coverage.** Every calculator in [07-calculator-inventory.md](07-calculator-inventory.md) with real math is implemented, or explicitly deferred with a reason.

---

## 7. 📖 Glossary

### 7.1 EMC / domain terms

| Term | Meaning |
|---|---|
| **EMC** | Electromagnetic Compatibility — making electronics work without emitting or being disrupted by interference. |
| **Skin depth (δ)** | Depth at which AC current density falls to 1/e of its surface value: δ = √(1 / (π f μ σ)). Higher frequency → shallower. |
| **Antenna factor** | Ratio relating incident E-field to antenna terminal voltage (dB/m); convertible to/from gain. |
| **Decibel (dB)** | Logarithmic ratio of two power/field quantities; the Converter category does dB↔ratio math. |
| **VSWR** | Voltage Standing Wave Ratio — impedance-mismatch measure; interconvertible with reflection coefficient, return loss, mismatch loss, transmission loss. |
| **Microstrip** | A PCB trace (width *W*, thickness *T*) over a dielectric (height *H*, εᵣ) above a ground plane; its Z₀ is solved and inverse-solved. |
| **Characteristic impedance (Z₀)** | Impedance a line presents to a traveling wave; central to PCB-trace and transmission-line calculators. |
| **Relative permittivity (εᵣ)** | Dielectric constant relative to vacuum; an impedance-calc input. |
| **Permeability (μ), relative (μᵣ)** | Material's magnetic response; μ = μ₀·μᵣ. μ₀ is an exact constant in `emc::constants`. |
| **Conductivity (σ) / resistivity (ρ)** | Ability/inability to conduct current (ρ = 1/σ); per-material data in `emc::materials`. |
| **Shielding effectiveness (SE)** | How much an enclosure attenuates an EM field (dB); a Shielding calculator. |
| **Cavity resonance** | Discrete frequencies (modes f₁₁₀…f₁₂₁) at which a metallic enclosure resonates; a rectangular box yields 12 modes. |
| **Cable braid optical coverage** | Fraction of a cable shield covered by its braid weave; a Cabling calculator. |
| **Crosstalk** | Unwanted coupling between adjacent conductors; a Cabling calculator. |
| **Ferrite** | Magnetic material for EMI suppression (toroids/beads); a Filtering calculator. |
| **ESD / lightning coupling** | Coupling of ESD or lightning transients into circuits; Prediction calculators. |

### 7.2 Modern-C++ terms

| Term | Meaning |
|---|---|
| **mp-units `quantity`** | A value carrying both number and unit/dimension in its *type*, e.g. `quantity<isq::frequency[si::hertz]>`. Mixing incompatible units fails to compile. |
| **`isq` / SI** | mp-units' International System of Quantities (frequency, length) and SI unit set (hertz, metre). A quantity binds a kind to a unit. |
| **`std::expected<T, E>`** | C++23 sum type holding either value `T` or error `E`. `calculate()` returns `std::expected<Result, emc::Error>` — errors are *values*. |
| **`emc::Error` / `emc::ErrorCode`** | The single library error type: an `enum class ErrorCode` plus context. The `E` in every `std::expected`. |
| **`[[nodiscard]]`** | Makes ignoring a return value a compile warning — applied to every `calculate()`. |
| **`Calculator` concept** | A compile-time predicate checking that an (Input, Result, `calculate`) triple has the required shape; enforced via `static_assert`. |
| **aggregate + designated initializers** | An aggregate `struct` initialized like `SkinDepthInput{ .frequency = 27.0 * MHz, ... }` — readable, order-independent, with member defaults. |
| **`constexpr` (incl. `<cmath>`)** | Compile-time evaluation; C++23 makes much of `<cmath>` `constexpr`, so constants/results can be `static_assert`-checked. |
| **`std::format` / `std::print`** | C++23 type-safe formatted output, used in tests/diagnostics. |
| **C++26 forward-looking** | Reflection, contracts, senders, pattern matching — noted in clearly-marked callouts, not part of the C++23 baseline. |

---

## Cross-references

See §5 for the full document map; [01-architecture-and-layout.md](01-architecture-and-layout.md) gives the concrete layers and compiled-library shape.
