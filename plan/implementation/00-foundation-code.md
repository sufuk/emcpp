# Foundation Code — the shared bedrock every calculator uses ⚙️

> Purpose: the **complete, copy-paste-quality** C++23 code for the shared `emc` core
> (`error.hpp`, `constants.hpp`, `units.hpp`, `materials.hpp`, `calculator.hpp`) plus the Catch2 v3
> test-support helpers. Every per-calculator guide reuses these **exact declarations** — names,
> namespaces, and signatures here are *canonical*.

The 15 calculator guides all `#include` headers from `include/emc/core/`, return
`emc::Result<XxxResult>`, validate with `emc::in_range` / `emc::require_positive` /
`emc::require_nonzero`, pull constants from `emc::constants`, materials from `emc::materials`, and
compare values with `emc::test::approx`.

> [!IMPORTANT]
> If any calculator guide disagrees with a signature below, **this file is the implementation
> contract.** The short canonical names (`c`, `mu0`, `eps0`, `from_name`, …) are the ones every guide
> uses, even where a top-level design doc sketched a longer spelling.

Design rationale lives in the top-level plan docs and is *cited*, not repeated:

- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — reference-vector harness and tolerances.

---

## 0. Directory layout for the core + tests

```text
emcpp/
├── include/
│   └── emc/
│       └── core/
│           ├── error.hpp        # ErrorCode, Error, Result<T>, in_range/require_positive/require_nonzero
│           ├── constants.hpp    # emc::constants:: pi, c, mu0, eps0, h, z0   (+ static_assert relations)
│           ├── units.hpp        # emc::units:: Frequency, Length, ... + Decibel/Dbm wrappers
│           ├── materials.hpp    # emc::materials:: Material, MaterialProperties, properties(), named consts
│           └── calculator.hpp   # emc::Calculator / ValidatedCalculator concepts
├── src/
│   ├── core/
│   │   └── materials.cpp        # (optional) out-of-line non-constexpr helpers, if any
│   └── <category>/<name>.cpp    # each calculator's compiled body (per its own guide)
└── tests/
    ├── CMakeLists.txt
    ├── support/
    │   ├── csv.hpp              # emc::test::load_csv  (ranges/string_view parser)
    │   └── approx.hpp          # emc::test::approx     (unit-aware comparison matcher)
    ├── reference/              # reference CSVs copied here at configure time
    │   ├── SkinDepth.csv
    │   ├── MicrostripTrace.csv
    │   └── … (one per calculator)
    ├── core/
    │   ├── constants_test.cpp
    │   └── materials_test.cpp
    └── <category>/<name>_test.cpp   # each calculator's test file (per its own guide)
```

> [!NOTE]
> **Why split `include/emc/core/` headers from compiled `src/`?** mp-units is template-heavy; the
> decision is a *compiled* library, not header-only, so the expensive quantity
> instantiations live in `src/*.cpp` and consumers pay only for the alias declarations they touch. The
> core headers are deliberately algorithm-free. Almost everything is `constexpr`/`inline constexpr`, so
> `materials.cpp` exists mainly as a home for any future non-`constexpr` helper and to anchor the CMake
> target. See [`16-build-and-scaffolding.md`](16-build-and-scaffolding.md).

---

## 1. `include/emc/core/error.hpp`

A single, GUI-free, value-oriented error channel: a scoped error code, a structured `Error` payload,
and a `Result<T>` alias used library-wide.

```c++
// include/emc/core/error.hpp
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>      // std::pair, std::move

namespace emc {

// ---------------------------------------------------------------------------
//  ErrorCode — the machine-readable failure category.
//  Scoped enum class : no implicit int conversion, so a code can never be
//  confused with a status integer, a resistivity, or a control index.
// ---------------------------------------------------------------------------
enum class ErrorCode : std::uint8_t {
    OutOfRange,        // a quantity is outside its physically/numerically valid interval
    InvalidInput,      // structurally bad input (NaN, empty selection, wrong combination)
    DomainError,       // math-domain violation (log of <= 0, sqrt of negative, acos > 1, ...)
    DivisionByZero,    // a denominator the formula cannot tolerate evaluated to 0
    UnknownMaterial,   // Material id has no table row (or Custom queried without props)
    NotConverged,      // an iterative solver hit its iteration cap without a result
    Unsupported,       // a valid-but-not-implemented case
};

// Stable, lowercase token for logs / CSV diffs / std::format. constexpr so it is
// usable in compile-time tables (doc 09 §3).
[[nodiscard]] constexpr std::string_view to_string(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::OutOfRange:      return "out_of_range";
        case ErrorCode::InvalidInput:    return "invalid_input";
        case ErrorCode::DomainError:     return "domain_error";
        case ErrorCode::DivisionByZero:  return "division_by_zero";
        case ErrorCode::UnknownMaterial: return "unknown_material";
        case ErrorCode::NotConverged:    return "not_converged";
        case ErrorCode::Unsupported:     return "unsupported";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
//  Error — the rich, structured payload. A front end and the tests can inspect
//  it field-by-field instead of parsing a free-text string.
//
//  Intentionally an aggregate (no user-declared ctor) so the factory helpers and
//  call sites can use designated initializers, and `.message` / `.code` are plain
//  members. `.message()`-style access is provided via the accessor methods below
//  WITHOUT making the type non-aggregate (the methods do not count against
//  aggregate-ness; only user-declared *constructors* / private data would).
// ---------------------------------------------------------------------------
struct Error {
    ErrorCode                            code;
    std::string                          message;
    std::string_view                     field{};
    std::optional<std::pair<double, double>> range{};   // (lo, hi) in the field's display unit
    std::source_location                 where = std::source_location::current();

    // Accessors required by the pinned canonical API (".message accessor").
    // These coexist with the public data members; calculators may read either.
    [[nodiscard]] ErrorCode        code_of()  const noexcept { return code; }
    [[nodiscard]] const std::string& message_of() const noexcept { return message; }

    // Convenience one-liner for CLI/CI logs. A front end builds its own localized
    // string from the structured fields; this is a fallback, not the path.
    [[nodiscard]] std::string what() const {
        std::string s;
        s += '[';
        s += to_string(code);
        s += "] ";
        s += message;
        if (!field.empty()) { s += " (field: "; s += field; s += ')'; }
        if (range) {
            s += " (allowed: [";
            s += std::to_string(range->first);
            s += ", ";
            s += std::to_string(range->second);
            s += "])";
        }
        return s;
    }
};
```

