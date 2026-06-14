# Migration Roadmap

> Purpose: sequence the entire NinjaEMC → `emc` conversion into discrete, verifiable phases — with concrete deliverables, exit criteria, effort, risks, and dependencies — so a team (or a single implementer) can execute the plan in documents 00–09 in the right order, never blocking on un-built foundations, and know exactly when the program is *done*.

This document is the **execution plan** for everything the rest of the plan specifies. The other documents say *what to build* and *how to build it*; this one says *in what order, with what gate at each step, and how to tell when to stop*. It is deliberately the last numbered document (the `README.md` index is written separately) because it references all of them.

---

## 0. How to read this document

The conversion is divided into **six phases (0–5)**. They are mostly sequential, but several activities inside and across phases can run in parallel — §6 (the ASCII timeline) makes the parallelism explicit. Each phase below is described with a fixed shape:

- **Goal** — one sentence.
- **Scope** — what is in / out.
- **Deliverables** — concrete artifacts that must exist at the end.
- **Exit / acceptance criteria** — the gate; the phase is not done until every box is checkable.
- **Effort** — a T-shirt size (S ≈ 1–2 days, M ≈ 3–5 days, L ≈ 1–2 weeks, XL ≈ 2–4 weeks) plus a rough day range. These are *planning* estimates for one experienced C++ engineer; parallelize per §6.
- **Risks & mitigations** — phase-local; the program-wide risk register is in §3.
- **Dependencies** — which earlier phase(s) must be complete.

A short legend used throughout:

| Doc | Owns |
| --- | --- |
| [`00-overview-and-goals.md`](00-overview-and-goals.md) | vision, scope, non-goals, success criteria |
| [`01-architecture-and-layout.md`](01-architecture-and-layout.md) | layers, directory/namespace layout, dependency rules |
| [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) | C++23 feature → pain-point catalog |
| [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) | the `emc::units` mp-units vocabulary |
| [`04-constants-and-material-database.md`](04-constants-and-material-database.md) | `emc::constants` + `emc::materials` |
| [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md) | `emc::Error` / `std::expected` / validation |
| [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) | the per-calculator pattern + worked conversions |
| [`07-calculator-inventory.md`](07-calculator-inventory.md) | the 52-calculator work-list (the checklist) |
| [`08-build-system-cmake.md`](08-build-system-cmake.md) | CMake compiled lib, mp-units dep, install/export, app rewire |
| [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) | the 52 CSVs as Qt-free regression vectors + property/constexpr tests |

---

## 1. Phased plan

### Phase 0 — Scaffold

**Goal.** Stand up an empty-but-buildable `emc` library repository so that *every later phase has a place to put code that compiles, links, tests, and is checked by CI from day one*.

**Scope.**
- In: repo bootstrap at `/Users/sufuk/CLionProjects/emcpp`, the CMake skeleton from [doc 08](08-build-system-cmake.md), wiring `mp-units` as a dependency, the directory/namespace skeleton from [doc 01](01-architecture-and-layout.md), a "hello" sentinel header + `.cpp` proving the compiled-library shape, a single passing test proving the test runner works, and CI.
- Out: any actual EMC math, constants, units vocabulary, or calculators. Nothing physics-related ships in Phase 0.

**Deliverables.**
- `CMakeLists.txt`, `CMakePresets.json`, and a `cmake/emc-config.cmake.in` package-config template exactly as specified in [doc 08](08-build-system-cmake.md). Targets: `emc` (the lib, static + shared selectable), `emc::emc` alias, and a `tests` target.
- `mp-units` acquired reproducibly (CMake `FetchContent` or a package manager such as Conan/vcpkg — choose one and pin a version) and linked into `emc`. A 3-line `.cpp` that constructs one `quantity<isq::frequency[si::hertz]>` and returns its numeric value proves the dependency compiles and links.
- The empty directory tree from [doc 01](01-architecture-and-layout.md): `include/emc/{constants,units,materials,...category dirs...}/`, `src/` mirror, `tests/`, `cmake/`, `docs/`.
- `EMC_API` export macro header and a generated `version.hpp` (per [doc 08](08-build-system-cmake.md)).
- One trivial sentinel: `emc::library_version()` declared in a public header, defined in `src/`, exercised by one unit test (`EXPECT(emc::library_version() == ...)`).
- A CI workflow (GitHub Actions or equivalent) that on every push: configures via a preset, builds, runs the test target, and fails the job on any compiler warning (treat-warnings-as-errors on for the `emc` target).

```cmake
# Phase 0 acceptance sketch (full version in doc 08)
cmake_minimum_required(VERSION 3.28)
project(emc VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(mp-units CONFIG REQUIRED)   # or FetchContent — pin the version

add_library(emc)                          # STATIC/SHARED via BUILD_SHARED_LIBS
add_library(emc::emc ALIAS emc)
target_compile_features(emc PUBLIC cxx_std_23)
target_link_libraries(emc PUBLIC mp-units::mp-units)
target_sources(emc PRIVATE src/version.cpp)
target_include_directories(emc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
```

