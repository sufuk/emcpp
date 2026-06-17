# 🏛️ Architecture & Project Layout

> Purpose: define the layered architecture, directory/namespace layout, dependency rules, public/internal
> boundary, the calculator contract, and the ABI/versioning stance for the `emc` library — the structural
> skeleton every other plan document hangs off of.

---

## 0. 📖 Reading this document

This is the *structural* document for the `emc` library. It answers **where does code live, what may
depend on what, and what shape does each unit of the library take**. It deliberately stays shallow on
the *contents* of each piece — those are owned by sibling documents:

| If you want…                                  | Read…                                   |
|-----------------------------------------------|-----------------------------------------|
| Why each modern C++ feature is used           | `02-modern-cpp-feature-catalog.md`      |
| The units vocabulary on mp-units              | `03-quantities-and-units-mp-units.md`   |
| Constants & material database internals       | `04-constants-and-material-database.md` |
| `emc::Error` / `std::expected` mechanics       | `05-error-handling-and-validation.md`   |
| The full per-calculator pattern (deep dive)   | `06-calculator-design-pattern.md`       |
| The complete calculator catalog               | `07-calculator-inventory.md`            |
| CMake, the `EMC_API` macro def, install/export | `08-build-system-cmake.md`              |
| Testing strategy (known-value / property / constexpr) | `09-testing-and-golden-vectors.md` |

This document does not contain buildable source. The fenced blocks are *illustrative* and show the
intended shape of headers, namespaces, and signatures.

---

## 1. 🧱 Layered architecture

The library is organized into **three layers**. The arrows mean "may depend on" — they point *down*
the stack, and there are exactly two of them. There is **no upward dependency and no sideways dependency
between domain calculators.**

### 1.1 The three layers

**Foundation layer** — the shared vocabulary. Nothing here knows about any specific calculator.

- `emc::units` — the project's unit/quantity vocabulary, built **on top of mp-units** (type aliases,
  named references, dimension shortcuts, parsing helpers used only at boundaries). EMC inputs span Hz to
  GHz and metres to mils, so a single typed vocabulary makes unit handling type-safe and conversion
  automatic. See doc 03.
- `emc::constants` — one `constexpr` source of truth for physical constants (`c`, `mu_0`, `eps_0`,
  Planck, etc.), expressed as mp-units quantities. Physical constants must be exact and usable at compile
  time, so they live in a single header as `constexpr` mp-units quantities that every calculator shares.
  See doc 04.
- `emc::materials` — one `constexpr` material database (conductivity, relative permeability,
  resistivity, …) with a single representation. EMC calculations select a conductor or dielectric by
  name, so a single immutable table guarantees every calculator reads the same physical properties for a
  given material. See doc 04.
- `emc::Error` + `emc::ErrorCode` — the single error type returned (inside `std::expected`) by every
  fallible operation. A calculation can be called with out-of-domain geometry, so failures are made
  explicit and recoverable through `std::expected` rather than thrown or signalled out of band. See
  doc 05.

**Domain layer** — the calculators, grouped into category namespaces. Each calculator is a self-contained
`(Input, Result, calculate())` triple (see §5). A calculator depends *only* on the Foundation layer and
the standard library — **never on another calculator's namespace directly**. (A handful of calculators
legitimately reuse a Foundation-level helper such as skin depth; that helper is promoted into a shared
spot — see §1.4 — rather than letting `emc::component` reach into `emc::basic`.)

**Facade layer (optional, thin)** — convenience umbrella headers (`include/emc/emc.hpp`,
`include/emc/basic.hpp`, …) that re-include groups of calculators so a downstream user can write one
`#include`. The facade contains **no logic** — only `#include` directives and possibly a couple of
`using` re-exports. It exists purely for ergonomics and is allowed to depend on everything below it.

### 1.2 The dependency rule (the one invariant that must never break)

> [!IMPORTANT]
> The dependency direction is the hard architectural invariant of the entire library. It points **down**
> the stack and never up or sideways. Foundation depends only on the standard library and mp-units;
> Domain depends only on Foundation; Facade is include-only over Domain and Foundation. **No domain
> calculator may depend on another domain calculator's namespace.** Every other rule in this document is
> negotiable detail; this one is not.

