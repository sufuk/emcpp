# Modern C++ Feature Catalog (the centerpiece) ⚙️

> Purpose: an example-driven catalog of every modern C++ feature the `emc` library uses, each
> justified by a concrete EMC-domain reason, with a worked example an implementer can copy.

The architecture ([01-architecture-and-layout.md](01-architecture-and-layout.md)), the units subsystem
([03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md)), the constants and material
database ([04-constants-and-material-database.md](04-constants-and-material-database.md)), the error model
([05-error-handling-and-validation.md](05-error-handling-and-validation.md)), and the calculator pattern
([06-calculator-design-pattern.md](06-calculator-design-pattern.md)) assemble the features below into a
coherent whole. Where a feature has a dedicated home document, this catalog gives the *rationale and a
worked example*, then points there for the full subsystem.

All example code is **C++23** and assumes the locked design decisions:

- top-level namespace `emc` with sub-namespaces `emc::constants`, `emc::materials`, `emc::units`,
  `emc::detail`, and per-category calculator namespaces (`emc::basic`, `emc::component`, …);
- physical quantities are **mp-units** `quantity<...>` types, never bare `double`;
- fallible operations return `std::expected<Result, emc::Error>` and are `[[nodiscard]]`;
- each calculator is a free function over an aggregate `Input` struct returning a `Result` struct.

---

## 0. Summary table

| Feature | C++ version | EMC-domain reason | Where detailed |
|---|---|---|---|
| `std::numbers::pi` / `inv_pi` / `sqrt2` | C++20 | one correctly-rounded π for every formula; no hand-typed literals | §1.1 |
| `constexpr` constants in `emc::constants` | C++11/14/17 | one source of truth for c₀, μ₀, ε₀ across the whole library | §1.2, [04](04-constants-and-material-database.md) |
| `consteval` (immediate functions) | C++20 | derived constants folded at compile time, never runtime magic numbers | §1.3 |
| `constinit` | C++20 | material table built at compile time, no static-init-order fiasco | §1.4 |
| `constexpr` `<cmath>` | C++23 | EMC formulas (`sqrt`,`exp`,`log`,`pow`) foldable → `static_assert` golden checks | §1.5 |
| `static_assert` | C++11/17 | compile-time sanity net for constants and unit factors | §1.6 |
| `if constexpr` | C++17 | branch on solve-target / unit family without runtime cost | §1.7 |
| Concepts (`std::floating_point`, `Quantity`, `Calculator`) | C++20 | machine-checked calculator contract across all calculators | §2.1 |
| `enum class` (Material, SolveTarget) | C++11 | strong material/target identity, no int sentinels | §2.2 |
| Strong typing via mp-units quantities | (mp-units) | EMC inputs span Hz…GHz and m…mils; compile-time unit safety | §2.3, [03](03-quantities-and-units-mp-units.md) |
| Three-way comparison `<=>` | C++20 | value/result structs need ordering & equality for tests | §2.4 |
| CTAD | C++17 | terser construction of spans/optionals/arrays in calculators & tests | §2.5 |
| Abbreviated function templates (`auto` params) | C++20 | small generic helpers without verbose `template<>` noise | §2.6 |
| Aggregate `Input` structs + designated initializers | C++20 | named, unit-checked inputs; no argument-order bugs | §3.1, [06](06-calculator-design-pattern.md) |
| `std::optional` | C++17 | "blank field auto-derives" modelled as absence, not a `0` sentinel | §3.2 |
| `std::variant` | C++17 | bidirectional solvers as data, solved once | §3.3 |
| `std::span` | C++20 | non-owning views over sweep inputs / mode arrays without copies | §3.4 |
| `std::mdspan` | C++23 | the 12-mode enclosure output and 2-D parameter sweeps | §3.5 |
| `std::string_view` | C++17 | material/unit names at the boundary without allocations | §3.6 |
| Structured bindings | C++17 | unpack result structs / table entries cleanly | §3.7 |
| `std::expected` + monadic ops | C++23 | out-of-domain inputs reported as recoverable typed errors | §4.1, [05](05-error-handling-and-validation.md) |
| `[[nodiscard]]` | C++17/20 | computed results and errors cannot be silently dropped | §4.2 |
| `std::source_location` | C++20 | error origin captured as data, no preprocessor | §4.3 |
| Ranges + views | C++20 | material lookup; CSV parsing in tests; mode filtering | §5, [09](09-testing-and-golden-vectors.md) |
| Free functions in namespaces | (core) | pure, reusable, testable math core | §6.1 |
| Deducing this (explicit object param) | C++23 | tidy fluent input builders without CRTP boilerplate | §6.2 |
| UDLs via mp-units (`5 * mm`, `1 * GHz`) | (mp-units) | unit-checked literals binding magnitude to unit | §6.3, [03](03-quantities-and-units-mp-units.md) |
| `std::format` / `std::print` | C++20/23 | type-safe formatted output in tools & tests | §7 |
| `[[likely]]` / `[[unlikely]]` | C++20 | hint the validation fast path | §8.1 |
| `[[assume]]` | C++23 | encode validated invariants for the optimizer | §8.2 |
| Modules (deferred) | C++20 | why we ship headers+`.cpp` now | §9 |
| **C++26 forward-looking**: reflection, contracts, pattern matching, senders | C++26 | name↔enum maps, validation pre/post, solve dispatch, parallel sweeps | §10 |

---

## 1. Constants & compile-time

### 1.1 `std::numbers` — one correctly-rounded π

