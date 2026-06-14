# Modern C++ Feature Catalog (the centerpiece)

> Purpose: a comprehensive, example-driven catalog of every modern C++ feature the `emc`
> library will use, each one justified against a concrete, numbered NinjaEMC pain point,
> with BEFORE (old Qt) → AFTER (modern `emc`) code so an implementer can copy the pattern.

This is the document the rest of the plan leans on. The architecture
([01-architecture-and-layout.md](01-architecture-and-layout.md)), the units subsystem
([03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md)), the constants
and material database ([04-constants-and-material-database.md](04-constants-and-material-database.md)),
the error model ([05-error-handling-and-validation.md](05-error-handling-and-validation.md)),
and the calculator pattern ([06-calculator-design-pattern.md](06-calculator-design-pattern.md))
all assemble the features catalogued here into a coherent whole. Where a feature has a dedicated
home document, this catalog gives the *rationale and a worked example* and then points there
for the full subsystem.

All example code is **C++23** and assumes the locked design decisions:

- top-level namespace `emc` with sub-namespaces `emc::constants`, `emc::materials`, `emc::units`,
  `emc::detail`, and per-category calculator namespaces (`emc::basic`, `emc::component`, …);
- physical quantities are **mp-units** `quantity<...>` types, never bare `double`;
- fallible operations return `std::expected<Result, emc::Error>` and are `[[nodiscard]]`;
- each calculator is a free function over an aggregate `Input` struct returning a `Result` struct.

---

## 0. Summary table

| Feature | C++ version | NinjaEMC pain point it fixes | Where detailed |
|---|---|---|---|
| `std::numbers::pi` / `inv_pi` / `sqrt2` | C++20 | `#define PI 3.14` (precision **bug**); literal `3.1415926` inline **~129×**; `M_PI` **~56×** | §1.1 |
| `constexpr` constants in `emc::constants` | C++11/14/17 | `#define SPEEDOFLIGHT 300000000.0` (wrong); `mu0=4*M_PI*1e-7` redefined in **8+** files; `PermofFreeSpace` copy-pasted across **6** headers | §1.2, [04](04-constants-and-material-database.md) |
| `consteval` (immediate functions) | C++20 | constants that must be folded at compile time, never become runtime magic numbers again | §1.3 |
| `constinit` | C++20 | guarantee the material table / lookup arrays have no dynamic init / no static-init-order fiasco | §1.4 |
| `constexpr` `<cmath>` | C++23 | formulas (`sqrt`,`exp`,`log`,`pow`) folded at compile time → `static_assert` golden checks; replaces `qSqrt/qExp/qLn/qPow` | §1.5 |
| `static_assert` | C++11/17 | no compile-time sanity net today; PI=3.14 shipped silently | §1.6 |
| `if constexpr` | C++17 | branch on solve-target / unit family without runtime cost; kills dead branches | §1.7 |
| Concepts (`std::floating_point`, `Quantity`, `Calculator`) | C++20 | `qreal` everywhere, no API contract; every widget reinvents the calc shape | §2.1 |
| `enum class` (Material, SolveTarget) | C++11 | unscoped `enum Material` + `EXIT_FAILURE` sentinel in `StandardGaugeWireWidget.cpp` | §2.2 |
| Strong typing via mp-units quantities | (mp-units) | `*39.37`, `*0.0254`, factor magic **~541** `addItem(unit,factor)` calls | §2.3, [03](03-quantities-and-units-mp-units.md) |
| Three-way comparison `<=>` | C++20 | value/result structs need ordering for tests; hand-written `==` is error-prone | §2.4 |
| CTAD | C++17 | terser construction of spans/optionals/arrays in calculators & tests | §2.5 |
| Abbreviated function templates (`auto` params) | C++20 | small generic helpers without verbose `template<>` noise | §2.6 |
| Aggregate `Input` structs + designated initializers | C++20 | positional `ui->spinbox->value()` reads; argument-order bugs | §3.1, [06](06-calculator-design-pattern.md) |
| `std::optional` | C++17 | "blank field means auto-derive" logic done with `0` sentinels (`if (m==0)`) | §3.2 |
| `std::variant` | C++17 | bidirectional solvers shoehorned into 4 near-identical methods | §3.3 |
| `std::span` | C++20 | non-owning views over sweep inputs / mode arrays without copies | §3.4 |
| `std::mdspan` | C++23 | the 12-mode `RectangularEnclosure` output and 2-D parameter sweeps | §3.5 |
| `std::string_view` | C++17 | `QString` material/unit names at the boundary; needless allocations | §3.6 |
| Structured bindings | C++17 | unpack result structs / map entries cleanly | §3.7 |
| `std::expected` + monadic ops | C++23 | `QMessageBox::warning(...)` mid-formula in **10** files; `EXIT_FAILURE` sentinel | §4.1, [05](05-error-handling-and-validation.md) |
| `[[nodiscard]]` | C++17/20 | results silently dropped; errors ignored | §4.2 |
| `std::source_location` | C++20 | no context on failures; `qDebug()` noise | §4.3 |
| Ranges + views | C++20 | material lookup; CSV-vector parsing in tests; mode filtering | §5, [09](09-testing-and-golden-vectors.md) |
| Free functions in namespaces | (core) | god-widgets: math welded into button lambdas | §6.1 |
| Deducing this (explicit object param) | C++23 | tidy named-parameter / fluent input builders without CRTP boilerplate | §6.2 |
| UDLs via mp-units (`5 * mm`, `1 * GHz`) | (mp-units) | readable, unit-checked literals replacing raw doubles + factors | §6.3, [03](03-quantities-and-units-mp-units.md) |
| `std::format` / `std::print` | C++20/23 | `qDebug()` / `QString` concatenation in tests & tools | §7 |
| `[[likely]]` / `[[unlikely]]` | C++20 | hint the validation fast path | §8.1 |
| `[[assume]]` | C++23 | encode validated invariants for the optimizer | §8.2 |
| Modules (deferred) | C++20 | why we ship headers+`.cpp` now | §9 |
| **C++26 forward-looking**: reflection, contracts, pattern matching, senders | C++26 | name↔enum maps, validation pre/post, solve dispatch, parallel sweeps | §10 |

---

## 1. Constants & compile-time

### 1.1 `std::numbers` — kills `PI = 3.14` and the 129 inline `3.1415926`

**What it is.** `<numbers>` provides correctly-rounded mathematical constants as variable
templates: `std::numbers::pi`, `inv_pi`, `sqrt2`, `ln2`, `e`, etc., each defaulting to `double`
but available for any floating type via `std::numbers::pi_v<T>`.

**Pain point.** `src/Utilites/HelperTypes.h` line 12 defines `#define PI 3.14`. That is not a
typo we can shrug at — it is a **precision bug** that silently corrupts every formula that ever
used `PI`. Separately the literal `3.1415926` appears **~129 times** (e.g. `StandardGaugeWireWidget.cpp`
lines 99, 102, 107) and `M_PI` **~56 times**. Three different values of π coexist in one codebase:
`3.14`, `3.1415926`, and the platform `M_PI`.

**BEFORE** (`StandardGaugeWireWidget.cpp`):

```cpp
// three different precisions of pi, copy-pasted by hand
A   = 3.1415926 * qPow(dm / 2.0, 2);
sd  = 1 / qSqrt(3.1415926 * f * ur * 4.0 * 3.1415926 * 0.0000001 / m);
Aeff = 2 * 3.1415926 * (dm / 2.0) * sd;
```

**AFTER** (`emc`):