> [!NOTE]
> **`.message` accessor.** Because `Error` is an aggregate, `.message` is a public *data member*
> (`err.message`) — the simplest possible accessor, no getter call needed. The extra
> `message_of()`/`code_of()` methods exist for call sites that prefer method syntax; both forms read
> the same storage. (A method does not break aggregate-ness; only a user-declared constructor or a
> private member would.) Calculator guides use `err.message` and `err.code` directly.

### Factory helpers + the `Result<T>` alias and validators

```c++
// include/emc/core/error.hpp  (continued)

namespace emc {

// ---------------------------------------------------------------------------
//  Result<T> — the universal success-or-Error channel for the whole library.
//  Every calculate()/solve_*()/properties() returns emc::Result<...>.
// ---------------------------------------------------------------------------
template <class T>
using Result = std::expected<T, Error>;

// ---------------------------------------------------------------------------
//  Error factories — keep calculators terse. Each defaults its source_location
//  to the CALL SITE, so an Error records where it was raised for free.
// ---------------------------------------------------------------------------
[[nodiscard]] inline Error out_of_range(double lo, double hi, std::string_view field,
                                        std::source_location w = std::source_location::current()) {
    return Error{
        .code    = ErrorCode::OutOfRange,
        .message = std::string{field}.append(" is out of range"),
        .field   = field,
        .range   = std::pair{lo, hi},
        .where   = w,
    };
}

[[nodiscard]] inline Error invalid_input(std::string_view why, std::string_view field = {},
                                         std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::InvalidInput, .message = std::string{why},
                  .field = field, .range = std::nullopt, .where = w };
}

[[nodiscard]] inline Error domain_error(std::string_view why, std::string_view field = {},
                                        std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::DomainError, .message = std::string{why},
                  .field = field, .range = std::nullopt, .where = w };
}

[[nodiscard]] inline Error division_by_zero(std::string_view field,
                                            std::source_location w = std::source_location::current()) {
    return Error{ .code = ErrorCode::DivisionByZero,
                  .message = std::string{field}.append(" would divide by zero"),
                  .field = field, .range = std::nullopt, .where = w };
}

// ---------------------------------------------------------------------------
//  Validation primitives — the three checks every calculator's validate() body
//  is built from. They operate on RAW doubles: a calculator extracts the numeric
//  value of an mp-units quantity in its display unit (q.numerical_value_in(u))
//  and passes that here, so the reported [lo, hi] is in the user's unit.
//
//  Returning std::expected<void, Error> is the canonical "ok or first failure"
//  shape. {} == success.  All three are [[nodiscard]] and constexpr.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::expected<void, Error>
in_range(double v, double lo, double hi, std::string_view field,
         std::source_location w = std::source_location::current()) {
    if (v < lo || v > hi)
        return std::unexpected(out_of_range(lo, hi, field, w));
    return {};
}

[[nodiscard]] constexpr std::expected<void, Error>
require_positive(double v, std::string_view field,
                 std::source_location w = std::source_location::current()) {
    if (!(v > 0.0))     // also rejects NaN (NaN > 0 is false)
        return std::unexpected(Error{ .code = ErrorCode::OutOfRange,
                                      .message = std::string{field}.append(" must be > 0"),
                                      .field = field, .range = std::nullopt, .where = w });
    return {};
}

[[nodiscard]] constexpr std::expected<void, Error>
require_nonzero(double v, std::string_view field,
                std::source_location w = std::source_location::current()) {
    if (v == 0.0)
        return std::unexpected(division_by_zero(field, w));
    return {};
}

} // namespace emc
```

### Modern C++ features used here — and why

- **`std::expected<T, Error>` (C++23)** — calculator inputs have physical domains, so an out-of-domain
  input is a *recoverable typed error*, not a thrown exception or a sentinel. Failure becomes a value
  the caller *must* handle, with result and error mutually exclusive by construction (no meaningless
  default result on the error path).
- **`enum class ErrorCode : std::uint8_t`** — a scoped, fixed-underlying-type enum cannot implicitly
  convert to `int`, so an error code can never be mistaken for a numeric result. Fixing the width at
  one byte keeps `Error` small.
- **`[[nodiscard]]` on every fallible call** — ignoring an `Error` is almost always a bug; the
  attribute turns "computed a value and discarded the failure" into a compiler warning.
- **Aggregate `Error` + designated-initializer factories** — `out_of_range(...)` builds the struct
  with named fields, so the *valid range* (e.g. `[1, 15]` for permittivity) is structured data a front
  end can echo verbatim, not free text buried in an `if`.
- **`std::source_location` defaulted in the factory args** — captures the raise site for diagnostics
  for free. It is never shown to end users.
- **`std::optional<std::pair<double,double>>` for the range** — makes "no range applies" (e.g. a
  `DivisionByZero`) a type-level state instead of a sentinel value.
- **`constexpr to_string` / `constexpr` validators** — let reference-vector tables and `static_assert`
  checks evaluate at compile time (doc 09 §5).

---

## 2. `include/emc/core/constants.hpp`

One place for every physical constant, as `inline constexpr` mp-units quantities with **correct** SI /
CODATA values. A single namespaced definition per constant is the single source of truth.

