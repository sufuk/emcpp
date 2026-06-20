# 🏛️ Architecture & Project Layout

> The *structural* skeleton of the `emc` library: where code lives, what may depend on what, and the
> shape of each unit. It stays shallow on contents — those are owned by the implementation guides.

---

## 1. 🧱 Layered architecture

Three layers. Dependencies point **down** the stack only — never up, never sideways between calculators.

| Layer          | Contents                                                                                  | May depend on            |
|----------------|-------------------------------------------------------------------------------------------|--------------------------|
| **Foundation** | `emc::units`, `emc::constants`, `emc::materials`, `emc::Error`/`emc::ErrorCode`            | C++23 stdlib + mp-units  |
| **Domain**     | the calculators, in 9 category namespaces; each a self-contained `(Input, Result, calculate())` triple (§5) | Foundation only |
| **Facade**     | umbrella headers (`emc/emc.hpp`, per-category) that re-`#include` groups; **no logic**     | Domain + Foundation (include-only) |

Foundation is the shared vocabulary — nothing in it knows any specific calculator. A Domain calculator
depends only on Foundation and the stdlib, **never on another calculator's namespace**. The Facade exists
purely so a downstream user can write one `#include`.

### 1.2 The dependency rule (the one hard invariant)

> [!IMPORTANT]
> The dependency direction is the hard architectural invariant of the whole library. Foundation depends
> only on stdlib + mp-units; Domain only on Foundation; Facade is include-only over both. **No domain
> calculator may depend on another domain calculator's namespace.** Every other rule here is negotiable
> detail; this one is not.

```text
Foundation  ->  C++23 standard library + mp-units
Domain      ->  Foundation (+ stdlib + mp-units)
Facade      ->  Domain + Foundation (include-only)
```

Enforcement is mechanical (see implementation/16-build-and-scaffolding.md and 09-testing-and-golden-vectors.md): a layering lint (`clang-tidy` `misc-include-cleaner` + a
custom check) and a CI check fail the build if any `src/<categoryA>/...` pulls in a sibling category's
public header, keeping "no sideways domain dependency" honest.

### 1.3 Dependency graph (ASCII)