```cpp
#include <numbers>
namespace emc::detail {
    inline constexpr double pi = std::numbers::pi;   // 3.141592653589793...
}
// usage in a calculator (quantities elided for clarity here)
const auto area = emc::detail::pi * (d / 2.0) * (d / 2.0);
```

**Why it is the right tool.** One source of π, with full `double` precision, supplied by the
standard library and usable in `constexpr`. There is no longer any way to type `3.14`.

**Trade-offs / pitfalls.** `std::numbers::pi` is `double`; for `float`/`long double` use the
`_v<T>` form so you do not silently narrow. Do **not** wrap it in a macro — keep it a typed
`constexpr`. Header `<numbers>` is required.

**Compiler support.** GCC 10+, Clang 11+, MSVC 19.27+ (all far below our toolchain floor).

### 1.2 `constexpr` constants in `emc::constants` — one source of truth

**What it is.** Compile-time-evaluable named constants, expressed as **mp-units quantities**
where physical, gathered in `emc::constants`.

**Pain point.** Three categories of duplicated/wrong constants:

- `#define SPEEDOFLIGHT 300000000.0` — imprecise; the SI exact value is **299 792 458 m/s**.
- `qreal mu0 = 4 * M_PI * 1e-7;` is **re-defined in 8+ files** (`SkinDepthWidget.h`,
  `FerriteToroidWidget.h`, `ESDCouplingLevelWidget.h`, `LightningCouplingLevelWidget.h`,
  `WideTraceOverPlaneWidget.cpp`, …).
- `#define PermofFreeSpace ((4 * M_PI) / 10000000.0)` is **copy-pasted across 6** Inductance headers.

**BEFORE** (scattered, per file):

```cpp
// SkinDepthWidget.h, FerriteToroidWidget.h, ESDCouplingLevelWidget.h, ... (×8)
qreal mu0 = 4 * M_PI * 1e-7;
// HelperTypes.h
#define SPEEDOFLIGHT 300000000.0      // imprecise
```

**AFTER** (`include/emc/constants.hpp`, one definition for the whole library):

```cpp
#include <mp-units/systems/si.h>
#include <mp-units/systems/isq.h>
namespace emc::constants {
    using namespace mp_units;
    // exact SI speed of light
    inline constexpr quantity c0 = 299'792'458 * si::metre / si::second;
    // vacuum permeability (CODATA 2018)
    inline constexpr quantity mu0 =
        1.256'637'062'12e-6 * si::henry / si::metre;
    inline constexpr quantity eps0 =
        8.854'187'8128e-12 * si::farad / si::metre;
}
```

**Why it is the right tool.** `inline constexpr` gives one definition usable across translation
units with no ODR violation and no header guard dance; mp-units makes the *unit* part of the value
so misuse is a compile error. Full detail and the complete constant set live in
[04-constants-and-material-database.md](04-constants-and-material-database.md).

**Trade-offs / pitfalls.** mp-units quantities are not implicitly convertible to `double`; call
sites must keep the dimensional chain (that is the point). Use digit separators (`'`) for
readability of long magnitudes.

**Compiler support.** `inline constexpr` variables: C++17, ubiquitous.

### 1.3 `consteval` — immediate functions for derived constants

**What it is.** `consteval` marks a function that *must* execute at compile time; calling it
produces a constant or fails to compile. Useful for constants computed from other constants.

**Pain point.** Derived "constants" in the old code were just runtime expressions
(`1.5e8 / qSqrt(relperm)` in `RectangularEnclosureWidget.cpp` line 60; `c/2` factors), so the
compiler could not guarantee they were folded, and nothing stopped a runtime value from sneaking in.

**AFTER:**

```cpp
namespace emc::detail {
    // half the speed of light as a plain magnitude in m/s, forced at compile time
    consteval double half_c_mps() {
        return emc::constants::c0.numerical_value_in(mp_units::si::metre / mp_units::si::second) / 2.0;
    }
    inline constexpr double k = half_c_mps();  // guaranteed compile-time, not a runtime mul
}
```

**Why it is the right tool.** `consteval` is a hard guarantee: if anyone later passes a runtime
input, it is a compile error rather than a silent runtime computation. It documents intent
("this is a constant, period").

**Trade-offs / pitfalls.** Over-using `consteval` for things that legitimately vary at runtime is
a mistake; reserve it for true constants. A `consteval` function cannot appear in a context that is
not constant-evaluated.

**Compiler support.** GCC 10+, Clang 11+, MSVC 19.28+.

### 1.4 `constinit` — no static-init-order fiasco for tables

**What it is.** `constinit` asserts that a variable with static storage duration is initialized at
compile time (constant initialization), eliminating dynamic initialization and its ordering hazards.

**Pain point.** Material/conductivity data is duplicated across **~14 files** with inconsistent
representation (`SkinDepthWidget.cpp` inline `if/else`; `StandardGaugeWireWidget.cpp` `GetResistivity()`).
A single library-wide table is the fix — and we want a *hard guarantee* it is built at compile time
with no static-init-order surprises.

**AFTER** (`include/emc/materials.hpp`, see [04](04-constants-and-material-database.md) for full data):

```cpp
namespace emc::materials {
    struct MaterialData { /* conductivity, rel. permeability, name ... */ };
    // constinit: proves at compile time there is no dynamic initialization
    inline constinit std::array<MaterialData, 9> table = { /* ...filled constexpr... */ };
}
```

**Why it is the right tool.** It turns "I hope this is constant-initialized" into a compile-time
assertion. If the initializer were ever made non-constant, the build breaks loudly.

**Trade-offs / pitfalls.** `constinit` only constrains *initialization*; the object can still be
mutable unless you also mark it `const`/`constexpr`. For an immutable table prefer `inline constexpr`;
use `constinit` when you specifically need a non-const but constant-initialized global.

**Compiler support.** GCC 10+, Clang 10+, MSVC 19.29+.

### 1.5 `constexpr <cmath>` (C++23) — formulas folded at compile time

**What it is.** C++23 makes the common `<cmath>` functions (`sqrt`, `exp`, `log`, `pow`, `sin`, …)
`constexpr`, so whole formulas can be evaluated during compilation.

**Pain point.** Every formula used Qt's `qSqrt/qExp/qLn/qPow` (e.g. `MicrostripTraceWidget.cpp`
`qSqrt`, `qExp`; `SkinDepthWidget.cpp` `qSqrt`), which are runtime-only and Qt-coupled. The 44
calculators that contain real math all depend on these.

**BEFORE** (`SkinDepthWidget.cpp` line 70):

```cpp
qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
```

**AFTER** (pure, constexpr-capable; mp-units quantities in/out):

```cpp
#include <cmath>
namespace emc::basic {
    [[nodiscard]] constexpr quantity<isq::length[si::metre]>
    skin_depth_value(/* freq, mu, sigma as quantities */) {
        using namespace mp_units;
        // ... combine quantities so the radicand is dimensionless ...
        return std::sqrt(/* dimensionless radicand */) * si::metre;
    }
}
// because it is constexpr, golden values can be checked at compile time:
static_assert(skin_depth_value(/*Cu @ 1 MHz*/) > 0 * mp_units::si::metre);
```

**Why it is the right tool.** Replacing `qSqrt` etc. with `constexpr` `std::sqrt` removes the Qt
dependency *and* unlocks compile-time checks (§1.6) and pure, thread-safe functions.