```c++
// include/emc/core/constants.hpp
#pragma once

#include <numbers>     // std::numbers::pi — one namespaced, full-precision pi
#include <cmath>       // std::sqrt is constexpr in C++23 (used in the sanity static_asserts)

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

namespace emc::constants {

using namespace mp_units;
using mp_units::si::unit_symbols::A;    // ampere
using mp_units::si::unit_symbols::F;    // farad
using mp_units::si::unit_symbols::H;    // henry
using mp_units::si::unit_symbols::J;    // joule
using mp_units::si::unit_symbols::m;    // metre
using mp_units::si::unit_symbols::s;    // second
using mp_units::si::unit_symbols::ohm;  // ohm

// ----- pi (pure math) — a plain double for raw angle/closed-form math. ---------
inline constexpr double pi = std::numbers::pi_v<double>;

// ----- c : speed of light in vacuum --------------------------------------------
// SI EXACT defining constant.  c = 299 792 458 m/s.
inline constexpr quantity c = 299'792'458.0 * (m / s);

// ----- mu0 : vacuum permeability -----------------------------------------------
// CODATA-2018 value; very close to 4*pi*1e-7 but no longer EXACT in SI.
inline constexpr quantity mu0 = 1.256'637'062'12e-6 * (H / m);

// ----- eps0 : vacuum permittivity ----------------------------------------------
// CODATA literal; the static_assert below proves it equals 1/(mu0*c^2).
inline constexpr quantity eps0 = 8.854'187'8128e-12 * (F / m);

// ----- h : Planck constant ------------------------------------------------------
// SI EXACT defining constant since the 2019 redefinition.  h = 6.626 070 15e-34 J*s.
inline constexpr quantity h = 6.626'070'15e-34 * (J * s);

// ----- z0 : impedance of free space  = sqrt(mu0/eps0) = mu0*c ~ 376.730313668 ohm
inline constexpr quantity z0 = 376.730'313'668 * ohm;

// ----- e : elementary charge (ESD / lightning coupling calculators) ------------
// EXACT since 2019.  coulomb = A*s.
inline constexpr quantity elementary_charge = 1.602'176'634e-19 * (A * s);

} // namespace emc::constants
```

> [!IMPORTANT]
> **Why `inline constexpr`, not just `constexpr`?** A header-defined `constexpr` namespace-scope
> variable has *internal* linkage by default — each translation unit gets its own copy and its own
> address (an ODR foot-gun). `inline` (C++17 inline variables) gives one shared definition across the
> whole program, which is exactly what "single source of truth" demands. The short names
> `c`, `mu0`, `eps0`, `h`, `z0`, `pi` are the canonical contract used by every calculator guide.

### Compile-time sanity relations

```c++
// include/emc/core/constants.hpp  (appended)
namespace emc::constants::detail {

using namespace mp_units;

// Relative comparison helper (avoids brittle exact == on floating point).
[[nodiscard]] constexpr bool close(double a, double b, double rel = 1e-6) noexcept {
    const double d = a - b;
    const double ad = d < 0 ? -d : d;
    const double am = a < 0 ? -a : a;
    return ad <= rel * (am == 0.0 ? 1.0 : am);
}

// 1) c == 1 / sqrt(eps0 * mu0)
inline constexpr double c_check =
    1.0 / std::sqrt((eps0 * mu0).numerical_value_in(si::farad * si::henry
                                                    / (si::metre * si::metre)));
static_assert(close(c_check, c.numerical_value_in(si::metre / si::second)),
              "c must equal 1/sqrt(eps0*mu0)");

// 2) z0 == sqrt(mu0 / eps0)
inline constexpr double z0_check =
    std::sqrt((mu0 / eps0).numerical_value_in(si::ohm * si::ohm));
static_assert(close(z0_check, z0.numerical_value_in(si::ohm)),
              "z0 must equal sqrt(mu0/eps0)");

// 3) z0 == mu0 * c   (alternative identity)
static_assert(close((mu0 * c).numerical_value_in(si::ohm),
                    z0.numerical_value_in(si::ohm)),
              "z0 must equal mu0*c");

// 4) The library mu0 is within 0.01% of 4*pi*1e-7 (the classical low-frequency value).
static_assert(close(mu0.numerical_value_in(si::henry / si::metre), 4.0 * pi * 1e-7, 1e-4),
              "mu0 should be close to 4*pi*1e-7");

// 5) pi is the full-precision pi.
static_assert(pi > 3.14159 && pi < 3.14160, "pi must be std::numbers::pi");

} // namespace emc::constants::detail
```

### Modern C++ features used here — and why

- **`std::numbers::pi` (C++20)** — one namespaced, full-precision pi shared by every calculator, so
  two formulas computing the same quantity agree to the last digit.
- **mp-units `quantity` constants** — `c`, `mu0`, `eps0`, `h`, `z0` carry their unit in the type, so
  `c * frequency` is dimension-checked and a dimensionally wrong product cannot compile.
- **`inline constexpr` variables (C++17)** — one ODR-clean definition; immutable compile-time values
  with no init-order or data-race concern.
- **`static_assert` + C++23 `constexpr <cmath>`** — the electromagnetic identities
  (`c = 1/√(ε₀μ₀)`, `z0 = √(μ₀/ε₀) = μ₀c`) are *build-time invariants*. Edit one literal into an
  inconsistent set and the build fails (doc 09 §5.1).

---

## 3. `include/emc/core/units.hpp`

The curated mp-units vocabulary used in every `Input`/`Result`. Calculator signatures read
`emc::units::Frequency`, not `quantity<isq::frequency[si::hertz], double>`. One `Rep = double` knob for
the library. Relative permeability/permittivity are **dimensionless doubles** (mu_r/eps_r), and
**dB/dBm are typed log wrappers**, never linear mp-units units.

