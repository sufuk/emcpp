# Implementation Guides — Index ⚡

> [!NOTE]
> This folder turns the high-level plan ([`../README.md`](../README.md)) into **code-level,
> copy-paste-quality** implementation guides — public header + compiled `.cpp` + worked example +
> Catch2 v3 unit tests — for **all 51** calculators in the modern-C++ `emc` library.

The top-level plan says *what* and *why*. These guides say **how**, line by line: every calculator
ships realistic C++23 code you can paste straight into the source tree.

---

## How the guides are organized

- **[`00-foundation-code.md`](00-foundation-code.md) is the shared bedrock — read it first.** It gives
  the *complete, canonical* code for the `emc` core: `include/emc/core/{error,constants,units,
  materials,calculator}.hpp` plus the Catch2 v3 test-support helpers (`emc::test::approx` and the
  reference-vector loader). Every calculator guide `#include`s these headers, returns
  `emc::Result<XxxResult>`, validates with `emc::in_range` / `require_positive` / `require_nonzero`,
  pulls constants from `emc::constants`, materials from `emc::materials`, and unit-compares expected
  values with `emc::test::approx`.

> [!IMPORTANT]
> If any calculator guide disagrees with a signature in `00`, **`00` wins.**

- **Each `01`..`15` guide maps to one library header-group / namespace.** A guide opens with an H1
  title and a one-line purpose, an intro naming the calculators it covers and their target
  namespace + header, then **one H2 section per calculator**. Every per-calculator section follows the
  same template:
  1. **Overview** — what it computes and the standard EMC engineering formula it implements.
  2. **Public header** — the `Input` aggregate (mp-units-typed fields, sensible defaults), the
     `Result` struct, `[[nodiscard]]` `calculate()` (and `validate()`) declarations.
  3. **Implementation** — the formula using `emc::constants` / `emc::materials` / `emc::units` and
     mp-units, with `validate()` rejecting out-of-domain inputs and `calculate()` threading
     `std::expected` (monadic `and_then` / `transform`).
  4. **"Modern C++ features used here — and why"** — a bullet list, each feature tied to the EMC
     domain reason it serves.
  5. **Example usage** — designated initializers + unit literals, `calculate()`, both `expected` arms.
  6. **Unit tests** — a known-value test against a textbook closed-form result, plus round-trip /
     monotonicity / property tests, validation/edge tests asserting the right `ErrorCode`, and a
     `constexpr` / `static_assert` test where the math allows it.

---

## Guide → namespace/header → calculators (sums to 51)

| Guide | Namespace / header(s) | Calculators covered | Count |
| --- | --- | --- | --- |
| [`00-foundation-code.md`](00-foundation-code.md) | `emc` core — `include/emc/core/{error,constants,units,materials,calculator}.hpp` + test support | *(shared bedrock — no leaf calculators)* | 0 |
| [`01-basic.md`](01-basic.md) | `emc::basic` — `include/emc/basic/` | Skin Depth; Decibel (bidirectional); Dipole Antenna; Loop Antenna; Far-Field Criteria | 5 |
| [`02-converter.md`](02-converter.md) | `emc::converter` — `include/emc/converter/` | Antenna Factor → Gain; E-Field → Power Density; Energy ↔ Frequency; Wavelength ↔ Frequency; VSWR / RC / RL / ML / IL | 5 |
| [`03-component-capacitance.md`](03-component-capacitance.md) | `emc::component` — `capacitance.hpp` | Parallel Plate; Sphere | 2 |
| [`04-component-inductance.md`](04-component-inductance.md) | `emc::component` — `inductance.hpp` | Circular Loop; Connector Pin; Rectangular Loop; Solenoid; Square Loop; Toroid; Via | 7 |
| [`05-component-resistance.md`](05-component-resistance.md) | `emc::component` — `resistance.hpp` | Circuit Board Trace; Cylindrical Conductor; Rectangular Conductor; Standard Gauge Wire (AWG) | 4 |
| [`06-component-board-impedance.md`](06-component-board-impedance.md) | `emc::component` — bidirectional board-impedance solvers | Microstrip Trace; Stripline Trace; Dual Stripline Trace; Embedded Microstrip | 4 |
| [`07-component-transmission-line.md`](07-component-transmission-line.md) | `emc::component` — `transmission_line.hpp` | Coaxial Line; Microstrip Line; Stripline; Narrow Trace Over Plane; Wide Trace Over Plane; Wire Over Plane; Wire Pair | 7 |
| [`08-component-harmonic-trap.md`](08-component-harmonic-trap.md) | `emc::component` — `harmonic_trap.hpp` | Harmonic Trap (waveform) | 1 |
| [`09-prediction.md`](09-prediction.md) | `emc::prediction` — `include/emc/prediction/` | ESD Coupling Level; Lightning Coupling Level; RF E-Field (from EIRP); Friis Transmission | 4 |
| [`10-shielding-cavity.md`](10-shielding-cavity.md) | `emc::shielding` — cavity-resonance header | Rectangular Enclosure; Cylindrical Enclosure; Circuit Board Planes | 3 |
| [`11-shielding-effectiveness.md`](11-shielding-effectiveness.md) | `emc::shielding` — shielding-effectiveness header | Aperture (slot / round); Near-Field SE (E/H); Plane-Wave SE; Slot (λ/2 resonance) | 4 |
| [`12-filtering.md`](12-filtering.md) | `emc::filtering` — `include/emc/filtering/` | Ferrite Toroid Impedance | 1 |
| [`13-cabling.md`](13-cabling.md) | `emc::cabling` — `include/emc/cabling/` | Cable Braid Optical Coverage; Crosstalk NEXT/FEXT | 2 |
| [`14-grounding.md`](14-grounding.md) | `emc::grounding` — `include/emc/grounding/` | Microstrip Line Current Distribution (ground-plane J) | 1 |
| [`15-testing.md`](15-testing.md) | `emc::testing` — `include/emc/testing/` | Noise Figure of an RF Receiver (Friis cascade, N-stage) | 1 |
| **Total** | | | **51** |

