# EMC Library (`emc`) — Plan Index ⚡

> The front page of the implementation plan for **`emc`**, a clean, GUI-free, reusable modern-C++
> library for electromagnetic-compatibility (EMC) engineering math. Start here, then follow one of the
> reading orders below.

---

## 1. Purpose

This folder (`/Users/sufuk/CLionProjects/emcpp/plan/`) is the complete, multi-document **implementation
plan** for `emc`, a standalone modern-C++ EMC library living at `/Users/sufuk/CLionProjects/emcpp`. The
library provides ~51 leaf calculators as pure, dimensionally-typed, `std::expected`-returning free
functions with **zero GUI dependency**, so any front end can become a thin *consumer* that calls
`find_package(emc)` / links `emc::emc`.

These are **planning documents, not source code.** They contain extensive *illustrative* code (in fenced
blocks) showing exactly how each piece should be built and **why** each modern-C++ feature was chosen —
but nothing here is meant to be compiled as-is. The real `include/emc/*` headers and `src/*.cpp` are
written by following these documents.

---

## 2. Locked decisions (do not contradict)

These choices are fixed for the whole plan. Every document and every code example obeys them; if a doc
ever seems to disagree, this box wins.

| Area | Decision |
| --- | --- |
| **C++ standard** | Baseline **C++23** for all example code — `std::expected`, `std::print`/`std::format`, `std::mdspan`, deducing-`this`, `constexpr <cmath>`. Clearly-marked **C++26 forward-looking** callouts (reflection, contracts, `std::execution`/senders, pattern matching) where relevant. |
| **Units** | Adopt **mp-units** (the ISO/IEC 80000, standardization-track quantities/units library) for *all* dimensional quantities. **No hand-rolled units system.** mp-units is the single representation of physical quantities and the single mechanism for conversions. |
| **Library form** | A **traditional compiled library**: public headers in `include/emc/`, compiled `.cpp` in `src/`, built into static and/or shared `emc` via CMake, with `install()`/`export()` + a package config so downstream code can `find_package(emc)` and link **`emc::emc`**. *Not* header-only, *not* modules-first (modules are mentioned only as a future option). |
| **Namespaces** | Top-level `emc`. Sub-namespaces: `emc::constants`, `emc::materials`, `emc::units`, `emc::detail`. Calculators grouped by category namespaces: `emc::basic`, `emc::converter`, `emc::component`, `emc::prediction`, `emc::shielding`, `emc::filtering`, `emc::cabling`, `emc::grounding`, `emc::testing`. |
| **Errors** | One error type **`emc::Error`** (`enum class emc::ErrorCode` + context). Fallible operations return **`std::expected<Result, emc::Error>`** and are marked **`[[nodiscard]]`**. No exceptions for domain failures, no GUI dialogs, no integer sentinels. |
| **Calculator shape** | Each calculator is a **free function** (not a class): an aggregate `Input` struct (designated initializers, member defaults), a `Result` struct, `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, an optional `validate(const Input&) -> std::expected<void, Error>`, and a `Calculator` concept the triple satisfies. |
| **Quantities** | mp-units types only (e.g. `quantity<isq::frequency[si::hertz]>`). Units never live as bare `double`; conversions are compile-checked and zero-cost. |
| **Constants** | `constexpr` in `emc::constants`, expressed as mp-units quantities where physical. One source of truth for `mu0`, `c`, etc. |
| **Purity** | Pure functions, no global mutable state, thread-safe by construction. |

---

## 3. Document map

Every plan file, what it covers, and a suggested reading order for two audiences.

| # | File | What it covers |
| --- | --- | --- |
| — | [`README.md`](README.md) | **This index** — purpose, locked decisions, document map, reading orders, the target end-user API, and the status legend. |
| 00 | [`00-overview-and-goals.md`](00-overview-and-goals.md) | The front door: *why* a type-safe, GUI-free EMC library, scope, non-goals, glossary, and success criteria. |
| 01 | [`01-architecture-and-layout.md`](01-architecture-and-layout.md) | The structural skeleton: layered architecture, directory/namespace layout, dependency rules, public/internal boundary, the calculator contract, and the ABI/versioning stance. |
| 02 | [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) | **The centerpiece** — an example-driven catalog of every modern C++ feature the library uses, each justified against a concrete EMC-domain need, with worked `emc` code. |
| 03 | [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) | The mp-units subsystem: how every physical quantity and conversion is represented with compile-time unit safety. |
| 04 | [`04-constants-and-material-database.md`](04-constants-and-material-database.md) | Exactly **one** `constexpr` place for every physical constant and material property, and how calculators consume it. |
| 05 | [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md) | The single GUI-free error model (`ErrorCode` + `Error` + `std::expected`) and a declarative validation layer. |
| 06 | [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) | **The template you copy 51 times** — the `Input`/`Result`/`calculate()`/`validate()`/`Calculator`-concept shape every calculator takes, with two full worked examples. |
| 07 | [`07-calculator-inventory.md`](07-calculator-inventory.md) | **Calculator Catalog & Library Map** — the catalog of all calculators (inputs, units, formulas, solve directions, validation, materials, complexity) mapped to their target library locations. |
| 08 | [`08-build-system-cmake.md`](08-build-system-cmake.md) | The complete modern-CMake compiled-library build: mp-units dependency acquisition, shared-lib symbol visibility, `install()`/`export()` + package config for `find_package(emc)`, presets, and quality tooling. |
| 09 | [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) | **Testing Strategy** — a fast, deterministic, GUI-free regression suite for the pure `calculate()` functions, layered with compile-time, property, and CI checks. |

### Suggested reading order

**(a) Decision-maker / reviewer** — *is the plan sound?*

> **00** (vision, scope, success criteria) → **01** (what the thing looks like) → **02** §intro
> (skim the modern-C++ value story) → **07** (the catalog — scope & size). *Optional:* **09** to gauge
> how correctness is proven.

**(b) Implementer** — *I'm building this; what do I do, in what order?*

> **00** → **01** → **02** (read in full — it is referenced everywhere) → **03** (units vocabulary)
> → **04** (constants & materials) → **05** (error model) → **06** (the per-calculator template) →
> **07** (pick a calculator and its row) → **08** (get it building) → **09** (prove it with reference
> vectors). Then loop **06 → 07 → 09** once per calculator.

---

## 4. North-star: the target end-user API

This is the shape every calculator converges on. Note the four moves: build an `Input` with
**designated initializers** + **mp-units literals**, call a **free** `calculate()`, branch on the
**`std::expected`** result, and extract the answer in **whatever unit you want** (`.in(...)`) — the
library never bakes in a presentation unit. (Conventions per [`06`](06-calculator-design-pattern.md) and
[`03`](03-quantities-and-units-mp-units.md).)

```c++
#include <emc/basic/skin_depth.hpp>     // one focused public header per calculator
#include <emc/materials/database.hpp>   // single source of truth for conductivity / mu_r