```c++
// include/emc/core/units.hpp
#pragma once

#include <cmath>       // std::pow / std::log10 for the Decibel/Dbm wrappers (constexpr in C++23)

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

namespace emc::units {

namespace mpu = mp_units;
namespace isq = mp_units::isq;
namespace si  = mp_units::si;

// --- Representation type: one knob for the whole library. ----------
using Rep = double;

// --- Helper alias so every line below stays short. -----------------------------
template <auto Reference>
using Q = mpu::quantity<Reference, Rep>;

// ----------------------------------------------------------------------
//  Core EMC quantities — pin BOTH the ISQ quantity kind AND the SI unit.
// ----------------------------------------------------------------------
using Frequency   = Q<isq::frequency[si::hertz]>;                         // Hz
using Length      = Q<isq::length[si::metre]>;                           // m
using Area        = Q<isq::area[si::square(si::metre)]>;                 // m^2
using Time        = Q<isq::time[si::second]>;                            // s

using Voltage     = Q<isq::voltage[si::volt]>;                          // V
using Current     = Q<isq::current[si::ampere]>;                       // A
using Power       = Q<isq::power[si::watt]>;                           // W
using Impedance   = Q<isq::resistance[si::ohm]>;                        // ohm  (Z0, R, RL, ...)
using Capacitance = Q<isq::capacitance[si::farad]>;                    // F
using Inductance  = Q<isq::inductance[si::henry]>;                     // H

// Material / field quantities
using Conductivity =
    Q<isq::electrical_conductivity[si::siemens / si::metre]>;            // S/m
using Resistivity =
    Q<isq::resistivity[si::ohm * si::metre]>;                            // ohm*m
using ElectricField =
    Q<isq::electric_field_strength[si::volt / si::metre]>;               // V/m
using MagneticField =
    Q<isq::magnetic_field_strength[si::ampere / si::metre]>;             // A/m
using PowerDensity =
    Q<(isq::power / isq::area)[si::watt / si::square(si::metre)]>;        // W/m^2

// Angle (mp-units models the radian explicitly).
using Angle = Q<isq::angular_measure[si::radian]>;                       // rad

// ----------------------------------------------------------------------
//  Dimensionless — quantities of dimension ONE (ratios, coverage, VSWR).
//  NOTE: relative permeability (mu_r) and relative permittivity (eps_r) are
//  modeled as PLAIN doubles, per the pinned canonical API, because they are
//  pure ratios that almost always appear bare in the empirical formulas. The
//  generic Dimensionless quantity remains available for ratio OUTPUTS (VSWR,
//  gain, optical coverage) where carrying a kind adds value.
// ----------------------------------------------------------------------
using Dimensionless = Q<mpu::one>;                                       // generic ratio quantity

// ----------------------------------------------------------------------
//  Per-length results (microstrip etc.). Kept here so multiple guides share them.
// ----------------------------------------------------------------------
using CapacitancePerLength =
    Q<(isq::capacitance / isq::length)[si::farad / si::metre]>;          // F/m
using TimePerLength =
    Q<(isq::time / isq::length)[si::second / si::metre]>;                // s/m (propagation delay)

// ----------------------------------------------------------------------
//  dB / dBm — LOGARITHMIC. NOT mp-units linear units. A decibel is
//  10*log10(ratio); dBm is dB relative to 1 mW. Modeled as explicit typed
//  wrappers so the linear physics (Power in watts, fields in V/m) stays type-safe
//  under mp-units, and conversions are explicit functions, never implicit math.
// ----------------------------------------------------------------------
struct Decibel { double value; };   // a pure-ratio dB value, e.g. shielding effectiveness, gain
struct Dbm     { double value; };   // dB relative to 1 mW (absolute power level)

// Linear <-> log conversions are explicit functions only.
[[nodiscard]] inline Power to_power(Dbm x) noexcept {            // dBm -> W
    return std::pow(10.0, x.value / 10.0) * (si::milli<si::watt>);
}
[[nodiscard]] inline Dbm to_dbm(Power p) noexcept {             // W -> dBm
    return Dbm{ 10.0 * std::log10(p.numerical_value_in(si::milli<si::watt>)) };
}
[[nodiscard]] constexpr double to_ratio(Decibel d) noexcept {   // dB -> linear power ratio
    return std::pow(10.0, d.value / 10.0);
}
[[nodiscard]] inline Decibel to_decibel(double power_ratio) noexcept {  // linear ratio -> dB
    return Decibel{ 10.0 * std::log10(power_ratio) };
}

} // namespace emc::units
```

> [!NOTE]
> **`mu_r` / `eps_r` are doubles.** The canonical API models relative permeability and relative
> permittivity as *dimensionless doubles*, not quantities. The empirical EMC formulas (Wheeler
> microstrip, skin depth) multiply them as bare numbers, and `MaterialProperties` (next section) stores
> them as `double`. The generic `Dimensionless` quantity alias remains for ratio *outputs* (VSWR, gain)
> where a kind helps.

### Modern C++ features used here — and why

- **mp-units `quantity` aliases** — one named vocabulary (`Frequency`, `Length`, `Impedance`, …). EMC
  inputs span Hz..GHz and m..mils, so pinning each quantity's kind and unit in the type gives
  compile-time unit safety: a wrong unit factor or a dimensionally wrong product simply does not
  compile, because mp-units derives every conversion factor itself.
- **A single `Rep = double` alias** — makes a future representation change a one-line edit.
- **Typed `Decibel` / `Dbm` wrappers** — keep logarithmic semantics correct without ever letting a dB
  value masquerade as a linear mp-units unit (which would make `dBm + dBm` silently wrong). Linear
  power stays `emc::units::Power`; the boundary uses explicit `to_power`/`to_dbm`.
- **`constexpr` where the math allows** (`to_ratio`) — C++23 `constexpr <cmath>` lets dB ratios fold at
  compile time.

---

## 4. `include/emc/core/materials.hpp`

One `enum class Material`, one `MaterialProperties` (mp-units-typed conductivity + resistivity, plain
doubles for mu_r/eps_r), one `constexpr` table, and `properties()` returning `Result<MaterialProperties>`
— a single source of truth for the conductor database.