```cpp
// tests/smoke_test.cpp — the only thing Phase 0 must prove green
#include <emc/version.hpp>
#include <mp-units/systems/si.h>
int main() {
    using namespace mp_units;
    auto f = 27.0 * si::mega<si::hertz>;        // mp-units compiles & links
    return (emc::library_version().major >= 0 && f.numerical_value_in(si::hertz) > 0) ? 0 : 1;
}
```

**Exit / acceptance criteria.**
- [ ] `cmake --preset default && cmake --build --preset default` succeeds on at least one target platform with a C++23 toolchain.
- [ ] The smoke test runs green via `ctest`.
- [ ] CI is green on a fresh clone (no manual setup steps beyond the documented bootstrap).
- [ ] `find_package(emc)` works from a throwaway downstream `CMakeLists.txt` that links `emc::emc` and includes one header (validates install/export early — cheap now, expensive later).
- [ ] Warnings-as-errors is on for `emc`; the build is warning-clean.

**Effort.** **S–M (2–4 days).** Most of the time is toolchain/`mp-units` acquisition and getting `find_package` clean, not code.

**Risks & mitigations.**
- *mp-units acquisition friction / version drift* → pin an exact version; commit the lockfile (Conan/vcpkg manifest) or the `FetchContent` Git tag. Verify on CI, not just one dev machine.
- *C++23 toolchain gaps* (`std::expected`, `std::print`) → in Phase 0, run a feature-probe `.cpp` that `#include`s `<expected>`, `<format>`, `<print>` and uses each once; fail fast with a clear message if the chosen compiler lacks them. See §3 (R2).

**Dependencies.** None. This is the root of the dependency graph.

---

### Phase 1 — Foundation

**Goal.** Build the shared vocabulary every calculator depends on — constants, units, materials, and the error type — so that Phase 2+ can be written against a stable, tested base with *zero* duplicated or buggy constants.

**Scope.**
- In: `emc::units` ([doc 03](03-quantities-and-units-mp-units.md)), `emc::constants` ([doc 04](04-constants-and-material-database.md)), `emc::materials` ([doc 04](04-constants-and-material-database.md)), `emc::Error` + `emc::ErrorCode` + the validation helpers and the `Calculator` concept ([doc 05](05-error-handling-and-validation.md)).
- Out: any specific calculator. The Foundation layer must *not* know about a single named calculation.

**Deliverables.**
- `include/emc/units/…` — the project quantity/unit aliases (frequency, length, conductivity, permeability, field strength, power density, impedance, …) and the boundary parse/format helpers, all built on mp-units. This is the artifact that retires the **541** `addItem(unit, factor)` conversions ([doc 03](03-quantities-and-units-mp-units.md)).
- `include/emc/constants/…` — one `constexpr` definition each for `c` (= **299 792 458 m/s** exactly), `mu_0`, `eps_0`, `pi`, Planck `h`, etc., as mp-units quantities. This retires `#define PI 3.14`, `#define SPEEDOFLIGHT 300000000.0`, the **8+** `qreal mu0 = 4*M_PI*1e-7;` redefinitions, and the **6** copy-pasted `PermofFreeSpace` macros ([doc 04](04-constants-and-material-database.md)).
- `include/emc/materials/…` — one `constexpr` material database with a single representation (conductivity, relative permeability, resistivity, …), replacing the ~**14** inconsistent inline tables ([doc 04](04-constants-and-material-database.md)).
- `include/emc/error.hpp` — `enum class ErrorCode`, `struct Error`, and the `validate(...) -> std::expected<void, Error>` convention, plus the `Calculator` concept ([doc 05](05-error-handling-and-validation.md)).
- `static_assert`-based **constexpr tests** for the constants (e.g. `static_assert(constants::c == 299'792'458 * si::metre / si::second);`) and a small unit test suite for `parse`/`format` of units and for `Error` construction/inspection.

**Exit / acceptance criteria.**
- [ ] Every constant in [doc 04](04-constants-and-material-database.md) exists exactly once, is `constexpr`, is an mp-units quantity, and is covered by a `static_assert`.
- [ ] The materials DB exposes every material referenced by the source app's calculators (cross-check against [doc 07](07-calculator-inventory.md)), each with a single canonical value, and a lookup that returns `std::expected<Material, Error>` (no `EXIT_FAILURE` sentinel).
- [ ] `emc::Error`, `ErrorCode`, `validate`, and the `Calculator` concept compile and are unit-tested; a deliberate `static_assert(Calculator<...>)` against a dummy triple passes.
- [ ] Foundation has **no** dependency on any domain namespace and **no** Qt include (grep-enforced in CI).
- [ ] Library still builds warning-clean; tests green.

**Effort.** **M–L (5–9 days).** `emc::units` and the materials DB carry the weight; constants and `Error` are fast.

**Risks & mitigations.**
- *Wrong canonical values when de-duplicating constants/materials* → for each de-duplicated value, record the authoritative source (SI defining constant, CODATA, manufacturer datasheet) in a comment; do **not** copy the buggy app values. This is where some golden outputs will later need re-blessing (§3 R3, [doc 09](09-testing-and-golden-vectors.md)).
- *Over-engineering the units vocabulary* → drive `emc::units` only by the quantities the inventory actually uses ([doc 07](07-calculator-inventory.md)); don't model dimensions no calculator needs.