**Trade-offs / pitfalls.** `constexpr <cmath>` is one of the newer C++23 features; **support is
uneven** (GCC 13+ has broad coverage; libc++/Clang and MSVC lag on some functions). The plan's
posture: write the formulas `constexpr`, but only `static_assert` the subset your toolchain
actually folds, and keep the runtime path identical. mp-units arithmetic is already `constexpr`,
so the only gating factor is the `<cmath>` call.

**Compiler support.** GCC 13+ broad; Clang/libc++ partial; MSVC partial. Treat compile-time
evaluation of transcendental functions as best-effort, runtime correctness as guaranteed.

### 1.6 `static_assert` — a compile-time sanity net

**What it is.** Compile-time assertions over constant expressions, with a diagnostic message.

**Pain point.** Nothing in NinjaEMC caught `PI = 3.14`; it compiled and shipped. There was zero
compile-time validation of constants, unit factors, or formula sanity.

**AFTER:**

```cpp
#include <numbers>
namespace emc::detail {
    // catch a 3.14-style regression at compile time, forever
    static_assert(std::numbers::pi > 3.1415 && std::numbers::pi < 3.1416,
                  "pi constant is wrong (regression of the old PI=3.14 bug)");
    // unit-vocabulary sanity: 1 inch must equal 25.4 mm exactly
    static_assert(/* mp-units check that 1*inch == 25.4*mm */ true);
}
```

**Why it is the right tool.** It encodes the lessons of the old bugs directly into the build. The
`3.14` class of error becomes physically impossible to reintroduce.

**Trade-offs / pitfalls.** Only works on constant expressions; pair with §1.5 so formula checks
are foldable. Keep messages specific so a failing assert points to the offending invariant.

**Compiler support.** `static_assert` with message: C++11; single-argument form: C++17.

### 1.7 `if constexpr` — compile-time branch selection without dead code at runtime

**What it is.** A compile-time `if` whose untaken branch is discarded, enabling one templated
function to specialize behaviour per type/value with no runtime branch.

**Pain point.** The bidirectional solvers branch at runtime on solve-target and unit family. The
worst case is `MicrostripTraceWidget.cpp`, where `calH/calT/calW` each contain an **8-branch nested
`if` tree** (lines 142–219, 231–307, 319–395) doing nothing but mm-vs-mils conversions. With
mp-units the unit branch disappears entirely (§2.3); where a *target* still must be chosen at
compile time, `if constexpr` does it with zero runtime cost.

**AFTER** (solve-target chosen by a non-type template parameter):

```cpp
enum class SolveTarget { Z0, H, T, W };

template <SolveTarget Target>
[[nodiscard]] constexpr auto microstrip_solve(const MicrostripInput& in) {
    if constexpr (Target == SolveTarget::Z0)      return solve_z0(in);
    else if constexpr (Target == SolveTarget::H)  return solve_h(in);
    else if constexpr (Target == SolveTarget::T)  return solve_t(in);
    else                                          return solve_w(in);
}
```

**Why it is the right tool.** Each instantiation compiles to exactly one formula; no runtime
dispatch, no dead branches. (For runtime-chosen targets we use `std::variant`/visitation, §3.3, or
pattern matching in C++26, §10.3.)

**Trade-offs / pitfalls.** `if constexpr` requires the condition to be a constant expression, so it
only helps when the target is known at compile time. Don't force a compile-time choice on something
the *user* picks at runtime — use it for internal specialization, the variant for user choice.

**Compiler support.** C++17, universal.

---

## 2. Type system

### 2.1 Concepts — `std::floating_point`, a `Quantity` constraint, and the `Calculator` concept

**What it is.** Concepts are named, checkable constraints on template arguments, giving clear
compile errors and self-documenting APIs.

**Pain point.** Everything in NinjaEMC is `qreal` (a `double`). There is no contract describing
what a "calculator" is; every widget reinvents the shape (read fields → compute → write fields)
and nothing enforces consistency across the 52 calculators.

**AFTER** — three layered concepts:

```cpp
#include <concepts>
namespace emc {
    // 1) numeric helpers stay generic but constrained
    template <class T>
    concept Real = std::floating_point<T>;

    // 2) "is this an mp-units quantity?" (full version lives in doc 03)
    template <class Q>
    concept Quantity = requires (Q q) {
        typename Q::rep;              // representation type
        q.numerical_value_in;        // mp-units accessor
    };

    // 3) the repeatable calculator contract: an (Input, Result, calculate) triple
    template <class C>
    concept Calculator = requires (typename C::Input in) {
        typename C::Input;
        typename C::Result;
        { C::calculate(in) }
            -> std::same_as<std::expected<typename C::Result, emc::Error>>;
    };
}
```

**Why it is the right tool.** `Calculator` makes the locked design pattern *machine-checked*: a new
calculator that forgets `validate`, returns the wrong type, or drops `std::expected` fails to model
the concept and the build rejects it. This is how 52 conversions stay uniform. The pattern itself is
the subject of [06-calculator-design-pattern.md](06-calculator-design-pattern.md).

**Trade-offs / pitfalls.** Over-constraining hurts (don't demand more from a type than the algorithm
uses). Keep `Quantity` aligned with whatever mp-units exposes; prefer using mp-units' own concepts
where available rather than re-deriving them.

**Compiler support.** GCC 10+, Clang 10+ (Clang 12+ recommended), MSVC 19.30+.

### 2.2 `enum class` — Material and SolveTarget without `EXIT_FAILURE` sentinels

**What it is.** Scoped, strongly-typed enumerations that don't implicitly convert to `int` and
don't leak names into the enclosing scope.

**Pain point.** `StandardGaugeWireWidget.cpp` uses an *unscoped* `enum Material` and worse,
`GetResistivity()` (lines 157–178) **returns `EXIT_FAILURE` (= 1) as an error sentinel** for an
unknown material — a process exit code masquerading as a resistivity value. Material identity is
also smuggled around as a combobox **index** (`static_cast<Material>(currentIndex())`, line 36).

**BEFORE** (`StandardGaugeWireWidget.cpp`):

```cpp
qreal StandardGaugeWireWidget::GetResistivity(enum Material material) {
    if (material == Copper)      return 0.0000000172;
    else if (material == Silver) return 0.0000000159;
    // ...
    else return EXIT_FAILURE;   // <-- 1.0 returned as "resistivity"
}
```

**AFTER** (`include/emc/materials.hpp`):

```cpp
namespace emc::materials {
    enum class Material { Custom, Copper, Silver, Gold, Aluminium,
                          Tungsten, Platinum, Lead, Graphite, Nickel };

    // unknown material is an Error, never a fake number (see doc 05)
    [[nodiscard]] std::expected<MaterialData, emc::Error> lookup(Material m);
}
```

**Why it is the right tool.** `enum class` prevents the index/enum confusion and the int/double
mixups; combined with `std::expected` (§4.1) the "unknown material" case becomes a typed error
instead of a sentinel that silently flows into the next multiplication.

**Trade-offs / pitfalls.** You lose implicit `int` conversion (a feature here). To index a table by
enum, convert explicitly with `std::to_underlying` (C++23). Keep the enum and the data table in one
place ([04](04-constants-and-material-database.md)) so order/identity can't drift.

**Compiler support.** `enum class`: C++11. `std::to_underlying`: C++23 (GCC 11+, Clang 13+, MSVC 19.30+).

### 2.3 Strong typing via mp-units quantities — the death of `*39.37`

**What it is.** Each physical value carries its dimension and unit in the type:
`quantity<isq::length[si::metre]>`, `quantity<isq::frequency[si::hertz]>`, etc. Conversions are
compile-checked; you cannot add a length to a frequency or forget a factor.