```text
Foundation  ->  depends ONLY on: the C++23 standard library + mp-units
Domain      ->  depends ONLY on: Foundation (+ stdlib + mp-units)
Facade      ->  depends on: Domain + Foundation (include-only)
```

Enforcement is mechanical, not aspirational (detailed in docs 08 and 09):

- Domain headers `#include` only Foundation headers; a layering test (a tiny tool or `clang-tidy`
  `misc-include-cleaner` + a custom check) verifies no `src/<categoryA>/...` includes
  `include/emc/<categoryB>/...`.
- A CI check fails the build if any Domain translation unit pulls in a sibling category's public header,
  keeping the "no sideways domain dependency" rule honest over the life of the project.

### 1.3 Dependency graph

```text
graph TD
    subgraph EXTERNAL["External (3rd party)"]
        STD["C++23 standard library<br/>(expected, format, mdspan, cmath)"]
        MPU["mp-units<br/>(ISO 80000 quantities/units)"]
    end

    subgraph FOUND["Foundation layer — emc::* core vocabulary"]
        UNITS["emc::units<br/>(unit/quantity vocabulary)"]
        CONST["emc::constants<br/>(c, mu0, eps0, h, ...)"]
        MAT["emc::materials<br/>(conductivity, mu_r, rho)"]
        ERR["emc::Error / emc::ErrorCode"]
    end

    subgraph DOMAIN["Domain layer — calculators (9 category namespaces)"]
        BASIC["emc::basic"]
        CONV["emc::converter"]
        COMP["emc::component"]
        PRED["emc::prediction"]
        SHIELD["emc::shielding"]
        FILT["emc::filtering"]
        CABLE["emc::cabling"]
        GND["emc::grounding"]
        TEST["emc::testing"]
    end

    subgraph FACADE["Facade (optional, include-only)"]
        UMB["emc/emc.hpp + per-category umbrellas"]
    end

    UNITS --> MPU
    CONST --> UNITS
    CONST --> STD
    MAT --> UNITS
    MAT --> CONST
    ERR --> STD

    BASIC --> FOUND
    CONV --> FOUND
    COMP --> FOUND
    PRED --> FOUND
    SHIELD --> FOUND
    FILT --> FOUND
    CABLE --> FOUND
    GND --> FOUND
    TEST --> FOUND

    FOUND --> STD
    DOMAIN --> STD

    UMB --> DOMAIN
    UMB --> FOUND
```

ASCII fallback (same information, for environments without Mermaid):

```text
              +------------------------------------------------+
              |  C++23 stdlib            mp-units              |   (external)
              +------------------------------------------------+
                         ^                    ^
                         |                    |
   +------------------------------------------------------------------+
   | FOUNDATION:  emc::units --> mp-units                              |
   |              emc::constants --> emc::units, stdlib                |
   |              emc::materials --> emc::units, emc::constants        |
   |              emc::Error / emc::ErrorCode --> stdlib               |
   +------------------------------------------------------------------+
                         ^
                         | (Domain depends ONLY on Foundation)
   +------------------------------------------------------------------+
   | DOMAIN (calculators, no cross-calculator deps):                  |
   |   basic  converter  component  prediction  shielding             |
   |   filtering  cabling  grounding  testing                         |
   +------------------------------------------------------------------+
                         ^
                         | (include-only)
   +------------------------------------------------------------------+
   | FACADE:  emc/emc.hpp  +  per-category umbrella headers           |
   +------------------------------------------------------------------+
```

### 1.4 Where do shared physics helpers go?

Several calculators share a sub-computation — the clearest example is **skin depth**, which is both a
top-level `emc::basic` calculator *and* an internal step inside the AC-resistance computation for
standard-gauge wire (the AC-resistance branch reuses the skin-depth formula
`δ = 1 / √(π f μ σ)`). The rule:

- If a helper is a **public, user-meaningful calculation**, it stays a normal calculator (e.g.
  `emc::basic::skin_depth`) and other calculators call its public `calculate()` — this is *not* a
  forbidden cross-dependency because it goes through the documented public API, exactly like a downstream
  user would. We treat `emc::basic` skin depth as a de-facto Foundation primitive in this case.
- If a helper is a **private numeric kernel** with no standalone meaning, it lives in `emc::detail`
  (Foundation-adjacent) and is shared from there, so no domain namespace reaches sideways into another.

