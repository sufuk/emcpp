# EMC Library (`emc`) — Plan Index ⚡

> Front page of the implementation plan for **`emc`**, a clean, GUI-free, reusable modern-C++ library
> for electromagnetic-compatibility (EMC) math. Start here, then pick a reading order below.

---

## 1. Purpose

This folder is the multi-document **implementation plan** for `emc`, a standalone modern-C++ EMC library
at `/Users/sufuk/CLionProjects/emcpp`. It provides ~51 leaf calculators as pure, dimensionally-typed,
`std::expected`-returning free functions with **zero GUI dependency**, so any front end is a thin
*consumer* that calls `find_package(emc)` and links `emc::emc`.

These are **planning documents, not source code.** The real `include/emc/*` headers and `src/*.cpp` are
written by following them; the detailed worked code lives in the `implementation/` guides.

---

## 2. Locked decisions (do not contradict)

Fixed for the whole plan. If a doc ever seems to disagree, this box wins.

| Area | Decision |
| --- | --- |
| **C++ standard** | Baseline **C++23** (`std::expected`, `std::print`/`std::format`, `std::mdspan`, deducing-`this`, `constexpr <cmath>`). **C++26 forward-looking** callouts (reflection, contracts, senders, pattern matching) where relevant. |
| **Units** | **mp-units** (ISO/IEC 80000 quantities/units library) for *all* dimensional quantities. No hand-rolled units; mp-units is the single representation and conversion mechanism. |
| **Library form** | **Traditional compiled library**: headers in `include/emc/`, `.cpp` in `src/`, built static/shared via CMake with `install()`/`export()` + package config for `find_package(emc)` → link `emc::emc`. Not header-only, not modules-first. |
| **Namespaces** | Top-level `emc`; `emc::constants`, `emc::materials`, `emc::units`, `emc::detail`. Calculators grouped by category: `basic`, `converter`, `component`, `prediction`, `shielding`, `filtering`, `cabling`, `grounding`, `testing`. |
| **Errors** | One error type **`emc::Error`** (`enum class ErrorCode` + context). Fallible ops return **`std::expected<Result, emc::Error>`**, marked **`[[nodiscard]]`**. No exceptions for domain failures, no integer sentinels. |
| **Calculator shape** | Each calculator is a **free function**: aggregate `Input` struct (designated initializers, defaults), `Result` struct, `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, optional `validate(...) -> std::expected<void, Error>`, and a `Calculator` concept the triple satisfies. |
| **Quantities** | mp-units types only (e.g. `quantity<isq::frequency[si::hertz]>`). Never bare `double`; conversions compile-checked and zero-cost. |
| **Constants** | `constexpr` in `emc::constants`, as mp-units quantities where physical. One source of truth for `mu0`, `c`, etc. |
| **Purity** | Pure functions, no global mutable state, thread-safe by construction. |

---

## 3. Document map

| # | File | What it covers |
| --- | --- | --- |
| — | [`README.md`](README.md) | **This index** — purpose, locked decisions, document map, reading orders, target API, status legend. |
| 00 | [`00-overview-and-goals.md`](00-overview-and-goals.md) | *Why* a type-safe, GUI-free EMC library: scope, non-goals, glossary, success criteria. |
| 01 | [`01-architecture-and-layout.md`](01-architecture-and-layout.md) | Layered architecture, directory/namespace layout, dependency rules, public/internal boundary, calculator contract, ABI/versioning. |
| — | [`implementation/`](implementation/) | **Per-calculator implementation guides** (header + `.cpp` + tests for all 51 calculators), built on [`implementation/00-foundation-code.md`](implementation/00-foundation-code.md) — the units, constants, materials, error model, and the calculator pattern, *as actual code*. |
| 07 | [`07-calculator-inventory.md`](07-calculator-inventory.md) | **Calculator catalog & library map** — all calculators (inputs, units, formulas, solve directions, validation, materials, complexity) and their target locations. |
| 08 | [`08-build-system-cmake.md`](08-build-system-cmake.md) | The modern-CMake build: mp-units acquisition, symbol visibility, `install()`/`export()` + package config, presets, tooling. |
| 09 | [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) | **Testing strategy** — fast, deterministic, GUI-free regression suite layered with compile-time, property, and CI checks. |

### Suggested reading order

- **Decision-maker / reviewer** — *is the plan sound?*
  **00** (vision, scope, success) → **01** (shape) → **07** (catalog scope & size). *Optional:* **09**.
- **Implementer** — *what do I build, in what order?*
  **00** → **01** → [`implementation/00-foundation-code.md`](implementation/00-foundation-code.md) (units,
  constants, materials, errors, the calculator pattern — *as code*) → **07** (pick a calculator) → its
  [`implementation/`](implementation/) guide → **08** (build) → **09** (prove). Loop the last three per calculator.

---

## 4. North-star: the target end-user API

Every calculator converges on four moves: build an `Input` with **designated initializers** +
**mp-units literals**, call a **free** `calculate()`, branch on the **`std::expected`** result, and
extract the answer in **any unit** (`.in(...)`) — the core never bakes in a presentation unit.
The shared foundation code (units, constants, error model, the calculator pattern) lives in [`implementation/00-foundation-code.md`](implementation/00-foundation-code.md).

```cpp
#include <emc/basic/skin_depth.hpp>
#include <emc/materials/database.hpp>
#include <mp-units/systems/si.hpp>
#include <print>

int main() {
    using mp_units::si::unit_symbols::Hz;

    const emc::basic::SkinDepthInput in{
        .frequency    = 27 * Hz,                                  // wrong units won't compile
        .conductivity = emc::materials::nickel.conductivity,     // from the DB
        .mu_r         = emc::materials::nickel.relative_permeability,
    };

    const std::expected<emc::basic::SkinDepthResult, emc::Error> r =
        emc::basic::calculate(in);                               // [[nodiscard]] free function

    if (!r) {
        std::print("skin-depth calc failed: {}\n", r.error().message());
        return 1;
    }
    std::print("skin depth = {}\n", r->skin_depth.in(mp_units::si::micro<mp_units::si::metre>));
    return 0;
}
```

A consumer preferring monadic flow can `.transform(...)` the same result type instead of branching.

> [!NOTE]
> The core is callable, testable, and unit-safe with no front end in sight. A generic consumer maps
> inputs and outputs without pulling any GUI-toolkit dependency into the math.

---

## 5. Status legend

Progress is tracked **per calculator** against the catalog in
[`07-calculator-inventory.md`](07-calculator-inventory.md) (one row per calculator). Use these exact values:

| Status | Meaning |
| --- | --- |
| **TODO** | Not started. *All 51 calculators start here.* |
| **In-progress** | Header and/or `.cpp` exist, but not yet proven against the reference vector. |
| **Done** | Pure `calculate()` implemented per its [`implementation/`](implementation/) guide, passing its reference vector ([`09`](09-testing-and-golden-vectors.md)). |

> [!IMPORTANT]
> **51** math-bearing leaf calculators, each backed by hand-computed / textbook reference values used as
> GUI-free regression vectors. "Done" for the *program* means every row in **07** is **Done**.

---

**Cross-references:** every plan file is linked once in the document map (§3).