**Dependencies.** Phase 0 (needs the buildable skeleton + mp-units linked).

---

### Phase 2 — Pilot

**Goal.** Convert **2–3 representative calculators end-to-end** to prove the [doc 06](06-calculator-design-pattern.md) pattern and the [doc 09](09-testing-and-golden-vectors.md) golden-test harness *before* committing the team to bulk migration. This is the phase that de-risks the other ~49.

**Scope.**
- In: three deliberately chosen calculators spanning the difficulty range, plus the reusable golden-CSV test harness:
  - **SkinDepth** (`src/BasicCalculations/SkinDepth/SkinDepthWidget.cpp`) — *simple*; one formula, a material combobox, exercises `emc::materials`, `emc::constants`, units. Golden: `resources/data/SkinDepthWidget.csv` (`"27,Nickel"` = frequency, material).
  - **Wavelength ↔ Frequency** (`src/Converter/WavelengthvsFrequency/WavelengthvsFrequency.cpp`) — *bidirectional converter*; proves the converter shape and the precise `c`. Golden: `resources/data/WavelengthvsFrequency.csv` (`"177.16"` wavelength → expected frequency).
  - **MicrostripTrace** (`src/.../MicrostripTrace/MicrostripTraceWidget.cpp`) — *hard*; the 4-method bidirectional solver with the ~8-branch unit trees and inline `QMessageBox` validation. If the pattern survives this, it survives anything. Golden: `resources/data/MicrostripTraceWidget.csv` (`"18.65,11.16,15.00,5"`).
- Out: any other calculator; performance tuning; release packaging.

**Deliverables.**
- Three `(Input, Result, calculate())` triples under their category namespaces (`emc::basic`, `emc::converter`, `emc::component`), each with `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)`, an aggregate input with defaults, optional `validate`, and a `static_assert(Calculator<…>)` ([doc 06](06-calculator-design-pattern.md)).
- The **reusable golden-vector harness** from [doc 09](09-testing-and-golden-vectors.md): a Qt-free CSV reader + a parametrized test driver that maps a CSV row → `Input`, calls `calculate`, and compares against expected outputs within a documented tolerance. Generic enough that Phase 3 adds calculators by registering a row-mapper, not by writing a new harness.
- A **re-blessing log**: for any golden whose expected value changes because the new code fixes a bug (`PI 3.14`, `c = 3e8`, drifted material values), a recorded entry with old value, new value, and the physical justification ([doc 09](09-testing-and-golden-vectors.md), §3 R3).
- A short "pattern retrospective" note feeding back into [doc 06](06-calculator-design-pattern.md) if the pilot exposes a gap (e.g. the bidirectional solver wants a `Mode`/`Solve-for` enum member in the input struct).

**Exit / acceptance criteria.**
- [ ] All three calculators pass their golden CSV vectors (re-blessed where a fixed bug changes the expected output, each change justified in the log).
- [ ] The bidirectional cases (Wavelength↔Frequency both directions; MicrostripTrace solving for Z0/H/T/W) are expressed *without* re-introducing the 8-branch unit `if` trees — units are mp-units quantities, conversions are compile-checked ([doc 03](03-quantities-and-units-mp-units.md)).
- [ ] No `QMessageBox`, no `EXIT_FAILURE`, no `ui->...` anywhere in the new code; validation is `validate()` returning `std::expected<void, Error>` ([doc 05](05-error-handling-and-validation.md)).
- [ ] The harness is reusable: adding a 4th calculator's golden test requires only a row-mapper + registration, demonstrably (write one throwaway extra mapping to prove it, then delete it).
- [ ] The team signs off that the [doc 06](06-calculator-design-pattern.md) pattern is final (or [doc 06](06-calculator-design-pattern.md) is amended and re-approved).

**Effort.** **M (4–6 days).** The harness is the bulk; MicrostripTrace is the hardest single conversion in the program and is intentionally front-loaded here.

**Risks & mitigations.**
- *The pattern doesn't fit the hardest calculator* → that is exactly why MicrostripTrace is in the pilot. If it forces a pattern change, do it **now** (cheap, 3 calculators) not in Phase 3 (expensive, ~49).
- *Golden re-blessing turns into "the new answer is just different"* → require a written physical justification per changed value; if a change can't be justified, it's a porting bug, not a re-bless. See §3 R3.

**Dependencies.** Phase 1 (needs constants, units, materials, Error). Soft dependency on Phase 0's CI to run the new tests.

---

### Phase 3 — Bulk migration

**Goal.** Convert the remaining ~49 calculators, category by category, using [doc 07](07-calculator-inventory.md) as the checklist and the [doc 06](06-calculator-design-pattern.md) per-calculator implementer checklist for each one — turning the proven pilot pattern into complete coverage.

**Scope.**
- In: every calculator in [doc 07](07-calculator-inventory.md) not done in Phase 2; for each, an `(Input, Result, calculate())` triple, `validate`, `static_assert(Calculator<…>)`, and its golden CSV regression test.
- Out: the Qt app (Phase 4); release hardening (Phase 5). Navigation widgets (`MainWindow.cpp`, `InductanceWidget.cpp`, …) are explicitly **not** ported — they hold no domain logic (they only switch a `QStackedWidget` index) and stay in the app.