> [!TIP]
> Promoting a shared sub-computation into a public Foundation primitive (skin depth) or into
> `emc::detail` keeps the "no sideways domain dependency" rule intact while still letting calculators
> compose. Reach for `emc::detail` only when the kernel has no standalone physical meaning.

---

## 2. 🗂️ Directory & file layout

Target root: `/Users/sufuk/CLionProjects/emcpp`.

The layout follows the conventional **`include/` (public) + `src/` (compiled) split** required by the
locked decision "traditional compiled library". Public headers are installed; `src/` and `src`-local
headers are not.

```text
/Users/sufuk/CLionProjects/emcpp/
├── CMakeLists.txt                      # top-level project(); see doc 08
├── CMakePresets.json                   # configure/build/test presets; see doc 08
├── README.md                           # repo readme (NOT the plan index; that lives in plan/)
├── LICENSE
├── .clang-format / .clang-tidy         # style + the layering lint checks
│
├── cmake/                              # CMake support files (see doc 08)
│   ├── emc-config.cmake.in             # package config template for find_package(emc)
│   ├── emcConfigVersion handling       # generated; semver compatibility
│   ├── CompilerWarnings.cmake          # warnings-as-errors, sanitizers
│   └── Dependencies.cmake              # FetchContent / find_package(mp-units)
│
├── include/                            # PUBLIC headers — installed, this is the API surface
│   └── emc/
│       ├── emc.hpp                      # FACADE: includes everything (convenience)
│       ├── version.hpp                  # EMC_VERSION_MAJOR/MINOR/PATCH (generated; doc 08)
│       ├── export.hpp                   # EMC_API macro (generated by CMake; def in doc 08)
│       │
│       ├── core/                        # FOUNDATION public headers
│       │   ├── error.hpp                # emc::Error, emc::ErrorCode (doc 05)
│       │   ├── units.hpp                # emc::units vocabulary on mp-units (doc 03)
│       │   ├── constants.hpp            # emc::constants (constexpr quantities) (doc 04)
│       │   ├── materials.hpp            # emc::materials database (doc 04)
│       │   └── calculator.hpp           # the Calculator concept + shared traits (doc 06)
│       │
│       ├── basic/                       # DOMAIN: category 1 (Basic Calculations)
│       │   ├── basic.hpp                #   per-category facade umbrella
│       │   ├── antenna.hpp
│       │   ├── decibel.hpp
│       │   └── skin_depth.hpp
│       │
│       ├── converter/                   # DOMAIN: category 2 (Converter)
│       │   ├── converter.hpp
│       │   ├── antenna_factor_gain.hpp
│       │   ├── efield_power_density.hpp
│       │   ├── energy_frequency.hpp
│       │   ├── wavelength_frequency.hpp
│       │   └── vswr.hpp                  # VSWR / RC / RL / ML / TL family
│       │
│       ├── component/                   # DOMAIN: category 3 (Component Calculations)
│       │   ├── component.hpp
│       │   ├── capacitance.hpp
│       │   ├── inductance.hpp
│       │   ├── resistance.hpp           # incl. standard-gauge-wire calc
│       │   ├── microstrip_trace.hpp     # example public header path (see below)
│       │   ├── transmission_line.hpp
│       │   └── harmonic_trap.hpp
│       │
│       ├── prediction/                  # DOMAIN: category 4 (EMC Predictions)
│       │   ├── prediction.hpp
│       │   ├── esd_lightning_coupling.hpp
│       │   └── rf_field.hpp
│       │
│       ├── shielding/                   # DOMAIN: category 5 (Shielding)
│       │   ├── shielding.hpp
│       │   ├── cavity_resonance.hpp     # rectangular enclosure, resonant modes
│       │   └── shielding_effectiveness.hpp
│       │
│       ├── filtering/                   # DOMAIN: category 6 (Filtering)
│       │   ├── filtering.hpp
│       │   └── ferrite.hpp
│       │
│       ├── cabling/                     # DOMAIN: category 7 (Cabling)
│       │   ├── cabling.hpp
│       │   ├── braid_coverage.hpp
│       │   └── crosstalk.hpp
│       │
│       ├── grounding/                   # DOMAIN: category 8 (Grounding)
│       │   ├── grounding.hpp
│       │   └── microstrip_current.hpp
│       │
│       └── testing/                     # DOMAIN: category 9 (Testing)
│           ├── testing.hpp
│           └── noise_figure.hpp
│
├── src/                                # COMPILED implementation (.cpp) + src-local private headers
│   ├── core/
│   │   ├── error.cpp                    # message tables, formatting
│   │   ├── materials.cpp                # out-of-line tables if not fully constexpr
│   │   └── detail/                      # INTERNAL headers (NOT installed)
│   │       └── math_kernels.hpp         # emc::detail shared numeric helpers
│   ├── basic/
│   │   ├── antenna.cpp
│   │   ├── decibel.cpp
│   │   └── skin_depth.cpp
│   ├── converter/
│   │   ├── antenna_factor_gain.cpp
│   │   ├── efield_power_density.cpp
│   │   ├── energy_frequency.cpp
│   │   ├── wavelength_frequency.cpp
│   │   └── vswr.cpp
│   ├── component/
│   │   ├── capacitance.cpp
│   │   ├── inductance.cpp
│   │   ├── resistance.cpp
│   │   ├── microstrip_trace.cpp         # matching impl for the public header above
│   │   ├── transmission_line.cpp
│   │   └── harmonic_trap.cpp
│   ├── prediction/ ...                  # (same per-category shape)
│   ├── shielding/ ...
│   ├── filtering/ ...
│   ├── cabling/ ...
│   ├── grounding/ ...
│   └── testing/ ...
│
├── tests/                              # all tests; the ONLY place I/O is allowed (doc 09)
│   ├── CMakeLists.txt
│   ├── unit/                           # per-calculator known-value tests
│   ├── property/                       # property-based / invariant / round-trip tests
│   └── constexpr/                      # static_assert compile-time checks
│
├── tools/                             # optional dev tools (codegen) — NOT part of the library
│
├── docs/                              # generated/manual API docs (Doxygen, mdBook, etc.)
│
├── plan/                             # THIS PLAN (markdown only)
│   ├── README.md
│   ├── 00-overview-and-goals.md
│   ├── 01-architecture-and-layout.md   # <-- you are here
│   └── ... (02 .. 09)
│
└── third_party/                      # vendored deps ONLY if not using FetchContent
    └── (mp-units pulled via FetchContent by default — see doc 08)
```