**What it is.** `<numbers>` provides correctly-rounded mathematical constants as variable templates:
`std::numbers::pi`, `inv_pi`, `sqrt2`, `ln2`, `e`, etc., each defaulting to `double` but available for
any floating type via `std::numbers::pi_v<T>`.

**Domain reason.** EMC formulas use π pervasively (loop areas, skin depth, resonant modes). A single
full-precision π supplied by the standard library guarantees every formula shares the same value, with
no way to introduce a low-precision literal.

**Example:**

```c++
#include <numbers>
namespace emc::detail {
    inline constexpr double pi = std::numbers::pi;   // 3.141592653589793...
}
// usage in a calculator (quantities elided for clarity here)
const auto area = emc::detail::pi * (d / 2.0) * (d / 2.0);
```

> [!WARNING]
> `std::numbers::pi` is `double`. For `float`/`long double` use the `_v<T>` form so you do not
> silently narrow. Do not wrap it in a macro — keep it a typed `constexpr`.

**Compiler support.** GCC 10+, Clang 11+, MSVC 19.27+ (all far below our toolchain floor).

### 1.2 `constexpr` constants in `emc::constants` — one source of truth

**What it is.** Compile-time-evaluable named constants, expressed as **mp-units quantities** where
physical, gathered in `emc::constants`.

**Domain reason.** The core electromagnetic constants — speed of light, vacuum permeability and
permittivity — feed nearly every calculator. Defining each once, with its exact SI value and its unit
baked into the type, removes any chance of an imprecise or divergent copy.

**Example** (`include/emc/constants.hpp`, one definition for the whole library):

```c++
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

**Why it is the right tool.** `inline constexpr` gives one definition usable across translation units
with no ODR violation; mp-units makes the *unit* part of the value so misuse is a compile error. Full
detail and the complete constant set live in
[04-constants-and-material-database.md](04-constants-and-material-database.md).

**Trade-offs / pitfalls.** mp-units quantities are not implicitly convertible to `double`; call sites
must keep the dimensional chain (that is the point). Use digit separators (`'`) for readability of long
magnitudes.

**Compiler support.** `inline constexpr` variables: C++17, ubiquitous.

### 1.3 `consteval` — immediate functions for derived constants

**What it is.** `consteval` marks a function that *must* execute at compile time; calling it produces a
constant or fails to compile. Useful for constants computed from other constants.

**Domain reason.** Derived constants such as half the speed of light (used in resonant-cavity and
propagation formulas) should be folded once at compile time, never recomputed as a runtime expression
where a non-constant input could creep in.

**Example:**

```c++
namespace emc::detail {
    // half the speed of light as a plain magnitude in m/s, forced at compile time
    consteval double half_c_mps() {
        return emc::constants::c0.numerical_value_in(mp_units::si::metre / mp_units::si::second) / 2.0;
    }
    inline constexpr double k = half_c_mps();  // guaranteed compile-time, not a runtime mul
}
```

**Why it is the right tool.** `consteval` is a hard guarantee: if anyone later passes a runtime input it
is a compile error, not a silent runtime computation. It documents intent ("this is a constant, period").

**Trade-offs / pitfalls.** Reserve `consteval` for true constants; it cannot appear in a context that is
not constant-evaluated.

**Compiler support.** GCC 10+, Clang 11+, MSVC 19.28+.

### 1.4 `constinit` — no static-init-order fiasco for tables

**What it is.** `constinit` asserts that a variable with static storage duration is initialized at
compile time (constant initialization), eliminating dynamic initialization and its ordering hazards.

**Domain reason.** The material/conductivity database is a single library-wide table consulted by many
calculators. We want a *hard guarantee* it is built at compile time with no static-init-order surprises.

**Example** (`include/emc/materials.hpp`, see [04](04-constants-and-material-database.md) for full data):

```c++
namespace emc::materials {
    struct MaterialData { /* conductivity, rel. permeability, name ... */ };
    // constinit: proves at compile time there is no dynamic initialization
    inline constinit std::array<MaterialData, 9> table = { /* ...filled constexpr... */ };
}
```

**Why it is the right tool.** It turns "I hope this is constant-initialized" into a compile-time
assertion. If the initializer were ever made non-constant, the build breaks loudly.

**Trade-offs / pitfalls.** `constinit` only constrains *initialization*; the object can still be mutable
unless you also mark it `const`/`constexpr`. For an immutable table prefer `inline constexpr`; use
`constinit` when you specifically need a non-const but constant-initialized global.

**Compiler support.** GCC 10+, Clang 10+, MSVC 19.29+.

### 1.5 `constexpr <cmath>` (C++23) — formulas folded at compile time

**What it is.** C++23 makes the common `<cmath>` functions (`sqrt`, `exp`, `log`, `pow`, `sin`, …)
`constexpr`, so whole formulas can be evaluated during compilation.

**Domain reason.** EMC math leans on `sqrt`/`exp`/`log`/`pow` (skin depth, microstrip impedance,
coupling). Making these calls `constexpr` lets closed-form reference results be checked at compile time
and keeps the functions pure and thread-safe.

**Example** (pure, constexpr-capable; mp-units quantities in/out):