**Suggested batching order (easiest / highest-value first).** Each batch is a unit of parallelizable work; the order front-loads confidence and shared infrastructure.

| Batch | Category (source dir) | ~Count | Rationale for ordering |
| --- | --- | --- | --- |
| B1 | **Basic** (`BasicCalculations/`) — Antenna, Decibel (+ SkinDepth from pilot) | ~3 | Simplest math, smallest input structs; finishes the category the pilot started. Builds team fluency. |
| B2 | **Converter** (`Converter/`) — AntennaFactor↔Gain, EField↔PowerDensity, Energy↔Frequency (+ Wavelength↔Frequency from pilot), VSWR/RC/RL/ML/TL | ~8 | Bidirectional pattern already proven in pilot; high value (Converter is a heavy `addItem`/magic-factor offender per [doc 03](03-quantities-and-units-mp-units.md)); mostly small formulas. |
| B3 | **Testing** (`Testing/`) — Noise Figure; **Filtering** (`Filtering/`) — Ferrite; **Grounding** (`Grounding/`) — Microstrip Line Current Distribution | ~3 | Single-calculator categories; quick wins that close whole categories. |
| B4 | **Cabling** (`Cabling/`) — Cable Braid Optical Coverage, Crosstalk | ~2 | Self-contained; modest math. |
| B5 | **Shielding** (`Shielding/`) — Cavity Resonance (the 12-mode `RectangularEnclosure`), EM Shielding Effectiveness | ~2 (multi-output) | Proves the multi-output `Result` struct (12 resonant modes f110..f121 in one result). |
| B6 | **Component** (`ComponentCalculations/`) — Capacitance, Inductance, Resistance (StandardGaugeWire + others), Circuit Board Trace Impedance (+ MicrostripTrace from pilot), Transmission Line Parameters, Harmonic Trap | ~20+ | Largest, hardest batch; intentionally last. Reuses the bidirectional + material patterns proven everywhere above; `StandardGaugeWire`'s `enum Material`/`GetResistivity`/`EXIT_FAILURE` collapses into the Phase-1 materials DB + `std::expected`. |
| B7 | **EMC Predictions** (`EMCPredictions/`) — ESD & Lightning Coupling, RF Field | ~2 | Depends on `mu_0`/constants and possibly skin-depth helper (already in Foundation); placed late so the shared helpers are battle-tested. |

> Counts are approximate; the authoritative per-calculator list and exact mappings live in [doc 07](07-calculator-inventory.md). The total across all batches + pilot is the full ~52 (44 with real math).

**Parallelization.** Calculators are pure free functions with **no sideways dependencies** ([doc 01](01-architecture-and-layout.md)), so within and across batches they are embarrassingly parallel. Recommended split: one engineer (or one PR) per category; B1–B5 can proceed concurrently the moment Phase 2 closes; B6 (Component) gets the most hands because it is ~40% of the work; B7 starts once any shared helper it needs (e.g. skin depth) is merged. Each calculator is a small, independently reviewable, independently mergeable PR carrying its own golden test.

**Per-calculator definition of done (the [doc 06](06-calculator-design-pattern.md) implementer checklist, applied to each):**
- [ ] Aggregate `Input` struct with mp-units-typed members + sensible defaults.
- [ ] `Result` struct (mp-units-typed; multi-output where the source produced multiple values).
- [ ] `[[nodiscard]] std::expected<Result, Error> calculate(const Input&)` — pure, no global state.
- [ ] `validate(const Input&) -> std::expected<void, Error>` capturing the source's range checks (the ones that were `QMessageBox`).
- [ ] `static_assert(Calculator<Input, Result, …>)`.
- [ ] Golden CSV test registered and passing (re-blessed + justified where a fixed bug moves the expected value).
- [ ] No Qt, no magic constants, no inline material tables, no hand-wired unit factors (grep-clean).
- [ ] [doc 07](07-calculator-inventory.md) row ticked off.

**Exit / acceptance criteria.**
- [ ] Every calculator in [doc 07](07-calculator-inventory.md) is implemented and its inventory row is checked.
- [ ] All **52** golden CSV vectors run as Qt-free regression tests and pass (with the re-blessing log complete and justified).
- [ ] Property/constexpr tests from [doc 09](09-testing-and-golden-vectors.md) (e.g. round-trip converters, dimensional sanity) pass.
- [ ] CI builds the whole library warning-clean and runs the full test suite green.
- [ ] Grep gates pass: no `QMessageBox`, no `EXIT_FAILURE`, no `#define PI`, no inline `mu0`, no `addItem(...factor...)`, no `#include <Q...>` under `include/emc` or `src/`.

**Effort.** **XL (3–5 weeks serial; ~1.5–2.5 weeks with 3–4 parallel engineers).** Component (B6) dominates.