### 2.1 The 9 categories: folders & namespaces

Each calculator category becomes one `include/emc/<dir>/`, one `src/<dir>/`, and one
`emc::<namespace>`. The nine categories follow the natural division of EMC engineering topics.

| #  | Category                | Library folder         | Namespace          | Representative leaves                                  |
|----|-------------------------|------------------------|--------------------|-------------------------------------------------------|
| 1  | Basic Calculations      | `…/basic/`             | `emc::basic`       | Antenna, Decibel, SkinDepth                            |
| 2  | Converter               | `…/converter/`         | `emc::converter`   | AF↔Gain, EField↔PowerDensity, Energy↔Freq, λ↔f, VSWR  |
| 3  | Component Calculations  | `…/component/`         | `emc::component`   | Capacitance, Inductance, Resistance, MicrostripTrace…  |
| 4  | EMC Predictions         | `…/prediction/`        | `emc::prediction`  | ESD/Lightning Coupling, RF Field                       |
| 5  | Shielding               | `…/shielding/`         | `emc::shielding`   | Cavity Resonance, Shielding Effectiveness             |
| 6  | Filtering               | `…/filtering/`         | `emc::filtering`   | Ferrite                                                |
| 7  | Cabling                 | `…/cabling/`           | `emc::cabling`     | Braid Optical Coverage, Crosstalk                     |
| 8  | Grounding               | `…/grounding/`         | `emc::grounding`   | Microstrip Line Current Distribution                  |
| 9  | Testing                 | `…/testing/`           | `emc::testing`     | Noise Figure                                          |

### 2.2 Example: one calculator's two files

Public header — `include/emc/component/microstrip_trace.hpp`:

```c++
// include/emc/component/microstrip_trace.hpp
#pragma once
#include <expected>
#include <emc/core/error.hpp>
#include <emc/core/units.hpp>      // emc::units quantity vocabulary (doc 03)
#include <emc/export.hpp>          // EMC_API (doc 08)

namespace emc::component {

// Which quantity we solve for. A microstrip can be solved for impedance or for any
// one of its physical dimensions, so the target is selected explicitly and the
// mm-vs-mils handling is left entirely to mp-units rather than to call-site branching.
enum class MicrostripSolveFor { impedance, height, thickness, width };

struct MicrostripTraceInput {
    emc::units::length              trace_width{};         // W
    emc::units::length              trace_thickness{};     // T
    emc::units::length              substrate_height{};    // H
    double                          relative_permittivity{4.0};
    emc::units::impedance           target_impedance{};    // Z0 (used when solving for a dimension)
    MicrostripSolveFor              solve_for{MicrostripSolveFor::impedance};
};

struct MicrostripTraceResult {
    emc::units::impedance           characteristic_impedance{};
    // plus whichever dimension was solved for, etc.
};

[[nodiscard]] EMC_API
std::expected<MicrostripTraceResult, emc::Error>
calculate(const MicrostripTraceInput& in);

[[nodiscard]] EMC_API
std::expected<void, emc::Error>
validate(const MicrostripTraceInput& in);   // permittivity 1..15, W/H 0.1..3, etc.

}  // namespace emc::component
```

Matching implementation — `src/component/microstrip_trace.cpp`:

```c++
// src/component/microstrip_trace.cpp
#include <emc/component/microstrip_trace.hpp>
#include <emc/core/constants.hpp>
#include "../core/detail/math_kernels.hpp"   // src-local INTERNAL header (not installed)

namespace emc::component {

std::expected<void, emc::Error>
validate(const MicrostripTraceInput& in) { /* range checks -> emc::Error */ }

std::expected<MicrostripTraceResult, emc::Error>
calculate(const MicrostripTraceInput& in) {
    if (auto ok = validate(in); !ok) return std::unexpected(ok.error());
    // single formula path; unit conversion handled by mp-units
    ...
}

}  // namespace emc::component
```

The two files share the **same relative path** (`component/microstrip_trace`) under `include/emc/` and
`src/` respectively. This 1:1 convention makes navigation trivial and lets CMake glob/list predictably.

---

## 3. 🏷️ Namespace design

All public symbols live under the top-level `emc` namespace. The full table:

| Namespace             | Layer       | Lives in                                | Contents                                                                                 |
|-----------------------|-------------|-----------------------------------------|------------------------------------------------------------------------------------------|
| `emc`                 | top         | everywhere                              | `Error`, `ErrorCode`, facade re-exports, version constants                                |
| `emc::units`          | Foundation  | `include/emc/core/units.hpp`            | project quantity/unit vocabulary on mp-units: `length`, `frequency`, `impedance`, `conductivity`, parse helpers (doc 03) |
| `emc::constants`      | Foundation  | `include/emc/core/constants.hpp`        | `constexpr` physical constants as mp-units quantities: `c`, `mu_0`, `eps_0`, `h`, `eta_0` (doc 04) |
| `emc::materials`      | Foundation  | `include/emc/core/materials.hpp`        | `constexpr` material DB: per-material conductivity, `mu_r`, resistivity; one representation (doc 04) |
| `emc::detail`         | internal    | `src/.../detail/*.hpp` (not installed)  | private numeric kernels, implementation helpers; **not** part of the API contract         |
| `emc::basic`          | Domain      | `include/emc/basic/`                    | category 1 calculators                                                                    |
| `emc::converter`      | Domain      | `include/emc/converter/`                | category 2 calculators                                                                    |
| `emc::component`      | Domain      | `include/emc/component/`                | category 3 calculators                                                                    |
| `emc::prediction`     | Domain      | `include/emc/prediction/`               | category 4 calculators                                                                    |
| `emc::shielding`      | Domain      | `include/emc/shielding/`                | category 5 calculators                                                                    |
| `emc::filtering`      | Domain      | `include/emc/filtering/`                | category 6 calculators                                                                    |
| `emc::cabling`        | Domain      | `include/emc/cabling/`                  | category 7 calculators                                                                    |
| `emc::grounding`      | Domain      | `include/emc/grounding/`                | category 8 calculators                                                                    |
| `emc::testing`        | Domain      | `include/emc/testing/`                  | category 9 calculators                                                                    |