```text
              +------------------------------------------------+
              |  C++23 stdlib            mp-units              |   (external)
              +------------------------------------------------+
                         ^                    ^
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

### 1.4 Where shared physics helpers go

Some calculators share a sub-computation — e.g. **skin depth** is both an `emc::basic` calculator and an
internal step in AC-resistance for standard-gauge wire (`δ = 1/√(π f μ σ)`). Two placements:

- **Public, user-meaningful calculation** → stays a normal calculator; other calculators call its public
  `calculate()`. This is *not* a forbidden cross-dependency — it goes through the documented public API,
  exactly like a downstream user would. We treat `emc::basic` skin depth as a de-facto Foundation primitive.
- **Private numeric kernel** with no standalone meaning → lives in `emc::detail` (Foundation-adjacent), so
  no domain namespace reaches sideways into another.

> [!TIP]
> Promote a shared sub-computation to a public Foundation primitive (skin depth) or to `emc::detail`.
> Reach for `emc::detail` only when the kernel has no standalone physical meaning.

---

## 2. 🗂️ Directory & file layout

Root: `/Users/sufuk/CLionProjects/emcpp`. The conventional **`include/` (public) + `src/` (compiled)**
split (locked decision: traditional compiled library). Public headers install; `src/` and src-local
headers do not.

```text
/Users/sufuk/CLionProjects/emcpp/
├── CMakeLists.txt                      # top-level project(); see build guide
├── CMakePresets.json                   # configure/build/test presets
├── README.md  LICENSE
├── .clang-format / .clang-tidy         # style + the layering lint checks
│
├── cmake/                              # support files (config template, warnings, deps) — (see build guide)
│
├── include/emc/                        # PUBLIC headers — installed, the API surface
│   ├── emc.hpp                         #   FACADE: includes everything
│   ├── version.hpp                     #   EMC_VERSION_* (generated; see build guide)
│   ├── export.hpp                      #   EMC_API macro (generated; def see build guide)
│   ├── core/                           #   FOUNDATION public headers
│   │   ├── error.hpp                   #     emc::Error, emc::ErrorCode
│   │   ├── units.hpp                   #     emc::units vocabulary on mp-units
│   │   ├── constants.hpp               #     emc::constants constexpr quantities
│   │   ├── materials.hpp               #     emc::materials database
│   │   └── calculator.hpp              #     Calculator concept + shared traits
│   ├── basic/        (basic.hpp + antenna, decibel, skin_depth)            # category 1
│   ├── converter/    (+ antenna_factor_gain, efield_power_density, …, vswr) # category 2
│   ├── component/    (+ capacitance, inductance, resistance, microstrip_trace, …) # category 3
│   ├── prediction/   (+ esd_lightning_coupling, rf_field)                  # category 4
│   ├── shielding/    (+ cavity_resonance, shielding_effectiveness)         # category 5
│   ├── filtering/    (+ ferrite)                                           # category 6
│   ├── cabling/      (+ braid_coverage, crosstalk)                         # category 7
│   ├── grounding/    (+ microstrip_current)                                # category 8
│   └── testing/      (+ noise_figure)                                      # category 9
│
├── src/                                # COMPILED .cpp + src-local PRIVATE headers (mirrors include/)
│   ├── core/  (error.cpp, materials.cpp, detail/math_kernels.hpp)  # detail/ NOT installed
│   └── basic/ converter/ component/ …  # one .cpp per public header, same relative path
│
├── tests/                             # all tests; the ONLY place I/O is allowed (doc 09)
│   ├── unit/  property/  constexpr/
│
├── tools/                            # optional dev tools (codegen) — NOT part of the library
├── docs/                             # generated/manual API docs
├── plan/                             # THIS PLAN (markdown only)
└── third_party/                      # vendored deps ONLY if not using FetchContent (mp-units default: FetchContent)
```

Every public header has a 1:1 matching `.cpp` at the **same relative path** under `src/`, so navigation
is trivial and CMake can list files predictably.

### 2.1 The 9 categories: folders & namespaces

| #  | Category               | Folder / Namespace         | Representative leaves                                  |
|----|------------------------|----------------------------|-------------------------------------------------------|
| 1  | Basic Calculations     | `basic/` `emc::basic`      | Antenna, Decibel, SkinDepth                           |
| 2  | Converter              | `converter/` `emc::converter` | AF↔Gain, EField↔PowerDensity, Energy↔Freq, λ↔f, VSWR |
| 3  | Component Calculations | `component/` `emc::component` | Capacitance, Inductance, Resistance, MicrostripTrace… |
| 4  | EMC Predictions        | `prediction/` `emc::prediction` | ESD/Lightning Coupling, RF Field                  |
| 5  | Shielding              | `shielding/` `emc::shielding` | Cavity Resonance, Shielding Effectiveness          |
| 6  | Filtering              | `filtering/` `emc::filtering` | Ferrite                                            |
| 7  | Cabling                | `cabling/` `emc::cabling`  | Braid Optical Coverage, Crosstalk                     |
| 8  | Grounding              | `grounding/` `emc::grounding` | Microstrip Line Current Distribution               |
| 9  | Testing                | `testing/` `emc::testing`  | Noise Figure                                          |

---

## 3. 🏷️ Namespace design

All public symbols live under `emc`.

| Namespace          | Layer      | Lives in                               | Contents                                                          |
|--------------------|------------|----------------------------------------|------------------------------------------------------------------|
| `emc`              | top        | everywhere                             | `Error`, `ErrorCode`, facade re-exports, version constants        |
| `emc::units`       | Foundation | `core/units.hpp`                       | quantity/unit vocabulary on mp-units: `length`, `frequency`, `impedance`, `conductivity`, parse helpers (see implementation/00-foundation-code.md) |
| `emc::constants`   | Foundation | `core/constants.hpp`                   | `constexpr` constants as mp-units quantities: `c`, `mu_0`, `eps_0`, `h`, `eta_0` (see implementation/00-foundation-code.md) |
| `emc::materials`   | Foundation | `core/materials.hpp`                   | `constexpr` material DB: conductivity, `mu_r`, resistivity (see implementation/00-foundation-code.md) |
| `emc::detail`      | internal   | `src/.../detail/*.hpp` (not installed) | private kernels/helpers; **not** part of the API contract         |
| `emc::basic` … `emc::testing` | Domain | `include/emc/<dir>/`             | the 9 category calculators                                        |

### 3.1 Conventions

- Every calculator exposes a **`calculate`** free function in its category namespace, with its own
  descriptive `Input`/`Result` structs (e.g. `emc::component::MicrostripTraceInput`). Descriptive struct
  names keep overloads unambiguous via ADL on the argument type.
- `emc::detail` is the *only* home for internal symbols; its headers live under `src/`, so they are
  physically un-installable and may change in any patch release.
- `emc::units` is a **vocabulary**, not a units library — it re-uses mp-units and adds project-specific
  named aliases so call sites read `emc::units::frequency` instead of raw `mp_units::quantity<...>`.

### 3.2 `using namespace` policy

> [!CAUTION]
> Library **headers** must never `using namespace` — the alias leaks into every consumer TU and silently
> changes name lookup at their call sites. Implementation `.cpp` files *may* use a file-local
> `using namespace mp_units;` (and `mp_units::si`/`isq`) for readability, since that cannot leak.

---

## 4. 🚪 Public vs internal boundary

```text
include/emc/**                          -> PUBLIC. Installed. Part of the semver contract. Stable.
include/emc/.../detail (if any)         -> "soft-internal": installed but documented as unstable.
src/** (incl. src/.../detail/*.hpp)     -> PRIVATE. Never installed. Free to change anytime.
```

Prefer truly internal headers under **`src/.../detail/`** so they cannot be installed at all. Reserve an
installed `include/emc/detail/` only when a public *inline/template* must reference an internal helper
that therefore has to ship in a header.

### 4.2 When to use PImpl

The calculator pattern is **free functions over POD-ish aggregate structs** (§5) — no class, no vtable,
no hidden state — so the classic PImpl motivation mostly does not apply. Use PImpl only when:

- a calculator must hold **expensive precomputed state** across calls (cached factorization / interpolation
  table) — wrap it in a class with `std::unique_ptr<Impl>` to keep heavy members out of the public header; or
- you need to keep a **large mp-units template type out of the ABI surface** (those types bloat compile
  times and freeze template details into the ABI).

For the overwhelming majority, **no PImpl is needed**: aggregate `Input`/`Result` cross by value and the
mp-units-heavy math lives in the `.cpp`.

### 4.3 The `EMC_API` export macro

Every public, non-inline, non-template entity that is part of the API is annotated `EMC_API`, which
expands to the platform's symbol-visibility / DLL import-export keyword.

> [!NOTE]
> The **authoritative** macro definition (CMake's `generate_export_header` + `-fvisibility=hidden`) belongs
> to implementation/16-build-and-scaffolding.md. Here we fix only the *convention*: out-of-line `calculate()`/`validate()` get `EMC_API`;
> `inline`/`template`/`constexpr` entities fully defined in headers do **not** (no out-of-line symbol). The
> aggregate `Input`/`Result` structs are header-only data and need no `EMC_API`.

---

## 5. 📐 The Calculator contract (architectural shape)

Architecturally, every calculator is the **same shape** — an `(Input, Result, calculate)` triple plus
optional `validate`, all returning `std::expected<…, emc::Error>`, satisfying a shared `Calculator`
concept defined once in `include/emc/core/calculator.hpp`:

```cpp
template <class Input, class Result>
concept Calculator = requires (const Input& in) {
    // calculate(Input) -> std::expected<Result, emc::Error>  (found via ADL)
    { calculate(in) } -> std::same_as<std::expected<Result, emc::Error>>;
};
```

The concept lets generic test harnesses and batch drivers (doc 09) operate over *any* calculator
uniformly and gives a compile-time contract, so a malformed calculator fails to compile rather than
silently diverging. The **deep treatment** (designated initializers, member defaults, `validate`
composition, ADL nuances) is owned by implementation/00-foundation-code.md (the calculator pattern); here we fix only the invariant: *every calculator is
`(Input, Result, calculate) [+ validate]` returning `std::expected<…, emc::Error>`, nothing else.*

---

## 6. 🔁 Cross-cutting concerns

These hold uniformly across the library and are architectural, not per-calculator.

- **Error type placement** — one error type for the whole library: `emc::Error`/`emc::ErrorCode` in
  Foundation (`core/error.hpp`), no per-category enums. Failures travel through the value channel as
  `std::expected<…, emc::Error>`, keeping them explicit and recoverable. Mechanics: implementation/00-foundation-code.md.
- **Units vocabulary placement** — `emc::units` (Foundation) is the *only* vocabulary domain code uses for
  dimensional quantities. No calculator stores a quantity as a bare double + remembered factor; conversion
  is compile-checked and done once, at the application boundary. Details: implementation/00-foundation-code.md.
- **Thread-safety** — every `calculate()`/`validate()` is a **pure function**: reads only `const Input&`,
  writes only its result, touches no global mutable state. All calculators are therefore reentrant and
  thread-safe by construction; constants/materials are `constexpr`/immutable. No singletons, no lazy
  global caches, no `static` mutable locals. Caching, if needed, uses a *caller-owned* object (§4.2).
- **No I/O in the core** — no file reading, no `std::print` from `calculate()`, no env access, no logging
  side-channels. Any data loading lives in `tests/` (and optionally `tools/`).

> [!WARNING]
> Storing a physical quantity as a bare `double` plus a separate scale factor is the classic source of
> unit-mismatch errors (Hz vs MHz, mm vs mils). The typed `emc::units` vocabulary makes such a mismatch a
> **compile error**, not a silent numeric one.

---

## 7. 📦 ABI / versioning note (compiled library)

A traditional compiled library has an ABI surface downstream binaries link against.

- **Semver** (`MAJOR.MINOR.PATCH`) via `include/emc/version.hpp` (`EMC_VERSION_*`, generated by CMake —
  implementation/16-build-and-scaffolding.md) and the CMake package version file, so `find_package(emc 1.2)` enforces compatibility.
  - **MAJOR** — breaking API/ABI (changed `calculate` signature, removed/renamed symbol, incompatible
    `Input`/`Result` layout change).
  - **MINOR** — additive (new calculator, appended optional struct member with a default, new overload).
  - **PATCH** — bug fixes that don't touch API/ABI, including numerical refinements that change *numbers*,
    not *signatures*; known-value/property tests (doc 09) make these deliberate and reviewed.
- **The export header is the ABI boundary** — `export.hpp` (`EMC_API`) + `-fvisibility=hidden` mean the ABI
  is *exactly* the set of `EMC_API`-marked symbols. Everything else (`emc::detail`, file-local statics,
  inline/template instantiations) is not ABI and may change freely. A small, explicit export surface is
  what makes the semver promise enforceable.
- **Keep heavy mp-units templates inside `.cpp`** — mp-units quantity types are deeply templated, so
  exposing them in exported signatures risks ABI fragility and high per-consumer compile cost. The public
  `calculate()` signature uses `emc::units` aliases (desired: compile-checked units), but the
  template-heavy implementation is out-of-line in the `.cpp`, compiled **once**. Prefer free functions over
  aggregate structs (no exported templated members); where a class must hold mp-units state, use PImpl (§4.2).

> [!NOTE]
> Forward-looking (C++26 / modules): a future iteration could ship `emc` as C++ modules to cut template
> recompilation cost. The plan targets **headers + `.cpp`** for now (locked decision); implementation/16-build-and-scaffolding.md expands.

---

## Cross-references

See `plan/README.md` for the full plan index; the foundation (units / constants / materials / error model / calculator pattern) lives in implementation/00-foundation-code.md and the build & scaffolding in implementation/16-build-and-scaffolding.md.