**Risks & mitigations.**
- *Formula-fidelity drift* (a ported formula silently diverges) → the golden CSVs are the backstop; every calculator must pass its vectors. For calculators whose golden output legitimately changes (fixed `PI`/`c`/material), require the written justification (§3 R3).
- *Inconsistent quality across parallel work* → the per-calculator checklist + a PR template that encodes it; one reviewer owns pattern consistency.
- *Scope creep* (adding new features/materials/units mid-migration) → freeze feature scope to "what the source app did"; capture new ideas as post-1.0 backlog (§3 R4, [doc 00](00-overview-and-goals.md) non-goals).

**Dependencies.** Phase 2 (pattern + harness must be approved). Phase 1 (foundation). Batches are mutually independent.

---

### Phase 4 — Rewire the Qt app

**Goal.** Turn NinjaEMC into a *thin consumer*: each widget becomes **parse-units → `calculate()` → display**, and every duplicated constant, material table, unit factor, and inline validation is **deleted** from the app, with behavior verified against the same goldens.

**Scope.**
- In: make the app's CMake `find_package(emc)` / link `emc::emc` ([doc 08](08-build-system-cmake.md)); rewrite each calculator widget's button-clicked lambda to build the calculator `Input` from `ui->...->value()` + the chosen unit, call `calculate`, and render `Result` or map `Error` back to a `QMessageBox` *at the UI boundary only*; delete the app's `#define PI`, `mu0` redefinitions, `PermofFreeSpace` macros, inline material if/else tables, and inline range-check dialogs.
- Out: redesigning the UI; changing navigation; any new widgets. Navigation widgets stay untouched (they hold no math).

**Deliverables.**
- App builds and runs against `emc::emc`; no domain math remains in the app — widgets only marshal I/O.
- A boundary mapping layer: combobox unit selection → `emc::units` parse; `emc::Error` → user-facing `QMessageBox` text (the *only* place a dialog appears, and now driven by a value, not welded into the formula).
- Deletion PRs removing the duplicated constants/materials/units/validation from `src/Utilites/HelperTypes.h` and the ~14 material sites and the 10 inline-validation sites.

