# EMC Library (`emc`) — Conversion Plan Index

> The front page of the implementation plan that converts the **NinjaEMC** Qt desktop/mobile
> application (`emc-prediction`) into a clean, Qt-free, reusable modern-C++ library named **`emc`**.
> Start here, then follow one of the reading orders below.

---

## 1. Purpose

This folder (`/Users/sufuk/CLionProjects/emcpp/plan/`) is the complete, multi-document **implementation
plan** for extracting the electromagnetic-compatibility (EMC) engineering math out of the existing
Qt-bound NinjaEMC app and re-homing it in a standalone, modern-C++ library called **`emc`** (living at
`/Users/sufuk/CLionProjects/emcpp`). Today the ~52 leaf calculators (44 with real math) are *welded* to
the UI: every formula lives inside a `clicked` lambda in a widget constructor, reads `ui->spinbox->value()`,
and writes `ui->result->setValue()` — there is no separable core. The plan describes how to lift that math
into pure, dimensionally-typed, `std::expected`-returning free functions with **zero** Qt dependency, so
the existing app can later become a thin *consumer* that calls `find_package(emc)` / links `emc::emc`.

These are **planning documents, not source code.** They contain extensive *illustrative* code (in fenced
blocks) showing exactly how each piece should be built and **why** each modern-C++ feature was chosen
against a concrete NinjaEMC pain point — but nothing here is meant to be compiled as-is. The real
`include/emc/*` headers and `src/*.cpp` are written during the migration described in
[`10-migration-roadmap.md`](10-migration-roadmap.md).

---

## 2. Locked decisions (do not contradict)

These choices are fixed for the whole plan. Every document and every code example obeys them; if a doc
ever seems to disagree, this box wins.

| Area | Decision |
| --- | --- |
| **C++ standard** | Baseline **C++23** for all example code — `std::expected`, `std::print`/`std::format`, `std::mdspan`, deducing-`this`, `constexpr <cmath>`. Clearly-marked **C++26 forward-looking** callouts (reflection, contracts, `std::execution`/senders, pattern matching) where relevant. |
| **Units** | Adopt **mp-units** (the ISO/IEC 80000, standardization-track quantities/units library) for *all* dimensional quantities. **No hand-rolled units system.** mp-units is the single representation of physical quantities and the single mechanism for conversions. |
| **Library form** | A **traditional compiled library**: public headers in `include/emc/`, compiled `.cpp` in `src/`, built into static and/or shared `emc` via CMake, with `install()`/`export()` + a package config so downstream code can `find_package(emc)` and link **`emc::emc`**. *Not* header-only, *not* modules-first (modules are mentioned only as a future option). |
| **Namespaces** | Top-level `emc`. Sub-namespaces: `emc::constants`, `emc::materials`, `emc::units`, `emc::detail`. Calculators grouped by category namespaces mirroring the app: `emc::basic`, `emc::converter`, `emc::component`, `emc::prediction`, `emc::shielding`, `emc::filtering`, `emc::cabling`, `emc::grounding`, `emc::testing`. |
| **Errors** | One error type **`emc::Error`** (`enum class emc::ErrorCode` + context). Fallible operations return **`std::expected<Result, emc::Error>`** and are marked **`[[nodiscard]]`**. No exceptions for domain failures, no `QMessageBox`, no `EXIT_FAILURE` sentinels. |
| **Calculator shape** | Each calculator is a **free function** (not a class): an aggregate `Input` struct (designated initializers, member defaults), a `Result` struct, `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, an optional `validate(const Input&) -> std::expected<void, Error>`, and a `Calculator` concept the triple satisfies. |
| **Quantities** | mp-units types only (e.g. `quantity<isq::frequency[si::hertz]>`). Units never live as bare `double`; conversions are compile-checked and zero-cost. |
| **Constants** | `constexpr` in `emc::constants`, expressed as mp-units quantities where physical. One source of truth — kills `#define PI 3.14`, the 8+ re-definitions of `mu0`, etc. |
| **Purity** | Pure functions, no global mutable state, thread-safe by construction. |