### 3.1 Conventions inside namespaces

- Every calculator exposes a **`calculate`** free function in its category namespace. Two calculators in
  the same namespace each define their own `Input`/`Result` structs (e.g.
  `emc::component::MicrostripTraceInput`, `emc::component::CapacitanceInput`), so the descriptive struct
  names keep `calculate` overloads unambiguous via ADL on the argument type.
- `emc::detail` is the *only* place internal symbols live, and `detail` headers live under `src/` so they
  are physically un-installable. A symbol in `emc::detail` is **never** part of the stable API and may
  change in any patch release.
- `emc::units` is a **vocabulary**, not a units library — it re-uses mp-units and adds project-specific
  named aliases (so call sites read `emc::units::frequency` instead of a raw
  `mp_units::quantity<...>`). The single source for unit semantics remains mp-units (locked decision).

### 3.2 `using namespace` policy

> [!CAUTION]
> Library **headers** must never do `using namespace`, or the alias leaks into every consumer translation
> unit and silently changes name lookup at their call sites.

Implementation `.cpp` files may use a file-local `using namespace mp_units;` (and the relevant
`mp_units::si`/`mp_units::isq`) for readability, because that is confined to a translation unit and
cannot leak to consumers.

---

## 4. 🚪 Public vs internal boundary

### 4.1 The boundary rule

```text
include/emc/**        -> PUBLIC. Installed. Part of the semver contract. Stable.
include/emc/.../detail (if any) -> "soft-internal": installed but documented as unstable.
src/**  (incl. src/.../detail/*.hpp) -> PRIVATE. Never installed. Free to change anytime.
```

Preferred convention: put truly internal headers under **`src/.../detail/`** (e.g.
`src/core/detail/math_kernels.hpp`) so they cannot be installed at all. Reserve an installed
`include/emc/detail/` only for the rare case where a public *inline/template* must reference an internal
helper that therefore has to ship in a header.

### 4.2 When to use PImpl

The calculator pattern is **free functions over POD-ish aggregate structs** (§5). There is *no class
with a vtable or hidden state* in the normal case, so the classic motivation for PImpl (hide members,
stabilize ABI of a class) mostly does not apply. Use PImpl only in these narrow situations:

- A calculator must hold **expensive precomputed state** across many calls (e.g. a cached factorization
  or interpolation table). Then wrap it in a small class with a `std::unique_ptr<Impl>` so the heavy
  members (and any mp-units template instantiations they imply) stay out of the public header.
- You need to keep a **large mp-units template type out of the ABI surface**. Because mp-units quantity
  types are heavily templated, exposing them in a class's data members bloats compile times and freezes
  template details into the ABI. PImpl (or simply keeping the computation in the `.cpp`) avoids that.

For the overwhelming majority of calculators, **no PImpl is needed** — the aggregate `Input` and
`Result` structs cross the boundary by value, and all the mp-units-heavy math lives in the `.cpp`.

### 4.3 The `EMC_API` export macro

Every public, non-inline, non-template function/class that is part of the API is annotated with
`EMC_API`. It expands to the platform's symbol-visibility / DLL import-export keyword:

```c++
// include/emc/export.hpp  (this file is GENERATED by CMake's generate_export_header — see doc 08)
#ifndef EMC_API
#  ifdef EMC_STATIC_DEFINE
#    define EMC_API
#  else
#    ifdef emc_EXPORTS          // defined by CMake when BUILDING the shared lib
#      define EMC_API __declspec(dllexport)   /* or [[gnu::visibility("default")]] */
#    else
#      define EMC_API __declspec(dllimport)   /* or empty on ELF/Mach-O */
#    endif
#  endif
#endif
```

> [!NOTE]
> The **authoritative** definition (and the CMake `generate_export_header` invocation that produces it,
> plus `-fvisibility=hidden` defaults) belongs to **doc 08 — Build System**. Here we only fix the
> *convention*: public exported entities are marked `EMC_API`; `inline`/`template`/`constexpr` entities
> that are fully defined in headers are **not** marked (they have no out-of-line symbol to export). The
> aggregate `Input`/`Result` structs are header-only data definitions and need no `EMC_API`; the
> out-of-line `calculate()`/`validate()` functions **do** get `EMC_API`.