```c++
// include/emc/core/materials.hpp
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>

#include <emc/core/error.hpp>   // emc::Error, emc::ErrorCode, emc::Result
#include <emc/core/units.hpp>   // emc::units::Conductivity, emc::units::Resistivity

namespace emc::materials {

using namespace mp_units;

// ---------------------------------------------------------------------------
//  Material — scoped enum (no implicit int conversion). A single canonical
//  spelling and no index/sentinel confusion. Order is the CANONICAL pinned order.
// ---------------------------------------------------------------------------
enum class Material {
    Custom = 0,
    Copper,
    Silver,
    Gold,
    Aluminium,
    Nickel,
    Tungsten,
    Platinum,
    Lead,
    Graphite,
    Count        // sentinel = one past the last real material (table sizing / iteration)
};

// ---------------------------------------------------------------------------
//  MaterialProperties — one record. Units live in the TYPE for the physical
//  quantities; mu_r / eps_r are plain dimensionless doubles (pinned canonical API).
// ---------------------------------------------------------------------------
struct MaterialProperties {
    emc::units::Conductivity conductivity;            // sigma   [S/m]
    double                   relative_permeability;   // mu_r    [-]
    double                   relative_permittivity;   // eps_r   [-]
    emc::units::Resistivity  resistivity;             // rho = 1/sigma  [ohm*m]  (cached)
};

namespace detail {
using mp_units::si::unit_symbols::S;     // siemens
using mp_units::si::unit_symbols::m;     // metre
using mp_units::si::unit_symbols::ohm;   // ohm

// Compact constructor so the table reads like a clean data sheet.
// consteval forces the row to be built at COMPILE time -> a typo is a hard error.
[[nodiscard]] consteval MaterialProperties make(double sigma_S_per_m, double mu_r, double eps_r) {
    return MaterialProperties{
        .conductivity          = sigma_S_per_m * (S / m),
        .relative_permeability = mu_r,
        .relative_permittivity = eps_r,
        .resistivity           = (1.0 / sigma_S_per_m) * (ohm * m),
    };
}
} // namespace detail

// ---------------------------------------------------------------------------
//  THE single source of truth. Conductivity values are
//  standard handbook figures (IACS-consistent for the common conductors).
//  Order MUST mirror the enum (Custom excluded); asserted below.
//   * Copper:    5.96e7 S/m (IACS-consistent).
//   * Silver:    6.30e7 S/m.
//   * Gold:      4.10e7 S/m.
//   * Aluminium: 3.77e7 S/m; single canonical spelling "Aluminium".
//   * Nickel:    1.43e7 S/m, magnetic (mu_r = 600).
//   * Tungsten / Platinum / Lead / Graphite: mu_r = 1 each.
// ---------------------------------------------------------------------------
inline constexpr std::array<MaterialProperties,
                            static_cast<std::size_t>(Material::Count) - 1> table{{
    //            sigma [S/m]   mu_r        eps_r
    detail::make(5.96e7,   0.999991, 1.0),  // Copper
    detail::make(6.30e7,   0.99998,  1.0),  // Silver
    detail::make(4.10e7,   1.0,      1.0),  // Gold
    detail::make(3.77e7,   1.00002,  1.0),  // Aluminium
    detail::make(1.43e7, 600.0,      1.0),  // Nickel    (magnetic: high mu_r)
    detail::make(1.79e7,   1.0,      1.0),  // Tungsten
    detail::make(9.43e6,   1.0,      1.0),  // Platinum
    detail::make(4.55e6,   1.0,      1.0),  // Lead
    detail::make(1.00e5,   1.0,      1.0),  // Graphite
}};

// Name <-> enum mapping (Custom + every real material exactly).
struct NameEntry { Material id; std::string_view name; };
inline constexpr std::array<NameEntry, static_cast<std::size_t>(Material::Count)> names{{
    {Material::Custom,    "Custom"},
    {Material::Copper,    "Copper"},
    {Material::Silver,    "Silver"},
    {Material::Gold,      "Gold"},
    {Material::Aluminium, "Aluminium"},
    {Material::Nickel,    "Nickel"},
    {Material::Tungsten,  "Tungsten"},
    {Material::Platinum,  "Platinum"},
    {Material::Lead,      "Lead"},
    {Material::Graphite,  "Graphite"},
}};

// ---------------------------------------------------------------------------
//  properties() — constexpr lookup by enum. Returns Result<MaterialProperties>:
//  a typed error for Custom / out-of-range, never a numeric sentinel.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr Result<MaterialProperties> properties(Material m) noexcept {
    if (m == Material::Custom)
        return std::unexpected(Error{ .code = ErrorCode::UnknownMaterial,
                                      .message = "Custom material: supply properties explicitly",
                                      .field = "material" });
    const auto idx = static_cast<std::size_t>(m);
    if (idx == 0 || idx >= static_cast<std::size_t>(Material::Count))
        return std::unexpected(Error{ .code = ErrorCode::UnknownMaterial,
                                      .message = "Material enum out of range",
                                      .field = "material" });
    return table[idx - 1];   // -1 because Custom occupies slot 0 and has no table row
}

// name -> enum
[[nodiscard]] constexpr std::optional<Material> from_name(std::string_view n) noexcept {
    for (const auto& e : names)
        if (e.name == n) return e.id;
    return std::nullopt;
}

// enum -> name
[[nodiscard]] constexpr std::string_view to_name(Material m) noexcept {
    for (const auto& e : names)
        if (e.id == m) return e.name;
    return "Unknown";
}

// ---------------------------------------------------------------------------
//  Named inline constants — so `emc::materials::nickel.conductivity` works.
//  (Pinned canonical API.) Each pulls its row from the single table.
// ---------------------------------------------------------------------------
inline constexpr MaterialProperties copper    = table[static_cast<std::size_t>(Material::Copper)    - 1];
inline constexpr MaterialProperties silver    = table[static_cast<std::size_t>(Material::Silver)    - 1];
inline constexpr MaterialProperties gold      = table[static_cast<std::size_t>(Material::Gold)      - 1];
inline constexpr MaterialProperties aluminium = table[static_cast<std::size_t>(Material::Aluminium) - 1];
inline constexpr MaterialProperties nickel    = table[static_cast<std::size_t>(Material::Nickel)    - 1];
inline constexpr MaterialProperties tungsten  = table[static_cast<std::size_t>(Material::Tungsten)  - 1];
inline constexpr MaterialProperties platinum  = table[static_cast<std::size_t>(Material::Platinum)  - 1];
inline constexpr MaterialProperties lead      = table[static_cast<std::size_t>(Material::Lead)      - 1];
inline constexpr MaterialProperties graphite  = table[static_cast<std::size_t>(Material::Graphite)  - 1];

} // namespace emc::materials
```