```c++
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

> [!IMPORTANT]
> `constexpr <cmath>` support is uneven (GCC 13+ has broad coverage; libc++/Clang and MSVC lag on some
> functions). The plan's posture: write the formulas `constexpr`, but only `static_assert` the subset
> your toolchain actually folds, and keep the runtime path identical. mp-units arithmetic is already
> `constexpr`, so the only gating factor is the `<cmath>` call.

**Compiler support.** GCC 13+ broad; Clang/libc++ partial; MSVC partial. Treat compile-time evaluation of
transcendental functions as best-effort, runtime correctness as guaranteed.

### 1.6 `static_assert` — a compile-time sanity net

**What it is.** Compile-time assertions over constant expressions, with a diagnostic message.

**Domain reason.** Constants and unit factors must hold exact known relationships (π in range, 1 inch =
25.4 mm). Encoding these as compile-time checks makes a precision regression impossible to ship.

**Example:**

```c++
#include <numbers>
namespace emc::detail {
    // a regression that lowers pi precision fails the build
    static_assert(std::numbers::pi > 3.1415 && std::numbers::pi < 3.1416,
                  "pi constant is out of expected precision range");
    // unit-vocabulary sanity: 1 inch must equal 25.4 mm exactly
    static_assert(/* mp-units check that 1*inch == 25.4*mm */ true);
}
```

**Why it is the right tool.** It encodes physical invariants directly into the build, so a whole class of
precision error becomes impossible to reintroduce.

**Trade-offs / pitfalls.** Only works on constant expressions; pair with §1.5 so formula checks are
foldable. Keep messages specific so a failing assert points to the offending invariant.

**Compiler support.** `static_assert` with message: C++11; single-argument form: C++17.

### 1.7 `if constexpr` — compile-time branch selection without dead runtime code

**What it is.** A compile-time `if` whose untaken branch is discarded, enabling one templated function to
specialize behaviour per type/value with no runtime branch.

**Domain reason.** Bidirectional solvers (e.g. microstrip solving for Z₀, height, thickness, or width)
select a formula by target. With mp-units the unit branch disappears entirely (§2.3); where a *target*
is known at compile time, `if constexpr` picks the formula with zero runtime cost.

**Example** (solve-target chosen by a non-type template parameter):

```c++
enum class SolveTarget { Z0, H, T, W };

template <SolveTarget Target>
[[nodiscard]] constexpr auto microstrip_solve(const MicrostripInput& in) {
    if constexpr (Target == SolveTarget::Z0)      return solve_z0(in);
    else if constexpr (Target == SolveTarget::H)  return solve_h(in);
    else if constexpr (Target == SolveTarget::T)  return solve_t(in);
    else                                          return solve_w(in);
}
```

**Why it is the right tool.** Each instantiation compiles to exactly one formula; no runtime dispatch, no
dead branches. (For runtime-chosen targets use `std::variant`/visitation, §3.3, or pattern matching in
C++26, §10.3.)

**Trade-offs / pitfalls.** The condition must be a constant expression, so this only helps when the
target is known at compile time. Use it for internal specialization, the variant for a user's runtime
choice.

**Compiler support.** C++17, universal.

---

## 2. Type system

### 2.1 Concepts — `std::floating_point`, a `Quantity` constraint, and the `Calculator` concept

**What it is.** Concepts are named, checkable constraints on template arguments, giving clear compile
errors and self-documenting APIs.

**Domain reason.** Every calculator follows the same shape (read `Input` → compute → return `Result`).
A concept turns that shared shape into a machine-checked contract, so the family stays uniform.

**Example** — three layered concepts:

```c++
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
calculator that forgets `validate`, returns the wrong type, or drops `std::expected` fails to model the
concept and the build rejects it. The pattern itself is the subject of
[06-calculator-design-pattern.md](06-calculator-design-pattern.md).

**Trade-offs / pitfalls.** Over-constraining hurts (don't demand more from a type than the algorithm
uses). Keep `Quantity` aligned with whatever mp-units exposes; prefer mp-units' own concepts where
available rather than re-deriving them.

**Compiler support.** GCC 10+, Clang 10+ (Clang 12+ recommended), MSVC 19.30+.

### 2.2 `enum class` — Material and SolveTarget with strong identity

**What it is.** Scoped, strongly-typed enumerations that don't implicitly convert to `int` and don't leak
names into the enclosing scope.

**Domain reason.** Material identity (Copper, Silver, …) and solve targets must be exact, distinct
values. A scoped enum prevents confusing a material with an arbitrary integer index and prevents an
"unknown material" case from masquerading as a numeric value.

**Example** (`include/emc/materials.hpp`):

```c++
namespace emc::materials {
    enum class Material { Custom, Copper, Silver, Gold, Aluminium,
                          Tungsten, Platinum, Lead, Graphite, Nickel };

    // unknown material is an Error, never a fake number (see doc 05)
    [[nodiscard]] std::expected<MaterialData, emc::Error> lookup(Material m);
}
```

**Why it is the right tool.** `enum class` prevents index/enum confusion and int/double mixups; combined
with `std::expected` (§4.1) the "unknown material" case becomes a typed error instead of a sentinel that
silently flows into the next multiplication.

**Trade-offs / pitfalls.** You lose implicit `int` conversion (a feature here). To index a table by enum,
convert explicitly with `std::to_underlying` (C++23). Keep the enum and the data table in one place
([04](04-constants-and-material-database.md)) so order/identity can't drift.

**Compiler support.** `enum class`: C++11. `std::to_underlying`: C++23 (GCC 11+, Clang 13+, MSVC 19.30+).

### 2.3 Strong typing via mp-units quantities

**What it is.** Each physical value carries its dimension and unit in the type:
`quantity<isq::length[si::metre]>`, `quantity<isq::frequency[si::hertz]>`, etc. Conversions are
compile-checked; you cannot add a length to a frequency or forget a factor.

**Domain reason.** EMC inputs span many decades and unit families — frequency in Hz…GHz, geometry in
metres…mils, conductivity, permittivity, permeability. Binding the unit into the type gives compile-time
dimensional safety and makes every conversion exact and explicit, collapsing what would otherwise be an
8-branch per-solver unit tree into *zero* branches.