**Exit / acceptance criteria.**
- [ ] The app builds against the installed/exported `emc` package (proves Phase 0's `find_package` end-to-end with real consumers).
- [ ] For each migrated widget, driving it with a golden CSV input produces the library's golden output (the existing `TEST_MODE` CSV harness, or a thin replacement, now bottoms out in `emc::...::calculate`).
- [ ] `grep` over the **app** finds no `#define PI`, no `qreal mu0 = 4*M_PI*1e-7;`, no `PermofFreeSpace`, no inline material tables, no domain formula inside a widget lambda.
- [ ] Validation dialogs now originate from mapped `emc::Error` values, not from inside the math.
- [ ] Manual smoke pass of each category in the running app matches expectations.

**Effort.** **L (1–2 weeks).** Mostly mechanical per-widget rewiring + aggressive deletion; ~one widget per calculator.

**Risks & mitigations.**
- *Behavioral regressions at the UI boundary* (unit parsing, formatting, rounding) → the app's existing 52 `TEST_MODE` CSV fixtures are the regression net; route them through the library and diff.
- *Partial deletion leaves dead duplicates* → grep gates in the app's CI mirror the library's gates; a constant or material must exist in exactly one place (the library).

**Dependencies.** Phase 3 (the calculators the widgets call must exist) — though a widget *can* be rewired as soon as *its* calculator is merged, so Phase 4 can begin trickling in per-calculator during late Phase 3 (see §6).

---

### Phase 5 — Harden & release

**Goal.** Make `emc` a shippable, packaged, documented **1.0** library with the quality bars a reusable dependency needs.

**Scope.**
- In: sanitizer + fuzz coverage, API docs (Doxygen), finalized CMake package config / install-export, semantic-versioning policy, the 1.0 tag and release artifacts.
- Out: new calculators or features (post-1.0 backlog).

**Deliverables.**
- A CI matrix building with **ASan/UBSan** (and, where available, a second compiler) and running the full test suite under sanitizers.
- **Fuzz targets** for the parsing/boundary surfaces (unit parsing, any string→quantity entry points) and for a few numerically sensitive calculators, run for a bounded budget in CI ([doc 09](09-testing-and-golden-vectors.md)).
- **Doxygen** (or equivalent) docs for every public header in `include/emc/`, plus a short usage README showing `find_package(emc)` + a `calculate` call.
- Final `install()`/`export()` + `emc-config.cmake` package config verified by a *separate* downstream sample project ([doc 08](08-build-system-cmake.md)).
- A documented **SemVer** policy (what the public API surface is, what counts as breaking) and a `1.0.0` git tag + release.

**Exit / acceptance criteria.**
- [ ] Full test suite green under ASan/UBSan; no leaks/UB reported.
- [ ] Fuzz targets run clean for the configured budget; any crash is fixed and added as a regression vector.
- [ ] Every public symbol is documented; `docs/` builds without warnings.
- [ ] The downstream sample project consumes the *installed* package (not the build tree) and links/runs.
- [ ] `1.0.0` tagged; release notes list the deliberate behavioral changes vs. NinjaEMC (the re-blessed goldens) so consumers know the "wrong" old answers are intentionally corrected.

**Effort.** **M–L (5–10 days).**

**Risks & mitigations.**
- *Sanitizers/fuzzing surface latent UB late* → run a sanitizer build in CI from Phase 2 onward, not only in Phase 5, so issues surface continuously.
- *Package config breaks for real consumers* → Phase 0 and Phase 4 already exercise `find_package`; Phase 5 only finalizes it — keep the downstream sample in CI permanently.

**Dependencies.** Phase 3 (complete library) and Phase 4 (proves real consumer integration).

---

## 2. Dependency-ordered task list

Checkbox work-list spanning all phases, in an order that never blocks on an unbuilt prerequisite. Items at the same indentation under a phase can largely proceed in parallel.

**Phase 0 — Scaffold**
- [ ] Create `/Users/sufuk/CLionProjects/emcpp` repo + branch protection + PR template (encode the per-calculator checklist).
- [ ] Add CMake skeleton, `CMakePresets.json`, `EMC_API` macro, generated `version.hpp` ([doc 08](08-build-system-cmake.md)).
- [ ] Wire `mp-units` reproducibly (pin version; commit lockfile/tag) ([doc 08](08-build-system-cmake.md)).
- [ ] Create the directory/namespace tree ([doc 01](01-architecture-and-layout.md)).
- [ ] Add the smoke test + `emc::library_version()` sentinel.
- [ ] Stand up CI (configure/build/test, warnings-as-errors) and a throwaway downstream `find_package(emc)` check.
- [ ] Add a C++23 feature-probe TU (`<expected>`, `<format>`, `<print>`, constexpr `<cmath>`).

**Phase 1 — Foundation**
- [ ] `emc::constants` — every constant once, `constexpr`, mp-units, `static_assert`ed (kills `PI 3.14`, `c=3e8`, 8+ `mu0`, 6× `PermofFreeSpace`) ([doc 04](04-constants-and-material-database.md)).
- [ ] `emc::units` vocabulary + boundary parse/format (kills the 541 `addItem(unit,factor)`) ([doc 03](03-quantities-and-units-mp-units.md)).
- [ ] `emc::materials` single-representation DB + `std::expected` lookup (kills the ~14 inline tables + `EXIT_FAILURE`) ([doc 04](04-constants-and-material-database.md)).
- [ ] `emc::Error`, `ErrorCode`, `validate`, `Calculator` concept ([doc 05](05-error-handling-and-validation.md)).
- [ ] CI grep gate: no Qt include, no domain dependency in Foundation.

**Phase 2 — Pilot**
- [ ] Reusable golden-CSV harness + row-mapper registration ([doc 09](09-testing-and-golden-vectors.md)).
- [ ] SkinDepth triple + golden test.
- [ ] Wavelength↔Frequency triple + golden test (both directions).
- [ ] MicrostripTrace triple + golden test (solve Z0/H/T/W; no 8-branch unit trees).
- [ ] Re-blessing log started; pattern sign-off (amend [doc 06](06-calculator-design-pattern.md) if needed).

**Phase 3 — Bulk migration** (per category; parallelizable)
- [ ] B1 Basic (Antenna, Decibel).
- [ ] B2 Converter (AntennaFactor↔Gain, EField↔PowerDensity, Energy↔Frequency, VSWR/RC/RL/ML/TL).
- [ ] B3 Testing (Noise Figure), Filtering (Ferrite), Grounding (Microstrip Line Current Distribution).
- [ ] B4 Cabling (Braid Optical Coverage, Crosstalk).
- [ ] B5 Shielding (Cavity Resonance 12-mode, EM Shielding Effectiveness).
- [ ] B6 Component (Capacitance, Inductance, Resistance/StandardGaugeWire, CircuitBoardTraceImpedance, TransmissionLineParameters, HarmonicTrap).
- [ ] B7 EMC Predictions (ESD & Lightning Coupling, RF Field).
- [ ] All 52 golden vectors green; property/constexpr tests green; grep gates pass; [doc 07](07-calculator-inventory.md) fully checked.

**Phase 4 — Rewire the Qt app**
- [ ] App CMake: `find_package(emc)` + link `emc::emc` ([doc 08](08-build-system-cmake.md)).
- [ ] Per widget: build `Input` → `calculate()` → render `Result`/map `Error`.
- [ ] Delete app-side constants/materials/units/validation duplicates.
- [ ] Re-route `TEST_MODE` CSV harness through the library; diff against goldens.
- [ ] App grep gates green (no math/constants/materials in widgets).

**Phase 5 — Harden & release**
- [ ] ASan/UBSan CI matrix (started earlier; finalized here).
- [ ] Fuzz targets for parsing + sensitive calculators ([doc 09](09-testing-and-golden-vectors.md)).
- [ ] Doxygen for all public headers + usage README.
- [ ] Finalize install/export + downstream sample project in CI ([doc 08](08-build-system-cmake.md)).
- [ ] SemVer policy + `1.0.0` tag + release notes (list re-blessed/corrected behaviors).

---

## 3. Risk register

| ID | Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| **R1** | **mp-units learning curve & compile times.** The team is new to the type-level quantities API; heavy template instantiation slows builds. | Medium | Medium | Front-load a short mp-units spike in Phase 0–1; centralize the vocabulary in `emc::units` so calculators use *aliases*, not raw mp-units soup ([doc 03](03-quantities-and-units-mp-units.md)). Keep heavy arithmetic in `.cpp` (compiled once) per [doc 01](01-architecture-and-layout.md), not in headers. Enable ccache + warnings-as-errors so regressions are caught fast. |
| **R2** | **C++23 compiler availability.** `std::expected`, `std::print`/`std::format`, constexpr `<cmath>`, `std::mdspan`, deducing-this not uniformly available across target platforms (notably older Apple/Android toolchains). | Medium | High | Pick a minimum toolchain matrix in Phase 0 and gate CI on it; add the feature-probe TU so gaps fail fast. Confine bleeding-edge features (mdspan, deducing-this) to spots with a documented fallback; mark C++26 items as forward-looking only ([doc 02](02-modern-cpp-feature-catalog.md)). |
| **R3** | **Golden re-blessing due to fixed bugs.** Correcting `PI 3.14`, `c = 3e8`, and drifted material values changes some expected golden outputs; risk of masking a real porting bug as a "re-bless." | High | High | Require a written physical justification per changed golden (old → new + source); keep a re-blessing log from Phase 2; an unjustifiable change is treated as a bug, not a re-bless. Surface the corrected behaviors in 1.0 release notes ([doc 09](09-testing-and-golden-vectors.md), [doc 00](00-overview-and-goals.md)). |
| **R4** | **Scope creep.** Temptation to add new materials/units/calculators or redesign UI mid-migration. | Medium | Medium | Freeze scope to "what NinjaEMC did" ([doc 00](00-overview-and-goals.md) non-goals); route new ideas to a post-1.0 backlog; the inventory ([doc 07](07-calculator-inventory.md)) is the closed work-list. |
| **R5** | **Physics-formula fidelity.** A ported formula silently diverges from the source (operator precedence, log base, unit assumption). | Medium | High | The 52 golden CSVs are the backstop — every calculator must pass its vectors before its inventory row is checked. Add round-trip/property tests for converters and dimensional-sanity checks ([doc 09](09-testing-and-golden-vectors.md)). MicrostripTrace (the worst offender) is converted first, in the pilot. |
| **R6** | **Inconsistent quality across parallel work.** Many calculators by many hands diverge in style/pattern. | Medium | Medium | One reviewer owns pattern consistency; the per-calculator checklist is encoded in the PR template; `static_assert(Calculator<…>)` mechanically enforces the shape ([doc 06](06-calculator-design-pattern.md)). |
| **R7** | **Install/export breakage for real consumers.** `find_package(emc)` works in the build tree but not when installed. | Low | Medium | Exercise `find_package` against the *installed* package from Phase 0 (throwaway consumer) and again in Phase 4 (the real app) and Phase 5 (sample project) — keep one in CI permanently ([doc 08](08-build-system-cmake.md)). |

---

## 4. Definition of Done (whole conversion)

The program is **done** when *all* of the following hold:

1. **The library exists and is complete.** Every calculator in [doc 07](07-calculator-inventory.md) (~52; the 44 with real math) is implemented as an `(Input, Result, `[[nodiscard]]`std::expected calculate())` triple under the canonical namespaces, satisfying the `Calculator` concept, with `validate()` capturing the source's range checks ([doc 06](06-calculator-design-pattern.md)).
2. **One source of truth.** Exactly one `constexpr` definition of each constant (`c = 299 792 458 m/s` exactly, correct `pi`, single `mu_0`/`eps_0`/`h`) and one material database — the duplicates (`PI 3.14`, `c = 3e8`, 8+ `mu0`, 6× `PermofFreeSpace`, ~14 material tables) exist **nowhere** ([doc 04](04-constants-and-material-database.md)).
3. **Units are type-safe.** All quantities are mp-units types; the 541 hand-wired `addItem(unit, factor)` conversions and the if/else unit chains and magic `*39.37`-style factors are gone; conversions are compile-checked ([doc 03](03-quantities-and-units-mp-units.md)).
4. **Errors are values.** No `QMessageBox` in the math, no `EXIT_FAILURE` sentinel, no value-less `return`; every fallible op returns `std::expected<Result, emc::Error>` and is `[[nodiscard]]` ([doc 05](05-error-handling-and-validation.md)).
5. **Qt-free core.** No `#include <Q...>` anywhere under `include/emc/` or `src/` (grep-enforced in CI). The library depends only on the standard library and mp-units.
6. **Verified against the goldens.** All 52 golden CSVs run as Qt-free regression tests and pass; every deliberate output change is recorded and physically justified in the re-blessing log; property/constexpr tests pass ([doc 09](09-testing-and-golden-vectors.md)).
7. **The app is a thin consumer.** NinjaEMC `find_package(emc)`-links `emc::emc`; every widget is parse-units → `calculate()` → display; no domain math, constants, materials, units, or validation remain in the app; its behavior matches the goldens ([doc 08](08-build-system-cmake.md)).
8. **Shippable quality.** Builds warning-clean on the agreed toolchain matrix; passes under ASan/UBSan; fuzz targets run clean; all public headers documented; install/export verified by a downstream sample; SemVer policy published and `1.0.0` tagged.

---

## 5. Effort summary

| Phase | Effort (serial) | Parallel note |
| --- | --- | --- |
| 0 Scaffold | S–M (2–4 d) | Single-threaded (foundation of everything). |
| 1 Foundation | M–L (5–9 d) | constants / units / materials / error can be split across people. |
| 2 Pilot | M (4–6 d) | Harness + 3 calculators; somewhat serial (pattern not yet final). |
| 3 Bulk | XL (3–5 wk serial; ~1.5–2.5 wk with 3–4 engineers) | Highly parallel by category; Component (B6) needs the most hands. |
| 4 Rewire app | L (1–2 wk) | Per-widget; can trickle in during late Phase 3. |
| 5 Harden & release | M–L (5–10 d) | Docs/fuzz/sanitizers parallelizable. |

---

## 6. Sequencing / timeline diagram

```text
LEGEND:  ===  critical path (must finish before the next depends-on starts)
         ...  can run in parallel / trickle in
         |    dependency edge

 PHASE 0  Scaffold
 ============================
   repo + CMake + mp-units + CI + skeleton
            |
            v
 PHASE 1  Foundation
 ============================
   emc::constants ...┐
   emc::units .......┤  (these 4 run in PARALLEL)
   emc::materials ...┤
   emc::Error .......┘
            |
            v
 PHASE 2  Pilot  (pattern + harness — somewhat serial)
 ============================
   golden-CSV harness ===> SkinDepth ===> Wavelength<->Freq ===> MicrostripTrace
                                                                       |
                                              [PATTERN SIGN-OFF gate]  v
 PHASE 3  Bulk migration  (parallel by category once pattern is signed off)
 ============================
   B1 Basic .........................┐
   B2 Converter .....................┤
   B3 Testing/Filtering/Grounding ...┤   all start together; independent;
   B4 Cabling .......................┤   one PR per calculator, each w/ its golden
   B5 Shielding (12-mode) ...........┤
   B6 Component (BIGGEST — most hands)┤
   B7 EMC Predictions ...............┘   (starts once shared helpers, e.g. skin depth, merged)
            |
            |  (a widget can be rewired as soon as ITS calculator merges -> Phase 4 trickles in here)
            v
 PHASE 4  Rewire Qt app  (parallel per widget)
 ============================
   find_package(emc) ===> per-widget: Input->calculate()->display ...
                          delete duplicated constants/materials/units/validation
                          verify against goldens
            |
            v
 PHASE 5  Harden & release
 ============================
   ASan/UBSan (started back in Phase 2) ...┐
   fuzzing ................................┤  parallel
   doxygen + usage docs ..................┤
   install/export + downstream sample ....┘
            |
            v
        v1.0.0  TAG + RELEASE NOTES (lists re-blessed/corrected behaviors)

 PARALLELISM SUMMARY
 -------------------
   * Phase 1's four foundation pieces: parallel.
   * Phase 3's seven batches: parallel; Component (B6) is ~40% of the work — staff it heaviest.
   * Phase 4 overlaps the tail of Phase 3 (rewire a widget the moment its calculator lands).
   * Phase 5's hardening tracks (sanitizers/fuzz/docs/packaging): parallel; sanitizers run from Phase 2 on.
   * Critical path: 0 -> 1 -> 2(pattern sign-off) -> 3(Component) -> 4 -> 5.
```

---

## Cross-references

- [`00-overview-and-goals.md`](00-overview-and-goals.md) — vision, scope, non-goals, success criteria, glossary (the Definition of Done in §4 realizes its success criteria).
- [`01-architecture-and-layout.md`](01-architecture-and-layout.md) — the layers, namespaces, and dependency rules the phases build in order.
- [`02-modern-cpp-feature-catalog.md`](02-modern-cpp-feature-catalog.md) — the C++23 features (and C++26 forward-looking items) referenced in R1/R2.
- [`03-quantities-and-units-mp-units.md`](03-quantities-and-units-mp-units.md) — the `emc::units` vocabulary delivered in Phase 1 (retires the 541 conversions).
- [`04-constants-and-material-database.md`](04-constants-and-material-database.md) — `emc::constants` + `emc::materials` delivered in Phase 1 (the single source of truth in DoD §2).
- [`05-error-handling-and-validation.md`](05-error-handling-and-validation.md) — `emc::Error` / `std::expected` / `validate` delivered in Phase 1, used by every calculator.
- [`06-calculator-design-pattern.md`](06-calculator-design-pattern.md) — the pattern proven in Phase 2 and applied per-calculator in Phase 3 (the implementer checklist).
- [`07-calculator-inventory.md`](07-calculator-inventory.md) — the closed work-list and category mapping that drives the Phase 3 batches.
- [`08-build-system-cmake.md`](08-build-system-cmake.md) — the CMake skeleton (Phase 0), install/export (Phase 5), and app rewire (Phase 4).
- [`09-testing-and-golden-vectors.md`](09-testing-and-golden-vectors.md) — the golden-CSV harness (Phase 2), the 52 regression vectors, re-blessing log, property/constexpr/fuzz tests.