---

## 5. 📐 The Calculator contract (architectural shape)

At the architectural level, every calculator is the **same shape** — a triple plus optional validation,
satisfying a shared `Calculator` concept. This uniformity is what lets generic tooling treat all
calculators interchangeably.

```c++
// The canonical shape (per calculator). Full deep-dive: doc 06.
namespace emc::basic {

struct SkinDepthInput {
    emc::units::frequency           frequency{};
    emc::materials::Material        material{emc::materials::Material::copper};
    // members may carry sensible defaults so call sites use designated initializers:
    //   skin_depth({.frequency = 27 * MHz, .material = Material::nickel});
};

struct SkinDepthResult {
    emc::units::length              depth{};
};

[[nodiscard]] EMC_API
std::expected<void, emc::Error>          validate(const SkinDepthInput&);

[[nodiscard]] EMC_API
std::expected<SkinDepthResult, emc::Error> calculate(const SkinDepthInput&);

}  // namespace emc::basic
```

And the `Calculator` concept (defined once in `include/emc/core/calculator.hpp`) that the
`(Input, Result, calculate)` triple must satisfy:

```c++
// include/emc/core/calculator.hpp
#pragma once
#include <expected>
#include <concepts>
#include <emc/core/error.hpp>

namespace emc {

template <class Input, class Result>
concept Calculator = requires (const Input& in) {
    // calculate(Input) -> std::expected<Result, emc::Error>  (found via ADL)
    { calculate(in) } -> std::same_as<std::expected<Result, emc::Error>>;
};

}  // namespace emc
```