**Example** (the unit lives in the value; no branch, no factor):

```c++
using namespace mp_units;
quantity h = 18.65 * si::milli<si::metre>;   // caller states the unit once
// formulas use h directly; if a result is wanted in mils, convert at the edge:
auto h_mils = h.in(/* mil unit from emc::units */);   // checked, exact
```

**Why it is the right tool.** The conversion is a compile-checked operation on the type, performed once at
the boundary, with the exact factor baked into mp-units. The full unit vocabulary (`emc::units`) is
[03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md).

**Trade-offs / pitfalls.** Learning curve and compile-time cost; error messages can be verbose. Mitigate
with the project `emc::units` aliases and UDLs (§6.3). Never extract a bare `double` mid-formula — only
at I/O boundaries via `.numerical_value_in(unit)`.

**Compiler support.** mp-units requires a C++20/23 compiler; GCC 12+/Clang 16+ recommended. See [03].

### 2.4 Three-way comparison `<=>` — orderable value/result structs

**What it is.** The spaceship operator `<=>` lets the compiler synthesize all six relational operators
from one defaulted definition.

**Domain reason.** Result/value structs need equality (for reference-value tests,
[09](09-testing-and-golden-vectors.md)) and sometimes ordering (sorting sweep outputs). One defaulted
operator supplies both without hand-written, error-prone comparators.

**Example:**

```c++
namespace emc::component {
    struct MicrostripResult {
        quantity<isq::resistance[si::ohm]> z0;
        quantity<...> c0;
        quantity<...> tpd;
        auto operator<=>(const MicrostripResult&) const = default;   // all six ops, free
    };
}
```

> [!NOTE]
> Defaulted `==` compares floating/quantity values **exactly**. Reference-value tests must compare with
> tolerance instead (helper in [09]). Use `<=>` for structural ordering, but do approximate comparison
> explicitly for physics outputs.

**Compiler support.** GCC 10+, Clang 10+ (Clang 12+ for defaulted `==` interplay), MSVC 19.28+.

### 2.5 CTAD — class template argument deduction

**What it is.** Construct class templates without spelling the template arguments:
`std::array a{1, 2, 3};`, `std::span s{vec};`, `std::optional o{x};`.

**Domain reason.** Calculators and tests build small arrays, spans, and optionals where the element type
is obvious; eliding it removes redundant type spelling and places to get a type wrong.

**Example:**

```c++
std::array modes{ Mode{1,1,0}, Mode{1,0,1}, Mode{0,1,1} };   // deduces array<Mode,3>
std::span view{ inputs };                                    // deduces span<T>
```