#include <mp-units/systems/si.hpp>
#include <print>

int main() {
    using namespace mp_units;
    using mp_units::si::unit_symbols::Hz;          // quantity literals: 27 * Hz, etc.

    // (1) Build the typed input with designated initializers + real units.
    //     Wrong units simply will not compile — no bare doubles, no magic factors.
    const emc::basic::SkinDepthInput in{
        .frequency    = 27 * Hz,
        .conductivity = emc::materials::nickel.conductivity,    // from the DB
        .mu_r         = emc::materials::nickel.relative_permeability,
    };

    // (2) Call the pure free function. Returns std::expected<Result, emc::Error>.
    //     It is [[nodiscard]] — you cannot accidentally drop the result or the error.
    const std::expected<emc::basic::SkinDepthResult, emc::Error> r =
        emc::basic::calculate(in);

    // (3) Handle both arms explicitly — a recoverable typed error, no integer sentinel.
    if (!r) {
        std::print("skin-depth calc failed: {}\n", r.error().message());   // emc::Error context
        return 1;
    }

    // (4) Pull the answer out in the unit the caller wants (presentation-unit-free core).
    std::print("skin depth = {}\n", r->skin_depth.in(si::micro<si::metre>));
    return 0;
}
```

A consumer that prefers monadic flow can chain instead of branching — same result type:

```c++
auto micrometres =
    emc::basic::calculate({ .frequency = 27 * Hz,
                            .conductivity = emc::materials::nickel.conductivity,
                            .mu_r = emc::materials::nickel.relative_permeability })
        .transform([](const emc::basic::SkinDepthResult& res) {
            return res.skin_depth.in(mp_units::si::micro<mp_units::si::metre>);
        });   // -> std::expected<quantity<...>, emc::Error>
```

> [!NOTE]
> The core is callable, testable, and unit-safe with no front end in sight. A generic consumer — for
> example one that prints results with `std::print` — maps inputs and outputs without pulling any
> GUI-toolkit dependency into the math.

---

## 5. Status legend

Implementation progress is tracked **per calculator** in the **Status** column of the
[`07-calculator-inventory.md`](07-calculator-inventory.md) catalog. Use these exact values:

| Status | Meaning |
| --- | --- |
| **TODO** | Not started. *All 51 calculators start here.* |
| **In-progress** | The header (`Input`/`Result`/`calculate`/`validate`/concept) and/or `.cpp` body exist, but the calculator is not yet proven against its reference vector. |
| **Done** | Pure `calculate()` implemented per [`06`](06-calculator-design-pattern.md), passing its reference vector ([`09`](09-testing-and-golden-vectors.md)). |

> [!IMPORTANT]
> **Scope counts** (from [`07`](07-calculator-inventory.md)): **51** math-bearing leaf calculators,
> each backed by hand-computed / textbook reference values used as GUI-free regression vectors.
> "Done" for the *program* means every row in 07 is **Done**.

---

## Cross-references

- Vision, scope, non-goals, success criteria: [`00-overview-and-goals.md`](00-overview-and-goals.md)
- Architecture, layout, dependency rules: [`01-architecture-and-layout.md`](01-architecture-and-layout.md)
- Modern C++ feature rationale (the centerpiece): [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md)
- Units subsystem (mp-units): [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md)
- Constants & materials single source of truth: [`04-constants-and-material-database.md`](04-constants-and-material-database.md)
- Error model & validation: [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md)
- The per-calculator pattern: [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md)
- Calculator catalog & status: [`07-calculator-inventory.md`](07-calculator-inventory.md)
- Build, install, export: [`08-build-system-cmake.md`](08-build-system-cmake.md)
- Testing strategy: [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md)
