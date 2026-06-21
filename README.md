# emcpp

[![ci](https://github.com/sufuk/emcpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sufuk/emcpp/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A53.28-064F8C.svg)

A modern **C++23 library of closed-form EMC (electromagnetic-compatibility) engineering calculators**. Each calculator solves one well-known EMC formula — conductor skin depth, microstrip/stripline characteristic impedance, the Friis link budget, shielding effectiveness, cable crosstalk, cavity resonances, and many more — with **compile-time unit safety** powered by [mp-units](https://github.com/mpusz/mp-units).

```cpp
#include <emc/basic/skin_depth.hpp>

emc::basic::SkinDepthInput in{ .frequency = 1.0 * MHz, .material = emc::materials::Material::Copper };
auto result = emc::basic::calculate(in);   // -> emc::Result<SkinDepthResult>
// result->skin_depth == ~65.21 um  (wrong units simply don't compile)
```

---

## Table of contents

- [Highlights](#highlights)
- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Using emcpp in your project](#using-emcpp-in-your-project)
- [The API model](#the-api-model)
- [Usage example](#usage-example)
- [Calculator catalog](#calculator-catalog)
- [Testing](#testing)
- [Continuous integration](#continuous-integration)
- [Project layout](#project-layout)
- [Versioning & releases](#versioning--releases)
- [License](#license)

---

## Highlights

- **Unit-safe by construction.** All inputs and outputs are [mp-units](https://github.com/mpusz/mp-units) quantities (`Frequency` in Hz, `Length` in m, `Impedance` in Ω, …). Mixing units is a **compile error**, not a runtime surprise. Call sites read like physics: `1.5 * mm`, `1000.0 * Hz`.
- **Errors are values, not exceptions.** Every `calculate()` returns `emc::Result<T>` (`std::expected<T, emc::Error>`). Failures carry a structured `ErrorCode`, a human message, the offending field name, the valid range, and a captured `std::source_location`.
- **Compile-time contracts.** Each calculator is bound to a `Calculator` / `ValidatedCalculator` concept via a zero-cost tag struct and proven with `static_assert` — generic code, no virtual dispatch.
- **Logarithmic types are distinct.** `emc::units::Decibel` / `emc::units::Dbm` are kept separate from linear units; crossing the linear↔log boundary is always an explicit `to_power` / `to_dbm` / `to_ratio` call.
- **Batteries included.** Built-in conductor materials table (copper, silver, gold, aluminium, nickel, …) and exact 2019-SI EM constants (`c`, `μ₀`, `ε₀`, `Z₀`, elementary charge) whose physical identities are checked at compile time.
- **Broad coverage.** 60+ calculators across 9 EMC domains — see the [catalog](#calculator-catalog).

## Requirements

| Requirement | Version |
|---|---|
| C++ standard | **C++23** |
| CMake | **≥ 3.28** |
| Compiler | GCC, Clang, AppleClang, or MSVC with C++23 support (CI runs **gcc-14** and **clang-18**) |
| [mp-units](https://github.com/mpusz/mp-units) | **v2.5.0** — fetched automatically if not found |
| [Catch2](https://github.com/catchorg/Catch2) | **v3.7.1** — tests only, fetched automatically |

Dependencies are resolved with `find_package(... CONFIG QUIET)` first and fall back to `FetchContent` (shallow clone, pinned tags), so a clean checkout builds with **no manual dependency setup**.

## Quick start

```sh
# Configure (Release is the default build type if none is set)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run the test suite (tests are built by default at top level)
ctest --test-dir build --output-on-failure
```

Notes:
- In-source builds are blocked — use a separate `build/` directory.
- The **Ninja** generator works too (C++ module scanning is disabled): `cmake -S . -B build -G Ninja`.
- `CMAKE_EXPORT_COMPILE_COMMANDS` is ON; `BUILD_SHARED_LIBS` is OFF by default (static library).

### CMake options

| Option | Default | Purpose |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build `emc` as a shared library instead of static. |
| `EMC_BUILD_TESTS` | `ON` top-level / `OFF` as subproject | Build the test suite and pull in Catch2. |
| `EMC_BUILD_EXAMPLES` | `ON` top-level / `OFF` as subproject | Build the usage examples (`examples/`). |
| `EMC_WARNINGS_AS_ERRORS` | `ON` top-level / `OFF` as subproject | Treat warnings as errors (`-Werror` / `/WX`). |
| `CMAKE_BUILD_TYPE` | `Release` (when unset, single-config) | Standard CMake build configuration. |

## Using emcpp in your project

Install the library and consume it via `find_package`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /your/install/prefix
```

```cmake
# In your CMakeLists.txt
find_package(emc CONFIG REQUIRED)          # version compatibility: SameMajorVersion
target_link_libraries(your_target PRIVATE emc::emc)
```

The installed package config calls `find_dependency(mp-units CONFIG)`, so **mp-units must be findable by the consumer**. The package can also be consumed directly from the build tree (an `emcTargets.cmake` is exported there).

## The API model

Every calculator follows the same shape, so once you know one you know them all:

```cpp
namespace emc::basic {                       // category namespace mirrors the include path

  struct SkinDepthInput  { /* fields, designated-initializer friendly, with defaults */ };
  struct SkinDepthResult { /* computed quantities */ };

  // Free functions — no objects to construct, no virtual calls.
  std::expected<void, emc::Error> validate(const SkinDepthInput&);
  emc::Result<SkinDepthResult>    calculate(const SkinDepthInput&);   // validates, then computes

  // A zero-data tag binds the (Input, Result, calculate) triple to a concept.
  struct SkinDepth { using Input = SkinDepthInput; using Result = SkinDepthResult; /* ... */ };
  static_assert(emc::ValidatedCalculator<SkinDepth>);                 // contract proven at compile time
}
```

Key conventions:

- **Inputs/outputs are mp-units quantities** (via `emc::units` aliases). Dimensionless empirical ratios such as relative permeability (`μ_r`) and permittivity (`ε_r`) are deliberately plain `double`.
- **Results flow through `emc::Result<T>` = `std::expected<T, emc::Error>`.** Check with `if (r) { use(*r); } else { handle(r.error()); }`.
- **Some calculators are bidirectional** — e.g. the microstrip/stripline traces add inverse `solve_height` / `solve_width` / `solve_thickness` functions.
- **Materials** are looked up with `emc::materials::properties(Material)` (also `from_name` / `to_name` helpers).

## Usage example

`examples/skin_depth.cpp` computes conductor skin depth two ways — from the built-in material table and from a user-supplied conductor:

```cpp
#include <print>
#include <mp-units/systems/si.h>            // unit symbols: Hz, MHz, um, S, m, ...

#include <emc/basic/skin_depth.hpp>         // SkinDepthInput / SkinDepthResult / calculate
#include <emc/core/materials.hpp>           // emc::materials::Material

int main() {
    using namespace mp_units;
    using namespace mp_units::si::unit_symbols;

    // 1) Material-table path: pick a conductor, leave sigma/mu_r at their defaults.
    const emc::basic::SkinDepthInput cu{
        .frequency = 1.0 * MHz,
        .material  = emc::materials::Material::Copper,
    };
    if (const auto r = emc::basic::calculate(cu))
        std::println("Cu @ 1 MHz: skin depth = {}", r->skin_depth.in(um));   // ~65.21 um
    else
        std::println(stderr, "error: {}", r.error().what());

    // 2) Custom path: supply conductivity and relative permeability yourself.
    const emc::basic::SkinDepthInput custom{
        .frequency             = 100.0 * MHz,
        .material              = emc::materials::Material::Custom,
        .conductivity          = 3.5e7 * (S / m),   // sigma is a typed S/m quantity
        .relative_permeability = 1.0,               // mu_r is a plain double
    };
    if (const auto r = emc::basic::calculate(custom))
        std::println("Custom @ 100 MHz: skin depth = {}", r->skin_depth.in(um));  // ~8.52 um

    return 0;
}
```

## Calculator catalog

> 60+ calculators across 9 domains. Headers live under `include/emc/<domain>/`.

### `basic` — fundamentals
| Calculator | What it computes |
|---|---|
| Skin depth | Conductor skin depth `δ = √(1/(π·f·μ₀·μ_r·σ))` from frequency + material (or custom σ, μ_r). |
| Decibel / level conversions | Convert between dB, voltage gain, power gain; and between dBm ↔ power ↔ RMS/peak voltage across a load. |
| Dipole near-field | Short-dipole `E_r`, `E_θ` [V/m] and `H_φ` [A/m] from current, length, distance, frequency, angle. |
| Loop near-field | Small-loop `H_r`, `H_θ` [A/m] and `E_φ` [V/m] (magnetic dual of the dipole). |
| Far-field criteria | Wavelength + reactive/radiating near-field boundaries; branches on electrically large vs small. |

### `cabling`
| Calculator | What it computes |
|---|---|
| Braid optical coverage | Shield optical coverage fraction, weave angle, and fill factor from braid weave geometry. |
| Crosstalk | Near-end (`V_NE`) and far-end (`V_FE`) coupled crosstalk [dB] from terminations and mutual L/C (with a −200 dB floor). |

### `component` — lumped parts & transmission lines
| Calculator | What it computes |
|---|---|
| Capacitance | Parallel-plate `C = ε₀ε_r·A/d` and isolated-sphere `C = 4πε₀·r`. |
| Inductance | Circular / rectangular / square loop, solenoid, toroid, via, and connector-pin (self + mutual) inductances. |
| Resistance | AC/DC resistance + skin depth of traces, round/rectangular conductors, and AWG-gauge wires (incl. AWG parser). |
| Harmonic trap | Trapezoidal pulse-train spectrum: fundamental, n-th harmonic line, and 0/−20/−40 dB-per-decade envelope. |
| Transmission lines | Z₀ + per-length L/C of coax, microstrip, stripline, traces/wires over a plane, and wire pairs. |
| Microstrip trace | Z₀, C₀, propagation delay (IPC/Wheeler) — **forward + inverse** solve for H / T / W. |
| Embedded microstrip trace | Buried-microstrip Z₀, C₀, delay under a thin dielectric cover (forward). |
| Stripline trace | Centered-stripline Z₀, C₀, delay — **forward + inverse** solve for H / T / W. |
| Dual stripline trace | Offset dual-stripline Z₀, C₀, delay — **forward + inverse** solve for H / gap / T / W. |

### `converter`
| Calculator | What it computes |
|---|---|
| Antenna factor ↔ gain | Antenna factor [dB/m] at frequency → realized gain [dBi] (+ wavelength). |
| E-field → power density | `P_D = E²/η` [W/m²] (η defaults to free-space 377 Ω). |
| Energy ↔ frequency | Planck relation `E = h·f` both directions, plus eV ↔ joule helpers. |
| VSWR mismatch | Reflection coefficient Γ, return loss, mismatch loss, and insertion loss from a single VSWR. |
| Wavelength ↔ frequency | `f = c/λ` and `λ = c/f`. |

### `filtering`
| Calculator | What it computes |
|---|---|
| Ferrite toroid impedance | Wound-toroid inductance plus reactance, lossy resistance, and total \|Z\| from complex permeability + geometry. |

### `grounding`
| Calculator | What it computes |
|---|---|
| Microstrip return current | Lateral ground-plane current density `J(x) = (I₀/πw)·1/(1+(x/h)²)` beneath a microstrip trace. |

### `prediction`
| Calculator | What it computes |
|---|---|
| ESD coupling | Voltage induced in a ground loop by an ESD current ramp (mutual-inductance / dI/dt model). |
| Friis link budget | Received power [dBm] over a free-space link from Tx power, Tx/Rx gains, frequency, range. |
| Lightning coupling | Loop voltage induced by a lightning current slew rate dI/dt. |
| RF far-field | Far-field `E`, `H`, and power density from EIRP and distance. |

### `shielding`
| Calculator | What it computes |
|---|---|
| Aperture absorption | Below-cutoff waveguide absorption loss [dB] of a slot or round hole. |
| Slot SE | λ/2-resonant slot shielding effectiveness `SE = 20·log10(λ/2L)`. |
| Shielding effectiveness | Near-field (E/H) and plane-wave absorption + reflection + total SE [dB] from material/geometry. |
| Cavity resonance | Resonant modes of rectangular, cylindrical, and circuit-board plane-pair cavities (+ dominant mode). |

### `testing`
| Calculator | What it computes |
|---|---|
| Cascade noise figure | Overall receiver-chain NF [dB] and total gain via the Friis cascade equation. |

### `core` — foundation
`emc::Result<T>` / `emc::Error` / `ErrorCode`, the `Calculator` / `ValidatedCalculator` concepts, the `emc::units` quantity aliases and dB/dBm wrappers, exact `emc::constants`, and the `emc::materials` table.

## Testing

Tests use **Catch2 v3** (`Catch2::Catch2WithMain`) and are registered with CTest via `catch_discover_tests`. The suite is **~200 tests / 993 assertions** across all domains, with two unit-aware support helpers:

- `tests/support/approx.hpp` — relative-tolerance `approx()` and a Catch2 `WithinUnits` matcher built on mp-units quantities.
- `tests/support/csv.hpp` — header-only CSV reference-vector loader.

```sh
ctest --test-dir build --output-on-failure
```

## Continuous integration

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs on every push/PR against `main`:

| Job | What it does |
|---|---|
| **build-test** | Matrix of `{gcc-14, clang-18} × {Debug, Release}` on ubuntu-24.04 — configure, build (`-Werror`), and `ctest`. |
| **sanitizers** | clang-18 / Debug with **AddressSanitizer + UndefinedBehaviorSanitizer** (`halt_on_error`, leak detection). |
| **install-smoke** | Installs the package, then builds a tiny out-of-tree `find_package(emc)` consumer to prove it is self-contained. |

## Project layout

```
include/emc/<domain>/   public headers (one per calculator family)
src/<domain>/           out-of-line implementations
examples/               runnable usage examples
tests/<domain>/         Catch2 test suites + tests/support/ helpers
cmake/                  dependencies, warnings, install rules, package config
.github/workflows/      ci.yml (build/test/sanitizers/install) + release.yml
```

## Versioning & releases

Releases are **tag-driven** ([`.github/workflows/release.yml`](.github/workflows/release.yml)). Pushing a SemVer tag (`vX.Y.Z`, with `-rc/-alpha/-beta` marked pre-release) runs a gated pipeline:

1. **Gate** — full build + `ctest` on the tagged commit with both gcc-14 and clang-18.
2. **Package** — verifies the tag matches `project(... VERSION ...)`, builds deterministic `emc-<ver>-linux-x86_64` archives (`.tar.gz`, `.zip`) plus a `.sha256` sums file.
3. **Publish** — creates the GitHub Release with auto-generated notes and attaches the artifacts.

## License

No license file is currently present in this repository. Add a `LICENSE` before distributing or accepting external contributions.