---

## 3. Document map

Every plan file, what it actually covers (summaries built from reading each document's H1 + opening),
and a suggested reading order for two audiences.

| # | File | What it covers |
| --- | --- | --- |
| — | [`README.md`](README.md) | **This index** — purpose, locked decisions, document map, reading orders, the target end-user API, and the status legend. |
| 00 | [`00-overview-and-goals.md`](00-overview-and-goals.md) | The front door: *why* extract a Qt-free, type-safe EMC library out of NinjaEMC, scope, non-goals, before/after, glossary, success criteria, and how to read the plan. |
| 01 | [`01-architecture-and-layout.md`](01-architecture-and-layout.md) | The structural skeleton: layered architecture, directory/namespace layout, dependency rules, public/internal boundary, the calculator contract, and the ABI/versioning stance. |
| 02 | [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) | **The centerpiece** — an example-driven catalog of every modern C++ feature the library uses, each justified against a numbered NinjaEMC pain point, with BEFORE (Qt) → AFTER (`emc`) code. |
| 03 | [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) | The mp-units subsystem: how every physical quantity and conversion is represented, replacing the ~541 hand-wired `addItem(unit, factor)` conversions, if/else unit chains, and scattered magic factors. |
| 04 | [`04-constants-and-material-database.md`](04-constants-and-material-database.md) | Exactly **one** `constexpr` place for every physical constant and material property, and how the ~14 widgets that hard-code (and contradict) these values consume it instead. |
| 05 | [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md) | The single Qt-free error model (`ErrorCode` + `Error` + `std::expected`) and a declarative validation layer replacing `QMessageBox`-in-the-math, value-less `return`, and `EXIT_FAILURE`-as-a-number. |
| 06 | [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) | **The template you copy 52 times** — the `Input`/`Result`/`calculate()`/`validate()`/`Calculator`-concept shape every calculator takes, with two full worked Qt→`emc` conversions. |
| 07 | [`07-calculator-inventory.md`](07-calculator-inventory.md) | **The master work-list** — the source-verified catalog of all calculators (inputs, units, formulas, solve directions, validation, materials, CSV fixtures, complexity) mapped to their target library locations, each with a migration **Status**. |
| 08 | [`08-build-system-cmake.md`](08-build-system-cmake.md) | The complete modern-CMake compiled-library build: mp-units dependency acquisition, shared-lib symbol visibility, `install()`/`export()` + package config for `find_package(emc)`, presets, quality tooling, and how the Qt app rewires onto it. |
| 09 | [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) | Turning the 52 UI-coupled CSV fixtures into a fast, deterministic, Qt-free regression suite for the pure `calculate()` functions, layered with compile-time, property, and CI checks. |
| 10 | [`10-migration-roadmap.md`](10-migration-roadmap.md) | **The execution plan** — sequences the conversion into discrete, verifiable phases with deliverables, exit criteria, effort, risks, and dependencies, and defines when the program is *done*. |

### Suggested reading order

**(a) Decision-maker / reviewer** — *do we approve this rewrite, and is the plan sound?*

> **00** (vision, scope, success criteria) → **01** (what the thing looks like) → **02** §intro
> (skim the modern-C++ value story) → **07** (the work-list — scope & size) → **10** (phasing,
> effort, risk, definition of done). *Optional:* **09** to gauge how equivalence is proven.

**(b) Implementer** — *I'm building this; what do I do, in what order?*

> **00** → **01** → **02** (read in full — it is referenced everywhere) → **03** (units vocabulary)
> → **04** (constants & materials) → **05** (error model) → **06** (the per-calculator template) →
> **07** (pick a calculator and its row) → **08** (get it building) → **09** (prove it with golden
> vectors) → **10** (follow the phase sequence and exit gates). Then loop **06 → 07 → 09** once per
> calculator.

---

## 4. North-star: the target end-user API

This is the shape every calculator converges on. Note the four moves: build an `Input` with
**designated initializers** + **mp-units literals**, call a **free** `calculate()`, branch on the
**`std::expected`** result, and extract the answer in **whatever unit you want** (`.in(...)`) — the
library never bakes in a presentation unit. (Conventions per [`06`](06-calculator-design-pattern.md) and
[`03`](03-quantities-and-units-mp-units.md).)

```cpp
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
        .conductivity = emc::materials::nickel.conductivity,    // from the DB, not inline if/else
        .mu_r         = emc::materials::nickel.relative_permeability,
    };

    // (2) Call the pure free function. Returns std::expected<Result, emc::Error>.
    //     It is [[nodiscard]] — you cannot accidentally drop the result or the error.
    const std::expected<emc::basic::SkinDepthResult, emc::Error> r =
        emc::basic::calculate(in);

    // (3) Handle both arms explicitly — no QMessageBox, no EXIT_FAILURE sentinel.
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

```cpp
auto micrometres =
    emc::basic::calculate({ .frequency = 27 * Hz,
                            .conductivity = emc::materials::nickel.conductivity,
                            .mu_r = emc::materials::nickel.relative_permeability })
        .transform([](const emc::basic::SkinDepthResult& res) {
            return res.skin_depth.in(mp_units::si::micro<mp_units::si::metre>);
        });   // -> std::expected<quantity<...>, emc::Error>
```

Contrast with today's NinjaEMC, where the *entire* skin-depth "calculator" is trapped inside a button
lambda in `SkinDepthWidget.cpp` (lines ~64–72), reading spin boxes and writing a result box, using a
locally re-declared `qreal mu0 = 4 * M_PI * 1e-7;` (one of 8+ copies) and an inline `if/else` material
table. The north-star API above is callable, testable, and unit-safe with **no widget in sight** —
see [`06`](06-calculator-design-pattern.md) for the full before/after.

---

## 5. Status legend

Migration progress is tracked **per calculator** in the **Status** column of the
[`07-calculator-inventory.md`](07-calculator-inventory.md) work-list and rolls up into the phase exit
gates of [`10-migration-roadmap.md`](10-migration-roadmap.md). Use these exact values:

| Status | Meaning |
| --- | --- |
| **TODO** | Not started. The math still lives only inside the Qt widget. *All 51 calculators start here.* |
| **In-progress** | The header (`Input`/`Result`/`calculate`/`validate`/concept) and/or `.cpp` body exist, but the calculator is not yet proven against its golden vector. |
| **Done** | Pure `calculate()` implemented per [`06`](06-calculator-design-pattern.md), passing its CSV golden vector ([`09`](09-testing-and-golden-vectors.md)) — or *intentionally divergent* with a documented re-blessing where an old NinjaEMC bug (e.g. `PI = 3.14`, `c = 300000000.0`) made the legacy output wrong. |

**Scope counts** (from [`07`](07-calculator-inventory.md)): **51** math-bearing leaf calculators to port;
**21** navigation-only widgets (pure `QStackedWidget` index switchers, no domain logic) that are **not
ported** and instead become consumer-side UI; **51** golden CSV fixtures reused as Qt-free regression
vectors. "Done" for the *program* means every row in 07 is **Done** and every roadmap phase in 10 has met
its exit criteria (its **definition of done**).

---

## Cross-references

- Vision, scope, non-goals, success criteria: [`00-overview-and-goals.md`](00-overview-and-goals.md)
- Architecture, layout, dependency rules: [`01-architecture-and-layout.md`](01-architecture-and-layout.md)
- Modern C++ feature rationale (the centerpiece): [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md)
- Units subsystem (mp-units): [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md)
- Constants & materials single source of truth: [`04-constants-and-material-database.md`](04-constants-and-material-database.md)
- Error model & validation: [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md)
- The per-calculator pattern: [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md)
- The master work-list & status: [`07-calculator-inventory.md`](07-calculator-inventory.md)
- Build, install, export: [`08-build-system-cmake.md`](08-build-system-cmake.md)
- Testing & golden vectors: [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md)
- Phasing, effort, risk, definition of done: [`10-migration-roadmap.md`](10-migration-roadmap.md)