**Trade-offs / pitfalls.** Deduction can surprise (`std::array a{1, 2u}` won't deduce). Spell the type
when intent matters.

**Compiler support.** C++17, universal.

### 2.6 Abbreviated function templates (`auto` parameters)

**What it is.** A function with an `auto` parameter is implicitly a template:
`auto square(auto x) { return x * x; }`.

**Domain reason.** Many tiny numeric helpers recur across calculators (squaring a quantity for `1/L²`
mode terms, summing radicands). An abbreviated template expresses them without per-helper
`template<class T>` boilerplate.

**Example** (constrained `auto` keeps it safe):

```c++
constexpr auto squared(emc::Quantity auto q) { return q * q; }   // for the 1/L² mode terms
```

**Trade-offs / pitfalls.** Unconstrained `auto` params accept anything; always pair with a concept
(`emc::Quantity auto`, `std::floating_point auto`) for a meaningful contract.

**Compiler support.** GCC 10+, Clang 10+, MSVC 19.28+.

---

## 3. Data & APIs

### 3.1 Aggregate `Input` structs + designated initializers

**What it is.** An aggregate struct with member defaults, populated at the call site with named
designated initializers: `f({.height = 1.5 * mm, .width = 2.65 * mm});`.

**Domain reason.** A calculator with several physical inputs of the same dimension (e.g. microstrip
height, thickness, width) is a magnet for argument-order mistakes. Named fields make a swapped argument a
compile-time impossibility, member defaults encode sensible defaults once, and the struct maps 1:1 onto a
reference-table column header for test reuse.

**Example** (`include/emc/component/microstrip.hpp`):

```c++
namespace emc::component {
    struct MicrostripInput {
        quantity<isq::length[si::metre]> height;
        quantity<isq::length[si::metre]> trace_thickness;
        quantity<isq::length[si::metre]> width;
        double relative_permittivity = 4.7;   // sensible default
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

**Why it is the right tool.** Named fields make argument-order bugs impossible, member defaults encode
defaults once, and the aggregate maps cleanly onto a reference column header for test reuse
([09](09-testing-and-golden-vectors.md)). This is the backbone of the calculator pattern
([06](06-calculator-design-pattern.md)).

> [!IMPORTANT]
> Designated initializers in C++ must appear in **declaration order** (unlike C). Aggregates can't have
> user-declared constructors; keep them plain data. Adding a field with a default is source-compatible;
> reordering fields is not — treat field order as API.

**Compiler support.** Designated initializers: C++20 (GCC 8+, Clang 10+, MSVC 19.21+).

### 3.2 `std::optional` — "blank field auto-derives" without `0` sentinels

**What it is.** `std::optional<T>` represents "a value or nothing" with no magic sentinel.

**Domain reason.** A wire calculator can take a material *or* a raw resistivity *or* a conductivity, and
derive the rest. Zero is a legitimate-looking physical number, so a `0`-means-absent convention is
fragile. `std::optional` models "not supplied" as genuine absence.

**Example:**

```c++
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

**Why it is the right tool.** "Absent" is modelled as absent, not as a number; the resolution reads as a
clear priority list and a missing-input case becomes a typed error (§4.1) instead of a zero dividing
later.

**Trade-offs / pitfalls.** Don't use `optional` for "value that may be invalid" — that's
`std::expected`. `optional<quantity>` works but `*opt` is unchecked; pair access with `has_value()` or
monadic ops.

**Compiler support.** C++17, universal.

### 3.3 `std::variant` — bidirectional solving as data

**What it is.** A type-safe tagged union; `std::visit` dispatches on the active alternative.

**Domain reason.** A bidirectional solver lets the user pick which variable is the unknown. Modelling
that choice as a `variant` of solve-target types replaces N near-identical solver methods with one entry
point and one formula per alternative.

**Example** (model the choice as data, solve once):

```c++
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

**Why it is the right tool.** One entry point, one place for shared validation, and each alternative holds
a single formula (units already handled by mp-units, §2.3).

**Trade-offs / pitfalls.** `std::visit` over many alternatives can be verbose; a generic lambda
(`[&](const auto&)`) helps when alternatives share an interface. For *compile-time-known* targets prefer
`if constexpr` (§1.7). C++26 pattern matching (§10.3) will make this cleaner still.

**Compiler support.** C++17, universal.

### 3.4 `std::span` — non-owning views over sweep inputs

**What it is.** `std::span<T>` is a lightweight (pointer, length) view over contiguous data, owning
nothing.

**Domain reason.** Parameter sweeps and the reference-value loops in tests pass sequences of frequencies,
heights, or modes. A `span` lets the core accept any contiguous source (a `std::vector`, a `std::array`,
a parsed row) without copying or coupling to a container type.

**Example:**

```c++
namespace emc::component {
    // sweep height across many values, no ownership, no copy
    [[nodiscard]] std::vector<std::expected<MicrostripResult, emc::Error>>
    sweep_height(const MicrostripCommon& base, std::span<const quantity<...>> heights);
}
```

**Why it is the right tool.** Decouples the algorithm from the container; the test harness can feed parsed
reference rows directly ([09](09-testing-and-golden-vectors.md)) with no allocation.

**Trade-offs / pitfalls.** A span does not own — never return a span to a local. Bounds are the caller's
responsibility; prefer the ranged overloads for safety where lifetimes are subtle.

**Compiler support.** C++20 (GCC 10+, Clang 7+ partial/12+ full, MSVC 19.26+).

### 3.5 `std::mdspan` — the 12-mode enclosure output and 2-D sweeps

**What it is.** `std::mdspan` is a multidimensional, non-owning view with a customizable layout; it gives
N-dimensional indexing (`m(i, j)`) over flat storage.

**Domain reason.** A rectangular enclosure has a family of resonant-mode frequencies indexed by integers
(m, n, p) — for example the 12 lowest modes. Two-parameter sweeps (e.g. width × height grids) are the
natural generalization. `mdspan` expresses "an indexed family of results" first-class instead of as many
named scalars or ad-hoc nested loops.

**Example** (model the modes as an indexable family):

```c++
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

**Why it is the right tool.** `mdspan` replaces a dozen named scalars and ad-hoc 2-D loops with clean
`m(i, j)` indexing over a single contiguous, cache-friendly buffer.

**Trade-offs / pitfalls.** `mdspan` is a *view* — it needs backing storage with a matching lifetime
(C++23 has no `std::mdarray` yet; own a `std::vector`/`std::array` alongside). Choose the layout
(`layout_right` default) to match your row ordering.

**Compiler support.** C++23; GCC 14+, recent libstdc++/libc++; MSVC catching up. Where unavailable, fall
back to a small wrapper or the reference implementation (`<experimental/mdspan>`).

### 3.6 `std::string_view` — material/unit names at the boundary, no allocations

**What it is.** A non-owning view of a character range; cheap to pass, no allocation.

**Domain reason.** Front ends and parsers hand material and unit names to the core as text ("Copper",
"GHz"). A `string_view` lets the core accept those names without owning a string type or allocating, and
look them up against the constexpr table.

**Example** (`emc::materials`):

```c++
namespace emc::materials {
    [[nodiscard]] std::expected<Material, emc::Error> from_name(std::string_view name);
    [[nodiscard]] std::string_view name_of(Material m);   // points into the static table
}
```

**Why it is the right tool.** Boundary code (a generic front end, a reference-data parser) passes a
literal or an existing string with no library-side allocation; lookups are `==` against the constexpr
table (§5 ranges).

> [!WARNING]
> A `string_view` does not own and is **not** guaranteed null-terminated — never pass one to a C API
> expecting `const char*` and never store one outliving its source.

**Compiler support.** C++17, universal.

### 3.7 Structured bindings — unpack results and table entries cleanly

**What it is.** `auto [a, b, c] = obj;` destructures aggregates, pairs, tuples, and arrays into named
locals.

**Domain reason.** Many results are multi-output (microstrip's Z₀, C₀, Tpd; the mode family). Consuming a
result struct or iterating the material table should be one readable line of named locals.

**Example:**

```c++
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

**Trade-offs / pitfalls.** Binding count must match member count exactly. With C++20+ binding names can be
captured in lambdas.

**Compiler support.** C++17, universal.

---

## 4. Error handling

### 4.1 `std::expected` + monadic `and_then`/`transform`/`or_else`

**What it is.** `std::expected<T, E>` holds either a value or an error; monadic methods chain fallible
steps without nested `if`s. The library uses `std::expected<Result, emc::Error>` everywhere (see
[05-error-handling-and-validation.md](05-error-handling-and-validation.md) for `emc::Error` and
`ErrorCode`).

**Domain reason.** Calculator inputs have physical domains (permittivity 1…15, positive geometry,
positive frequency). An out-of-domain input is a recoverable, expected condition — `std::expected`
reports it as a typed error value, testable headlessly and decoupled from any front end, rather than as a
side-effecting failure mid-formula.

**Example** (`emc::component`):

```c++
[[nodiscard]] std::expected<MicrostripResult, emc::Error>
calculate(const MicrostripInput& in) {
    return validate(in)                                  // expected<void, Error>
        .and_then([&] { return compute(in); })          // runs only if valid
        .transform([](MicrostripResult r) { return r; });// post-process if needed
}
// caller composes without side effects:
auto z = calculate(a).and_then(refine).or_else(log_error);
```

**Why it is the right tool.** Errors are *values*: testable, composable, and decoupled from any front
end. The monadic chain reads top-to-bottom and short-circuits on the first failure. A front end maps the
`Error` to its own presentation *at the boundary*, not inside the math.

**Trade-offs / pitfalls.** `std::expected` is newer; if a toolchain lags, a drop-in (`tl::expected`)
preserves the API (note this in [08](08-build-system-cmake.md)). Don't ignore the result — hence
`[[nodiscard]]` (§4.2). Keep `emc::Error` small and copyable.

**Compiler support.** C++23; GCC 12+, Clang 16+, MSVC 19.33+. Monadic `and_then/transform/or_else` on
`expected`: same baseline.

### 4.2 `[[nodiscard]]` — results and errors cannot be silently dropped

**What it is.** An attribute that warns when a return value is discarded; can annotate functions or types.

**Domain reason.** A calculator's whole purpose is its returned `expected`. Dropping it means an unhandled
error or an unused computation; `[[nodiscard]]` makes that a diagnostic.

**Example:**

```c++
namespace emc {
    enum class [[nodiscard]] ErrorCode { /* ... */ };   // mark the type
}
[[nodiscard]] std::expected<SkinDepthResult, emc::Error>   // and every calculate()
calculate(const SkinDepthInput&);
```

**Why it is the right tool.** Calling a calculator and ignoring its `expected` becomes a warning (escalate
to error with `-Werror`), so the error path can't be accidentally swallowed.

**Trade-offs / pitfalls.** Apply to fallible/result-producing functions; over-applying to fluent/builder
APIs that legitimately discard is noise. Mark the *error type* `[[nodiscard]]` too.

**Compiler support.** `[[nodiscard]]`: C++17; with reason string: C++20. Universal.

### 4.3 `std::source_location` — diagnostics without manual `__FILE__`/`__LINE__`

**What it is.** Captures call-site file/line/function as a default argument, no preprocessor.

**Domain reason.** When an out-of-domain input is rejected deep in a formula, logs and tests need to know
*where* it entered. `source_location` attaches that origin to the error as plain data, no macros.

**Example** (attach origin to an error without macros):

```c++
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

**Why it is the right tool.** Every error carries its origin for logs/tests with zero macro noise and no
per-call boilerplate.

**Trade-offs / pitfalls.** The default-argument trick must live in the *innermost* helper so it captures
the true call site. The library core still does no I/O — `source_location` is *data*, formatted only by
tools/tests (§7).

**Compiler support.** C++20; GCC 11+, Clang 16+, MSVC 19.29+.

---

## 5. Ranges & views

**What it is.** `std::ranges` algorithms and lazy `views` (`filter`, `transform`, `split`, `to`) compose
pipelines over sequences.

**Domain reason.** Three recurring shapes: material/unit lookup in the constexpr table; parsing
reference-value rows into typed inputs in the tests; and mode filtering (e.g. only resonances below a
device's operating band). Declarative pipelines express all three without `if/else` chains or index
juggling.

**Example** (lookup in the constexpr table; reference-row parse in tests):

```c++
#include <ranges>
#include <algorithm>
namespace emc::materials {
    [[nodiscard]] std::expected<Material, emc::Error> from_name(std::string_view n) {
        auto it = std::ranges::find(table, n, &MaterialData::name);
        if (it == table.end()) return emc::fail(ErrorCode::UnknownMaterial, n);
        return it->id;
    }
}
// test-side reference row -> typed inputs (see doc 09):
auto fields = line | std::views::split(',')
                   | std::views::transform([](auto sv){ return parse_double(sv); });
// mode filtering: only resonances below the device's operating band
auto below = modes.data | std::views::filter([&](auto f){ return f < cutoff; });
```

**Why it is the right tool.** Declarative pipelines are composable and pair naturally with `std::expected`
and structured bindings. The testing document ([09](09-testing-and-golden-vectors.md)) builds the full
reference-row→input→compare pipeline on this.

**Trade-offs / pitfalls.** `views::split` yields subranges (not `string_view` directly until C++23
helpers); convert deliberately. Lazy views can be re-evaluated — use `std::ranges::to` (C++23) when you
need a concrete container. Watch dangling on temporaries.

**Compiler support.** Ranges core: C++20 (GCC 10+, Clang 15+, MSVC 19.29+). `views::split` over
`string_view` ergonomics and `ranges::to`: C++23.

---

## 6. Functions & composition

### 6.1 Free functions in namespaces

**What it is.** Domain logic lives in free functions grouped by namespace, not in methods of a stateful
class.

**Domain reason.** A pure free function over `Input` → `Result` with no global mutable state is
thread-safe by construction and callable from any consumer — a front end, the tests, a CLI tool, or a
parallel sweep. This purity is what makes everything else (testing, reuse, units, error handling, the
parallelism in §10.4) possible.

**Example** (`include/emc/basic/skin_depth.hpp`, pure & reusable):

```c++
namespace emc::basic {
    struct SkinDepthInput {
        quantity<isq::frequency[si::hertz]>             frequency;
        quantity<isq::electric_conductivity[si::siemens/si::metre]> conductivity;
        double relative_permeability = 1.0;
    };
    struct SkinDepthResult { quantity<isq::length[si::metre]> depth; };

    [[nodiscard]] std::expected<SkinDepthResult, emc::Error>
    calculate(const SkinDepthInput& in);   // no I/O, thread-safe, testable
}
```

**Why it is the right tool.** Pure free functions are the premise of the design: no global mutable state,
thread-safe, callable from any consumer. The repeatable shape is
[06-calculator-design-pattern.md](06-calculator-design-pattern.md).

**Trade-offs / pitfalls.** Some shared setup (material lookup, validation) must be factored into helpers
rather than duplicated — addressed by the concept (§2.1) and the pattern doc.

**Compiler support.** N/A (design); C++17+ inline-variable conveniences universal.

### 6.2 Deducing this (explicit object parameter) — restrained fluent input builders

**What it is.** C++23 lets a member function name its object parameter explicitly (`auto&& self`),
enabling perfect-forwarding member chains and deduplicated const/non-const overloads without CRTP.

**Domain reason.** A small fluent builder is occasionally handy for assembling solver/sweep inputs.
Deducing-this writes one builder method that works for lvalue and rvalue builders alike, avoiding CRTP or
triplicated `&`/`const&`/`&&` overloads. Used **sparingly** — most call sites just use designated
initializers (§3.1).

**Example** (one builder method, correct value category preserved):

```c++
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
`const`/non-`const`/`&&` triplication.

**Trade-offs / pitfalls.** Easy to over-engineer; the conventions favor designated initializers, so
reserve deducing-this for genuinely repetitive forwarding cases. You cannot also have an implicit `this`
in the same function.

**Compiler support.** C++23; GCC 14+, Clang 18+, MSVC 19.32+.

### 6.3 User-defined literals via mp-units — `5 * mm`, `1 * GHz`

**What it is.** mp-units supplies unit symbols so inputs read as `quantity` expressions (`18.65 * mm`,
`1 * GHz`), the readable analogue of UDLs, with full dimensional checking.

**Domain reason.** Binding the magnitude and the unit together at the call site (`27 * MHz`) is the single
biggest readability and safety win for EMC inputs: the compiler checks dimensions and conversions are
exact, so a value can never drift from its unit.

**Example:**

```c++
using namespace mp_units;
using namespace mp_units::si::unit_symbols;   // mm, GHz, ohm, ... (project re-exports via emc::units)
auto in = emc::basic::SkinDepthInput{ .frequency = 27 * MHz,
                                      .conductivity = /* from material */ };
```

**Why it is the right tool.** The unit is inseparable from the magnitude at the call site, the compiler
checks dimensions, and conversions are exact. The `emc::units` vocabulary and which symbols to re-export
are [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md).

**Trade-offs / pitfalls.** `using namespace ..unit_symbols` can pollute scope; the project wraps the
needed symbols in `emc::units` and documents the import. Symbols are values to multiply, not C++ UDL
suffixes — keep that mental model.

**Compiler support.** Per mp-units (C++20/23; GCC 12+/Clang 16+ recommended).

---

## 7. Output / formatting — `std::format` and `std::print`

**What it is.** Type-safe formatting (`std::format`) and direct output (`std::print`/`std::println`,
C++23), replacing `printf`/iostream string building.

**Domain reason.** The library core does no I/O, but tools and tests need clean formatted output —
reference rows, diagnostic dumps. mp-units provides a formatter, so quantities print with their units and
no manual suffix juggling.

**Example** (tools/tests only; the core stays I/O-free):

```c++
#include <print>
// mp-units provides a formatter, so quantities print with their units
std::println("Z0 = {}", result.z0);                 // e.g. "Z0 = 50 Ω"
std::println("{},{},{}", result.z0, result.c0, result.tpd);  // reference row
```

**Why it is the right tool.** Compile-time-checked format strings, no manual unit suffixes, and a single
statement replaces stream concatenation. Used by the reference-vector writer in
[09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) and any CLI tool in
[08](08-build-system-cmake.md).

> [!NOTE]
> Keep all formatting/I/O **out** of `include/emc/**` and `src/**` core — only in `tools/` and `tests/`.
> `std::print` (C++23) is newer than `std::format` (C++20); if unavailable, `std::format` + a single
> `std::fputs` is the fallback.

**Compiler support.** `std::format`: C++20 (GCC 13+, Clang 17+/libc++, MSVC 19.29+). `std::print`: C++23
(GCC 14+, recent libc++, MSVC 19.37+).

---

## 8. Attributes & quality

### 8.1 `[[likely]]` / `[[unlikely]]` — hint the validation fast path

**What it is.** Branch-probability hints to the optimizer.

**Domain reason.** Validation runs on every calculator call, and the common case is "input in domain".
Hinting the rejection branch as unlikely keeps the happy path tight.

**Example:**

```c++
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

**What it is.** C++23 attribute stating a condition the compiler may assume true (UB if false), enabling
better codegen.

**Domain reason.** Once `validate()` has guaranteed positive width and height, downstream divisions are
provably safe; telling the optimizer removes redundant runtime guards.

**Example** (only *after* a real validation has run):

```c++
auto compute(const MicrostripInput& in) {
    // validate(in) already guaranteed these; tell the optimizer
    [[assume(in.width.numerical_value_in(si::metre) > 0.0)]];
    [[assume(in.height.numerical_value_in(si::metre) > 0.0)]];
    // ... division by width/height now provably safe ...
}
```

> [!WARNING]
> `[[assume]]` is **dangerous**: a false assumption is undefined behaviour. Use it *only* for conditions
> a preceding `validate()` truly enforces; never on raw user input. In C++26 this role is better served
> by **contracts** (§10.2), which are checkable.

**Compiler support.** C++23; GCC 13+, Clang 19+, MSVC 19.33+.

---

## 9. A note on modules (deferred)

C++20 **modules** would give faster builds and clean encapsulation (no header leakage of mp-units
internals). The plan nonetheless targets a **traditional compiled library** — public headers in
`include/emc/`, compiled `.cpp` in `src/`, built into static/shared libs with `install()/export()` and a
CMake package config (see [08-build-system-cmake.md](08-build-system-cmake.md)). Toolchain/IDE/
`find_package` support for modules is still uneven across our GCC/Clang/MSVC matrix and across mp-units
consumption, and a headers+`.cpp` library is the portable form a downstream `find_package(emc)` /
`emc::emc` link expects today. Modules remain an attractive *future* option once toolchains stabilize;
the namespace/layering is designed so a later module map is mechanical.

---

## 10. C++26 forward-looking (aspirational — not part of the initial build)

> [!NOTE]
> This section is explicitly speculative. None of it is required for the initial library; it shows where
> the design can go once C++26 features land. Each sketch names what it would replace.

### 10.1 Static reflection — auto-generate name↔enum maps and struct-field mapping

**Today's friction.** Two mappings are otherwise hand-maintained: `Material` ↔ string name (§3.6/§5) and
**struct fields ↔ reference columns** (each test lists field indices and the header string by hand).

**C++26 sketch** (reflection, syntax illustrative):

```c++
// generate name <-> enum without a hand-written table
template <auto E> constexpr std::string_view enum_name = std::meta::identifier_of(^^E);

// iterate an Input struct's fields to (de)serialize a reference row automatically
template <class Input>
Input from_row(std::span<const std::string_view> cols) {
    Input in{};
    [: expand fields of ^^Input :] >> [&]<auto field>(std::size_t i) {
        in.[:field:] = parse(cols[i]);     // field name + index, reflected
    };
    return in;
}
```

**Would replace.** Bespoke per-calculator row parsers and the material name table — one generic reflective
(de)serializer driven by the `Input`/`Result` struct definitions ([09](09-testing-and-golden-vectors.md)).

### 10.2 Contracts — checkable pre/postconditions for validation

**Today's friction.** `validate()` returns `expected<void, Error>` and `[[assume]]` (§8.2) is unchecked.
Contracts let pre/postconditions be *declared* and optionally *enforced*.

**C++26 sketch** (syntax illustrative):

```c++
quantity<...> microstrip_z0(const MicrostripInput& in)
    pre (in.relative_permittivity >= 1.0 && in.relative_permittivity <= 15.0)
    pre (in.width.numerical_value_in(si::metre) > 0.0)
    post (r: r > 0 * si::ohm);
```

**Would replace.** A chunk of the imperative `validate()` body and the risky `[[assume]]`s — the
preconditions become declarative and runtime-checkable in debug, assumable in release.

### 10.3 Pattern matching — cleaner solve-target dispatch

**Today's friction.** Runtime solve-target dispatch uses `std::variant` + `std::visit` (§3.3).

**C++26 sketch** (`match`, syntax illustrative):

```c++
auto result = query match {
    SolveZ0    => solve_z0(known);
    SolveHeight=> solve_h(known);
    SolveThickness => solve_t(known);
    SolveWidth => solve_w(known);
};
```

**Would replace.** The `std::visit` lambda boilerplate — one readable match expression.

### 10.4 `std::execution` / senders — parallel parameter sweeps

**Today's friction.** Sweeps (§3.4/§3.5) and batched reference runs are sequential loops. Pure
calculators are embarrassingly parallel.

**C++26 sketch** (senders/`bulk`, syntax illustrative):

```c++
auto work = stdexec::schedule(pool)
          | stdexec::bulk(heights.size(), [&](std::size_t i){ out[i] = calculate(at(i)); });
stdexec::sync_wait(std::move(work));
```

**Would replace.** Hand-rolled threading for sweeps and batch regression — structured, composable
parallelism over the already-pure `calculate()` functions, with no shared mutable state to guard (the
purity from §6.1 is what makes this safe).

---

## Cross-references

- [00-overview-and-goals.md](00-overview-and-goals.md) — vision, scope, success criteria.
- [01-architecture-and-layout.md](01-architecture-and-layout.md) — layers, namespaces, library shape.
- [03-quantities-and-units-mp-units.md](03-quantities-and-units-mp-units.md) — the mp-units subsystem (§2.3, §6.3).
- [04-constants-and-material-database.md](04-constants-and-material-database.md) — one constexpr source of truth (§1.2–1.4, §2.2).
- [05-error-handling-and-validation.md](05-error-handling-and-validation.md) — `std::expected` + `emc::Error` (§4).
- [06-calculator-design-pattern.md](06-calculator-design-pattern.md) — the Input/Result/`calculate` pattern + `Calculator` concept (§2.1, §3.1, §6.1).
- [08-build-system-cmake.md](08-build-system-cmake.md) — compiled lib, mp-units dependency, toolchain floor (§9, C++23 feature gates).
- [09-testing-and-golden-vectors.md](09-testing-and-golden-vectors.md) — reference vectors with ranges, `static_assert`, `std::print` (§1.5, §5, §7).