### Compile-time validation invariants

```c++
// include/emc/core/materials.hpp  (appended)
namespace emc::materials::detail {

using namespace mp_units;

// (a) Table length matches the enum (Custom excluded).
static_assert(table.size() == static_cast<std::size_t>(Material::Count) - 1,
              "materials::table size must equal number of non-Custom materials");

// (b) names[] covers Custom + every real material exactly.
static_assert(names.size() == static_cast<std::size_t>(Material::Count),
              "names[] must list Custom plus every material");

// (c) Every physical value is strictly positive and self-consistent (rho == 1/sigma).
consteval bool all_values_sane() {
    for (const auto& p : table) {
        const double sigma = p.conductivity.numerical_value_in(si::siemens / si::metre);
        const double rho   = p.resistivity.numerical_value_in(si::ohm * si::metre);
        if (sigma <= 0.0)                      return false;
        if (p.relative_permeability <= 0.0)    return false;
        if (p.relative_permittivity <= 0.0)    return false;
        if (rho <= 0.0)                        return false;
        if (rho * sigma < 0.99 || rho * sigma > 1.01) return false;   // rho ~= 1/sigma
    }
    return true;
}
static_assert(all_values_sane(),
              "every material must have positive, self-consistent properties");

// (d) Lookup ordering & name mapping.
static_assert(properties(Material::Gold).has_value());
static_assert(to_name(Material::Aluminium) == "Aluminium");
static_assert(from_name("Aluminium") == Material::Aluminium);
static_assert(!from_name("Aluminum").has_value());   // alternate spelling intentionally rejected

// (e) The named constants really alias their rows.
static_assert(nickel.relative_permeability == 600.0);

} // namespace emc::materials::detail
```

### Modern C++ features used here — and why

- **`enum class Material`** — blocks implicit int↔enum conversion, so a control index cannot be
  `static_cast` into a material and a status integer cannot be returned as a material-ish value.
  Namespaced enumerators enforce one canonical spelling.
- **`constexpr std::array` + `consteval make()`** — a true compile-time table in read-only data,
  trivially thread-safe, with row construction *forced* to compile time so a typo is a hard error, not
  a silent runtime cost.
- **`Result<MaterialProperties>` from `properties()`** — `std::expected` makes "unknown material" a
  distinct, `[[nodiscard]]` value the caller must handle, never a plausible-looking numeric sentinel.
- **mp-units-typed `conductivity`/`resistivity`** — units in the type; `1/sigma` is checked to be a
  resistivity. `mu_r`/`eps_r` are plain doubles per the canonical API (they appear bare in formulas).
- **`static_assert`/`consteval` invariants** — "table size == enum size", "ρ = 1/σ", "every value
  positive", "lookup hits the right row" are build-time invariants: the database cannot be merged in a
  broken state.

---

## 5. `include/emc/core/calculator.hpp`

The `Calculator` concept that binds each calculator's (Input, Result, calculate) triple into a
compile-checked contract, and the `ValidatedCalculator` refinement for those with a `validate()`.

```c++
// include/emc/core/calculator.hpp
#pragma once

#include <concepts>
#include <expected>

#include <emc/core/error.hpp>   // emc::Error, emc::Result

namespace emc {

// ---------------------------------------------------------------------------
//  Calculator — a tag type C that names Input/Result and provides a static
//  calculate(const Input&) -> Result<Result>. Calculators are FREE functions in
//  a category namespace; each guide adds a thin zero-data tag struct (forwarding
//  to the free calculate()) so generic code (the reference runner, batch sweeps,
//  future reflection) can name the triple as one entity.
// ---------------------------------------------------------------------------
template <class C>
concept Calculator =
    requires {
        typename C::Input;
        typename C::Result;
    } &&
    requires(const typename C::Input& in) {
        { C::calculate(in) } -> std::same_as<emc::Result<typename C::Result>>;
    };

// Refinement: also requires validate(const Input&) -> std::expected<void, Error>.
template <class C>
concept ValidatedCalculator =
    Calculator<C> &&
    requires(const typename C::Input& in) {
        { C::validate(in) } -> std::same_as<std::expected<void, Error>>;
    };

} // namespace emc
```

### How a calculator guide binds to the concept

Each guide adds, at the bottom of its header, a tag struct + a `static_assert` so the contract is a
build-time tripwire *in that header*:

```c++
// at the bottom of include/emc/basic/skin_depth.hpp (illustrative — full body in the SkinDepth guide)
namespace emc::basic {

struct SkinDepth {                       // tag: the calculator "as a type"
    using Input  = SkinDepthInput;
    using Result = SkinDepthResult;
    static emc::Result<Result> calculate(const Input& in) { return emc::basic::calculate(in); }
    static std::expected<void, emc::Error> validate(const Input& in) { return emc::basic::validate(in); }
};
static_assert(emc::ValidatedCalculator<SkinDepth>);   // contract checked at compile time

} // namespace emc::basic
```

### Modern C++ features used here — and why

- **Concepts (C++20)** — give "all calculators share a shape" with **zero runtime cost** and readable
  diagnostics, versus the OO alternative (`class Calculator { virtual Result solve() = 0; }`) that
  would force heap allocation, a vtable, and type erasure for math that is pure and known at the call
  site.