Why a concept at the architectural level: it lets generic test harnesses and batch drivers (doc 09)
operate over *any* calculator uniformly, and it gives a compile-time contract so a malformed calculator
fails to compile rather than silently diverging. The **deep treatment** of the pattern (designated
initializers, member defaults, `validate` composition, the concept's exact form, ADL nuances) is owned
by **doc 06**; this section only fixes the architectural invariant: *every calculator is
`(Input, Result, calculate) [+ validate]` returning `std::expected<…, emc::Error>`, and nothing else.*

---

## 6. 🔁 Cross-cutting concerns

These properties hold uniformly across the whole library and are part of its architecture, not any one
calculator.

### 6.1 Error type placement

`emc::Error` and `emc::ErrorCode` live in the **Foundation** layer (`include/emc/core/error.hpp`) so
every calculator can return them without creating an upward or sideways dependency. There is exactly
**one** error type for the whole library — no per-category error enums. Because an EMC calculation can be
invoked with out-of-domain geometry or a divide-by-zero input, failures travel back through the value
channel as `std::expected<…, emc::Error>` rather than through exceptions or sentinel return values, which
keeps the failure explicit at every call site and recoverable by the caller.

Mechanics (codes, context payload, formatting via `std::format`) are owned by **doc 05**.

### 6.2 Units vocabulary placement

`emc::units` is Foundation (`include/emc/core/units.hpp`) and is the **only** vocabulary domain code uses
for dimensional quantities. No calculator stores a unit "as a bare double + a remembered factor". EMC
inputs naturally span Hz to GHz and metres to mils, so representing every quantity as a typed mp-units
value makes conversion a compile-checked operation on the type, performed **once, at the application
boundary**. Details: doc 03.

> [!WARNING]
> Storing a physical quantity as a bare `double` plus a separately-remembered scale factor is the
> classic source of unit-mismatch errors (mixing Hz and MHz, mm and mils). The typed `emc::units`
> vocabulary makes such a mismatch a **compile error**, not a silent numeric one.

### 6.3 Thread-safety stance

Every `calculate()` and `validate()` is a **pure function**: it reads only its `const Input&` argument,
writes only its returned `Result`/`Error`, and touches **no global mutable state**. Consequently:

- All calculators are **reentrant and thread-safe by construction** — any number of threads may call any
  `calculate()` concurrently with no locking.
- Constants and the material database are `constexpr`/immutable, so they are safe to read from any thread.
- There are no singletons, no lazily-initialized global caches in the core, no `static` mutable locals.

This is a hard architectural rule; a calculator that needs caching uses a *caller-owned* object
(see §4.2 PImpl), never a hidden global.

### 6.4 No I/O in the core

The library performs **no I/O**: no file reading, no `std::cout`/`std::print` from inside `calculate()`,
no environment access, no logging side-channels. In particular, **any data loading lives in `tests/`**
(the test harness) and optionally `tools/`. The core is a pure function library; keeping it free of
filesystem and formatting dependencies makes it trivially embeddable and trivially testable.

---

## 7. 📦 ABI / versioning note (compiled library)

Because this is a **traditional compiled library** (locked decision), it has an ABI surface that
downstream binaries link against. The versioning stance:

### 7.1 Semantic versioning

- The library version is **semver** (`MAJOR.MINOR.PATCH`), exposed via `include/emc/version.hpp`
  (`EMC_VERSION_MAJOR/MINOR/PATCH`, generated by CMake — doc 08) and surfaced to consumers through the
  CMake package version file so `find_package(emc 1.2)` enforces compatibility.
- **MAJOR**: breaking API/ABI change (changed `calculate` signature, removed/renamed symbol, changed an
  `Input`/`Result` struct layout in an incompatible way).
- **MINOR**: additive (new calculator, new optional struct member appended with a default, new overload).
- **PATCH**: bug fixes that do not change the API/ABI — including refinements to numerical output (e.g.
  tightening a constant or correcting an intermediate rounding) that change *numbers*, not *signatures*.
  The known-value and property tests (doc 09) capture these so a refinement is a deliberate, reviewed
  change.

### 7.2 The export header is the ABI boundary

`include/emc/export.hpp` (the `EMC_API` macro, §4.3) plus `-fvisibility=hidden` defaults mean the ABI is
*exactly* the set of `EMC_API`-marked symbols. Everything else (`emc::detail`, file-local statics,
inline/template instantiations) is **not** part of the ABI and may change freely. Keeping the export
surface small and explicit is what makes the semver promise enforceable.

### 7.3 Keep heavy mp-units templates inside `.cpp`

mp-units quantity types are deeply templated. Two consequences drive an architectural rule:

1. **ABI fragility** — if a quantity type appears in an exported class's data members or in an exported
   *non-template* function's mangled signature, then a change in mp-units' template details can silently
   break ABI.
2. **Compile-time cost** — instantiating those templates in every consumer TU is expensive.

Mitigation (architectural, enforced by convention):

- The **public** signature of `calculate()` takes/returns the project's `emc::units` aliases (themselves
  mp-units types) — this is unavoidable and desired (compile-checked units). But the *implementation*
  (where the bulk of mp-units arithmetic and intermediate quantity types proliferate) is **out-of-line in
  the `.cpp`**, so the heavy instantiations are compiled **once** in the library, not in every consumer.
- Prefer **free functions over the aggregate structs** (no exported templated members) so the ABI surface
  is a handful of plain `EMC_API` function symbols, not a forest of template specializations.
- Where a class genuinely must hold mp-units-typed state, use **PImpl** (§4.2) to keep those types out of
  the public header and the ABI.

> [!NOTE]
> Forward-looking (C++26 / C++ modules): a future iteration could ship `emc` as C++ modules to cut the
> template recompilation cost further. The plan targets **headers + `.cpp`** for now (locked decision);
> modules are noted only as a later option (doc 08 expands on this).

---

## Cross-references

- `00-overview-and-goals.md` — vision, scope, non-goals, glossary, success criteria.
- `02-modern-cpp-feature-catalog.md` — every modern C++ feature, with example and domain rationale.
- `03-quantities-and-units-mp-units.md` — the `emc::units` vocabulary and mp-units adoption.
- `04-constants-and-material-database.md` — `emc::constants` and `emc::materials` single sources of truth.
- `05-error-handling-and-validation.md` — `emc::Error` / `emc::ErrorCode` and `std::expected`.
- `06-calculator-design-pattern.md` — the full per-calculator pattern + worked examples.
- `07-calculator-inventory.md` — the calculator catalog and library mapping.
- `08-build-system-cmake.md` — CMake compiled lib, the `EMC_API`/`version.hpp` definitions, install/export.
- `09-testing-and-golden-vectors.md` — testing strategy: known-value, property, and constexpr tests.