> [!NOTE]
> Counts come from the H2 calculator sections actually present in each guide. `00` is foundation
> code (no leaf calculators), so it contributes 0 to the 51.

---

## Suggested implementation order

Build **foundation first**, then a thin **pilot vertical slice** that exercises the whole pattern end
to end (one forward-only calc, one bidirectional converter, one branchy geometry calc), then fan out by
category.

1. **Foundation** — implement everything in [`00-foundation-code.md`](00-foundation-code.md):
   `error.hpp`, `constants.hpp`, `units.hpp`, `materials.hpp`, `calculator.hpp`, plus the test-support
   helpers. Nothing else compiles without it.

2. **Pilot slice — prove the pattern on three representative shapes:**
   - **Skin Depth** — [`01-basic.md`](01-basic.md) → first forward-only calculator; exercises
     `emc::materials`, the `Custom`-vs-named-material path, and the reference-value harness.
   - **Wavelength ↔ Frequency** — [`02-converter.md`](02-converter.md) → first **bidirectional**
     calculator; exercises the distinct-solver-functions pattern and forward-then-inverse round-trip
     tests.
   - **Microstrip Trace** — [`06-component-board-impedance.md`](06-component-board-impedance.md) → first
     **branchy, bidirectional geometry** calc (`solve_impedance` / `solve_width` round-trip); validates
     the design under real complexity before scaling out.

3. **Fan out by category**, reusing the now-proven pattern:
   - Finish **`emc::basic`** ([`01`](01-basic.md)) and **`emc::converter`** ([`02`](02-converter.md)).
   - Knock out the **`emc::component`** group together so they share `detail::` cores and headers:
     capacitance ([`03`](03-component-capacitance.md)), inductance ([`04`](04-component-inductance.md)),
     resistance ([`05`](05-component-resistance.md)), board-impedance
     ([`06`](06-component-board-impedance.md)), transmission-line
     ([`07`](07-component-transmission-line.md)), harmonic-trap ([`08`](08-component-harmonic-trap.md)).
   - **`emc::prediction`** ([`09`](09-prediction.md)) and the **`emc::shielding`** pair
     ([`10`](10-shielding-cavity.md), [`11`](11-shielding-effectiveness.md)).
   - The single-calculator tails: **`emc::filtering`** ([`12`](12-filtering.md)),
     **`emc::cabling`** ([`13`](13-cabling.md)), **`emc::grounding`** ([`14`](14-grounding.md)),
     **`emc::testing`** ([`15`](15-testing.md)).

> [!TIP]
> A generic front end (for example, printing results with `std::print`) stays framework-agnostic:
> consume `emc::Result<XxxResult>` directly, format the success arm, and surface the `ErrorCode` on
> the failure arm — no GUI dependency required.

---

## Where status lives

These guides are the **"how"** for each calculator. The per-calculator **Status** (TODO / done,
validation ranges, material dependence, proposed namespace + header + function name) is the
**"what/where"**, and it lives in [`../07-calculator-inventory.md`](../07-calculator-inventory.md).
Treat doc 07 as the spec for naming/placement: each TODO row there points at exactly one H2 section in
one of these guides.

---

## Cross-references (top-level plan, the "why" at depth)

- [`../02-modern-cpp-feature-catalog.md`](../02-modern-cpp-feature-catalog.md) — the C++23 feature
  catalog the per-calculator "features used here — and why" bullets draw from.
- [`../03-quantities-and-units-mp-units.md`](../03-quantities-and-units-mp-units.md) — the mp-units
  vocabulary and the `emc::units` aliases.
- [`../04-constants-and-material-database.md`](../04-constants-and-material-database.md) —
  `emc::constants` and `emc::materials` as the single source of truth.
- [`../05-error-handling-and-validation.md`](../05-error-handling-and-validation.md) —
  `Error` / `ErrorCode` / `std::expected`.
- [`../06-calculator-design-pattern.md`](../06-calculator-design-pattern.md) — the Input/Result/
  `calculate` triple and the `Calculator` concept.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the reference-vector
  harness and tolerances.