- **`static_assert(Calculator<...>)` in each header** — turns "did the developer follow the
  convention?" into a compile error *at the point of the mistake* if a `Result` type or `calculate`
  signature drifts.
- **One generic test harness** — `template <emc::Calculator C> void run_reference(...)` works for every
  calculator because they all satisfy the same concept (doc 09 §2.2).

---

## 6. `tests/support/` — Catch2 v3 helpers

Two header-only helpers shared by every calculator test: `emc::test::load_csv` (a GUI-free
ranges/`string_view` CSV parser for reference vectors) and `emc::test::approx` (unit-aware tolerance
comparison). Catch2 v3 is the chosen framework (plan doc 09 §3).

### 6.1 `tests/support/csv.hpp` — `emc::test::load_csv`

```c++
// tests/support/csv.hpp   (test-only; header-only is fine)
#pragma once

#include <charconv>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace emc::test {

// One row owns its line storage; fields are zero-copy views into that line.
struct Row {
    std::string line;                       // owns the bytes
    std::vector<std::string_view> fields;   // views into `line`

    [[nodiscard]] std::string_view at(std::size_t i) const { return fields.at(i); }

    // Parse field i as a double (locale-independent via std::from_chars).
    [[nodiscard]] double num(std::size_t i) const {
        const std::string_view sv = fields.at(i);
        double v{};
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (ec != std::errc{})
            throw std::runtime_error("bad double in reference field: " + std::string{sv});
        return v;
    }

    [[nodiscard]] std::string str(std::size_t i) const { return std::string{fields.at(i)}; }
};

// Split a line on `delim` into string_views (C++23 views::split + ranges::to).
[[nodiscard]] inline std::vector<std::string_view> split(std::string_view s, char delim = ',') {
    auto to_sv = [](auto&& sub) {
        return std::string_view{&*std::ranges::begin(sub),
                                static_cast<std::size_t>(std::ranges::distance(sub))};
    };
    return s | std::views::split(delim)
             | std::views::transform(to_sv)
             | std::ranges::to<std::vector>();
}

// Load a CSV of reference vectors. Skips blank lines, '#' comments, and any header
// line whose first field is non-numeric (so self-describing headers like
// "frequency[MHz]" are ignored). Returns owning rows so the field views stay valid.
[[nodiscard]] inline std::vector<Row> load_csv(const std::filesystem::path& path) {
    std::ifstream in{path};
    if (!in)
        throw std::runtime_error("cannot open reference file: " + path.string());

    std::vector<Row> rows;
    for (std::string line; std::getline(in, line);) {
        // Strip a trailing '\r' from CRLF files.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string_view sv{line};
        if (sv.empty() || sv.starts_with('#'))
            continue;

        Row r{.line = std::move(line)};
        r.fields = split(r.line);   // re-view the moved string
        if (r.fields.empty())
            continue;

        // Skip a header row: first field does not parse as a number.
        double probe{};
        const std::string_view f0 = r.fields.front();
        const auto [ptr, ec] = std::from_chars(f0.data(), f0.data() + f0.size(), probe);
        if (ec != std::errc{})
            continue;   // header / label line

        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace emc::test
```

### 6.2 `tests/support/approx.hpp` — `emc::test::approx`

```c++
// tests/support/approx.hpp
#pragma once

#include <algorithm>   // std::max
#include <cmath>       // std::abs
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <mp-units/systems/si.h>

namespace emc::test {

// Unit-aware comparison: extract BOTH quantities in the EXPECTED quantity's unit
// (compile-checked: ill-formed unless they share a dimension), then compare with a
// relative tolerance and a tiny absolute floor for values near zero.
//
//   REQUIRE(emc::test::approx(result.skin_depth, 559.0 * um));           // default rel_tol
//   REQUIRE(emc::test::approx(result.z0, 50.0 * ohm, 1e-4));             // looser tol
//
template <class Q>
[[nodiscard]] bool approx(Q actual, Q expected, double rel_tol = 1e-6) noexcept {
    const auto unit = expected.unit;                       // mp-units: the expected's unit
    const double a  = actual.numerical_value_in(unit);     // won't compile if dimensions differ
    const double e  = expected.numerical_value_in(unit);
    const double diff = std::abs(a - e);
    const double abs_floor = 1e-12;
    return diff <= abs_floor || diff <= rel_tol * std::max(std::abs(a), std::abs(e));
}

// Catch2 matcher form, so failures print the actual vs expected nicely:
//   REQUIRE_THAT(result.skin_depth, emc::test::WithinUnits(559.0 * um, 1e-6));
template <class Q>
struct UnitMatcher : Catch::Matchers::MatcherGenericBase {
    Q expected;
    double rel_tol;
    UnitMatcher(Q e, double t) : expected{e}, rel_tol{t} {}

    bool match(const Q& actual) const { return approx(actual, expected, rel_tol); }

    std::string describe() const override {
        return "is within " + std::to_string(rel_tol) + " (relative) of the expected quantity";
    }
};

template <class Q>
[[nodiscard]] UnitMatcher<Q> WithinUnits(Q expected, double rel_tol = 1e-6) {
    return UnitMatcher<Q>{expected, rel_tol};
}

} // namespace emc::test
```

### 6.3 Reference CSVs: copied into `tests/reference/`

Each calculator's reference vectors are hand-computed from its textbook closed-form formula and stored
as `tests/reference/<Name>.csv` (e.g. `SkinDepth.csv`, `MicrostripTrace.csv`). The test build copies
them next to the test binary so `load_csv("reference/<Name>.csv")` resolves:

```cmake
# tests/CMakeLists.txt   (full wiring in 16-build-and-scaffolding.md)
find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(emc_tests
    core/constants_test.cpp
    core/materials_test.cpp
    # ... one file per calculator, e.g. basic/skin_depth_test.cpp
)
target_link_libraries(emc_tests PRIVATE emc::emc Catch2::Catch2WithMain)
target_compile_features(emc_tests PRIVATE cxx_std_23)
target_include_directories(emc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # for support/*.hpp

# Copy reference vectors next to the test binary so load_csv("reference/<Name>.csv") resolves.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/reference DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

catch_discover_tests(emc_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
```

### Worked example: the core tests using these helpers

```c++
// tests/core/constants_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/core/constants.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;

// (a) compile-time: the constants hold their exact defining values.
static_assert(emc::constants::pi > 3.14159 && emc::constants::pi < 3.14160,
              "pi must be full-precision pi");
static_assert(emc::constants::c.numerical_value_in(m / s) == 299'792'458.0,
              "c must be the exact SI defining value");

TEST_CASE("constants satisfy EM identities", "[core][constants]") {
    // z0 == mu0 * c, compared unit-aware via approx.
    REQUIRE(emc::test::approx(emc::constants::z0,
                              (emc::constants::mu0 * emc::constants::c).in(ohm), 1e-6));
}
```

```c++
// tests/core/materials_test.cpp
#include <catch2/catch_test_macros.hpp>

#include <emc/core/materials.hpp>
#include "support/approx.hpp"

using namespace mp_units;
using namespace mp_units::si::unit_symbols;
using emc::ErrorCode;

TEST_CASE("properties() returns the right row", "[core][materials]") {
    auto cu = emc::materials::properties(emc::materials::Material::Copper);
    REQUIRE(cu.has_value());
    REQUIRE(emc::test::approx(cu->conductivity, 5.96e7 * (S / m), 1e-9));
    REQUIRE(emc::materials::nickel.relative_permeability == 600.0);   // named constant
}

TEST_CASE("Custom material is a typed error, never a sentinel", "[core][materials]") {
    auto r = emc::materials::properties(emc::materials::Material::Custom);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::UnknownMaterial);
}
```

### Modern C++ features used here — and why

- **`std::ranges::views::split` + `std::string_view` + `std::from_chars`** — allocation-free,
  GUI-free, *locale-independent* parsing; `from_chars` always reads `.` as the decimal separator, so
  reference files parse identically on every machine regardless of locale (doc 09 §2.1).
- **`std::ranges::to<std::vector>` (C++23)** — materializes the lazy split view in one expression.
- **Unit-aware `approx` via `numerical_value_in(expected.unit)`** — comparing in a fixed unit is
  *compile-checked*: comparing a length to an impedance won't compile, so a unit-mismatch test bug is
  impossible (doc 09 §4.3).
- **Catch2 v3 matchers** — `REQUIRE_THAT(q, WithinUnits(...))` prints actual-vs-expected on failure;
  `GENERATE(from_range(load_csv(...)))` turns each reference row into an individually-reported case
  (doc 09 §3.1).
- **`static_assert` over `constexpr` constants/materials** — a regression to a wrong constant or a
  broken material row fails the *build*, the strongest regression guard (doc 09 §5).

---

## 7. How a calculator guide builds on this (recap)

Every one of the 15 calculator guides follows this recipe, reusing the canonical surface above with
**no re-spelling**:

1. **Header** at `include/emc/<category>/<name>.hpp`:
   - `#include <emc/core/units.hpp>`, `<emc/core/error.hpp>`, `<emc/core/calculator.hpp>`, and
     `<emc/core/materials.hpp>` / `<emc/core/constants.hpp>` as needed.
   - An aggregate `XxxInput` of `emc::units::*` quantity fields (mu_r/eps_r as plain `double`), with
     sensible member defaults so designated-initializer call sites read cleanly.
   - An aggregate `XxxResult` of `emc::units::*` quantities (no baked-in display unit).
   - `[[nodiscard]] emc::Result<XxxResult> calculate(const XxxInput&);` (and
     `[[nodiscard]] std::expected<void, emc::Error> validate(const XxxInput&);` where validation
     applies). Bidirectional calcs use distinct `solve_*` free functions sharing a `detail::` core.
   - A tag struct + `static_assert(emc::Calculator<...>)` / `ValidatedCalculator`.

2. **Implementation** at `src/<category>/<name>.cpp`:
   - `validate()` built from `emc::in_range` / `emc::require_positive` / `emc::require_nonzero`.
   - `calculate()` using `emc::constants::*`, `emc::materials::properties(...)`, mp-units, and
     `std::expected` monadic `and_then`/`transform` where it reads cleanly. Evaluate empirical
     ratio-formulas in one coherent unit (`.numerical_value_in(mm)`) to keep the established formula
     constants exact.

3. **Tests** at `tests/<category>/<name>_test.cpp` (Catch2 v3):
   - a reference-vector test over `emc::test::load_csv("reference/<Name>.csv")`, mapping columns →
     `Input`, comparing with `emc::test::approx`, where each row's expected value is hand-computed from
     the calculator's textbook closed-form formula;
   - an independent hand-computed known-value test;
   - property/round-trip tests where meaningful (converter involutions,
     `solve_impedance`↔`solve_width` round-trips, monotonicity);
   - validation/edge tests asserting the right `emc::ErrorCode` (e.g.
     `REQUIRE(r.error().code == emc::ErrorCode::OutOfRange)`);
   - a `static_assert`/`constexpr` test when the calculator is constexpr-friendly.

> [!TIP]
> Each guide that introduces a modern-C++ feature adds a short "we used X here because Y" note tying it
> to a concrete EMC domain reason — exactly as modeled in the per-section notes above.

---

## Cross-references

- [`16-build-and-scaffolding.md`](16-build-and-scaffolding.md) — install/export, mp-units PUBLIC
  propagation, Catch2 discovery, and the `tests/reference/` copy step.
- [`../09-testing-and-golden-vectors.md`](../09-testing-and-golden-vectors.md) — the reference-vector
  harness and tolerance model that `load_csv` + `approx` feed.