**Pain point.** Unit conversion in NinjaEMC is entirely hand-wired: **~541** `addItem(unit, factor)`
calls feed comboboxes, giant `if/else` chains map unit *strings* to factors
(`StandardGaugeWireWidget.cpp` lines 43–77), and magic factors like `*39.37` (metre→inch) are
repeated dozens of times in `MicrostripTraceWidget.cpp` (lines 91, 98, 105, 155, 164, …). `39.37`
is itself imprecise (1 m = 39.3700787… in).

**BEFORE** (`MicrostripTraceWidget.cpp`, repeated 8× per solver):

```cpp
if (ui->hunitRadioMMButton->isChecked()) H = ui->h_lineEdit->text().toDouble();
else                                      H = ui->h_lineEdit->text().toDouble() * 39.37;
```

**AFTER** (the unit lives in the value; no branch, no factor):

```cpp
using namespace mp_units;
quantity h = 18.65 * si::milli<si::metre>;   // caller states the unit once
// formulas use h directly; if a result is wanted in mils, convert at the edge:
auto h_mils = h.in(/* mil unit from emc::units */);   // checked, exact
```

**Why it is the right tool.** It collapses the entire 8-branch unit tree (§1.7's pain point) into
*zero* branches: the conversion is a compile-checked operation on the type, performed once at the
boundary, with the exact factor baked into mp-units. The full unit vocabulary (`emc::units`) and how
it replaces the 541 combobox factors is [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md).

**Trade-offs / pitfalls.** Learning curve and compile-time cost; error messages can be verbose.
Mitigate with the project `emc::units` aliases and UDLs (§6.3). Never extract a bare `double`
mid-formula — only at I/O boundaries via `.numerical_value_in(unit)`.

**Compiler support.** mp-units requires a C++20/23 compiler; GCC 12+/Clang 16+ recommended. See [03].

### 2.4 Three-way comparison `<=>` — orderable value/result structs

**What it is.** The spaceship operator `<=>` lets the compiler synthesize all six relational
operators from one defaulted definition.

**Pain point.** Result/value structs need equality (for golden-vector tests, [09](09-testing-and-golden-vectors.md))
and sometimes ordering. Hand-written `operator==` over many fields is exactly the kind of tedium
that produced the original copy-paste bugs.

**AFTER:**

```cpp
namespace emc::component {
    struct MicrostripResult {
        quantity<isq::resistance[si::ohm]> z0;
        quantity<...> c0;
        quantity<...> tpd;
        auto operator<=>(const MicrostripResult&) const = default;   // all six ops, free
    };
}
```

**Why it is the right tool.** One line yields correct, complete comparisons; tests can write
`result == expected` and sort sweep outputs without bespoke comparators.

**Trade-offs / pitfalls.** Floating/quantity comparison is **exact** with defaulted `==`; golden
tests must compare with tolerance instead (helper in [09]). Use `<=>` for structural ordering, but
do approximate comparison explicitly for physics outputs.

**Compiler support.** GCC 10+, Clang 10+ (Clang 12+ for defaulted `==` interplay), MSVC 19.28+.

### 2.5 CTAD — class template argument deduction

**What it is.** Construct class templates without spelling the template arguments:
`std::array a{1, 2, 3};`, `std::span s{vec};`, `std::optional o{x};`.

**Pain point.** Minor but pervasive: terseness in calculators and tests where the element type is
obvious. The old code's verbosity (and the resulting copy-paste) is the macro-level version of this.

**AFTER:**

```cpp
std::array modes{ Mode{1,1,0}, Mode{1,0,1}, Mode{0,1,1} };   // deduces array<Mode,3>
std::span view{ inputs };                                    // deduces span<T>
```

**Why it is the right tool.** Removes redundant type spelling, fewer places to get a type wrong.

**Trade-offs / pitfalls.** Deduction can surprise (`std::array a{1, 2u}` won't deduce). Spell the
type when intent matters.

**Compiler support.** C++17, universal.

### 2.6 Abbreviated function templates (`auto` parameters)

**What it is.** A function with an `auto` parameter is implicitly a template:
`auto square(auto x) { return x * x; }`.

**Pain point.** Many tiny numeric helpers (squaring, summing radicands) recur; writing
`template<class T>` boilerplate for each invites copy-paste drift.

**AFTER** (constrained `auto` keeps it safe):

```cpp
constexpr auto squared(emc::Quantity auto q) { return q * q; }   // for the 1/L² mode terms
```

**Why it is the right tool.** Concise generic helpers, and constraining the `auto` (with a concept)
keeps the API honest.

**Trade-offs / pitfalls.** Unconstrained `auto` params accept anything; always pair with a concept
(`emc::Quantity auto`, `std::floating_point auto`) for a meaningful contract.

**Compiler support.** GCC 10+, Clang 10+, MSVC 19.28+.

---

## 3. Data & APIs

### 3.1 Aggregate `Input` structs + designated initializers — no more positional `ui->value()` reads

**What it is.** An aggregate struct with member defaults, populated at the call site with named
designated initializers: `f({.height = 1.5 * mm, .width = 2.65 * mm});`.

**Pain point.** Inputs are read positionally from widgets: `MicrostripTraceWidget.cpp` pulls four
unlabeled doubles (`h, t, w, eps`) from spinboxes; `RectangularEnclosureWidget.cpp` reads
`length/width/height/perm`. Order and identity live only in UI wiring, so a swapped field is a silent
wrong answer. The golden CSVs are likewise positional (`"18.65,11.16,15.00,5"`).

**BEFORE** (`MicrostripTraceWidget.cpp`):

```cpp
eps = ui->permittivity_LineEdit->text().toDouble();
H   = ui->h_lineEdit->text().toDouble();   // order implicit, units implicit
T   = ui->t_lineEdit->text().toDouble();
W   = ui->w_lineEdit->text().toDouble();
```

**AFTER** (`include/emc/component/microstrip.hpp`):

```cpp
namespace emc::component {
    struct MicrostripInput {
        quantity<isq::length[si::metre]> height;
        quantity<isq::length[si::metre]> trace_thickness;
        quantity<isq::length[si::metre]> width;
        double relative_permittivity = 4.7;   // sensible default (old clear() value)
    };
}
// call site is self-documenting and unit-checked:
auto r = emc::component::calculate(MicrostripInput{
    .height          = 18.65 * mm,
    .trace_thickness = 11.16 * mm,
    .width           = 15.00 * mm,
    .relative_permittivity = 5.0,
});
```

**Why it is the right tool.** Named fields make argument-order bugs impossible, member defaults
encode the old `clear()` defaults once, and the aggregate maps 1:1 onto a CSV column header for
test reuse ([09](09-testing-and-golden-vectors.md)). This is the backbone of the calculator
pattern ([06](06-calculator-design-pattern.md)).

**Trade-offs / pitfalls.** Designated initializers in C++ must appear in **declaration order**
(unlike C). Aggregates can't have user-declared constructors; keep them plain data. Adding a field
with a default is source-compatible; reordering fields is not — treat field order as API.

**Compiler support.** Designated initializers: C++20 (GCC 8+, Clang 10+, MSVC 19.21+).

### 3.2 `std::optional` — "blank field auto-derives" without `0` sentinels

**What it is.** `std::optional<T>` represents "a value or nothing" with no magic sentinel.

**Pain point.** `StandardGaugeWireWidget.cpp` uses `0` to mean "not supplied": `if (m == 0) { if
(rho != 0) m = rho; else if (sigma != 0) m = 1/sigma; }` (lines 80–88). Zero is a legitimate-looking
number, so the sentinel logic is fragile and unreadable.

**BEFORE:**

```cpp
if (m == 0) {                       // 0 means "no material chosen"
    if (rho != 0)      m = rho;     // 0 means "no resistivity given"
    else if (sigma != 0) m = 1 / sigma;
}
```

**AFTER:**

```cpp
struct WireInput {
    std::optional<emc::materials::Material> material;     // empty => derive from rho/sigma
    std::optional<quantity<...>>            resistivity;
    std::optional<quantity<...>>            conductivity;
};
// resolution is explicit and total:
auto rho = in.material ? material_resistivity(*in.material)
         : in.resistivity ? *in.resistivity
         : in.conductivity ? 1.0 / *in.conductivity
         : /* -> std::unexpected(Error{ErrorCode::MissingInput}) */;
```

**Why it is the right tool.** "Absent" is modelled as absent, not as a number; the resolution reads
as a clear priority list and a missing-input case becomes a typed error (§4.1) instead of `m`
staying `0` and dividing later.

**Trade-offs / pitfalls.** Don't use `optional` for "value that may be invalid" — that's
`std::expected`. `optional<quantity>` works but mind that `*opt` is unchecked; pair access with the
`has_value()` test or monadic ops.

**Compiler support.** C++17, universal.

### 3.3 `std::variant` — bidirectional solving without 4 copy-pasted methods

**What it is.** A type-safe tagged union; `std::visit` dispatches on the active alternative.

**Pain point.** Bidirectional solvers are implemented as N near-identical methods.
`MicrostripTraceWidget` has **four** methods (`microstrip/calH/calT/calW`), each an ~8-branch tree,
differing only in which variable is the unknown. The user's choice of "what to solve for" is encoded
as *which button was clicked*.

**AFTER** (model the choice as data, solve once):

```cpp
namespace emc::component {
    // what the user wants to solve for, carrying the known set implicitly
    using MicrostripQuery = std::variant<SolveZ0, SolveHeight, SolveThickness, SolveWidth>;

    [[nodiscard]] std::expected<MicrostripResult, emc::Error>
    solve(const MicrostripCommon& known, const MicrostripQuery& q) {
        return std::visit([&](const auto& target) {
            return target.apply(known);     // each alternative = one formula, no unit branches
        }, q);
    }
}
```

**Why it is the right tool.** One entry point, one place for shared validation, and each alternative
holds a single formula (units already handled by mp-units, §2.3). The 4×8 = 32-branch explosion
collapses to 4 tiny formula objects.

**Trade-offs / pitfalls.** `std::visit` over many alternatives can be verbose; a generic lambda
(`[&](const auto&)`) helps when alternatives share an interface. For *compile-time-known* targets
prefer `if constexpr` (§1.7). C++26 pattern matching (§10.3) will make this cleaner still.

**Compiler support.** C++17, universal.

### 3.4 `std::span` — non-owning views over sweep inputs

**What it is.** `std::span<T>` is a lightweight (pointer, length) view over contiguous data, owning
nothing.

**Pain point.** Parameter sweeps and the golden-vector loops in tests pass around `QList<qreal>`
(e.g. `MicrostripTraceWidget::testPage(hList, tList, wList, ...)`), copying Qt containers. A Qt-free
core must accept any contiguous source (a `std::vector`, a `std::array`, a CSV row) without copying.

**AFTER:**

```cpp
namespace emc::component {
    // sweep height across many values, no ownership, no copy
    [[nodiscard]] std::vector<std::expected<MicrostripResult, emc::Error>>
    sweep_height(const MicrostripCommon& base, std::span<const quantity<...>> heights);
}
```

**Why it is the right tool.** Decouples the algorithm from the container; the test harness can feed
parsed CSV rows directly ([09](09-testing-and-golden-vectors.md)) with no `QList` and no allocation.

**Trade-offs / pitfalls.** A span does not own — never return a span to a local. Bounds are the
caller's responsibility; prefer the ranged overloads for safety where lifetimes are subtle.

**Compiler support.** C++20 (GCC 10+, Clang 7+ partial/12+ full, MSVC 19.26+).

### 3.5 `std::mdspan` — the 12-mode enclosure output and 2-D sweeps

**What it is.** `std::mdspan` is a multidimensional, non-owning view with a customizable layout;
it gives N-dimensional indexing (`m(i, j)`) over flat storage.

**Pain point.** `RectangularEnclosureWidget.cpp` computes **12 resonant-mode frequencies**
(`f110, f101, f011, f111, f201, f120, f211, f210, f021, f220, f221, f121`, lines 62–102) into 12
separate spinboxes and a 12-column CSV. Each mode is indexed by integers (m,n,p). Two-parameter
sweeps (e.g. width × height grids) are the natural generalization. The old code has no structure for
"indexed family of results."

**AFTER** (model the modes as an indexable family):

```cpp
namespace emc::shielding {
    struct EnclosureModes {
        // flat storage of the 12 computed modes ...
        std::array<quantity<isq::frequency[si::hertz]>, 12> data;
        std::array<Mode, 12>                                indices;  // (m,n,p) per slot
    };

    // a 2-D parameter sweep over (width, height) returned as an mdspan over flat storage
    [[nodiscard]] auto resonance_grid(std::span<const quantity<...>> widths,
                                      std::span<const quantity<...>> heights)
        -> /* owning buffer + */ std::mdspan</*freq*/, std::dextents<std::size_t, 2>>;
    // caller: grid(i, j) is the dominant-mode frequency for widths[i] × heights[j]
}
```

**Why it is the right tool.** `mdspan` expresses "a grid of results" first-class, replacing 12 named
scalars and ad-hoc 2-D loops with clean `m(i, j)` indexing and a single contiguous buffer that is
cache-friendly and trivially serializable to the CSV layout.

**Trade-offs / pitfalls.** `mdspan` is a *view* — it needs backing storage with a matching lifetime
(C++23 has no `std::mdarray` yet; own a `std::vector`/`std::array` alongside). Choose the layout
(`layout_right` default) to match your CSV ordering.

**Compiler support.** C++23; GCC 14+, recent libstdc++/libc++; MSVC catching up. Where unavailable,
fall back to a small wrapper or the reference implementation (`<experimental/mdspan>`).

### 3.6 `std::string_view` — `QString` material/unit names at the boundary, no allocations

**What it is.** A non-owning view of a character range; cheap to pass, no allocation.

**Pain point.** Names are `QString` everywhere: material lookup compares `currentText()` against
`"Copper"`/`"Nickel"` (`SkinDepthWidget.cpp` lines 42–61), unit selection compares strings
(`StandardGaugeWireWidget.cpp` lines 43–65). A Qt-free core must take names without depending on
`QString` and without copying.

**BEFORE** (`SkinDepthWidget.cpp`):

```cpp
QString material = ui->material_combobox->currentText();
if (material == "Copper") { ui->conductivity_spinbox->setValue(5.8005E+7); ... }
```

**AFTER** (`emc::materials`):

```cpp
namespace emc::materials {
    [[nodiscard]] std::expected<Material, emc::Error> from_name(std::string_view name);
    [[nodiscard]] std::string_view name_of(Material m);   // points into the static table
}
```

**Why it is the right tool.** Boundary code (a future Qt consumer, the CSV parser) passes
`sv = qstr.toStdString()` / a literal with no library-side allocation; lookups are `==` against the
constexpr table (§5 ranges).

**Trade-offs / pitfalls.** A `string_view` does not own and is **not** guaranteed null-terminated —
never pass one to a C API expecting `const char*` and never store one outliving its source.

**Compiler support.** C++17, universal.

### 3.7 Structured bindings — unpack results and table entries cleanly

**What it is.** `auto [a, b, c] = obj;` destructures aggregates, pairs, tuples, and arrays into named
locals.

**Pain point.** Multi-output results (microstrip's `Z0, C0, Tpd`; the 12 modes) were splattered into
many `ui->...->setValue()` calls. Consuming a result struct should be one readable line.

**AFTER:**

```cpp
auto result = emc::component::calculate(in);
if (result) {
    const auto& [z0, c0, tpd] = *result;     // names match the struct fields
    std::println("Z0={}, C0={}, Tpd={}", z0, c0, tpd);
}
// iterating the material table:
for (const auto& [mat, sigma, mu_r, name] : emc::materials::table) { /* ... */ }
```

**Why it is the right tool.** Self-documenting unpacking, fewer temporaries, and a natural fit with
`std::expected` (§4) and ranges (§5).

**Trade-offs / pitfalls.** Binding names can't be `constexpr` or referenced in lambdas before C++20
captures; with C++20+ this is fine. Binding count must match member count exactly.

**Compiler support.** C++17, universal.

---

## 4. Error handling

### 4.1 `std::expected` + monadic `and_then`/`transform`/`or_else`

**What it is.** `std::expected<T, E>` holds either a value or an error; monadic methods chain
fallible steps without nested `if`s. The library uses `std::expected<Result, emc::Error>` everywhere
(see [05-error-handling-and-validation.md](05-error-handling-and-validation.md) for `emc::Error` and
`ErrorCode`).

**Pain point.** Validation is `QMessageBox::warning(...)` woven into the math in **10 files** —
`MicrostripTraceWidget.cpp` pops dialogs mid-formula (lines 109, 114, 221, 309, 397), and
`StandardGaugeWireWidget.cpp` returns `EXIT_FAILURE` as a value (§2.2). Error handling is therefore
UI-coupled, non-composable, and impossible to test headlessly.

**BEFORE** (`MicrostripTraceWidget.cpp`):

```cpp
Z = 87 * log(5.98 * H / (0.8 * W + T)) / qSqrt(eps + 1.41);
if ((eps < 1) || (eps > 15)) {
    QMessageBox::warning(this, "Warning", "Permittivity is out of range");
    return;                                  // silent no-op for the caller
}
```

**AFTER** (`emc::component`):

```cpp
[[nodiscard]] std::expected<MicrostripResult, emc::Error>
calculate(const MicrostripInput& in) {
    return validate(in)                                  // expected<void, Error>
        .and_then([&] { return compute(in); })          // runs only if valid
        .transform([](MicrostripResult r) { return r; });// post-process if needed
}
// caller composes without dialogs:
auto z = calculate(a).and_then(refine).or_else(log_error);
```

**Why it is the right tool.** Errors are *values*: testable, composable, and decoupled from any UI.
The monadic chain reads top-to-bottom and short-circuits on the first failure, eliminating the
nested `if/else/QMessageBox` ladders. A Qt consumer maps the `Error` to a `QMessageBox` *at the
boundary*, not inside the math.

**Trade-offs / pitfalls.** `std::expected` is newer; if a toolchain lags, a drop-in
(`tl::expected`) preserves the API (note this in [08](08-build-system-cmake.md)). Don't ignore the
result — hence `[[nodiscard]]` (§4.2). Keep `emc::Error` small and copyable.

**Compiler support.** C++23; GCC 12+, Clang 16+, MSVC 19.33+. Monadic `and_then/transform/or_else`
on `expected`: same baseline.

### 4.2 `[[nodiscard]]` — results and errors cannot be silently dropped

**What it is.** An attribute that warns when a return value is discarded; can annotate functions or
types.

**Pain point.** Old code routinely ignored outcomes (the solver `return;`s on error; results are
pushed straight into UI). Nothing flagged a dropped computation.

**AFTER:**

```cpp
namespace emc {
    enum class [[nodiscard]] ErrorCode { /* ... */ };   // mark the type
}
[[nodiscard]] std::expected<SkinDepthResult, emc::Error>   // and every calculate()
calculate(const SkinDepthInput&);
```

**Why it is the right tool.** Calling a calculator and ignoring its `expected` becomes a warning
(escalate to error with `-Werror`), so the error path can't be accidentally swallowed.

**Trade-offs / pitfalls.** Apply to fallible/result-producing functions; over-applying to
fluent/builder APIs that legitimately discard is noise. Mark the *error type* `[[nodiscard]]` too,
per the conventions.

**Compiler support.** `[[nodiscard]]`: C++17; with reason string: C++20. Universal.

### 4.3 `std::source_location` — diagnostics without manual `__FILE__`/`__LINE__`

**What it is.** Captures call-site file/line/function as a default argument, no preprocessor.

**Pain point.** Failures in NinjaEMC surfaced (if at all) via `qDebug()` with no structured context;
there's no record of *where* a bad input entered.

**AFTER** (attach origin to an error without macros):

```cpp
namespace emc {
    struct Error {
        ErrorCode code;
        std::string_view message;
        std::source_location where = std::source_location::current();
    };
    [[nodiscard]] inline std::unexpected<Error>
    fail(ErrorCode c, std::string_view msg,
         std::source_location loc = std::source_location::current()) {
        return std::unexpected(Error{c, msg, loc});
    }
}
// usage: return emc::fail(ErrorCode::OutOfRange, "permittivity 1..15");
```

**Why it is the right tool.** Every error carries its origin for logs/tests with zero macro noise and
no per-call boilerplate, replacing scattered `qDebug()`.

**Trade-offs / pitfalls.** The default-argument trick must live in the *innermost* helper so it
captures the true call site. The library core still does no I/O — `source_location` is *data*, formatted
only by tools/tests (§7).

**Compiler support.** C++20; GCC 11+, Clang 16+, MSVC 19.29+.

---

## 5. Ranges & views

**What it is.** `std::ranges` algorithms and lazy `views` (`filter`, `transform`, `split`,
`to`) compose pipelines over sequences.

**Pain point.** Two recurring shapes: (a) material/unit lookup via long `if/else` chains
(`SkinDepthWidget.cpp`, `StandardGaugeWireWidget.cpp`); (b) CSV-vector parsing in the 52 test
harnesses, done with `QString::split(",")` and indexed `list.at(i)`
(`MicrostripTraceWidget.cpp` lines 64–71). Mode filtering (e.g. only modes below a cutoff frequency)
has no expression at all.

**BEFORE** (material lookup, `SkinDepthWidget.cpp`):

```cpp
if (material == "Copper") { ... } else if (material == "Aluminum") { ... } /* ×5 */
```

**AFTER** (lookup in the constexpr table; CSV parse in tests):

```cpp
#include <ranges>
#include <algorithm>
namespace emc::materials {
    [[nodiscard]] std::expected<Material, emc::Error> from_name(std::string_view n) {
        auto it = std::ranges::find(table, n, &MaterialData::name);
        if (it == table.end()) return emc::fail(ErrorCode::UnknownMaterial, n);
        return it->id;
    }
}
// test-side CSV row -> typed inputs (see doc 09):
auto fields = line | std::views::split(',')
                   | std::views::transform([](auto sv){ return parse_double(sv); });
// mode filtering: only resonances below the device's operating band
auto below = modes.data | std::views::filter([&](auto f){ return f < cutoff; });
```

**Why it is the right tool.** Declarative pipelines replace `if/else` chains and index-juggling
loops, are composable, and pair naturally with `std::expected` and structured bindings. The testing
document ([09](09-testing-and-golden-vectors.md)) builds the full CSV→input→compare pipeline on this.

**Trade-offs / pitfalls.** `views::split` yields subranges (not `string_view` directly until C++23
helpers); convert deliberately. Lazy views can be re-evaluated — `std::ranges::to` (C++23) when you
need a concrete container. Watch dangling on temporaries.

**Compiler support.** Ranges core: C++20 (GCC 10+, Clang 15+, MSVC 19.29+). `views::split` over
`string_view` ergonomics and `ranges::to`: C++23.

---

## 6. Functions & composition

### 6.1 Free functions in namespaces — instead of god-widgets

**What it is.** Domain logic lives in free functions grouped by namespace, not in methods of a
stateful UI class.

**Pain point.** Every formula in NinjaEMC lives **inside a button-clicked lambda in a widget
constructor** — `SkinDepthWidget.cpp` lines 64–72, `RectangularEnclosureWidget.cpp` lines 45–104.
There is no separable core; the math reads `ui->...->value()` and writes `ui->...->setValue()`. This
is the root cause that makes everything else (testing, reuse, units, error handling) impossible.

**BEFORE** (`SkinDepthWidget.cpp`, math trapped in a lambda):

```cpp
connect(ui->solveButton, &RichButton::clicked, [this]() {
    qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
    qreal conductivity = ui->conductivity_spinbox->value();
    qreal relativePermeability = ui->ur->value() * mu0;
    qreal skinDepth = qSqrt(1 / (M_PI * frequency * relativePermeability * conductivity));
    ui->skinDepth->setValue(skinDepth / ui->skinDepth_unit->currentData().toReal());
});
```

**AFTER** (`include/emc/basic/skin_depth.hpp`, pure & reusable):

```cpp
namespace emc::basic {
    struct SkinDepthInput {
        quantity<isq::frequency[si::hertz]>             frequency;
        quantity<isq::electric_conductivity[si::siemens/si::metre]> conductivity;
        double relative_permeability = 1.0;
    };
    struct SkinDepthResult { quantity<isq::length[si::metre]> depth; };

    [[nodiscard]] std::expected<SkinDepthResult, emc::Error>
    calculate(const SkinDepthInput& in);   // no Qt, no I/O, thread-safe, testable
}
```

**Why it is the right tool.** Pure free functions are the whole premise of the rewrite: no global
mutable state, thread-safe by construction, callable from the Qt app, the tests, or any future
consumer. The repeatable shape is [06-calculator-design-pattern.md](06-calculator-design-pattern.md).

**Trade-offs / pitfalls.** Some shared setup (material lookup, validation) must be factored into
helpers rather than duplicated — addressed by the concept (§2.1) and the pattern doc.

**Compiler support.** N/A (design), C++17+ namespace inline-variable conveniences universal.

### 6.2 Deducing this (explicit object parameter) — restrained fluent input builders

**What it is.** C++23 lets a member function name its object parameter explicitly
(`auto&& self`), enabling perfect-forwarding member chains and deduplicated const/non-const overloads
without CRTP.

**Pain point.** Constructing inputs for the bidirectional solvers and sweeps benefits from a small
fluent helper; doing this the old way (CRTP mixins, or duplicated `&`/`const&`/`&&` overloads) is
exactly the boilerplate that bred copy-paste. Used **sparingly** — most call sites just use
designated initializers (§3.1).

**AFTER** (one builder method, correct value category preserved):

```cpp
namespace emc::component {
    struct MicrostripBuilder {
        MicrostripInput in{};
        // single definition works for lvalue & rvalue builders; returns same category
        template <class Self>
        auto&& with_width(this Self&& self, quantity<...> w) {
            self.in.width = w;
            return std::forward<Self>(self);
        }
    };
}
// fluent, optional convenience (designated initializers remain the default style):
auto r = emc::component::calculate(MicrostripBuilder{}
            .with_width(15 * mm).with_height(18.65 * mm).in);
```

**Why it is the right tool.** One function body covers all value categories — no CRTP base, no
`const`/non-`const`/`&&` triplication. It keeps optional fluent helpers tiny.

**Trade-offs / pitfalls.** Easy to over-engineer; the conventions favor designated initializers, so
reserve deducing-this for genuinely repetitive mixin/forwarding cases. You cannot also have an
implicit `this` in the same function.

**Compiler support.** C++23; GCC 14+, Clang 18+, MSVC 19.32+.

### 6.3 User-defined literals via mp-units — `5 * mm`, `1 * GHz`

**What it is.** mp-units supplies unit symbols so inputs read as `quantity` expressions
(`18.65 * mm`, `1 * GHz`), the readable analogue of UDLs, with full dimensional checking.

**Pain point.** Inputs were raw doubles plus a separately-selected factor (the 541 `addItem(unit,
factor)` calls); the unit and the number were never bound together, which is how `*39.37` and
`PI=3.14`-style errors slipped in.

**BEFORE:**

```cpp
qreal frequency = ui->frequency_spinbox->value() * ui->frequencyUnitBox->currentData().toReal();
```

**AFTER:**

```cpp
using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // mm, GHz, ohm, ... (project re-exports via emc::units)
auto in = emc::basic::SkinDepthInput{ .frequency = 27 * MHz,
                                      .conductivity = /* from material */ };
```

**Why it is the right tool.** The unit is inseparable from the magnitude at the call site, the
compiler checks dimensions, and conversions are exact — the single biggest readability/safety win
over the old factor soup. The `emc::units` vocabulary and which symbols to re-export are
[03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md).

**Trade-offs / pitfalls.** `using namespace ..unit_symbols` can pollute scope; the project wraps the
needed symbols in `emc::units` and documents the import. Symbols are values to multiply, not C++ UDL
suffixes — keep that mental model.

**Compiler support.** Per mp-units (C++20/23; GCC 12+/Clang 16+ recommended).

---

## 7. Output / formatting — `std::format` and `std::print`

**What it is.** Type-safe formatting (`std::format`) and direct output (`std::print`/`std::println`,
C++23), replacing `printf`/iostream and Qt string building.

**Pain point.** Diagnostics and the test harnesses use `qDebug()` and manual `QString` concatenation
(`"..." + ui->z0_lineEdit->text() + "," + ...`, `MicrostripTraceWidget.cpp` line 421). The library
*core* must do no I/O, but tools and tests need clean formatted output.

**AFTER** (tools/tests only; the core stays I/O-free):

```cpp
#include <print>
// mp-units provides a formatter, so quantities print with their units
std::println("Z0 = {}", result.z0);                 // e.g. "Z0 = 50 Ω"
std::println("{},{},{}", result.z0, result.c0, result.tpd);  // CSV row for golden vectors
```

**Why it is the right tool.** Compile-time-checked format strings, no manual unit suffixes (mp-units
formats them), and a single statement replaces stream/`QString` concatenation. Used by the
golden-vector writer in [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) and any
CLI tool in [08](08-build-system-cmake.md).

**Trade-offs / pitfalls.** Keep all formatting/I/O **out** of `include/emc/**` and `src/**` core —
only in `tools/` and `tests/`. `std::print` (C++23) is newer than `std::format` (C++20); if
unavailable, `std::format` + a single `std::fputs` is the fallback.

**Compiler support.** `std::format`: C++20 (GCC 13+, Clang 17+/libc++, MSVC 19.29+). `std::print`:
C++23 (GCC 14+, recent libc++, MSVC 19.37+).

---

## 8. Attributes & quality

### 8.1 `[[likely]]` / `[[unlikely]]` — hint the validation fast path

**What it is.** Branch-probability hints to the optimizer.

**Pain point.** Validation runs on every call (replacing the inline `QMessageBox` checks). The common
case is "input valid"; hinting it keeps the happy path tight.

**AFTER:**

```cpp
if (in.relative_permittivity < 1.0 || in.relative_permittivity > 15.0) [[unlikely]]
    return emc::fail(ErrorCode::OutOfRange, "permittivity 1..15");
// fall through: the common, valid path
```

**Why it is the right tool.** Encodes the expected control flow for the error-as-value model with no
behavioural change.

**Trade-offs / pitfalls.** Hints can hurt if wrong; only annotate genuinely lopsided branches. Don't
sprinkle gratuitously.

**Compiler support.** C++20; GCC 9+, Clang 12+, MSVC 19.26+.

### 8.2 `[[assume]]` — encode validated invariants for the optimizer

**What it is.** C++23 attribute stating a condition the compiler may assume true (UB if false),
enabling better codegen.

**Pain point.** After `validate()` passes, downstream formulas still re-check or guard
(divisions by `H`, `W`). Once validated, those guarantees can be told to the optimizer.

**AFTER** (only *after* a real validation has run):

```cpp
auto compute(const MicrostripInput& in) {
    // validate(in) already guaranteed these; tell the optimizer
    [[assume(in.width.numerical_value_in(si::metre) > 0.0)]];
    [[assume(in.height.numerical_value_in(si::metre) > 0.0)]];
    // ... division by width/height now provably safe ...
}
```

**Why it is the right tool.** Removes redundant runtime guards on already-validated invariants.

**Trade-offs / pitfalls.** **Dangerous**: a false assumption is undefined behaviour. Use *only* for
conditions a preceding `validate()` truly enforces; never on raw user input. In C++26 this role is
better served by **contracts** (§10.2), which are checkable.

**Compiler support.** C++23; GCC 13+, Clang 19+, MSVC 19.33+.

---

## 9. A note on modules (deferred)

C++20 **modules** would give faster builds and clean encapsulation (no header leakage of mp-units
internals). The plan nonetheless targets a **traditional compiled library** — public headers in
`include/emc/`, compiled `.cpp` in `src/`, built into static/shared libs with `install()/export()`
and a CMake package config (see [08-build-system-cmake.md](08-build-system-cmake.md)). Reasons:
toolchain/IDE/`find_package` support for modules is still uneven across our GCC/Clang/MSVC matrix and
across mp-units consumption, and a headers+`.cpp` library is the portable, well-understood form a
downstream `find_package(emc)`/`emc::emc` link expects today. Modules remain an attractive *future*
migration once toolchains stabilize; the namespace/layering is designed so a later module map is
mechanical.

---

## 10. C++26 forward-looking (aspirational — not part of the initial build)

> This section is explicitly speculative. None of it is required for the rewrite; it shows where the
> design can go once C++26 features land. Each sketch names what it would replace.

### 10.1 Static reflection — auto-generate name↔enum maps and struct-field CSV mapping

**Today's pain.** Two hand-maintained mappings recur: `Material` ↔ string name (the `if (material ==
"Copper")` chains, §3.6/§5) and **struct fields ↔ CSV columns** (every `testPage` manually lists
`list.at(0..n)` and the header string, e.g. `MicrostripTraceWidget.cpp` lines 67–70, 411).

**C++26 sketch** (reflection, syntax illustrative):

```cpp
// generate name <-> enum without a hand-written table
template <auto E> constexpr std::string_view enum_name = std::meta::identifier_of(^^E);

// iterate an Input struct's fields to (de)serialize a CSV row automatically
template <class Input>
Input from_csv(std::span<const std::string_view> cols) {
    Input in{};
    [: expand fields of ^^Input :] >> [&]<auto field>(std::size_t i) {
        in.[:field:] = parse(cols[i]);     // field name + index, reflected
    };
    return in;
}
```

**Would replace.** The 52 bespoke `testPage` CSV parsers and the material name tables — one generic
reflective (de)serializer driven by the `Input`/`Result` struct definitions ([09](09-testing-and-golden-vectors.md)).

### 10.2 Contracts — checkable pre/postconditions for validation

**Today's pain.** `validate()` returns `expected<void, Error>` and `[[assume]]` (§8.2) is unchecked.
Contracts let pre/postconditions be *declared* and optionally *enforced*.

**C++26 sketch** (syntax illustrative):

```cpp
quantity<...> microstrip_z0(const MicrostripInput& in)
    pre (in.relative_permittivity >= 1.0 && in.relative_permittivity <= 15.0)
    pre (in.width.numerical_value_in(si::metre) > 0.0)
    post (r: r > 0 * si::ohm);
```

**Would replace.** A chunk of the imperative `validate()` body and the risky `[[assume]]`s — the
preconditions become declarative and runtime-checkable in debug, assumable in release.

### 10.3 Pattern matching — cleaner solve-target dispatch

**Today's pain.** Runtime solve-target dispatch uses `std::variant` + `std::visit` (§3.3); the four
NinjaEMC `microstrip/calH/calT/calW` methods are the legacy form.

**C++26 sketch** (`inspect`/match, syntax illustrative):

```cpp
auto result = query match {
    SolveZ0    => solve_z0(known);
    SolveHeight=> solve_h(known);
    SolveThickness => solve_t(known);
    SolveWidth => solve_w(known);
};
```

**Would replace.** The `std::visit` lambda boilerplate and, historically, the 4×8-branch method
explosion — one readable match expression.

### 10.4 `std::execution` / senders — parallel parameter sweeps

**Today's pain.** Sweeps (§3.4/§3.5) and the 52-fixture golden runs are sequential loops. Pure
calculators are embarrassingly parallel.

**C++26 sketch** (senders/`bulk`, syntax illustrative):

```cpp
auto work = stdexec::schedule(pool)
          | stdexec::bulk(heights.size(), [&](std::size_t i){ out[i] = calculate(at(i)); });
stdexec::sync_wait(std::move(work));
```

**Would replace.** Hand-rolled threading for sweeps and batch regression — structured, composable
parallelism over the already-pure `calculate()` functions, with no shared mutable state to guard
(the purity from §6.1 is what makes this safe).

---

## Cross-references

- [00-overview-and-goals.md](00-overview-and-goals.md) — vision, before/after, success criteria.
- [01-architecture-and-layout.md](01-architecture-and-layout.md) — layers, namespaces, library shape.
- [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — the mp-units subsystem (§2.3, §6.3) that replaces the 541 unit factors.
- [04-constants-and-material-database.md](04-constants-and-material-database.md) — one constexpr source of truth (§1.2–1.4, §2.2).
- [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — `std::expected` + `emc::Error` (§4).
- [06-calculator-design-pattern.md](06-calculator-design-pattern.md) — the Input/Result/`calculate` pattern + `Calculator` concept (§2.1, §3.1, §6.1).
- [08-build-system-cmake.md](08-build-system-cmake.md) — compiled lib, mp-units dependency, toolchain floor (the §9 modules note, the C++23 feature gates).
- [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — reusing the 52 CSVs with ranges, `static_assert`, `std::print` (§1.5, §5, §7).
- [10-migration-roadmap.md](10-migration-roadmap.md) — phased sequencing and definition of done.
